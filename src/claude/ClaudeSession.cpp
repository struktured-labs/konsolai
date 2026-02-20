/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSession.h"
#include "ClaudeHookHandler.h"
#include "ClaudeSessionRegistry.h"
#include "KonsolaiSettings.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <unistd.h> // sysconf(_SC_CLK_TCK)
#endif

namespace Konsolai
{

// Build a display name for tabs/panels: "project (task desc) @host" for remote or "project (sessionId)" fallback
static QString buildDisplayName(const QString &projectName, const QString &taskDescription, const QString &sessionId, const QString &sshHost = QString())
{
    QString name;
    if (!taskDescription.isEmpty()) {
        QString desc = taskDescription;
        if (desc.length() > 30) {
            desc = desc.left(27) + QStringLiteral("...");
        }
        name = QStringLiteral("%1 (%2)").arg(projectName, desc);
    } else if (!sessionId.isEmpty()) {
        name = QStringLiteral("%1 (%2)").arg(projectName, sessionId.left(8));
    } else {
        name = projectName;
    }

    // Append @host for remote sessions
    if (!sshHost.isEmpty()) {
        name += QStringLiteral(" @%1").arg(sshHost);
    }

    return name;
}

ClaudeSession::ClaudeSession(const QString &profileName, const QString &workingDir, QObject *parent)
    : Konsole::Session(parent)
{
    initializeNew(profileName, workingDir);
}

ClaudeSession::ClaudeSession(QObject *parent)
    : Konsole::Session(parent)
{
    // Empty - used for reattach, initialization happens in initializeReattach()
}

ClaudeSession* ClaudeSession::createForReattach(const QString &existingSessionName,
                                                 QObject *parent)
{
    auto *session = new ClaudeSession(parent);
    session->initializeReattach(existingSessionName);
    return session;
}

ClaudeSession::~ClaudeSession()
{
    // Stop ALL timers FIRST to prevent ghost yolo:
    // timers firing during destruction would invoke lambdas on a partially-destroyed object.
    if (m_permissionPollTimer) {
        m_permissionPollTimer->stop();
    }
    if (m_idlePollTimer) {
        m_idlePollTimer->stop();
    }
    if (m_suggestionTimer) {
        m_suggestionTimer->stop();
    }
    if (m_suggestionFallbackTimer) {
        m_suggestionFallbackTimer->stop();
    }
    if (m_tokenRefreshTimer) {
        m_tokenRefreshTimer->stop();
    }
    if (m_resourceTimer) {
        m_resourceTimer->stop();
    }

    // BudgetController and SessionObserver own timers — delete early to stop them
    delete m_budgetController;
    m_budgetController = nullptr;
    delete m_sessionObserver;
    m_sessionObserver = nullptr;

    if (auto *registry = ClaudeSessionRegistry::instance()) {
        registry->unregisterSession(this);
    }

    if (m_isRemote) {
        cleanupRemoteHooks();
    } else {
        removeHooksFromProjectSettings();
    }
}

void ClaudeSession::initializeNew(const QString &profileName, const QString &workingDir)
{
    m_profileName = profileName;
    // Use home directory as fallback instead of current path (which might be build dir)
    m_workingDir = workingDir.isEmpty() ? QDir::homePath() : workingDir;
    m_isReattach = false;

    // Generate unique session ID
    m_sessionId = TmuxManager::generateSessionId();

    // Build session name from profile and ID
    m_sessionName = TmuxManager::buildSessionName(m_profileName, m_sessionId);

    // Create managers
    m_tmuxManager = new TmuxManager(this);
    m_claudeProcess = new ClaudeProcess(this);

    // Create hook handler for receiving Claude hook events
    m_hookHandler = new ClaudeHookHandler(m_sessionId, this);

    // Connect hook handler to Claude process for state tracking
    connect(m_hookHandler, &ClaudeHookHandler::hookEventReceived, m_claudeProcess, &ClaudeProcess::handleHookEvent);

    // Set initial working directory (may be overridden by ViewManager later)
    setInitialWorkingDirectory(m_workingDir);

    // Set tab title based on working directory name + task description
    QString projectName = QDir(m_workingDir).dirName();
    if (!projectName.isEmpty()) {
        QString displayName = buildDisplayName(projectName, m_taskDescription, m_sessionId, m_sshHost);
        setTitle(Konsole::Session::NameRole, displayName);
        setTitle(Konsole::Session::DisplayedTitleRole, displayName);
        // Use literal display name - format codes like %d (cwd) and %n (process name)
        // resolve to the tmux client process, not the Claude session's project
        setTabTitleFormat(Konsole::Session::LocalTabTitle, displayName);
        setTabTitleFormat(Konsole::Session::RemoteTabTitle, displayName);
        tabTitleSetByUser(true);
    }

    // Apply persisted yolo mode defaults to new sessions
    if (auto *settings = KonsolaiSettings::instance()) {
        m_yoloMode = settings->yoloMode();
        m_doubleYoloMode = settings->doubleYoloMode();
        m_tripleYoloMode = settings->tripleYoloMode();
        m_autoContinuePrompt = settings->autoContinuePrompt();
        m_trySuggestionsFirst = settings->trySuggestionsFirst();
    }

    // Override with per-session state if one was persisted for this project
    if (auto *registry = ClaudeSessionRegistry::instance()) {
        if (const auto *saved = registry->lastSessionState(m_workingDir)) {
            if (!saved->autoContinuePrompt.isEmpty()) {
                m_autoContinuePrompt = saved->autoContinuePrompt;
            }
            m_yoloMode = saved->yoloMode;
            m_doubleYoloMode = saved->doubleYoloMode;
            m_tripleYoloMode = saved->tripleYoloMode;
        }
    }

    // NOTE: We don't set program/arguments here - we do it in run()
    // This allows ViewManager to set the correct working directory first

    connectSignals();
}

void ClaudeSession::initializeReattach(const QString &existingSessionName)
{
    m_sessionName = existingSessionName;
    m_isReattach = true;

    // Parse session name to extract profile and ID if possible
    // Expected format: konsolai-{profile}-{id}
    static const QRegularExpression pattern(QStringLiteral("^konsolai-(.+)-([a-f0-9]{8})$"));
    QRegularExpressionMatch match = pattern.match(existingSessionName);

    if (match.hasMatch()) {
        m_profileName = match.captured(1);
        m_sessionId = match.captured(2);
    } else {
        // Non-standard session name, use defaults
        m_profileName = QStringLiteral("unknown");
        m_sessionId = QString();
    }

    // Leave working dir empty for reattach - it will be populated from tmux in run()
    // Don't use QDir::currentPath() as that gives the build directory, not the project directory
    m_workingDir = QString();

    // Create managers
    m_tmuxManager = new TmuxManager(this);
    m_claudeProcess = new ClaudeProcess(this);

    // Create hook handler for receiving Claude hook events
    // For reattach, use the existing session name as the socket identifier
    QString hookId = m_sessionId.isEmpty() ? m_sessionName : m_sessionId;
    m_hookHandler = new ClaudeHookHandler(hookId, this);

    // Connect hook handler to Claude process for state tracking
    connect(m_hookHandler, &ClaudeHookHandler::hookEventReceived, m_claudeProcess, &ClaudeProcess::handleHookEvent);

    setInitialWorkingDirectory(m_workingDir);

    // Set tab title to session name for reattached sessions (will be updated in run())
    setTitle(Konsole::Session::NameRole, existingSessionName);
    setTitle(Konsole::Session::DisplayedTitleRole, existingSessionName);
    // Use literal session name - format codes resolve to the tmux client, not the project
    setTabTitleFormat(Konsole::Session::LocalTabTitle, existingSessionName);
    setTabTitleFormat(Konsole::Session::RemoteTabTitle, existingSessionName);
    tabTitleSetByUser(true);

    // Restore yolo state from global defaults, then override with per-session state
    if (auto *settings = KonsolaiSettings::instance()) {
        m_yoloMode = settings->yoloMode();
        m_doubleYoloMode = settings->doubleYoloMode();
        m_tripleYoloMode = settings->tripleYoloMode();
        m_autoContinuePrompt = settings->autoContinuePrompt();
        m_trySuggestionsFirst = settings->trySuggestionsFirst();
    }
    if (auto *registry = ClaudeSessionRegistry::instance()) {
        if (const auto *saved = registry->sessionState(existingSessionName)) {
            m_yoloMode = saved->yoloMode;
            m_doubleYoloMode = saved->doubleYoloMode;
            m_tripleYoloMode = saved->tripleYoloMode;
            if (!saved->autoContinuePrompt.isEmpty()) {
                m_autoContinuePrompt = saved->autoContinuePrompt;
            }
            if (!saved->workingDirectory.isEmpty()) {
                m_workingDir = saved->workingDirectory;
                setInitialWorkingDirectory(m_workingDir);
            }
            m_taskDescription = saved->taskDescription;
        }
    }

    // Update tab title from restored state (workingDir gives project name, taskDescription is optional)
    if (!m_workingDir.isEmpty()) {
        QString projectName = QDir(m_workingDir).dirName();
        if (!projectName.isEmpty()) {
            QString displayName = buildDisplayName(projectName, m_taskDescription, m_sessionId, m_sshHost);
            setTitle(Konsole::Session::NameRole, displayName);
            setTitle(Konsole::Session::DisplayedTitleRole, displayName);
            setTabTitleFormat(Konsole::Session::LocalTabTitle, displayName);
            setTabTitleFormat(Konsole::Session::RemoteTabTitle, displayName);
            tabTitleSetByUser(true);
        }
    }

    // NOTE: We don't set program/arguments here - we do it in run()

    connectSignals();
}

void ClaudeSession::run()
{
    qDebug() << "ClaudeSession::run() called" << (m_isRemote ? "(remote SSH session)" : "(local session)");

    // For remote sessions, skip local prerequisite checks
    // (tmux and claude must be installed on the remote host)
    if (!m_isRemote) {
        // Validate prerequisites
        if (!TmuxManager::isAvailable()) {
            qWarning() << "tmux is NOT available!";
            qWarning() << "Showing error dialog...";
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setWindowTitle(QStringLiteral("Claude Session Error"));
            msgBox.setText(QStringLiteral("tmux is not installed or not in PATH."));
            msgBox.setInformativeText(
                QStringLiteral("Please install tmux to use Claude sessions:\n"
                               "  sudo apt install tmux  # Debian/Ubuntu\n"
                               "  sudo dnf install tmux  # Fedora\n"
                               "  sudo pacman -S tmux    # Arch"));
            msgBox.exec();
            qWarning() << "Error dialog closed, returning from run()";
            return;
        }

        if (!ClaudeProcess::isAvailable()) {
            qWarning() << "Claude CLI is NOT available!";
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setWindowTitle(QStringLiteral("Claude Session Error"));
            msgBox.setText(QStringLiteral("Claude CLI is not installed or not in PATH."));
            msgBox.setInformativeText(
                QStringLiteral("Please install Claude CLI:\n"
                               "  npm install -g @anthropic-ai/claude-code\n\n"
                               "Or visit: https://claude.ai/download"));
            msgBox.exec();
            return;
        }
    }

    // Use the current initialWorkingDirectory (may have been updated by ViewManager)
    QString workDir = initialWorkingDirectory();
    if (!workDir.isEmpty()) {
        m_workingDir = workDir;
    }

    // For reattached sessions, query tmux asynchronously to avoid blocking the main
    // thread at startup (multiple sessions doing sync tmux calls can freeze the UI).
    // The initial m_workingDir from stored metadata is used immediately; the async
    // callback updates it if tmux reports a different (more accurate) directory.
    if (m_isReattach && m_tmuxManager) {
        QPointer<ClaudeSession> guard(this);
        m_tmuxManager->sessionExistsAsync(m_sessionName, [this, guard](bool exists) {
            if (!guard || !exists || !m_tmuxManager) {
                return;
            }
            m_tmuxManager->getPaneWorkingDirectoryAsync(m_sessionName, [this, guard](const QString &tmuxWorkDir) {
                if (!guard) {
                    return;
                }
                if (!tmuxWorkDir.isEmpty() && QDir(tmuxWorkDir).exists() && tmuxWorkDir != m_workingDir) {
                    m_workingDir = tmuxWorkDir;
                    qDebug() << "ClaudeSession::run() - Got working dir from tmux (async):" << m_workingDir;

                    // Update tab title with the corrected directory
                    QString projectName = QDir(m_workingDir).dirName();
                    if (!projectName.isEmpty() && projectName != QStringLiteral(".")) {
                        QString displayName = buildDisplayName(projectName, m_taskDescription, m_sessionId, m_sshHost);
                        setTitle(Konsole::Session::NameRole, displayName);
                        setTitle(Konsole::Session::DisplayedTitleRole, displayName);
                        setTabTitleFormat(Konsole::Session::LocalTabTitle, displayName);
                        setTabTitleFormat(Konsole::Session::RemoteTabTitle, displayName);
                    }

                    Q_EMIT workingDirectoryChanged(m_workingDir);
                }
            });
        });
    }

    // Validate working directory (skip for remote sessions - path is on remote host)
    if (!m_isRemote && !m_workingDir.isEmpty() && !QDir(m_workingDir).exists()) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Claude Session Warning"),
                             QStringLiteral("Working directory does not exist:\n%1\n\n"
                                            "Using current directory instead.")
                                 .arg(m_workingDir));
        m_workingDir = QDir::currentPath();
    }

    // Update tab title to project name (directory basename) + task description
    // We use a literal string (no format codes) because format codes like %d and %n
    // resolve to the tmux client process info, not the Claude session's project.
    // We also set tabTitleSetByUser to prevent OSC 30 escape sequences from tmux/shell
    // from overriding the title (they would set it to the build directory name).
    QString projectName = QDir(m_workingDir).dirName();
    if (!projectName.isEmpty() && projectName != QStringLiteral(".")) {
        QString displayName = buildDisplayName(projectName, m_taskDescription, m_sessionId, m_sshHost);
        setTitle(Konsole::Session::NameRole, displayName);
        setTitle(Konsole::Session::DisplayedTitleRole, displayName);
        setTabTitleFormat(Konsole::Session::LocalTabTitle, displayName);
        setTabTitleFormat(Konsole::Session::RemoteTabTitle, displayName);
        tabTitleSetByUser(true);
    }

    // Emit signal so session panel can update metadata with correct working directory
    Q_EMIT workingDirectoryChanged(m_workingDir);

    // Start the hook handler to receive Claude events
    if (m_hookHandler) {
        if (m_isRemote) {
            // Remote sessions: use TCP mode with SSH reverse tunnel
            m_hookHandler->setMode(ClaudeHookHandler::TCP);
            if (m_hookHandler->start()) {
                qDebug() << "ClaudeSession::run() - Hook handler started in TCP mode on port:" << m_hookHandler->tcpPort();
                // Note: Remote hooks config is injected via SSH in buildRemoteSshCommand()
            } else {
                qWarning() << "ClaudeSession::run() - Failed to start TCP hook handler for remote session";
            }
        } else {
            // Local sessions: use Unix socket mode
            if (m_hookHandler->start()) {
                qDebug() << "ClaudeSession::run() - Hook handler started on:" << m_hookHandler->socketPath();

                // Write hooks config to project's .claude/settings.local.json
                // Claude Code reads hooks from settings.local.json, not hooks.json
                QString hooksConfig = m_hookHandler->generateHooksConfig();
                if (!hooksConfig.isEmpty()) {
                    QString claudeDir = m_workingDir + QStringLiteral("/.claude");
                    QDir dir(claudeDir);
                    if (!dir.exists()) {
                        dir.mkpath(claudeDir);
                    }

                    QString settingsPath = claudeDir + QStringLiteral("/settings.local.json");

                    // Read existing settings if any
                    QJsonObject settings;
                    QFile existingFile(settingsPath);
                    if (existingFile.open(QIODevice::ReadOnly)) {
                        QJsonDocument existingDoc = QJsonDocument::fromJson(existingFile.readAll());
                        if (existingDoc.isObject()) {
                            settings = existingDoc.object();
                        }
                        existingFile.close();
                    }

                    // Parse our hooks config and merge per event type.
                    // Multiple sessions in the same workDir each have their own socket,
                    // so we must ADD our hooks alongside any existing ones (from other
                    // sessions) rather than replacing the entire "hooks" object.
                    QJsonDocument hooksDoc = QJsonDocument::fromJson(hooksConfig.toUtf8());
                    if (hooksDoc.isObject()) {
                        QJsonObject hooksObj = hooksDoc.object();
                        if (hooksObj.contains(QStringLiteral("hooks"))) {
                            QJsonObject newHooks = hooksObj[QStringLiteral("hooks")].toObject();
                            QJsonObject existingHooks = settings[QStringLiteral("hooks")].toObject();
                            QString mySocket = m_hookHandler->socketPath();

                            // For each event type, merge our entry into the existing array
                            for (auto it = newHooks.begin(); it != newHooks.end(); ++it) {
                                QJsonArray existingArray = existingHooks[it.key()].toArray();
                                QJsonArray ourEntries = it.value().toArray();

                                // Remove any stale entries for THIS session's socket
                                QJsonArray filtered;
                                for (const auto &entry : existingArray) {
                                    QString entryStr = QString::fromUtf8(QJsonDocument(entry.toObject()).toJson());
                                    if (!entryStr.contains(mySocket)) {
                                        filtered.append(entry);
                                    }
                                }

                                // Append our entries
                                for (const auto &entry : ourEntries) {
                                    filtered.append(entry);
                                }
                                existingHooks[it.key()] = filtered;
                            }
                            settings[QStringLiteral("hooks")] = existingHooks;
                        }
                    }

                    // Write merged settings
                    QFile settingsFile(settingsPath);
                    if (settingsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QJsonDocument outDoc(settings);
                        settingsFile.write(outDoc.toJson(QJsonDocument::Indented));
                        settingsFile.close();
                        qDebug() << "ClaudeSession::run() - Wrote hooks to:" << settingsPath;
                    } else {
                        qWarning() << "ClaudeSession::run() - Failed to write settings to:" << settingsPath;
                    }
                }
            } else {
                qWarning() << "ClaudeSession::run() - Failed to start hook handler";
            }
        }
    }

    // Build the tmux command now that we have the correct working directory
    QString tmuxCommand = shellCommand();

    qDebug() << "ClaudeSession::run() - tmux command:" << tmuxCommand;
    qDebug() << "  Working dir:" << m_workingDir;
    qDebug() << "  Session name:" << m_sessionName;

    // Note: Session::run() strips the first argument (historical reasons),
    // so we need to include the program name as the first argument
    setProgram(QStringLiteral("sh"));
    setArguments({QStringLiteral("sh"), QStringLiteral("-c"), tmuxCommand});

    // Call parent run()
    Session::run();

    qDebug() << "ClaudeSession::run() - session started, isRunning:" << isRunning();

    // Start token usage tracking (parses Claude conversation JSONL files)
    startTokenTracking();

    // Start resource tracking (CPU%, RSS via /proc)
    startResourceTracking();

    // Start polling fallbacks for persisted yolo state.
    // After a konsolai restart, hook-based paths are broken (Claude CLI snapshots
    // hooks at startup, so rewritten settings.local.json with new socket paths
    // won't take effect until the Claude session is restarted).  Polling is the
    // reliable fallback.
    if (m_yoloMode) {
        startPermissionPolling();
    }
    if (m_doubleYoloMode || m_tripleYoloMode) {
        startIdlePolling();
    }
}

void ClaudeSession::connectSignals()
{
    // Forward signals from ClaudeProcess
    connect(m_claudeProcess, &ClaudeProcess::stateChanged,
            this, &ClaudeSession::stateChanged);
    connect(m_claudeProcess, &ClaudeProcess::permissionRequested,
            this, &ClaudeSession::permissionRequested);
    connect(m_claudeProcess, &ClaudeProcess::notificationReceived,
            this, &ClaudeSession::notificationReceived);
    connect(m_claudeProcess, &ClaudeProcess::taskStarted,
            this, &ClaudeSession::taskStarted);
    connect(m_claudeProcess, &ClaudeProcess::taskFinished,
            this, &ClaudeSession::taskFinished);

    // Handle yolo mode auto-approval for permission requests
    connect(m_claudeProcess, &ClaudeProcess::permissionRequested, this, [this](const QString &action, const QString &description) {
        Q_UNUSED(description);
        qDebug() << "ClaudeSession: Permission requested:" << action << "yoloMode:" << m_yoloMode;
        if (m_yoloMode) {
            if (m_budgetController && m_budgetController->shouldBlockYolo()) {
                qDebug() << "ClaudeSession: Budget gate blocking yolo approval";
                return;
            }
            qDebug() << "ClaudeSession: Auto-approving permission always (yolo mode)";
            // Use a short delay to ensure the prompt is ready
            QTimer::singleShot(100, this, [this, action]() {
                approvePermissionAlways();
                logApproval(action, QStringLiteral("auto-approved"), 1);
            });
        }
    });

    // Handle idle state for double/triple yolo modes
    // Precedence: when trySuggestionsFirst is true, double yolo fires first.
    // If Claude stays idle after 2s, triple yolo fires as fallback.
    // When an agent team is active, L2/L3 keyboard-based yolo is suppressed —
    // hook-based TeammateIdle continuation handles it instead.
    connect(m_claudeProcess, &ClaudeProcess::stateChanged, this, [this](ClaudeProcess::State newState) {
        qDebug() << "ClaudeSession: State changed to:" << static_cast<int>(newState) << "doubleYoloMode:" << m_doubleYoloMode
                 << "tripleYoloMode:" << m_tripleYoloMode << "trySuggestionsFirst:" << m_trySuggestionsFirst << "hasActiveTeam:" << hasActiveTeam();

        // Cancel any pending suggestion/fallback timers if Claude is no longer idle
        if (newState != ClaudeProcess::State::Idle) {
            if (m_suggestionTimer && m_suggestionTimer->isActive()) {
                m_suggestionTimer->stop();
                qDebug() << "ClaudeSession: Cancelled suggestion timer (state changed)";
            }
            if (m_suggestionFallbackTimer && m_suggestionFallbackTimer->isActive()) {
                m_suggestionFallbackTimer->stop();
                qDebug() << "ClaudeSession: Cancelled suggestion fallback (state changed)";
            }
            // Reset idle detection since hooks are delivering events
            m_idlePromptDetected = false;
            m_hookDeliveredIdle = false;
            return;
        }

        // State is Idle — decide what to fire
        // NOTE: We no longer call stopIdlePolling() here. The poller's own guards
        // (state checks, cooldown flag) prevent spam, and polling must stay alive so
        // it can re-attempt Tab+Enter when Claude stays Idle on background tabs.

        // When an agent team is active, suppress keyboard-based L2/L3.
        // Hook-based TeammateIdle continuation handles auto-continue for teams.
        if (hasActiveTeam()) {
            qDebug() << "ClaudeSession: Team active - suppressing keyboard-based L2/L3 yolo";
            return;
        }

        if (m_budgetController && m_budgetController->shouldBlockYolo()) {
            qDebug() << "ClaudeSession: Budget gate blocking L2/L3 yolo";
            return;
        }

        // Signal hook-based idle to suppress polling for a cooldown window.
        // This prevents both hook-based AND polling-based yolo from firing
        // for the same idle event (double-fire).
        m_hookDeliveredIdle = true;
        QTimer::singleShot(10000, this, [this]() {
            m_hookDeliveredIdle = false;
        });

        if (m_doubleYoloMode && (m_trySuggestionsFirst || !m_tripleYoloMode)) {
            // Double yolo fires first: Tab + Enter to accept suggestion
            // Use 5s delay to give Claude's inline suggestion time to appear
            // (suggestions can take several seconds depending on API latency)
            // Also gives user time to start typing, which cancels via state change
            qDebug() << "ClaudeSession: Auto-accepting suggestion (double yolo mode) in 5s";
            if (!m_suggestionTimer) {
                m_suggestionTimer = new QTimer(this);
                m_suggestionTimer->setSingleShot(true);
                connect(m_suggestionTimer, &QTimer::timeout, this, [this]() {
                    if (m_doubleYoloMode) {
                        autoAcceptSuggestion();
                    }
                });
            }
            m_suggestionTimer->start(5000);

            // If triple yolo is also on, schedule fallback
            if (m_tripleYoloMode) {
                scheduleSuggestionFallback();
            }
        } else if (m_tripleYoloMode) {
            // Triple yolo fires directly (trySuggestionsFirst is off, or double yolo is off)
            qDebug() << "ClaudeSession: Auto-continuing (triple yolo mode)";
            QTimer::singleShot(500, this, [this]() {
                sendPrompt(m_autoContinuePrompt);
                logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);
            });
        }
    });

    // Handle subagent/team events
    connect(m_claudeProcess,
            &ClaudeProcess::subagentStarted,
            this,
            [this](const QString &agentId, const QString &agentType, const QString &parentTranscriptPath) {
                SubagentInfo info;
                info.agentId = agentId;
                info.agentType = agentType;
                info.state = ClaudeProcess::State::Working;
                info.startedAt = QDateTime::currentDateTime();
                info.lastUpdated = info.startedAt;

                // Derive subagent transcript path eagerly from the parent transcript path.
                // Parent: ~/.claude/projects/{proj}/{uuid}.jsonl
                // Subagent: ~/.claude/projects/{proj}/{uuid}/subagents/agent-{id}.jsonl
                // (strip .jsonl from parent, append /subagents/agent-{id}.jsonl)
                if (!parentTranscriptPath.isEmpty()) {
                    QString base = parentTranscriptPath;
                    if (base.endsWith(QStringLiteral(".jsonl"))) {
                        base.chop(6); // remove ".jsonl"
                    }
                    info.transcriptPath = base + QStringLiteral("/subagents/agent-") + agentId + QStringLiteral(".jsonl");
                    qDebug() << "ClaudeSession: Derived subagent transcript:" << info.transcriptPath;
                }

                m_subagents.insert(agentId, info);
                qDebug() << "ClaudeSession: Subagent started -" << agentId << agentType << "total:" << m_subagents.size();
                Q_EMIT subagentStarted(agentId);
            });

    connect(m_claudeProcess, &ClaudeProcess::subagentStopped, this, [this](const QString &agentId, const QString &agentType, const QString &transcriptPath) {
        Q_UNUSED(agentType);
        if (m_subagents.contains(agentId)) {
            m_subagents[agentId].state = ClaudeProcess::State::NotRunning;
            m_subagents[agentId].lastUpdated = QDateTime::currentDateTime();
            // Use agent_transcript_path from SubagentStop as authoritative override
            if (!transcriptPath.isEmpty()) {
                m_subagents[agentId].transcriptPath = transcriptPath;
            }
        }
        qDebug() << "ClaudeSession: Subagent stopped -" << agentId << "remaining active:" << (hasActiveTeam() ? "yes" : "no");
        Q_EMIT subagentStopped(agentId);
    });

    connect(m_claudeProcess, &ClaudeProcess::teammateIdle, this, [this](const QString &teammateName, const QString &tName) {
        if (!tName.isEmpty()) {
            m_teamName = tName;
        }
        // Find and update the subagent by teammate name
        for (auto it = m_subagents.begin(); it != m_subagents.end(); ++it) {
            if (it->teammateName == teammateName || (it->teammateName.isEmpty() && it->state == ClaudeProcess::State::Working)) {
                it->teammateName = teammateName;
                it->state = ClaudeProcess::State::Idle;
                it->lastUpdated = QDateTime::currentDateTime();
                break;
            }
        }
        Q_EMIT teamInfoChanged();
    });

    connect(m_claudeProcess,
            &ClaudeProcess::taskCompleted,
            this,
            [this](const QString &taskId, const QString &taskSubject, const QString &teammateName, const QString &tName) {
                Q_UNUSED(taskId);
                if (!tName.isEmpty()) {
                    m_teamName = tName;
                }
                // Update the subagent's current task subject
                for (auto it = m_subagents.begin(); it != m_subagents.end(); ++it) {
                    if (it->teammateName == teammateName) {
                        it->currentTaskSubject = taskSubject;
                        break;
                    }
                }
                Q_EMIT teamInfoChanged();
            });

    // Stop polling when team starts (first subagent) to avoid interfering keystrokes
    connect(this, &ClaudeSession::subagentStarted, this, [this]() {
        if (m_permissionPollTimer && m_permissionPollTimer->isActive()) {
            qDebug() << "ClaudeSession: Stopping permission polling (team active)";
            m_permissionPollTimer->stop();
        }
        if (m_idlePollTimer && m_idlePollTimer->isActive()) {
            qDebug() << "ClaudeSession: Stopping idle polling (team active)";
            m_idlePollTimer->stop();
        }
    });

    // Resume polling when team dissolves (last subagent stops)
    connect(this, &ClaudeSession::subagentStopped, this, [this]() {
        if (!hasActiveTeam()) {
            qDebug() << "ClaudeSession: Team dissolved - resuming polling if needed";
            if (m_yoloMode) {
                startPermissionPolling();
            }
            if (m_doubleYoloMode || m_tripleYoloMode) {
                startIdlePolling();
            }
        }
    });

    // Handle yolo auto-approvals from hook handler
    connect(m_claudeProcess, &ClaudeProcess::yoloApprovalOccurred, this, [this](const QString &toolName) {
        qDebug() << "ClaudeSession: Yolo hook auto-approved:" << toolName;
        logApproval(toolName, QStringLiteral("auto-approved"), 1);
    });

    // Refresh token usage when Claude finishes a task (state → Idle)
    connect(m_claudeProcess, &ClaudeProcess::stateChanged, this, [this](ClaudeProcess::State newState) {
        if (newState == ClaudeProcess::State::Idle) {
            refreshTokenUsage();
        }
    });
}

void ClaudeSession::setTaskDescription(const QString &desc)
{
    m_taskDescription = desc;
    // Update tab title to include task description
    QString projectName = QDir(m_workingDir).dirName();
    if (!projectName.isEmpty()) {
        QString displayName = buildDisplayName(projectName, m_taskDescription, m_sessionId, m_sshHost);
        setTitle(Konsole::Session::NameRole, displayName);
        setTitle(Konsole::Session::DisplayedTitleRole, displayName);
        setTabTitleFormat(Konsole::Session::LocalTabTitle, displayName);
        setTabTitleFormat(Konsole::Session::RemoteTabTitle, displayName);
        tabTitleSetByUser(true);
    }

    // Update the registry so the description persists
    if (auto *registry = ClaudeSessionRegistry::instance()) {
        if (registry->findSession(m_sessionName)) {
            // Re-register to update state (including taskDescription)
            registry->registerSession(this);
        }
    }

    Q_EMIT taskDescriptionChanged();
}

ClaudeProcess::State ClaudeSession::claudeState() const
{
    return m_claudeProcess ? m_claudeProcess->state() : ClaudeProcess::State::NotRunning;
}

BudgetController *ClaudeSession::budgetController()
{
    if (!m_budgetController) {
        m_budgetController = new BudgetController(const_cast<ClaudeSession *>(this));

        // Wire token usage changes to the budget controller
        connect(this, &ClaudeSession::tokenUsageChanged, m_budgetController, [this]() {
            m_budgetController->onTokenUsageChanged(m_tokenUsage);
        });

        // Wire resource usage changes to the budget controller
        connect(this, &ClaudeSession::resourceUsageChanged, m_budgetController, [this]() {
            m_budgetController->onResourceUsageChanged(m_resourceUsage);
        });
    }
    return m_budgetController;
}

SessionObserver *ClaudeSession::sessionObserver()
{
    if (!m_sessionObserver) {
        m_sessionObserver = new SessionObserver(this);

        // Wire state changes
        connect(this, &ClaudeSession::stateChanged, m_sessionObserver, [this](ClaudeProcess::State newState) {
            m_sessionObserver->onStateChanged(static_cast<int>(newState));
        });

        // Wire token usage changes
        connect(this, &ClaudeSession::tokenUsageChanged, m_sessionObserver, [this]() {
            m_sessionObserver->onTokenUsageChanged(m_tokenUsage.inputTokens,
                                                   m_tokenUsage.outputTokens,
                                                   m_tokenUsage.totalTokens(),
                                                   m_tokenUsage.estimatedCostUSD());
        });

        // Wire approval logging
        connect(this, &ClaudeSession::approvalLogged, m_sessionObserver, [this](const ApprovalLogEntry &entry) {
            m_sessionObserver->onApprovalLogged(entry.toolName, entry.yoloLevel, entry.timestamp);
        });

        // Wire subagent events
        connect(this, &ClaudeSession::subagentStarted, m_sessionObserver, &SessionObserver::onSubagentStarted);
        connect(this, &ClaudeSession::subagentStopped, m_sessionObserver, &SessionObserver::onSubagentStopped);
    }
    return m_sessionObserver;
}

QString ClaudeSession::shellCommand() const
{
    // Remote SSH session: wrap everything in ssh -t
    if (m_isRemote) {
        return buildRemoteSshCommand();
    }

    if (m_isReattach) {
        // Attach to existing session
        return m_tmuxManager->buildAttachCommand(m_sessionName);
    }

    // Create new session or attach if exists
    QStringList extraArgs;
    if (!m_resumeSessionId.isEmpty()) {
        extraArgs << QStringLiteral("--resume") << m_resumeSessionId;
    }

    QString claudeCmd = ClaudeProcess::buildCommand(m_claudeModel, QString(), extraArgs);

    return m_tmuxManager->buildNewSessionCommand(
        m_sessionName,
        claudeCmd,
        true,  // attachExisting
        m_workingDir
    );
}

QString ClaudeSession::buildRemoteSshCommand() const
{
    // Build: ssh -t -R PORT:localhost:PORT [options] host "setup && tmux new-session -A -s name -c /path -- claude [args]"
    QStringList sshArgs;
    sshArgs << QStringLiteral("-t"); // Force TTY allocation

    // Add SSH reverse tunnel for hook events (TCP port tunneled back to local Konsolai)
    quint16 tunnelPort = 0;
    if (m_hookHandler && m_hookHandler->mode() == ClaudeHookHandler::TCP) {
        tunnelPort = m_hookHandler->tcpPort();
        if (tunnelPort > 0) {
            // -R remotePort:localhost:localPort - tunnel remote port back to local
            sshArgs << QStringLiteral("-R") << QStringLiteral("%1:localhost:%1").arg(tunnelPort);
        }
    }

    // Add port if non-default
    if (m_sshPort != 22 && m_sshPort > 0) {
        sshArgs << QStringLiteral("-p") << QString::number(m_sshPort);
    }

    // Build target: user@host or just host
    QString target;
    if (!m_sshUsername.isEmpty()) {
        target = QStringLiteral("%1@%2").arg(m_sshUsername, m_sshHost);
    } else {
        target = m_sshHost;
    }
    sshArgs << target;

    // Use the working directory on the remote
    QString remoteWorkDir = m_workingDir;
    if (remoteWorkDir.isEmpty()) {
        remoteWorkDir = QStringLiteral("~");
    }

    // Build claude args
    QStringList claudeArgs;
    if (!m_resumeSessionId.isEmpty()) {
        claudeArgs << QStringLiteral("--resume") << m_resumeSessionId;
    }
    QString claudeCmd = QStringLiteral("claude");
    if (!claudeArgs.isEmpty()) {
        claudeCmd += QStringLiteral(" ") + claudeArgs.join(QStringLiteral(" "));
    }

    // Build the remote command
    // If we have a tunnel port, inject the hook script and config before starting tmux
    QString remoteCmd;
    if (tunnelPort > 0 && m_hookHandler) {
        // Generate the hook script and config
        QString hookScript = m_hookHandler->generateRemoteHookScript(tunnelPort);
        QString scriptPath = QStringLiteral("/tmp/konsolai-hook-%1.sh").arg(m_sessionId);
        QString hooksConfig = m_hookHandler->generateRemoteHooksConfig(tunnelPort, scriptPath);

        // Create setup commands:
        // 1. Write hook script to temp file
        // 2. Make it executable
        // 3. Ensure .claude directory exists
        // 4. Write/merge hooks config to settings.local.json
        // 5. Run tmux with claude

        // Escape single quotes in the script and config for shell embedding
        QString escapedScript = hookScript;
        escapedScript.replace(QStringLiteral("'"), QStringLiteral("'\\''"));

        QString escapedConfig = hooksConfig;
        escapedConfig.replace(QStringLiteral("'"), QStringLiteral("'\\''"));

        // Build the compound remote command
        // Use python3 to merge hooks into existing settings.local.json
        // (preserves existing allowed tools, permissions, etc.)
        remoteCmd = QStringLiteral(
                        "cat > '%1' << 'KONSOLAI_HOOK_EOF'\n%2\nKONSOLAI_HOOK_EOF\n"
                        "chmod +x '%1' && "
                        "mkdir -p '%3/.claude' && "
                        "python3 -c \""
                        "import json,sys,os; "
                        "p='%3/.claude/settings.local.json'; "
                        "e={}; "
                        "r=open(p) if os.path.exists(p) else None; "
                        "e=json.load(r) if r else {}; "
                        "r and r.close(); "
                        "n=json.loads(sys.argv[1]); "
                        "e['hooks']=n.get('hooks',{}); "
                        "f=open(p,'w'); json.dump(e,f,indent=2); f.close()"
                        "\" '%4' && "
                        "tmux new-session -A -s %5 -c %3 -- %6 \\; set-option -p allow-passthrough off")
                        .arg(scriptPath, hookScript, remoteWorkDir, escapedConfig, m_sessionName, claudeCmd);
    } else {
        // No tunnel - just run tmux directly
        remoteCmd =
            QStringLiteral("tmux new-session -A -s %1 -c %2 -- %3 \\; set-option -p allow-passthrough off").arg(m_sessionName, remoteWorkDir, claudeCmd);
    }

    // Quote the entire remote command
    sshArgs << remoteCmd;

    // Build full ssh command
    return QStringLiteral("ssh ") + sshArgs.join(QStringLiteral(" "));
}

void ClaudeSession::detach()
{
    // Detach: disconnect from tmux session while keeping it running.
    // Use tmux detach-client to cleanly detach.
    // Don't emit signals or call close() - let tmux disconnection
    // naturally trigger the session end.

    qDebug() << "ClaudeSession::detach() called for session:" << m_sessionName;

    if (!m_sessionName.isEmpty()) {
        QProcess process;
        process.start(QStringLiteral("tmux"), {QStringLiteral("detach-client"), QStringLiteral("-s"), m_sessionName});
        process.waitForFinished(5000);
        qDebug() << "ClaudeSession::detach() - tmux detach-client exit code:" << process.exitCode();
    }

    // tmux detach-client will cause the "tmux attach" command in our terminal to exit,
    // which will naturally trigger session termination through normal Konsole flow
}

void ClaudeSession::kill()
{
    if (m_tmuxManager) {
        m_tmuxManager->killSession(m_sessionName);
    }
    Q_EMIT killed();
}

void ClaudeSession::sendText(const QString &text)
{
    if (m_tmuxManager) {
        m_tmuxManager->sendKeys(m_sessionName, text);
    }
}

QString ClaudeSession::transcript(int lines)
{
    if (m_tmuxManager) {
        return m_tmuxManager->capturePane(m_sessionName, -lines, 0);
    }
    return QString();
}

// D-Bus property accessors
QString ClaudeSession::stateString() const
{
    if (!m_claudeProcess) {
        return QStringLiteral("NotRunning");
    }

    switch (m_claudeProcess->state()) {
    case ClaudeProcess::State::NotRunning:
        return QStringLiteral("NotRunning");
    case ClaudeProcess::State::Starting:
        return QStringLiteral("Starting");
    case ClaudeProcess::State::Idle:
        return QStringLiteral("Idle");
    case ClaudeProcess::State::Working:
        return QStringLiteral("Working");
    case ClaudeProcess::State::WaitingInput:
        return QStringLiteral("WaitingInput");
    case ClaudeProcess::State::Error:
        return QStringLiteral("Error");
    default:
        return QStringLiteral("Unknown");
    }
}

QString ClaudeSession::currentTask() const
{
    return m_claudeProcess ? m_claudeProcess->currentTask() : QString();
}

// D-Bus methods
void ClaudeSession::sendPrompt(const QString &prompt)
{
    // Send text and Enter separately. Claude Code's Ink UI interprets a
    // literal \r inside a -l send-keys as a newline within the text field,
    // not as form submission. Sending Enter via sendKeySequence (without -l)
    // correctly triggers the submit action.
    if (m_tmuxManager) {
        m_tmuxManager->sendKeys(m_sessionName, prompt);
        QTimer::singleShot(150, this, [this]() {
            if (m_tmuxManager) {
                m_tmuxManager->sendKeySequence(m_sessionName, QStringLiteral("Enter"));
            }
        });
    }
}

void ClaudeSession::approvePermission()
{
    // Claude Code uses a selection UI where option 1 (Yes) is pre-selected
    // Just send Enter to confirm the selection
    sendText(QStringLiteral("\n"));
}

void ClaudeSession::approvePermissionAlways()
{
    if (!m_tmuxManager) {
        return;
    }

    // Capture the terminal to find where the cursor (❯) is and where the
    // "Always allow" option is, then navigate precisely.
    QPointer<ClaudeSession> guard(this);
    m_tmuxManager->capturePaneAsync(m_sessionName, [this, guard](bool ok, const QString &output) {
        if (!guard || !ok || !m_tmuxManager) {
            return;
        }

        // Parse the bottom of the terminal for the permission menu.
        // Claude Code's permission UI looks like:
        //   ❯ Allow once           (or "Yes")
        //     Always allow          (target)
        //     Deny                  (or "No")
        // The ❯ marks the currently selected line.
        const auto lines = output.split(QLatin1Char('\n'));

        int cursorLine = -1;
        int alwaysLine = -1;

        // Scan from the bottom (prompt is at the bottom of the terminal)
        for (int i = lines.size() - 1; i >= 0 && i >= lines.size() - 15; --i) {
            const QString &line = lines[i];
            if (cursorLine < 0 && line.contains(QStringLiteral("❯"))) {
                cursorLine = i;
            }
            if (alwaysLine < 0 && line.contains(QStringLiteral("Always"), Qt::CaseInsensitive)) {
                alwaysLine = i;
            }
        }

        if (cursorLine >= 0 && alwaysLine >= 0) {
            int delta = alwaysLine - cursorLine;
            QString key = delta > 0 ? QStringLiteral("Down") : QStringLiteral("Up");
            int steps = qAbs(delta);
            for (int i = 0; i < steps; ++i) {
                m_tmuxManager->sendKeySequence(m_sessionName, key);
            }
        }

        // Confirm selection
        QTimer::singleShot(50, this, [this]() {
            sendText(QStringLiteral("\n"));
        });
    });
}

void ClaudeSession::denyPermission()
{
    // Send 'n' followed by Enter to deny
    sendText(QStringLiteral("n\n"));
}

void ClaudeSession::stop()
{
    // Send Ctrl+C to stop Claude (use sendKeySequence, not sendKeys,
    // because sendKeys uses -l which would type literal "C-c")
    if (m_tmuxManager) {
        m_tmuxManager->sendKeySequence(m_sessionName, QStringLiteral("C-c"));
    }
}

void ClaudeSession::restart()
{
    // Kill and restart by sending a new claude command
    stop();
    // Wait a bit, then send claude command
    // This is a simple implementation - could be improved
    sendText(QStringLiteral("claude\n"));
}

QString ClaudeSession::getTranscript(int lines)
{
    return transcript(lines);
}

void ClaudeSession::setYoloMode(bool enabled)
{
    if (m_yoloMode != enabled) {
        m_yoloMode = enabled;
        qDebug() << "ClaudeSession::setYoloMode:" << enabled << "current state:" << static_cast<int>(claudeState());
        Q_EMIT yoloModeChanged(enabled);

        // Write yolo state file for hook handler to read
        // File location: same as socket but with .yolo extension
        if (m_hookHandler) {
            QString yoloPath = m_hookHandler->socketPath();
            yoloPath.replace(QStringLiteral(".sock"), QStringLiteral(".yolo"));

            if (enabled) {
                // Create the yolo file
                QFile yoloFile(yoloPath);
                if (yoloFile.open(QIODevice::WriteOnly)) {
                    yoloFile.write("1");
                    yoloFile.close();
                    qDebug() << "ClaudeSession: Created yolo state file:" << yoloPath;
                }
                // Also start terminal polling as a fallback
                startPermissionPolling();

                // If a permission prompt is already showing, approve it now
                // (unless budget gate is active)
                if (claudeState() == ClaudeProcess::State::WaitingInput && !(m_budgetController && m_budgetController->shouldBlockYolo())) {
                    qDebug() << "ClaudeSession: Permission prompt already showing - auto-approving always immediately";
                    QTimer::singleShot(100, this, [this]() {
                        approvePermissionAlways();
                        logApproval(QStringLiteral("permission"), QStringLiteral("auto-approved"), 1);
                    });
                }
            } else {
                // Remove the yolo file
                if (QFile::exists(yoloPath)) {
                    QFile::remove(yoloPath);
                    qDebug() << "ClaudeSession: Removed yolo state file:" << yoloPath;
                }
                stopPermissionPolling();
            }
        }
    }
}

void ClaudeSession::startPermissionPolling()
{
    if (!m_permissionPollTimer) {
        m_permissionPollTimer = new QTimer(this);
        connect(m_permissionPollTimer, &QTimer::timeout, this, &ClaudeSession::pollForPermissionPrompt);
    }

    if (!m_permissionPollTimer->isActive()) {
        qDebug() << "ClaudeSession: Starting permission polling for yolo mode";
        m_permissionPollTimer->start(300); // Poll every 300ms
    }
}

void ClaudeSession::stopPermissionPolling()
{
    if (m_permissionPollTimer && m_permissionPollTimer->isActive()) {
        qDebug() << "ClaudeSession: Stopping permission polling";
        m_permissionPollTimer->stop();
    }
    m_permissionPromptDetected = false;
}

void ClaudeSession::pollForPermissionPrompt()
{
    if (!m_yoloMode || !m_tmuxManager) {
        return;
    }

    // Budget gate: block yolo when budget exceeded or resources critical
    if (m_budgetController && m_budgetController->shouldBlockYolo()) {
        return;
    }

    // NOTE: We intentionally do NOT suppress L1 polling when a team is active.
    // Permission prompts appear in the parent terminal and approving them
    // (Down+Enter to select "Always allow") does not interfere with subagents,
    // which communicate via Claude Code's internal APIs.  Suppressing L1 here
    // caused a deadlock when hooks were stale: SubagentStop never arrived,
    // hasActiveTeam() stayed true, and all yolo paths were blocked.

    // Skip if an async capture is already in flight
    if (m_permissionPollInFlight) {
        return;
    }

    // Capture the full visible pane then check only the last 5 lines.
    // The permission prompt UI appears at the very bottom of the terminal.
    // NOTE: We must NOT pass -S/-E with negative values — tmux treats those
    // as scrollback history lines, not bottom-of-screen lines.
    m_permissionPollInFlight = true;
    QPointer<ClaudeSession> guard(this);
    m_tmuxManager->capturePaneAsync(m_sessionName, [this, guard](bool ok, const QString &output) {
        if (!guard) {
            return;
        }
        m_permissionPollInFlight = false;
        if (!ok || !m_yoloMode) {
            return;
        }

        // Extract last 5 lines from the full capture
        const auto allLines = output.split(QLatin1Char('\n'));
        const int bottomCount = 5;
        const int startIdx = qMax(0, allLines.size() - bottomCount);
        QString bottomLines;
        for (int i = startIdx; i < allLines.size(); ++i) {
            if (!bottomLines.isEmpty()) {
                bottomLines += QLatin1Char('\n');
            }
            bottomLines += allLines[i];
        }

        if (detectPermissionPrompt(bottomLines)) {
            if (!m_permissionPromptDetected) {
                m_permissionPromptDetected = true;
                qDebug() << "ClaudeSession: Permission prompt detected - auto-approving (yolo mode)";

                // Send approval with small delay to ensure prompt is ready
                QTimer::singleShot(50, this, [this]() {
                    approvePermissionAlways();
                    logApproval(QStringLiteral("permission"), QStringLiteral("auto-approved"), 1);

                    // Reset detection flag after a longer cooldown to avoid
                    // rapid-fire false positives from stale terminal content.
                    QTimer::singleShot(2000, this, [this]() {
                        m_permissionPromptDetected = false;
                    });
                });
            }
        } else {
            m_permissionPromptDetected = false;
        }
    });
}

bool ClaudeSession::detectPermissionPrompt(const QString &terminalOutput)
{
    // Match the Claude Code interactive permission selection UI that appears
    // at the bottom of the terminal.  We check individual LINES so that the
    // selector arrow (❯) must be on the SAME line as the approval keyword —
    // not just somewhere in the same 5-line window.
    //
    // Claude Code formats vary across versions:
    //   ❯ Yes                        (compact form)
    //   ❯ Yes, allow once            (verbose form)
    //   ❯ Allow once                 (alternate wording)
    //   ❯ Always allow               (second option, when pre-selected differently)

    const auto lines = terminalOutput.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.contains(QStringLiteral("❯"))) {
            continue;
        }
        // Selector arrow is on this line — check for permission-related keywords
        if (line.contains(QStringLiteral("Yes"), Qt::CaseInsensitive) || line.contains(QStringLiteral("Allow"), Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

void ClaudeSession::startIdlePolling()
{
    if (!m_idlePollTimer) {
        m_idlePollTimer = new QTimer(this);
        connect(m_idlePollTimer, &QTimer::timeout, this, &ClaudeSession::pollForIdlePrompt);
    }

    if (!m_idlePollTimer->isActive()) {
        qDebug() << "ClaudeSession: Starting idle polling for triple yolo mode";
        m_idlePollTimer->start(2000); // Poll every 2s
    }
}

void ClaudeSession::stopIdlePolling()
{
    if (m_idlePollTimer && m_idlePollTimer->isActive()) {
        qDebug() << "ClaudeSession: Stopping idle polling";
        m_idlePollTimer->stop();
    }
    m_idlePromptDetected = false;
}

void ClaudeSession::pollForIdlePrompt()
{
    if (!m_doubleYoloMode && !m_tripleYoloMode) {
        return;
    }
    if (!m_tmuxManager) {
        return;
    }

    // Budget gate: block yolo when budget exceeded or resources critical
    if (m_budgetController && m_budgetController->shouldBlockYolo()) {
        return;
    }

    // When a team is active, hooks handle continuation — polling would send
    // keystrokes to the parent tmux pane which interferes with subagents
    if (hasActiveTeam()) {
        return;
    }

    // Skip if an async capture is already in flight
    if (m_idlePollInFlight) {
        return;
    }

    // Skip if Claude is actively working or waiting for permission input (hook confirmed)
    if (claudeState() == ClaudeProcess::State::Working || claudeState() == ClaudeProcess::State::WaitingInput) {
        m_idlePromptDetected = false;
        return;
    }

    // Skip if hook-based idle already triggered yolo actions recently.
    // This prevents both hook and polling from firing for the same idle event.
    if (m_hookDeliveredIdle) {
        return;
    }

    // NOTE: We no longer skip when claudeState() == Idle. That's precisely
    // the case we need to handle — hooks delivered Idle once, but the signal
    // handler's Tab+Enter was a no-op (no suggestion present at the time).
    // Polling can re-detect idle and retry.

    // Capture full visible pane then check the last 3 lines for the idle prompt.
    // NOTE: We must NOT pass -S/-E with negative values — tmux treats those
    // as scrollback history lines, not bottom-of-screen lines.
    m_idlePollInFlight = true;
    QPointer<ClaudeSession> guard(this);
    m_tmuxManager->capturePaneAsync(m_sessionName, [this, guard](bool ok, const QString &output) {
        if (!guard) {
            return;
        }
        m_idlePollInFlight = false;
        if (!ok) {
            return;
        }
        if (!m_doubleYoloMode && !m_tripleYoloMode) {
            return;
        }

        // Extract last 3 lines from the full capture
        const auto allLines = output.split(QLatin1Char('\n'));
        const int bottomCount = 3;
        const int startIdx = qMax(0, allLines.size() - bottomCount);
        QString bottomLines;
        for (int i = startIdx; i < allLines.size(); ++i) {
            if (!bottomLines.isEmpty()) {
                bottomLines += QLatin1Char('\n');
            }
            bottomLines += allLines[i];
        }

        if (detectIdlePrompt(bottomLines)) {
            if (!m_idlePromptDetected) {
                m_idlePromptDetected = true;

                if (m_doubleYoloMode && (m_trySuggestionsFirst || !m_tripleYoloMode)) {
                    // Double yolo via polling: Tab+Enter to accept suggestion
                    qDebug() << "ClaudeSession: Idle detected via polling - auto-accepting suggestion (double yolo)";
                    QTimer::singleShot(500, this, [this]() {
                        if (m_doubleYoloMode) {
                            autoAcceptSuggestion();

                            // If triple yolo is also on, schedule fallback
                            if (m_tripleYoloMode) {
                                scheduleSuggestionFallback();
                            }
                        }

                        // Cooldown to avoid rapid-fire on stale output
                        QTimer::singleShot(5000, this, [this]() {
                            m_idlePromptDetected = false;
                        });
                    });
                } else if (m_tripleYoloMode) {
                    // Triple yolo fires directly (trySuggestionsFirst off, or double yolo off)
                    qDebug() << "ClaudeSession: Idle detected via polling - auto-continuing (triple yolo)";
                    QTimer::singleShot(500, this, [this]() {
                        if (m_tripleYoloMode) {
                            sendPrompt(m_autoContinuePrompt);
                            logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);

                            // Cooldown to avoid rapid-fire on stale output
                            QTimer::singleShot(5000, this, [this]() {
                                m_idlePromptDetected = false;
                            });
                        }
                    });
                }
            }
        } else {
            m_idlePromptDetected = false;
        }
    });
}

bool ClaudeSession::detectIdlePrompt(const QString &terminalOutput)
{
    // Claude Code's Ink UI shows a prompt like ">" or "❯" on the last line
    // when idle and waiting for user input.  We look for this WITHOUT any
    // permission prompt or selection UI present.
    const auto lines = terminalOutput.split(QLatin1Char('\n'));

    // Skip if this looks like a permission prompt (reuse same keywords as detectPermissionPrompt)
    for (const QString &line : lines) {
        if (line.contains(QStringLiteral("❯"))
            && (line.contains(QStringLiteral("Yes"), Qt::CaseInsensitive) || line.contains(QStringLiteral("Allow"), Qt::CaseInsensitive))) {
            return false;
        }
        if (line.contains(QStringLiteral("Allow"), Qt::CaseInsensitive) || line.contains(QStringLiteral("Deny"), Qt::CaseInsensitive)) {
            return false;
        }
    }

    // Check last non-empty line for the input prompt indicator
    for (int i = lines.size() - 1; i >= 0; --i) {
        QString trimmed = lines[i].trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        // Claude Code shows ">" as the input prompt when idle.
        // It may also show "❯" in some contexts.
        // The line should be short (just the prompt character, maybe with project name).
        if (trimmed == QStringLiteral(">") || trimmed.endsWith(QStringLiteral(">")) || trimmed.startsWith(QStringLiteral(">"))) {
            return true;
        }
        break; // Only check the last non-empty line
    }

    return false;
}

void ClaudeSession::startTokenTracking()
{
    if (!m_tokenRefreshTimer) {
        m_tokenRefreshTimer = new QTimer(this);
        connect(m_tokenRefreshTimer, &QTimer::timeout, this, &ClaudeSession::refreshTokenUsage);
    }
    // Refresh every 30s as a background poll; also refreshes on Idle state changes
    m_tokenRefreshTimer->start(30000);

    // Initial refresh
    refreshTokenUsage();
}

void ClaudeSession::refreshTokenUsage()
{
    if (m_workingDir.isEmpty()) {
        return;
    }

    // Convert working dir to Claude's hashed project path
    QString hashedName = m_workingDir;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));
    QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;

    if (!QDir(projectDir).exists()) {
        return;
    }

    // Find the most recently modified JSONL file
    QString newestFile;
    QDateTime newestTime;

    QDirIterator it(projectDir, {QStringLiteral("*.jsonl")}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        QDateTime mtime = it.fileInfo().lastModified();
        if (!newestTime.isValid() || mtime > newestTime) {
            newestTime = mtime;
            newestFile = it.filePath();
        }
    }

    if (newestFile.isEmpty()) {
        return;
    }

    // If the file changed, reset incremental state
    if (newestFile != m_lastTokenFile) {
        m_lastTokenFile = newestFile;
        m_lastTokenFilePos = 0;
        m_tokenUsage = TokenUsage();
    }

    TokenUsage usage = parseConversationTokens(newestFile);
    if (usage.totalTokens() != m_tokenUsage.totalTokens()) {
        m_tokenUsage = usage;
        Q_EMIT tokenUsageChanged();
    }
}

TokenUsage ClaudeSession::parseConversationTokens(const QString &jsonlPath)
{
    // Incremental parsing: start from where we left off last time.
    // m_tokenUsage holds the accumulated total; we add new lines to it.
    TokenUsage usage = m_tokenUsage;

    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return usage;
    }

    // Seek to last known position to avoid re-parsing the entire file
    if (m_lastTokenFilePos > 0 && m_lastTokenFilePos <= file.size()) {
        file.seek(m_lastTokenFilePos);
    } else if (m_lastTokenFilePos > file.size()) {
        // File was truncated/replaced — re-parse from scratch
        m_lastTokenFilePos = 0;
        usage = TokenUsage();
    }

    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("assistant")) {
            continue;
        }

        // Token data is in message.usage
        QJsonObject message = obj.value(QStringLiteral("message")).toObject();
        QJsonObject usageObj = message.value(QStringLiteral("usage")).toObject();

        if (usageObj.isEmpty()) {
            continue;
        }

        usage.inputTokens += usageObj.value(QStringLiteral("input_tokens")).toInteger();
        usage.outputTokens += usageObj.value(QStringLiteral("output_tokens")).toInteger();
        usage.cacheReadTokens += usageObj.value(QStringLiteral("cache_read_input_tokens")).toInteger();
        usage.cacheCreationTokens += usageObj.value(QStringLiteral("cache_creation_input_tokens")).toInteger();
    }

    // Save position for next incremental parse
    m_lastTokenFilePos = file.pos();

    return usage;
}

void ClaudeSession::autoAcceptSuggestion()
{
    if (!m_tmuxManager || !m_doubleYoloMode) {
        return;
    }

    // Budget gate: block yolo when budget exceeded or resources critical
    if (m_budgetController && m_budgetController->shouldBlockYolo()) {
        qDebug() << "ClaudeSession: Budget gate blocking auto-accept suggestion";
        return;
    }

    // When a team is active, suppress keyboard-based suggestion acceptance
    if (hasActiveTeam()) {
        return;
    }

    // Press Tab to accept any visible suggestion in Claude's input,
    // then Enter to submit it.  If there is no suggestion, Tab is a
    // no-op in Claude Code's Ink UI and Enter on an empty prompt is
    // ignored, so this is safe to fire speculatively.
    m_tmuxManager->sendKeySequence(m_sessionName, QStringLiteral("Tab"));
    QTimer::singleShot(100, this, [this]() {
        approvePermission(); // sends Enter

        // Defer approval counter: wait 1.5s then check if Claude left Idle.
        // Only count it as a real approval if the suggestion was actually
        // accepted (i.e., Claude started working on it).
        QTimer::singleShot(1500, this, [this]() {
            if (claudeState() != ClaudeProcess::State::Idle && claudeState() != ClaudeProcess::State::NotRunning) {
                logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);
            }
        });
    });
}

void ClaudeSession::scheduleSuggestionFallback()
{
    if (!m_suggestionFallbackTimer) {
        m_suggestionFallbackTimer = new QTimer(this);
        m_suggestionFallbackTimer->setSingleShot(true);
        connect(m_suggestionFallbackTimer, &QTimer::timeout, this, [this]() {
            // Only fire if Claude is still idle (suggestion wasn't accepted)
            if (m_tripleYoloMode && claudeState() == ClaudeProcess::State::Idle) {
                if (m_budgetController && m_budgetController->shouldBlockYolo()) {
                    qDebug() << "ClaudeSession: Budget gate blocking suggestion fallback";
                    return;
                }
                qDebug() << "ClaudeSession: Suggestion fallback - auto-continuing (triple yolo)";
                sendPrompt(m_autoContinuePrompt);
                logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);
            }
        });
    }

    // 2s after double yolo fires, check if Claude is still idle
    qDebug() << "ClaudeSession: Scheduling suggestion fallback in 2000ms";
    m_suggestionFallbackTimer->start(2000);
}

void ClaudeSession::setDoubleYoloMode(bool enabled)
{
    if (m_doubleYoloMode != enabled) {
        m_doubleYoloMode = enabled;
        Q_EMIT doubleYoloModeChanged(enabled);

        if (enabled) {
            // Start idle polling so double yolo can re-fire on background tabs
            startIdlePolling();

            // If Claude appears idle, apply immediately with new precedence.
            // Use m_suggestionTimer (not a one-shot) so that if stateChanged(Idle) also
            // fires, both paths share the same timer and can't double-fire.
            auto state = claudeState();
            if (state == ClaudeProcess::State::Idle || state == ClaudeProcess::State::NotRunning) {
                if (m_trySuggestionsFirst || !m_tripleYoloMode) {
                    qDebug() << "ClaudeSession: Claude state" << static_cast<int>(state) << "- auto-accepting suggestion immediately";
                    if (!m_suggestionTimer) {
                        m_suggestionTimer = new QTimer(this);
                        m_suggestionTimer->setSingleShot(true);
                        connect(m_suggestionTimer, &QTimer::timeout, this, [this]() {
                            if (m_doubleYoloMode) {
                                autoAcceptSuggestion();
                            }
                        });
                    }
                    if (!m_suggestionTimer->isActive()) {
                        m_suggestionTimer->start(500);
                    }
                    if (m_tripleYoloMode) {
                        scheduleSuggestionFallback();
                    }
                }
            }
        } else {
            // Cancel any pending suggestion timers so they don't fire after disable
            if (m_suggestionTimer && m_suggestionTimer->isActive()) {
                m_suggestionTimer->stop();
                qDebug() << "ClaudeSession: Cancelled pending suggestion timer (double yolo disabled)";
            }
            if (m_suggestionFallbackTimer && m_suggestionFallbackTimer->isActive()) {
                m_suggestionFallbackTimer->stop();
                qDebug() << "ClaudeSession: Cancelled suggestion fallback timer (double yolo disabled)";
            }

            // Only stop polling if triple yolo doesn't also need it
            if (!m_tripleYoloMode) {
                stopIdlePolling();
            }
        }
    }
}

void ClaudeSession::setTripleYoloMode(bool enabled)
{
    if (m_tripleYoloMode != enabled) {
        m_tripleYoloMode = enabled;
        Q_EMIT tripleYoloModeChanged(enabled);

        // Manage .yolo-team state file for hook-based team auto-continue
        if (m_hookHandler) {
            QString teamYoloPath = m_hookHandler->socketPath();
            teamYoloPath.replace(QStringLiteral(".sock"), QStringLiteral(".yolo-team"));

            if (enabled) {
                QFile teamYoloFile(teamYoloPath);
                if (teamYoloFile.open(QIODevice::WriteOnly)) {
                    teamYoloFile.write("1");
                    teamYoloFile.close();
                    qDebug() << "ClaudeSession: Created team yolo state file:" << teamYoloPath;
                }
            } else {
                if (QFile::exists(teamYoloPath)) {
                    QFile::remove(teamYoloPath);
                    qDebug() << "ClaudeSession: Removed team yolo state file:" << teamYoloPath;
                }
            }
        }

        if (enabled) {
            // Start idle polling as a fallback when hooks aren't delivering state
            startIdlePolling();

            // Also fire immediately if Claude appears idle right now.
            // Set m_idlePromptDetected to prevent polling from double-firing.
            auto state = claudeState();
            if (state == ClaudeProcess::State::Idle || state == ClaudeProcess::State::NotRunning) {
                m_idlePromptDetected = true;
                qDebug() << "ClaudeSession: Claude state" << static_cast<int>(state) << "- auto-continuing immediately (triple yolo enabled)";
                QTimer::singleShot(500, this, [this]() {
                    if (m_tripleYoloMode) {
                        sendPrompt(m_autoContinuePrompt);
                        logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);
                    }
                    // Reset after cooldown to allow polling to work for subsequent idle events
                    QTimer::singleShot(5000, this, [this]() {
                        m_idlePromptDetected = false;
                    });
                });
            }
        } else {
            // Only stop polling if double yolo doesn't also need it
            if (!m_doubleYoloMode) {
                stopIdlePolling();
            }
        }
    }
}

void ClaudeSession::startResourceTracking()
{
#ifndef Q_OS_LINUX
    // /proc filesystem is Linux-only; skip on other platforms
    return;
#else
    if (!QFile::exists(QStringLiteral("/proc"))) {
        return;
    }

    if (!m_resourceTimer) {
        m_resourceTimer = new QTimer(this);
        connect(m_resourceTimer, &QTimer::timeout, this, &ClaudeSession::refreshResourceUsage);
    }
    m_resourceTimer->start(15000); // 15s interval — resource stats don't need high frequency

    // Resolve the Claude PID asynchronously from the tmux pane PID
    if (m_tmuxManager) {
        QPointer<ClaudeSession> guard(this);
        m_tmuxManager->getPanePidAsync(m_sessionName, [this, guard](qint64 panePid) {
            if (!guard) {
                return;
            }
            if (panePid > 0) {
                m_claudePid = findClaudePid(panePid);
                if (m_claudePid > 0) {
                    qDebug() << "ClaudeSession: Resolved Claude PID:" << m_claudePid << "from pane PID:" << panePid;
                    refreshResourceUsage();
                } else {
                    qDebug() << "ClaudeSession: Could not find Claude process under pane PID:" << panePid;
                }
            }
        });
    }
#endif
}

qint64 ClaudeSession::findClaudePid(qint64 panePid)
{
    // First check if the pane PID itself is the Claude process.
    // tmux's #{pane_pid} often IS the claude process directly (not a wrapper shell).
    {
        QFile cmdlineFile(QStringLiteral("/proc/%1/cmdline").arg(panePid));
        if (cmdlineFile.open(QIODevice::ReadOnly)) {
            QString cmdline = QString::fromUtf8(cmdlineFile.readAll());
            cmdlineFile.close();
            if (cmdline.contains(QStringLiteral("claude"), Qt::CaseInsensitive)) {
                return panePid;
            }
        }
    }

    // Otherwise walk children: try /proc/{pid}/task/{pid}/children first,
    // then fall back to scanning /proc for matching ppid
    QStringList childPids;

    QFile childrenFile(QStringLiteral("/proc/%1/task/%1/children").arg(panePid));
    if (childrenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString childrenStr = QString::fromUtf8(childrenFile.readAll()).trimmed();
        childrenFile.close();
        if (!childrenStr.isEmpty()) {
            childPids = childrenStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
    }

    // Fallback: scan /proc for processes whose ppid matches panePid
    if (childPids.isEmpty()) {
        QDir procDir(QStringLiteral("/proc"));
        const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            bool isNum = false;
            qint64 pid = entry.toLongLong(&isNum);
            if (!isNum || pid <= 0) {
                continue;
            }
            QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
            if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            QString statLine = QString::fromUtf8(statFile.readAll()).trimmed();
            statFile.close();
            // ppid is field 4 (1-indexed); skip past comm "(name)" which may contain spaces
            int closeParenIdx = statLine.lastIndexOf(QLatin1Char(')'));
            if (closeParenIdx < 0) {
                continue;
            }
            QStringList fields = statLine.mid(closeParenIdx + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            // fields[0]=state, fields[1]=ppid
            if (fields.size() > 1 && fields[1].toLongLong() == panePid) {
                childPids.append(entry);
            }
        }
    }

    for (const QString &childPidStr : childPids) {
        qint64 childPid = childPidStr.toLongLong();
        if (childPid <= 0) {
            continue;
        }

        QFile cmdlineFile(QStringLiteral("/proc/%1/cmdline").arg(childPid));
        if (!cmdlineFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString cmdline = QString::fromUtf8(cmdlineFile.readAll());
        cmdlineFile.close();

        if (cmdline.contains(QStringLiteral("claude"), Qt::CaseInsensitive)) {
            return childPid;
        }

        // Recurse one level: child might be a shell wrapping Claude
        qint64 grandchild = findClaudePid(childPid);
        if (grandchild > 0) {
            return grandchild;
        }
    }

    return 0;
}

ResourceUsage ClaudeSession::readProcessResources(qint64 pid)
{
    ResourceUsage usage;

#ifdef Q_OS_LINUX
    // Read /proc/{pid}/stat for CPU ticks (fields 14=utime, 15=stime, 1-indexed)
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString statLine = QString::fromUtf8(statFile.readAll()).trimmed();
        statFile.close();

        // Skip past the comm field "(name)" which may contain spaces
        int closeParenIdx = statLine.lastIndexOf(QLatin1Char(')'));
        if (closeParenIdx > 0) {
            QString afterComm = statLine.mid(closeParenIdx + 2); // skip ") "
            QStringList fields = afterComm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            // After comm: state(0), ppid(1), pgrp(2), session(3), tty(4), tpgid(5),
            //             flags(6), minflt(7), cminflt(8), majflt(9), cmajflt(10),
            //             utime(11), stime(12)
            if (fields.size() > 12) {
                quint64 utime = fields[11].toULongLong();
                quint64 stime = fields[12].toULongLong();
                quint64 totalTicks = utime + stime;

                qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (m_lastCpuTime > 0 && now > m_lastCpuTime) {
                    double deltaTicks = static_cast<double>(totalTicks - m_lastCpuTicks);
                    double deltaTimeSec = (now - m_lastCpuTime) / 1000.0;
                    long clkTck = sysconf(_SC_CLK_TCK);
                    if (clkTck > 0 && deltaTimeSec > 0) {
                        usage.cpuPercent = (deltaTicks / (deltaTimeSec * clkTck)) * 100.0;
                        if (usage.cpuPercent < 0.0) {
                            usage.cpuPercent = 0.0;
                        }
                    }
                }
                m_lastCpuTicks = totalTicks;
                m_lastCpuTime = now;
            }
        }
    }

    // Read /proc/{pid}/status for VmRSS (in kB).
    // Read the whole file at once — /proc pseudo-files report size 0 which
    // can confuse QFile's buffered readLine() in text mode.
    QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
    if (statusFile.open(QIODevice::ReadOnly)) {
        const QByteArray statusData = statusFile.readAll();
        statusFile.close();
        const QList<QByteArray> statusLines = statusData.split('\n');
        for (const QByteArray &line : statusLines) {
            if (line.startsWith("VmRSS:")) {
                // Format: "VmRSS:\t  12345 kB"
                QString val = QString::fromUtf8(line).mid(6).trimmed();
                val = val.split(QLatin1Char(' ')).first();
                bool ok = false;
                quint64 kB = val.toULongLong(&ok);
                if (ok) {
                    usage.rssBytes = kB * 1024ULL;
                }
                break;
            }
        }
    }
#else
    Q_UNUSED(pid)
#endif

    return usage;
}

void ClaudeSession::refreshResourceUsage()
{
#ifndef Q_OS_LINUX
    return;
#endif

    // If we don't have a PID or the process is gone, try to re-resolve.
    // Use QDir::exists() not QFile::exists() — /proc/{pid} is a directory.
    if (m_claudePid <= 0 || !QDir(QStringLiteral("/proc/%1").arg(m_claudePid)).exists()) {
        m_claudePid = 0;
        m_lastCpuTicks = 0;
        m_lastCpuTime = 0;

        if (m_tmuxManager) {
            QPointer<ClaudeSession> guard(this);
            m_tmuxManager->getPanePidAsync(m_sessionName, [this, guard](qint64 panePid) {
                if (!guard) {
                    return;
                }
                if (panePid > 0) {
                    m_claudePid = findClaudePid(panePid);
                    if (m_claudePid > 0) {
                        qDebug() << "ClaudeSession: Re-resolved Claude PID:" << m_claudePid;
                    }
                }
            });
        }
        return;
    }

    ResourceUsage usage = readProcessResources(m_claudePid);

    // Only emit if something changed meaningfully
    if (qAbs(usage.cpuPercent - m_resourceUsage.cpuPercent) > 0.5 || usage.rssBytes != m_resourceUsage.rssBytes) {
        m_resourceUsage = usage;
        Q_EMIT resourceUsageChanged();
    }
}

void ClaudeSession::cleanupRemoteHooks()
{
    if (m_sshHost.isEmpty() || m_sessionId.isEmpty()) {
        return;
    }

    // Build SSH target
    QString target;
    if (!m_sshUsername.isEmpty()) {
        target = QStringLiteral("%1@%2").arg(m_sshUsername, m_sshHost);
    } else {
        target = m_sshHost;
    }

    // Build cleanup command: remove hook script and clean hooks from settings.local.json
    QString scriptPath = QStringLiteral("/tmp/konsolai-hook-%1.sh").arg(m_sessionId);
    QString remoteWorkDir = m_workingDir.isEmpty() ? QStringLiteral("~") : m_workingDir;
    QString settingsPath = remoteWorkDir + QStringLiteral("/.claude/settings.local.json");

    // Remove hook script and use python3 to remove hooks key from settings
    QString cleanupCmd = QStringLiteral(
                             "rm -f '%1'; "
                             "python3 -c \""
                             "import json,os; "
                             "p='%2'; "
                             "e={}; "
                             "r=open(p) if os.path.exists(p) else None; "
                             "e=json.load(r) if r else {}; "
                             "r and r.close(); "
                             "e.pop('hooks',None); "
                             "f=open(p,'w'); json.dump(e,f,indent=2); f.close()"
                             "\" 2>/dev/null || true")
                             .arg(scriptPath, settingsPath);

    QStringList sshArgs;
    sshArgs << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=3");
    sshArgs << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    if (m_sshPort != 22 && m_sshPort > 0) {
        sshArgs << QStringLiteral("-p") << QString::number(m_sshPort);
    }
    sshArgs << target << cleanupCmd;

    // Fire-and-forget: don't block the destructor.
    // Kill after 10s to prevent zombie processes if SSH hangs.
    QProcess *process = new QProcess();
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process, &QProcess::deleteLater);
    QTimer::singleShot(10000, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
    process->start(QStringLiteral("ssh"), sshArgs);

    qDebug() << "ClaudeSession: Initiated remote hook cleanup for" << target;
}

void ClaudeSession::removeHooksFromProjectSettings()
{
    if (m_workingDir.isEmpty()) {
        return;
    }

    QString settingsPath = m_workingDir + QStringLiteral("/.claude/settings.local.json");
    QFile file(settingsPath);
    if (!file.exists()) {
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    QJsonObject settings = doc.object();

    if (!settings.contains(QStringLiteral("hooks"))) {
        return;
    }

    // Only remove hook entries that reference THIS session's socket path.
    // Other sessions in the same workDir may still be running with their
    // own hooks — we must not remove those.
    QString mySocket = m_hookHandler ? m_hookHandler->socketPath() : QString();
    if (mySocket.isEmpty()) {
        return;
    }

    // Guard against reattach race condition: if a replacement session with the
    // same session ID is already registered in the registry, it owns the hooks
    // now.  Our destructor (via deleteLater) runs AFTER the new session wrote
    // its hooks, so removing them here would break the new session's yolo mode.
    auto *registry = ClaudeSessionRegistry::instance();
    if (registry) {
        const auto active = registry->activeSessions();
        for (auto *session : active) {
            if (session != this && session->sessionId() == m_sessionId) {
                qDebug() << "ClaudeSession: Skipping hook cleanup — replacement session active for ID:" << m_sessionId;
                return;
            }
        }
    }

    QJsonObject hooks = settings[QStringLiteral("hooks")].toObject();
    bool anyRemoved = false;

    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
        QJsonArray entries = it.value().toArray();
        QJsonArray filtered;
        for (const auto &entry : entries) {
            QString entryStr = QString::fromUtf8(QJsonDocument(entry.toObject()).toJson());
            if (entryStr.contains(mySocket)) {
                anyRemoved = true;
            } else {
                filtered.append(entry);
            }
        }
        hooks[it.key()] = filtered;
    }

    if (!anyRemoved) {
        return;
    }

    // Check if any hooks remain; if not, remove the key entirely
    bool anyHooksLeft = false;
    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
        if (!it.value().toArray().isEmpty()) {
            anyHooksLeft = true;
            break;
        }
    }

    if (anyHooksLeft) {
        settings[QStringLiteral("hooks")] = hooks;
    } else {
        settings.remove(QStringLiteral("hooks"));
    }

    QFile outFile(settingsPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument outDoc(settings);
        outFile.write(outDoc.toJson(QJsonDocument::Indented));
        outFile.close();
        qDebug() << "ClaudeSession: Removed this session's hooks from" << settingsPath;
    }
}

} // namespace Konsolai

#include "moc_ClaudeSession.cpp"
