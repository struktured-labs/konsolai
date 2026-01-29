/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSION_H
#define CLAUDESESSION_H

#include "ClaudeHookHandler.h"
#include "ClaudeProcess.h"
#include "TmuxManager.h"

#include "config-konsole.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

// Konsole includes
#include "../session/Session.h"
#include "konsoleprivate_export.h"

namespace Konsolai
{

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
     * Increment approval counts (called when auto-approving)
     */
    void incrementYoloApproval()
    {
        m_yoloApprovalCount++;
        Q_EMIT approvalCountChanged();
    }
    void incrementDoubleYoloApproval()
    {
        m_doubleYoloApprovalCount++;
        Q_EMIT approvalCountChanged();
    }
    void incrementTripleYoloApproval()
    {
        m_tripleYoloApprovalCount++;
        Q_EMIT approvalCountChanged();
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
     * Approve a pending permission request
     */
    Q_SCRIPTABLE void approvePermission();

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
    bool m_isReattach = false;

    TmuxManager *m_tmuxManager = nullptr;
    ClaudeProcess *m_claudeProcess = nullptr;
    ClaudeHookHandler *m_hookHandler = nullptr;

    // Per-session yolo mode settings
    bool m_yoloMode = false;
    bool m_doubleYoloMode = false;
    bool m_tripleYoloMode = false;
    QString m_autoContinuePrompt = QStringLiteral("Continue improving, debugging, fixing, adding features, or introducing tests where applicable.");

    // Approval counters
    int m_yoloApprovalCount = 0;
    int m_doubleYoloApprovalCount = 0;
    int m_tripleYoloApprovalCount = 0;

    // Permission prompt polling for yolo mode
    QTimer *m_permissionPollTimer = nullptr;
    bool m_permissionPromptDetected = false;
    void startPermissionPolling();
    void stopPermissionPolling();
    void pollForPermissionPrompt();
    bool detectPermissionPrompt(const QString &terminalOutput);
};

} // namespace Konsolai

#endif // CLAUDESESSION_H
