/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeStatusWidget.h"
#include "BudgetController.h"
#include "ClaudeSession.h"

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

    setObjectName(QStringLiteral("claudeStatusWidget"));
    m_stateLabel->setObjectName(QStringLiteral("claudeStateLabel"));
    m_stateLabel->setTextFormat(Qt::RichText);
    m_stateLabel->setToolTip(QStringLiteral("Claude status"));
    layout->addWidget(m_stateLabel);

    m_taskLabel->setObjectName(QStringLiteral("claudeTaskLabel"));
    m_taskLabel->setToolTip(QStringLiteral("Current task"));
    m_taskLabel->setVisible(false);
    layout->addWidget(m_taskLabel);

    // Setup spinner timer
    m_spinnerTimer->setInterval(150); // ~7 FPS — smooth enough for text spinner
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
        connect(m_session, &ClaudeSession::resourceUsageChanged, this, &ClaudeStatusWidget::updateDisplay);
        connect(m_session, &QObject::destroyed,
                this, &ClaudeStatusWidget::onSessionDestroyed);

        // Update with current state - use Idle if session is running
        ClaudeProcess::State initialState = m_session->claudeState();
        // Use Idle if session is running but process reports NotRunning (startup race)
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
    // QPointer already nulled m_session; just reset display state
    m_currentState = ClaudeProcess::State::NotRunning;
    m_currentTask.clear();
    m_spinnerTimer->stop();
    updateDisplay();
}

void ClaudeStatusWidget::setWeeklyUsage(double spent, double budget)
{
    m_weeklySpent = spent;
    m_weeklyBudget = budget;
    updateDisplay();
}

void ClaudeStatusWidget::setMonthlyUsage(double spent, double budget)
{
    m_monthlySpent = spent;
    m_monthlyBudget = budget;
    updateDisplay();
}

void ClaudeStatusWidget::updateDisplay()
{
    // Snapshot the QPointer once — if the session is destroyed mid-update,
    // all subsequent accesses through this local will be safe (null).
    ClaudeSession *session = m_session.data();

    QString stateStr = stateText(m_currentState);
    QString icon = stateIcon(m_currentState);

    // Add spinner if working
    if (m_currentState == ClaudeProcess::State::Working) {
        icon = QString::fromUtf8(SPINNER_FRAMES[m_spinnerIndex]);
    }

    // Build status text (rich text for colored elements)
    QString statusText = QStringLiteral("%1 Claude: %2").arg(icon, stateStr);

    // Model name — prefer detected model from JSONL, fall back to session enum
    if (session) {
        QString model = session->tokenUsage().detectedModel;
        if (model.isEmpty()) {
            model = ClaudeProcess::shortModelName(session->claudeModel());
        } else {
            // Shorten: "claude-opus-4-6" → "opus-4-6"
            model.remove(QStringLiteral("claude-"));
        }
        if (!model.isEmpty()) {
            statusText += QStringLiteral(" (%1)").arg(model);
        }
    }

    // Yolo bolts with per-level counts
    if (session) {
        int yoloCount = session->yoloApprovalCount();
        int doubleCount = session->doubleYoloApprovalCount();
        int tripleCount = session->tripleYoloApprovalCount();
        QString bolts;
        if (session->yoloMode() || yoloCount > 0) {
            bolts += QStringLiteral("<span style='color:#FFB300'>ϟ</span>");
            if (yoloCount > 0) {
                bolts += QStringLiteral("<span style='color:#FFB300'>[%1]</span>").arg(yoloCount);
            }
        }
        if (session->doubleYoloMode() || doubleCount > 0) {
            if (!bolts.isEmpty()) bolts += QStringLiteral(" ");
            bolts += QStringLiteral("<span style='color:#42A5F5'>ϟ</span>");
            if (doubleCount > 0) {
                bolts += QStringLiteral("<span style='color:#42A5F5'>[%1]</span>").arg(doubleCount);
            }
        }
        if (session->tripleYoloMode() || tripleCount > 0) {
            if (!bolts.isEmpty()) bolts += QStringLiteral(" ");
            bolts += QStringLiteral("<span style='color:#AB47BC'>ϟ</span>");
            if (tripleCount > 0) {
                bolts += QStringLiteral("<span style='color:#AB47BC'>[%1]</span>").arg(tripleCount);
            }
        }
        if (!bolts.isEmpty()) {
            statusText += QStringLiteral(" │ ") + bolts;
        }
    }

    // Token usage + cost
    if (session && session->tokenUsage().totalTokens() > 0) {
        const auto &usage = session->tokenUsage();
        statusText += QStringLiteral(" │ %1 ($%2)").arg(usage.formatCompact(), QString::number(usage.estimatedCostUSD(), 'f', 2));
    }

    // Context window percent
    if (session) {
        double ctxPct = session->tokenUsage().contextPercent();
        if (ctxPct >= 0.0) {
            QString ctxStr = QStringLiteral("Ctx:%1%").arg(ctxPct, 0, 'f', 0);
            if (ctxPct >= 80.0) {
                // Warning icon + orange/red coloring
                QString warnIcon = ctxPct >= 95.0 ? QStringLiteral("🔴") : QStringLiteral("⚠");
                QString color = ctxPct >= 95.0 ? QStringLiteral("#f44336") : QStringLiteral("#ff9800");
                statusText += QStringLiteral(" │ %1 <span style='color:%2'>%3</span>").arg(warnIcon, color, ctxStr);
            } else {
                statusText += QStringLiteral(" │ %1").arg(ctxStr);
            }
        }
    }

    // Session budget percent
    if (session) {
        auto *bc = session->budgetController();
        if (bc && bc->budget().hasAnyLimit()) {
            double pct = bc->budget().usedPercent(session->tokenUsage().estimatedCostUSD(), session->tokenUsage().totalTokens());
            if (pct >= 0.0) {
                QString pctStr = QStringLiteral("%1%").arg(pct, 0, 'f', 0);
                if (pct >= 100.0) {
                    statusText += QStringLiteral(" │ <span style='color:#f44336'>%1</span>").arg(pctStr);
                } else if (pct >= 80.0) {
                    statusText += QStringLiteral(" │ <span style='color:#ff9800'>%1</span>").arg(pctStr);
                } else {
                    statusText += QStringLiteral(" │ %1").arg(pctStr);
                }
            }
        }
    }

    // Weekly spending
    if (m_weeklySpent > 0.0 || m_weeklyBudget > 0.0) {
        QString weekStr = QStringLiteral("W:$%1").arg(m_weeklySpent, 0, 'f', 2);
        if (m_weeklyBudget > 0.0) {
            weekStr += QStringLiteral("/$%1").arg(m_weeklyBudget, 0, 'f', 0);
            double pct = m_weeklySpent / m_weeklyBudget * 100.0;
            if (pct >= 100.0) {
                weekStr = QStringLiteral("<span style='color:#f44336'>%1</span>").arg(weekStr);
            } else if (pct >= 80.0) {
                weekStr = QStringLiteral("<span style='color:#ff9800'>%1</span>").arg(weekStr);
            }
        }
        statusText += QStringLiteral(" │ %1").arg(weekStr);
    }

    // Monthly spending
    if (m_monthlySpent > 0.0 || m_monthlyBudget > 0.0) {
        QString monthStr = QStringLiteral("M:$%1").arg(m_monthlySpent, 0, 'f', 2);
        if (m_monthlyBudget > 0.0) {
            monthStr += QStringLiteral("/$%1").arg(m_monthlyBudget, 0, 'f', 0);
            double pct = m_monthlySpent / m_monthlyBudget * 100.0;
            if (pct >= 100.0) {
                monthStr = QStringLiteral("<span style='color:#f44336'>%1</span>").arg(monthStr);
            } else if (pct >= 80.0) {
                monthStr = QStringLiteral("<span style='color:#ff9800'>%1</span>").arg(monthStr);
            }
        }
        statusText += QStringLiteral(" │ %1").arg(monthStr);
    }

    // Resource usage
    if (session && (session->resourceUsage().rssBytes > 0 || session->resourceUsage().cpuPercent > 0.0)) {
        statusText += QStringLiteral(" │ %1").arg(session->resourceUsage().formatCompact());
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
