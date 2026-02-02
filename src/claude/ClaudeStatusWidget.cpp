/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeStatusWidget.h"
#include "ClaudeSession.h"

#include <QDebug>
#include <QHBoxLayout>

namespace Konsolai
{

ClaudeStatusWidget::ClaudeStatusWidget(QWidget *parent)
    : QWidget(parent)
    , m_stateLabel(new QLabel(this))
    , m_taskLabel(new QLabel(this))
    , m_spinnerTimer(new QTimer(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_stateLabel->setToolTip(QStringLiteral("Claude status"));
    layout->addWidget(m_stateLabel);

    m_taskLabel->setToolTip(QStringLiteral("Current task"));
    m_taskLabel->setVisible(false);
    layout->addWidget(m_taskLabel);

    // Setup spinner timer
    m_spinnerTimer->setInterval(80);  // ~12 FPS
    connect(m_spinnerTimer, &QTimer::timeout, this, &ClaudeStatusWidget::updateSpinner);

    updateDisplay();
}

ClaudeStatusWidget::~ClaudeStatusWidget() = default;

void ClaudeStatusWidget::setSession(ClaudeSession *session)
{
    if (m_session == session) {
        return;
    }

    // Disconnect old session
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
    }

    m_session = session;

    // Connect new session
    if (m_session) {
        connect(m_session, &ClaudeSession::stateChanged,
                this, &ClaudeStatusWidget::updateState);
        connect(m_session, &ClaudeSession::taskStarted,
                this, &ClaudeStatusWidget::updateTask);
        connect(m_session, &ClaudeSession::taskFinished,
                this, [this]() { updateTask(QString()); });
        connect(m_session, &ClaudeSession::approvalCountChanged, this, &ClaudeStatusWidget::updateDisplay);
        connect(m_session, &ClaudeSession::tokenUsageChanged, this, &ClaudeStatusWidget::updateDisplay);
        connect(m_session, &QObject::destroyed,
                this, &ClaudeStatusWidget::onSessionDestroyed);

        // Update with current state - use Idle if session is running
        ClaudeProcess::State initialState = m_session->claudeState();
        qDebug() << "ClaudeStatusWidget::setSession - initial state:" << static_cast<int>(initialState) << "isRunning:" << m_session->isRunning();
        if (initialState == ClaudeProcess::State::NotRunning && m_session->isRunning()) {
            initialState = ClaudeProcess::State::Idle;
        }
        updateState(initialState);
        if (m_session->claudeProcess()) {
            updateTask(m_session->claudeProcess()->currentTask());
        }
    } else {
        clearSession();
    }
}

void ClaudeStatusWidget::clearSession()
{
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        m_session = nullptr;
    }

    m_currentState = ClaudeProcess::State::NotRunning;
    m_currentTask.clear();
    m_spinnerTimer->stop();
    updateDisplay();
}

void ClaudeStatusWidget::updateState(ClaudeProcess::State state)
{
    qDebug() << "ClaudeStatusWidget::updateState:" << static_cast<int>(state) << "(was:" << static_cast<int>(m_currentState) << ")";
    m_currentState = state;

    // Start/stop spinner based on state
    if (state == ClaudeProcess::State::Working) {
        m_spinnerTimer->start();
    } else {
        m_spinnerTimer->stop();
        m_spinnerIndex = 0;
    }

    updateDisplay();
}

void ClaudeStatusWidget::updateTask(const QString &task)
{
    m_currentTask = task;
    updateDisplay();
}

void ClaudeStatusWidget::updateSpinner()
{
    m_spinnerIndex = (m_spinnerIndex + 1) % SPINNER_FRAME_COUNT;
    updateDisplay();
}

void ClaudeStatusWidget::onSessionDestroyed()
{
    m_session = nullptr;
    clearSession();
}

void ClaudeStatusWidget::updateDisplay()
{
    QString stateStr = stateText(m_currentState);
    QString icon = stateIcon(m_currentState);

    // Add spinner if working
    if (m_currentState == ClaudeProcess::State::Working) {
        icon = QString::fromUtf8(SPINNER_FRAMES[m_spinnerIndex]);
    }

    // Build status text with approval count and token usage
    QString statusText = QStringLiteral("%1 Claude: %2").arg(icon, stateStr);
    if (m_session && m_session->totalApprovalCount() > 0) {
        // Show bolt count matching the highest active yolo level
        QString bolts;
        if (m_session->tripleYoloMode()) {
            bolts = QStringLiteral("⚡⚡⚡");
        } else if (m_session->doubleYoloMode()) {
            bolts = QStringLiteral("⚡⚡");
        } else {
            bolts = QStringLiteral("⚡");
        }
        statusText += QStringLiteral(" │ %1%2").arg(bolts, QString::number(m_session->totalApprovalCount()));
    }
    if (m_session && m_session->tokenUsage().totalTokens() > 0) {
        const auto &usage = m_session->tokenUsage();
        statusText += QStringLiteral(" │ %1 ($%2)").arg(usage.formatCompact(), QString::number(usage.estimatedCostUSD(), 'f', 2));
    }
    m_stateLabel->setText(statusText);

    // Show task if present
    if (!m_currentTask.isEmpty()) {
        m_taskLabel->setText(QStringLiteral("│ Task: %1").arg(m_currentTask));
        m_taskLabel->setVisible(true);
    } else {
        m_taskLabel->setVisible(false);
    }

    // Update style based on state
    QString styleSheet;
    switch (m_currentState) {
    case ClaudeProcess::State::Error:
        styleSheet = QStringLiteral("color: #f44336;");  // Red
        break;
    case ClaudeProcess::State::WaitingInput:
        styleSheet = QStringLiteral("color: #ff9800;");  // Orange
        break;
    case ClaudeProcess::State::Working:
        styleSheet = QStringLiteral("color: #2196f3;");  // Blue
        break;
    case ClaudeProcess::State::Idle:
        styleSheet = QStringLiteral("color: #4caf50;");  // Green
        break;
    default:
        styleSheet = QStringLiteral("color: #757575;");  // Gray
        break;
    }

    m_stateLabel->setStyleSheet(styleSheet);
}

QString ClaudeStatusWidget::stateText(ClaudeProcess::State state) const
{
    switch (state) {
    case ClaudeProcess::State::NotRunning:
        return QStringLiteral("Not Running");
    case ClaudeProcess::State::Starting:
        return QStringLiteral("Starting");
    case ClaudeProcess::State::Idle:
        return QStringLiteral("Idle");
    case ClaudeProcess::State::Working:
        return QStringLiteral("Working");
    case ClaudeProcess::State::WaitingInput:
        return QStringLiteral("Waiting");
    case ClaudeProcess::State::Error:
        return QStringLiteral("Error");
    default:
        return QStringLiteral("Unknown");
    }
}

QString ClaudeStatusWidget::stateIcon(ClaudeProcess::State state) const
{
    switch (state) {
    case ClaudeProcess::State::NotRunning:
        return QStringLiteral("○");
    case ClaudeProcess::State::Starting:
        return QStringLiteral("◐");
    case ClaudeProcess::State::Idle:
        return QStringLiteral("●");
    case ClaudeProcess::State::Working:
        return QStringLiteral("◉");  // Will be replaced by spinner
    case ClaudeProcess::State::WaitingInput:
        return QStringLiteral("◎");
    case ClaudeProcess::State::Error:
        return QStringLiteral("✖");
    default:
        return QStringLiteral("?");
    }
}

} // namespace Konsolai

#include "moc_ClaudeStatusWidget.cpp"
