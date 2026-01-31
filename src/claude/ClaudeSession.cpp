/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSession.h"
#include "ClaudeHookHandler.h"
#include "KonsolaiSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace Konsolai
{

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

ClaudeSession::~ClaudeSession() = default;

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

    // Set tab title based on working directory name
    QString projectName = QDir(m_workingDir).dirName();
    if (!projectName.isEmpty()) {
        setTitle(Konsole::Session::NameRole, projectName);
        setTitle(Konsole::Session::DisplayedTitleRole, projectName);
        // Use literal project name - format codes like %d (cwd) and %n (process name)
        // resolve to the tmux client process, not the Claude session's project
        setTabTitleFormat(Konsole::Session::LocalTabTitle, projectName);
        setTabTitleFormat(Konsole::Session::RemoteTabTitle, projectName);
        tabTitleSetByUser(true);
    }

    // Apply persisted yolo mode defaults to new sessions
    if (auto *settings = KonsolaiSettings::instance()) {
        m_yoloMode = settings->yoloMode();
        m_doubleYoloMode = settings->doubleYoloMode();
        m_tripleYoloMode = settings->tripleYoloMode();
        m_autoContinuePrompt = settings->autoContinuePrompt();
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

    // NOTE: We don't set program/arguments here - we do it in run()

    connectSignals();
}

void ClaudeSession::run()
{
    qDebug() << "ClaudeSession::run() called";

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

    // Use the current initialWorkingDirectory (may have been updated by ViewManager)
    QString workDir = initialWorkingDirectory();
    if (!workDir.isEmpty()) {
        m_workingDir = workDir;
    }

    // For reattached sessions, get the actual working directory from tmux
    // This is where Claude is actually running, which may differ from where konsolai started
    if (m_isReattach && m_tmuxManager && m_tmuxManager->sessionExists(m_sessionName)) {
        QString tmuxWorkDir = m_tmuxManager->getPaneWorkingDirectory(m_sessionName);
        if (!tmuxWorkDir.isEmpty() && QDir(tmuxWorkDir).exists()) {
            m_workingDir = tmuxWorkDir;
            qDebug() << "ClaudeSession::run() - Got working dir from tmux:" << m_workingDir;
        }
    }

    // Validate working directory
    if (!m_workingDir.isEmpty() && !QDir(m_workingDir).exists()) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Claude Session Warning"),
                             QStringLiteral("Working directory does not exist:\n%1\n\n"
                                            "Using current directory instead.")
                                 .arg(m_workingDir));
        m_workingDir = QDir::currentPath();
    }

    // Update tab title to project name (directory basename)
    // We use a literal string (no format codes) because format codes like %d and %n
    // resolve to the tmux client process info, not the Claude session's project.
    // We also set tabTitleSetByUser to prevent OSC 30 escape sequences from tmux/shell
    // from overriding the title (they would set it to the build directory name).
    QString projectName = QDir(m_workingDir).dirName();
    if (!projectName.isEmpty() && projectName != QStringLiteral(".")) {
        setTitle(Konsole::Session::NameRole, projectName);
        setTitle(Konsole::Session::DisplayedTitleRole, projectName);
        setTabTitleFormat(Konsole::Session::LocalTabTitle, projectName);
        setTabTitleFormat(Konsole::Session::RemoteTabTitle, projectName);
        tabTitleSetByUser(true);
    }

    // Emit signal so session panel can update metadata with correct working directory
    Q_EMIT workingDirectoryChanged(m_workingDir);

    // Start the hook handler to receive Claude events
    if (m_hookHandler) {
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

                // Parse our hooks config and merge
                QJsonDocument hooksDoc = QJsonDocument::fromJson(hooksConfig.toUtf8());
                if (hooksDoc.isObject()) {
                    QJsonObject hooksObj = hooksDoc.object();
                    // The hooks are under "hooks" key in our generated config
                    if (hooksObj.contains(QStringLiteral("hooks"))) {
                        settings[QStringLiteral("hooks")] = hooksObj[QStringLiteral("hooks")];
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
            qDebug() << "ClaudeSession: Auto-approving permission (yolo mode)";
            // Use a short delay to ensure the prompt is ready
            QTimer::singleShot(100, this, [this]() {
                approvePermission();
                incrementYoloApproval();
            });
        }
    });

    // Handle idle state for double/triple yolo modes
    connect(m_claudeProcess, &ClaudeProcess::stateChanged, this, [this](ClaudeProcess::State newState) {
        qDebug() << "ClaudeSession: State changed to:" << static_cast<int>(newState) << "doubleYoloMode:" << m_doubleYoloMode
                 << "tripleYoloMode:" << m_tripleYoloMode;

        if (newState == ClaudeProcess::State::Idle) {
            if (m_tripleYoloMode) {
                // Triple yolo: auto-continue with a new prompt
                qDebug() << "ClaudeSession: Auto-continuing (triple yolo mode)";
                QTimer::singleShot(500, this, [this]() {
                    sendPrompt(m_autoContinuePrompt);
                    incrementTripleYoloApproval();
                });
            } else if (m_doubleYoloMode) {
                // Double yolo: auto-accept suggestion (Tab + Enter)
                qDebug() << "ClaudeSession: Auto-accepting suggestion (double yolo mode)";
                QTimer::singleShot(1000, this, [this]() {
                    autoAcceptSuggestion();
                });
            }
        }
    });

    // Handle yolo auto-approvals from hook handler
    connect(m_claudeProcess, &ClaudeProcess::yoloApprovalOccurred, this, [this](const QString &toolName) {
        qDebug() << "ClaudeSession: Yolo hook auto-approved:" << toolName;
        incrementYoloApproval();
    });
}

ClaudeProcess::State ClaudeSession::claudeState() const
{
    return m_claudeProcess ? m_claudeProcess->state() : ClaudeProcess::State::NotRunning;
}

QString ClaudeSession::shellCommand() const
{
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
    sendText(prompt + QStringLiteral("\n"));
}

void ClaudeSession::approvePermission()
{
    // Claude Code uses a selection UI where option 1 (Yes) is pre-selected
    // Just send Enter to confirm the selection
    sendText(QStringLiteral("\n"));
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
                if (claudeState() == ClaudeProcess::State::WaitingInput) {
                    qDebug() << "ClaudeSession: Permission prompt already showing - auto-approving immediately";
                    QTimer::singleShot(100, this, [this]() {
                        approvePermission();
                        incrementYoloApproval();
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

    // Only capture the last 5 lines — the permission prompt UI appears at the
    // very bottom of the terminal.  Scanning more risks false positives from
    // earlier output that happens to contain "Yes" or "❯".
    QString output = m_tmuxManager->capturePane(m_sessionName, -5, 0);

    if (detectPermissionPrompt(output)) {
        if (!m_permissionPromptDetected) {
            m_permissionPromptDetected = true;
            qDebug() << "ClaudeSession: Permission prompt detected - auto-approving (yolo mode)";

            // Send approval with small delay to ensure prompt is ready
            QTimer::singleShot(50, this, [this]() {
                approvePermission();
                incrementYoloApproval();

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
}

bool ClaudeSession::detectPermissionPrompt(const QString &terminalOutput)
{
    // Match the Claude Code interactive permission selection UI that appears
    // at the bottom of the terminal.  We check individual LINES so that the
    // selector arrow (❯) must be on the SAME line as "Yes" — not just
    // somewhere in the same 5-line window.

    const auto lines = terminalOutput.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        // The active selection line looks like:  ❯ Yes  (possibly with ANSI codes)
        if (line.contains(QStringLiteral("❯")) && line.contains(QStringLiteral("Yes"))) {
            return true;
        }
    }

    return false;
}

void ClaudeSession::autoAcceptSuggestion()
{
    if (!m_tmuxManager || !m_doubleYoloMode) {
        return;
    }

    // Press Tab to accept any visible suggestion in Claude's input,
    // then Enter to submit it.  If there is no suggestion, Tab is a
    // no-op in Claude Code's Ink UI and Enter on an empty prompt is
    // ignored, so this is safe to fire speculatively.
    m_tmuxManager->sendKeySequence(m_sessionName, QStringLiteral("Tab"));
    QTimer::singleShot(100, this, [this]() {
        approvePermission(); // sends Enter
        incrementDoubleYoloApproval();
    });
}

void ClaudeSession::setDoubleYoloMode(bool enabled)
{
    if (m_doubleYoloMode != enabled) {
        m_doubleYoloMode = enabled;
        Q_EMIT doubleYoloModeChanged(enabled);

        // If Claude is already idle, auto-accept any suggestion now
        if (enabled && !m_tripleYoloMode && claudeState() == ClaudeProcess::State::Idle) {
            qDebug() << "ClaudeSession: Claude already idle - auto-accepting suggestion immediately";
            QTimer::singleShot(500, this, [this]() {
                autoAcceptSuggestion();
            });
        }
    }
}

void ClaudeSession::setTripleYoloMode(bool enabled)
{
    if (m_tripleYoloMode != enabled) {
        m_tripleYoloMode = enabled;
        Q_EMIT tripleYoloModeChanged(enabled);

        // If Claude is already idle, send the auto-continue prompt now
        if (enabled && claudeState() == ClaudeProcess::State::Idle) {
            qDebug() << "ClaudeSession: Claude already idle - auto-continuing immediately";
            QTimer::singleShot(500, this, [this]() {
                sendPrompt(m_autoContinuePrompt);
                incrementTripleYoloApproval();
            });
        }
    }
}

} // namespace Konsolai

#include "moc_ClaudeSession.cpp"
