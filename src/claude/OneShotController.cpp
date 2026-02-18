/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "OneShotController.h"

#include "BudgetController.h"
#include "ClaudeProcess.h"
#include "ClaudeSession.h"
#include "SessionObserver.h"

#include <QDebug>

namespace Konsolai
{

OneShotController::OneShotController(QObject *parent)
    : QObject(parent)
{
}

OneShotController::~OneShotController() = default;

void OneShotController::setConfig(const OneShotConfig &config)
{
    m_config = config;
}

const OneShotConfig &OneShotController::config() const
{
    return m_config;
}

void OneShotController::attachToSession(ClaudeSession *session)
{
    m_session = session;
    if (!session) {
        return;
    }

    // Connect to state changes (cast enum to int for decoupled signal)
    connect(session, &ClaudeSession::stateChanged, this, [this](ClaudeProcess::State state) {
        onStateChanged(static_cast<int>(state));
    });

    // Configure budget if limits are set
    if (m_config.timeLimitMinutes > 0 || m_config.costCeilingUSD > 0.0 || m_config.tokenCeiling > 0) {
        BudgetController *bc = session->budgetController();
        SessionBudget budget;
        budget.timeLimitMinutes = m_config.timeLimitMinutes;
        budget.costCeilingUSD = m_config.costCeilingUSD;
        budget.tokenCeiling = m_config.tokenCeiling;
        budget.startedAt = QDateTime::currentDateTime();
        bc->setBudget(budget);

        // Forward budget status updates
        connect(bc, &BudgetController::budgetWarning, this, [this](const QString &, double) {
            Q_EMIT budgetStatusChanged(formatBudgetStatus());
        });
        connect(bc, &BudgetController::budgetExceeded, this, [this](const QString &type) {
            m_result.errors.append(QStringLiteral("Budget exceeded: %1").arg(type));
            Q_EMIT budgetStatusChanged(formatBudgetStatus());
        });
    }

    // Set yolo levels on the session
    session->setYoloMode(m_config.yoloLevel >= 1);
    session->setDoubleYoloMode(m_config.yoloLevel >= 2);
    session->setTripleYoloMode(m_config.yoloLevel >= 3);

    qDebug() << "OneShotController: attached to session" << session->sessionName() << "yoloLevel:" << m_config.yoloLevel;
}

void OneShotController::start()
{
    m_running = true;
    m_startedAt = QDateTime::currentDateTime();
    qDebug() << "OneShotController: started";
}

bool OneShotController::isRunning() const
{
    return m_running;
}

const OneShotResult &OneShotController::result() const
{
    return m_result;
}

void OneShotController::onStateChanged(int state)
{
    if (!m_running || !m_session) {
        return;
    }

    auto claudeState = static_cast<ClaudeProcess::State>(state);

    if (claudeState == ClaudeProcess::State::Idle && !m_promptSent) {
        // First idle: send the configured prompt
        m_session->sendPrompt(m_config.prompt);
        m_promptSent = true;
        Q_EMIT promptSent();
        qDebug() << "OneShotController: prompt sent";
        return;
    }

    if (claudeState == ClaudeProcess::State::NotRunning && m_promptSent) {
        // Session ended - build final result
        m_running = false;

        const auto &usage = m_session->tokenUsage();
        m_result.totalTokens = usage.totalTokens();
        m_result.costUSD = usage.estimatedCostUSD();
        m_result.durationSeconds = static_cast<int>(m_startedAt.secsTo(QDateTime::currentDateTime()));
        m_result.success = m_result.errors.isEmpty();

        qDebug() << "OneShotController: completed - success:" << m_result.success << "cost: $" << m_result.costUSD << "tokens:" << m_result.totalTokens
                 << "duration:" << m_result.durationSeconds << "s";
        Q_EMIT completed(m_result);
    }

    Q_EMIT budgetStatusChanged(formatBudgetStatus());
}

QString OneShotController::formatBudgetStatus() const
{
    if (!m_session || !m_startedAt.isValid()) {
        return QString();
    }

    int elapsedSecs = static_cast<int>(m_startedAt.secsTo(QDateTime::currentDateTime()));
    int elapsedMin = elapsedSecs / 60;
    int elapsedSec = elapsedSecs % 60;

    const auto &usage = m_session->tokenUsage();
    double currentCost = usage.estimatedCostUSD();

    QStringList parts;

    // Time part: "3:24 / 15:00" or just "3:24"
    QString elapsed = QStringLiteral("%1:%2").arg(elapsedMin).arg(elapsedSec, 2, 10, QLatin1Char('0'));

    if (m_config.timeLimitMinutes > 0) {
        int limitSec = m_config.timeLimitMinutes * 60;
        int limitMin = limitSec / 60;
        int limitS = limitSec % 60;
        QString limit = QStringLiteral("%1:%2").arg(limitMin).arg(limitS, 2, 10, QLatin1Char('0'));
        parts.append(QStringLiteral("%1 / %2").arg(elapsed, limit));
    } else {
        parts.append(elapsed);
    }

    // Cost part: "$0.14 / $0.50" or just "$0.14"
    if (m_config.costCeilingUSD > 0.0) {
        parts.append(QStringLiteral("$%1 / $%2").arg(currentCost, 0, 'f', 2).arg(m_config.costCeilingUSD, 0, 'f', 2));
    } else {
        parts.append(QStringLiteral("$%1").arg(currentCost, 0, 'f', 2));
    }

    return parts.join(QStringLiteral(" | "));
}

QString OneShotController::formatStateLabel() const
{
    if (!m_session) {
        return QStringLiteral("No session");
    }

    auto state = m_session->claudeState();
    int approvals = m_session->totalApprovalCount();

    switch (state) {
    case ClaudeProcess::State::NotRunning:
        return QStringLiteral("Stopped");
    case ClaudeProcess::State::Starting:
        return QStringLiteral("Starting...");
    case ClaudeProcess::State::Working: {
        // Build yolo bolt indicators based on active levels
        QString bolts;
        if (m_session->yoloMode()) {
            bolts += QStringLiteral("\u03DF"); // ϟ
        }
        if (m_session->doubleYoloMode()) {
            bolts += QStringLiteral("\u03DF");
        }
        if (m_session->tripleYoloMode()) {
            bolts += QStringLiteral("\u03DF");
        }

        QString label = QStringLiteral("Working");
        if (!bolts.isEmpty()) {
            label += QStringLiteral(" %1").arg(bolts);
        }
        if (approvals > 0) {
            label += QStringLiteral(" [%1]").arg(approvals);
        }
        return label;
    }
    case ClaudeProcess::State::Idle:
        return QStringLiteral("Idle");
    case ClaudeProcess::State::WaitingInput:
        return QStringLiteral("Waiting for input");
    case ClaudeProcess::State::Error:
        return QStringLiteral("Error");
    }

    return QStringLiteral("Unknown");
}

} // namespace Konsolai
