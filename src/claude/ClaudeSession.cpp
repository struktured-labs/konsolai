/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSession.h"

#include <QDir>
#include <QRegularExpression>

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
    m_workingDir = workingDir.isEmpty() ? QDir::currentPath() : workingDir;
    m_isReattach = false;

    // Generate unique session ID
    m_sessionId = TmuxManager::generateSessionId();

    // Build session name from profile and ID
    m_sessionName = TmuxManager::buildSessionName(m_profileName, m_sessionId);

    // Create managers
    m_tmuxManager = new TmuxManager(this);
    m_claudeProcess = new ClaudeProcess(this);

    // Set up the Session to run tmux
    QString tmuxCommand = shellCommand();
    setProgram(QStringLiteral("sh"));
    setArguments({QStringLiteral("-c"), tmuxCommand});
    setInitialWorkingDirectory(m_workingDir);

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

    m_workingDir = QDir::currentPath();

    // Create managers
    m_tmuxManager = new TmuxManager(this);
    m_claudeProcess = new ClaudeProcess(this);

    // Set up the Session to reattach to tmux
    QString tmuxCommand = shellCommand();
    setProgram(QStringLiteral("sh"));
    setArguments({QStringLiteral("-c"), tmuxCommand});
    setInitialWorkingDirectory(m_workingDir);

    connectSignals();
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
    QString claudeCmd = ClaudeProcess::buildCommand(m_claudeModel, QString(), {});

    return m_tmuxManager->buildNewSessionCommand(
        m_sessionName,
        claudeCmd,
        true,  // attachExisting
        m_workingDir
    );
}

void ClaudeSession::detach()
{
    if (m_tmuxManager) {
        // The detach command is sent to tmux, not run in the terminal
        // We need to use sendKeys to send the detach key sequence
        // Default tmux prefix is Ctrl+b, then 'd' to detach
        m_tmuxManager->sendKeys(m_sessionName, QStringLiteral("C-b d"));
    }
    Q_EMIT detached();
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
    // Send 'y' followed by Enter to approve
    sendText(QStringLiteral("y\n"));
}

void ClaudeSession::denyPermission()
{
    // Send 'n' followed by Enter to deny
    sendText(QStringLiteral("n\n"));
}

void ClaudeSession::stop()
{
    // Send Ctrl+C to stop Claude
    if (m_tmuxManager) {
        m_tmuxManager->sendKeys(m_sessionName, QStringLiteral("C-c"));
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

} // namespace Konsolai

#include "moc_ClaudeSession.cpp"
