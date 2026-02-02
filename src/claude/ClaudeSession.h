/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSION_H
#define CLAUDESESSION_H

#include "konsoleprivate_export.h"

#include "ClaudeHookHandler.h"
#include "ClaudeProcess.h"
#include "TmuxManager.h"

#include "config-konsole.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>

// Konsole includes
#include "../session/Session.h"

namespace Konsolai
{

/**
 * Per-session token usage counters
 */
struct KONSOLEPRIVATE_EXPORT TokenUsage {
    quint64 inputTokens = 0;
    quint64 outputTokens = 0;
    quint64 cacheReadTokens = 0;
    quint64 cacheCreationTokens = 0;

    quint64 totalTokens() const
    {
        return inputTokens + outputTokens + cacheReadTokens + cacheCreationTokens;
    }

    double estimatedCostUSD() const
    {
        // Anthropic pricing per million tokens (Claude Opus 4.5)
        return (inputTokens * 3.0 + outputTokens * 15.0 + cacheCreationTokens * 0.30 + cacheReadTokens * 0.30) / 1000000.0;
    }

    QString formatCompact() const
    {
        auto fmt = [](quint64 n) -> QString {
            if (n >= 1000000) {
                return QStringLiteral("%1M").arg(n / 1000000.0, 0, 'f', 1);
            }
            if (n >= 1000) {
                return QStringLiteral("%1K").arg(n / 1000.0, 0, 'f', 1);
            }
            return QString::number(n);
        };
        return QStringLiteral("%1↑ %2↓").arg(fmt(inputTokens + cacheReadTokens + cacheCreationTokens), fmt(outputTokens));
    }
};

/**
 * Log entry for auto-approved actions
 */
struct KONSOLEPRIVATE_EXPORT ApprovalLogEntry {
    QDateTime timestamp;
    QString toolName;
    QString action;
    int yoloLevel; // 1=yolo, 2=double, 3=triple
};

/**
 * ClaudeSession manages a Claude-enabled terminal session.
 *
 * A ClaudeSession extends Konsole Session to provide:
 * - tmux-backed session persistence
 * - Claude process lifecycle management
 * - State tracking and notifications
 * - D-Bus interface for external control
 *
 * Session naming convention: konsolai-{profile}-{8-char-id}
 *
 * Key behaviors:
 * - New tab → creates ClaudeSession with tmux session
 * - Closing tab → tmux session continues (can be reattached later)
 * - Session state visible in tab indicators
 *
 * Note: Inherits QDBusContext from parent Session class
 */
class KONSOLEPRIVATE_EXPORT ClaudeSession : public Konsole::Session
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.konsolai.Claude")

    Q_PROPERTY(QString state READ stateString)
    Q_PROPERTY(QString currentTask READ currentTask)
    Q_PROPERTY(QString sessionName READ sessionName)
    Q_PROPERTY(QString sessionId READ sessionId)
    Q_PROPERTY(QString profileName READ profileName)

public:
    /**
     * Create a new ClaudeSession
     *
     * @param profileName Name of the Konsole profile being used
     * @param workingDir Initial working directory
     * @param parent Parent object
     */
    explicit ClaudeSession(const QString &profileName,
                          const QString &workingDir = QString(),
                          QObject *parent = nullptr);

    /**
     * Create a ClaudeSession for reattaching to an existing tmux session
     *
     * @param existingSessionName Name of the tmux session to reattach
     * @param parent Parent object
     */
    static ClaudeSession* createForReattach(const QString &existingSessionName,
                                            QObject *parent = nullptr);

    ~ClaudeSession() override;

    // ========== Terminal Prompt Detection (static, for testability) ==========

    /**
     * Detect whether terminal output contains a Claude Code permission prompt.
     * Checks for selector arrow (❯) co-located with "Yes" or "Allow" on the same line.
     */
    static bool detectPermissionPrompt(const QString &terminalOutput);

    /**
     * Detect whether terminal output shows Claude Code's idle input prompt.
     * Looks for ">" or "❯" on the last non-empty line, excluding permission UIs.
     */
    static bool detectIdlePrompt(const QString &terminalOutput);

    /**
     * Get the tmux session name
     */
    QString sessionName() const { return m_sessionName; }

    /**
     * Get the unique session ID (8 hex chars)
     */
    QString sessionId() const { return m_sessionId; }

    /**
     * Get the profile name this session was created with
     */
    QString profileName() const { return m_profileName; }

    /**
     * Get the working directory
     */
    QString workingDirectory() const { return m_workingDir; }

    /**
     * Get the Claude model being used
     */
    ClaudeProcess::Model claudeModel() const { return m_claudeModel; }

    /**
     * Set the Claude model to use
     */
    void setClaudeModel(ClaudeProcess::Model model) { m_claudeModel = model; }

    /**
     * Get the current Claude state
     */
    ClaudeProcess::State claudeState() const;

    // ========== Per-Session Yolo Mode Settings ==========

    /**
     * Yolo Mode Level 1: Auto-approve all permission requests
     */
    bool yoloMode() const
    {
        return m_yoloMode;
    }
    void setYoloMode(bool enabled);

    /**
     * Yolo Mode Level 2: Auto-accept tab completions and suggestions
     */
    bool doubleYoloMode() const
    {
        return m_doubleYoloMode;
    }
    void setDoubleYoloMode(bool enabled);

    /**
     * Yolo Mode Level 3: Auto-continue with prompt when Claude finishes
     */
    bool tripleYoloMode() const
    {
        return m_tripleYoloMode;
    }
    void setTripleYoloMode(bool enabled);

    /**
     * The prompt to send in Triple Yolo mode when Claude becomes idle
     */
    QString autoContinuePrompt() const
    {
        return m_autoContinuePrompt;
    }
    void setAutoContinuePrompt(const QString &prompt)
    {
        m_autoContinuePrompt = prompt;
    }

    /**
     * Whether double yolo (accept suggestions) fires before triple yolo (auto-continue).
     * When true (default), Tab+Enter fires first; if Claude stays idle, triple yolo follows.
     * When false, triple yolo fires directly (old behavior).
     */
    bool trySuggestionsFirst() const
    {
        return m_trySuggestionsFirst;
    }
    void setTrySuggestionsFirst(bool enabled)
    {
        m_trySuggestionsFirst = enabled;
    }

    /**
     * Approval counts for each yolo mode level
     */
    int yoloApprovalCount() const
    {
        return m_yoloApprovalCount;
    }
    int doubleYoloApprovalCount() const
    {
        return m_doubleYoloApprovalCount;
    }
    int tripleYoloApprovalCount() const
    {
        return m_tripleYoloApprovalCount;
    }
    int totalApprovalCount() const
    {
        return m_yoloApprovalCount + m_doubleYoloApprovalCount + m_tripleYoloApprovalCount;
    }

    /**
     * Get the approval log
     */
    const QVector<ApprovalLogEntry> &approvalLog() const
    {
        return m_approvalLog;
    }

    /**
     * Log an approval (called when auto-approving)
     */
    void logApproval(const QString &toolName, const QString &action, int yoloLevel)
    {
        ApprovalLogEntry entry;
        entry.timestamp = QDateTime::currentDateTime();
        entry.toolName = toolName;
        entry.action = action;
        entry.yoloLevel = yoloLevel;
        m_approvalLog.append(entry);

        if (yoloLevel == 1) {
            m_yoloApprovalCount++;
        } else if (yoloLevel == 2) {
            m_doubleYoloApprovalCount++;
        } else if (yoloLevel == 3) {
            m_tripleYoloApprovalCount++;
        }

        qDebug() << "ClaudeSession: Logged approval -" << toolName << action << "level:" << yoloLevel << "total:" << totalApprovalCount();
        Q_EMIT approvalCountChanged();
        Q_EMIT approvalLogged(entry);
    }

    /**
     * Increment approval counts (called when auto-approving)
     * @deprecated Use logApproval() instead for detailed tracking
     */
    void incrementYoloApproval()
    {
        logApproval(QStringLiteral("unknown"), QStringLiteral("auto-approved"), 1);
    }
    void incrementDoubleYoloApproval()
    {
        logApproval(QStringLiteral("unknown"), QStringLiteral("auto-accepted"), 2);
    }
    void incrementTripleYoloApproval()
    {
        logApproval(QStringLiteral("unknown"), QStringLiteral("auto-continued"), 3);
    }

    /**
     * Get current token usage for this session
     */
    const TokenUsage &tokenUsage() const
    {
        return m_tokenUsage;
    }

    /**
     * Refresh token usage by parsing Claude CLI conversation files
     */
    void refreshTokenUsage();

    /**
     * Set a Claude CLI conversation ID to resume when starting this session.
     * When set, the session will pass --resume <id> to the Claude CLI.
     */
    void setResumeSessionId(const QString &id)
    {
        m_resumeSessionId = id;
    }

    /**
     * Get the Claude CLI conversation resume ID (empty if starting fresh).
     */
    QString resumeSessionId() const
    {
        return m_resumeSessionId;
    }

    /**
     * Whether this session is for reattaching to an existing tmux session
     */
    bool isReattach() const { return m_isReattach; }

    /**
     * Get the command to start/attach to this session
     *
     * This returns the tmux command that should be run in the terminal.
     */
    QString shellCommand() const;

    /**
     * Override run() to build the tmux command just before starting.
     * This ensures the correct working directory is used.
     */
    void run() override;

    /**
     * Get the ClaudeProcess instance for state tracking
     */
    ClaudeProcess* claudeProcess() { return m_claudeProcess; }

    /**
     * Get the TmuxManager instance
     */
    TmuxManager* tmuxManager() { return m_tmuxManager; }

    /**
     * Detach from the tmux session without killing it
     */
    void detach();

    /**
     * Kill the tmux session
     */
    void kill();

    /**
     * Send text to the Claude process
     */
    void sendText(const QString &text);

    /**
     * Get the session transcript (captured from tmux pane)
     */
    QString transcript(int lines = 1000);

    // D-Bus property accessors
    QString stateString() const;
    QString currentTask() const;

public Q_SLOTS:
    // D-Bus methods
    /**
     * Send a prompt to Claude
     */
    Q_SCRIPTABLE void sendPrompt(const QString &prompt);

    /**
     * Approve a pending permission request (selects default option)
     */
    Q_SCRIPTABLE void approvePermission();

    /**
     * Approve and allow all future similar requests.
     * Navigates to option 2 ("Always allow") before pressing Enter.
     * Used by yolo mode to reduce future permission prompts.
     */
    void approvePermissionAlways();

    /**
     * Deny a pending permission request
     */
    Q_SCRIPTABLE void denyPermission();

    /**
     * Stop Claude (send Ctrl+C)
     */
    Q_SCRIPTABLE void stop();

    /**
     * Restart the Claude session
     */
    Q_SCRIPTABLE void restart();

    /**
     * Get session transcript via D-Bus
     */
    Q_SCRIPTABLE QString getTranscript(int lines = 1000);

Q_SIGNALS:
    /**
     * Emitted when Claude state changes
     */
    void stateChanged(ClaudeProcess::State newState);

    /**
     * Emitted when a permission is requested
     */
    void permissionRequested(const QString &action, const QString &description);

    /**
     * Emitted when a notification should be shown
     */
    void notificationReceived(const QString &type, const QString &message);

    /**
     * Emitted when a task starts
     */
    void taskStarted(const QString &taskDescription);

    /**
     * Emitted when a task finishes
     */
    void taskFinished();

    /**
     * Emitted when a task completes with summary
     */
    void taskComplete(const QString &summary);

    /**
     * Emitted when Claude is waiting for user input
     */
    void waitingForInput(const QString &prompt);

    /**
     * Emitted when an error occurs
     */
    void errorOccurred(const QString &error);

    /**
     * Emitted when the session is detached
     */
    void detached();

    /**
     * Emitted when the session is killed
     */
    void killed();

    /**
     * Emitted when yolo mode changes
     */
    void yoloModeChanged(bool enabled);

    /**
     * Emitted when double yolo mode changes
     */
    void doubleYoloModeChanged(bool enabled);

    /**
     * Emitted when triple yolo mode changes
     */
    void tripleYoloModeChanged(bool enabled);

    /**
     * Emitted when approval count changes
     */
    void approvalCountChanged();

    /**
     * Emitted when an approval is logged
     */
    void approvalLogged(const ApprovalLogEntry &entry);

    /**
     * Emitted when working directory is determined (after run())
     */
    void workingDirectoryChanged(const QString &newPath);

    /**
     * Emitted when token usage is updated
     */
    void tokenUsageChanged();

private:
    ClaudeSession(QObject *parent);  // Private constructor for reattach

    void initializeNew(const QString &profileName, const QString &workingDir);
    void initializeReattach(const QString &existingSessionName);
    void connectSignals();

    QString m_sessionName;
    QString m_sessionId;
    QString m_profileName;
    QString m_workingDir;
    ClaudeProcess::Model m_claudeModel = ClaudeProcess::Model::Default;
    QString m_resumeSessionId;
    bool m_isReattach = false;

    TmuxManager *m_tmuxManager = nullptr;
    ClaudeProcess *m_claudeProcess = nullptr;
    ClaudeHookHandler *m_hookHandler = nullptr;

    // Per-session yolo mode settings
    bool m_yoloMode = false;
    bool m_doubleYoloMode = false;
    bool m_tripleYoloMode = false;
    bool m_trySuggestionsFirst = true;
    QString m_autoContinuePrompt = QStringLiteral("Continue improving, debugging, fixing, adding features, or introducing tests where applicable.");

    // Approval counters
    int m_yoloApprovalCount = 0;
    int m_doubleYoloApprovalCount = 0;
    int m_tripleYoloApprovalCount = 0;

    // Approval log
    QVector<ApprovalLogEntry> m_approvalLog;

    // Token usage tracking
    TokenUsage m_tokenUsage;
    QTimer *m_tokenRefreshTimer = nullptr;
    void startTokenTracking();
    TokenUsage parseConversationTokens(const QString &jsonlPath);

    // Permission prompt polling for yolo mode
    QTimer *m_permissionPollTimer = nullptr;
    bool m_permissionPromptDetected = false;
    void startPermissionPolling();
    void stopPermissionPolling();
    void pollForPermissionPrompt();

    // Double yolo: auto-accept suggestions
    void autoAcceptSuggestion();

    // Fallback timer: if double yolo fires but Claude stays idle, triple yolo follows
    QTimer *m_suggestionFallbackTimer = nullptr;
    void scheduleSuggestionFallback();

    // Idle polling for triple yolo when hooks aren't delivering state
    QTimer *m_idlePollTimer = nullptr;
    bool m_idlePromptDetected = false;
    void startIdlePolling();
    void stopIdlePolling();
    void pollForIdlePrompt();

    // Hook cleanup: remove konsolai hooks from project's .claude/settings.local.json
    void removeHooksFromProjectSettings();
};

} // namespace Konsolai

#endif // CLAUDESESSION_H
