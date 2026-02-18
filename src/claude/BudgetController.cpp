/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BudgetController.h"
#include "ClaudeSession.h"

#include <QDebug>

namespace Konsolai
{

BudgetController::BudgetController(QObject *parent)
    : QObject(parent)
    , m_checkTimer(new QTimer(this))
{
    m_checkTimer->setInterval(60000); // 60 seconds
    connect(m_checkTimer, &QTimer::timeout, this, &BudgetController::checkTimeBudget);
    m_checkTimer->start();
}

BudgetController::~BudgetController()
{
    m_checkTimer->stop();
}

void BudgetController::setBudget(const SessionBudget &b)
{
    m_budget = b;
    if (!m_budget.startedAt.isValid()) {
        m_budget.startedAt = QDateTime::currentDateTime();
    }
    // Reset deduplication flags when budget changes
    m_timeWarningEmitted = false;
    m_costWarningEmitted = false;
    m_tokenWarningEmitted = false;
    m_timeExceededEmitted = false;
    m_costExceededEmitted = false;
    m_tokenExceededEmitted = false;
}

void BudgetController::onTokenUsageChanged(const TokenUsage &usage)
{
    // Update velocity ring buffer
    m_velocity.addSample(usage.totalTokens(), usage.estimatedCostUSD());
    Q_EMIT velocityUpdated();

    // Check cost ceiling
    if (m_budget.costCeilingUSD > 0.0) {
        double cost = usage.estimatedCostUSD();
        double percent = (cost / m_budget.costCeilingUSD) * 100.0;

        if (cost >= m_budget.costCeilingUSD) {
            if (!m_budget.costExceeded) {
                m_budget.costExceeded = true;
                if (!m_costExceededEmitted) {
                    m_costExceededEmitted = true;
                    qDebug() << "BudgetController: Cost ceiling exceeded -" << cost << ">=" << m_budget.costCeilingUSD;
                    Q_EMIT budgetExceeded(QStringLiteral("cost"));
                }
            }
        } else if (percent >= m_budget.warningThresholdPercent && !m_costWarningEmitted) {
            m_costWarningEmitted = true;
            qDebug() << "BudgetController: Cost warning at" << percent << "%";
            Q_EMIT budgetWarning(QStringLiteral("cost"), percent);
        }
    }

    // Check token ceiling
    if (m_budget.tokenCeiling > 0) {
        quint64 tokens = usage.totalTokens();
        double percent = (static_cast<double>(tokens) / static_cast<double>(m_budget.tokenCeiling)) * 100.0;

        if (tokens >= m_budget.tokenCeiling) {
            if (!m_budget.tokenExceeded) {
                m_budget.tokenExceeded = true;
                if (!m_tokenExceededEmitted) {
                    m_tokenExceededEmitted = true;
                    qDebug() << "BudgetController: Token ceiling exceeded -" << tokens << ">=" << m_budget.tokenCeiling;
                    Q_EMIT budgetExceeded(QStringLiteral("token"));
                }
            }
        } else if (percent >= m_budget.warningThresholdPercent && !m_tokenWarningEmitted) {
            m_tokenWarningEmitted = true;
            qDebug() << "BudgetController: Token warning at" << percent << "%";
            Q_EMIT budgetWarning(QStringLiteral("token"), percent);
        }
    }
}

void BudgetController::onResourceUsageChanged(const ResourceUsage &usage)
{
    // CPU debounce gate
    if (usage.cpuPercent >= m_gate.cpuThresholdPercent) {
        m_gate.currentCpuExceedCount++;
        if (!m_gate.gateTriggered && m_gate.currentCpuExceedCount >= m_gate.cpuDebounceCount) {
            m_gate.gateTriggered = true;
            m_yoloPausedByGate = true;
            qDebug() << "BudgetController: CPU gate triggered after" << m_gate.currentCpuExceedCount << "ticks at" << usage.cpuPercent << "%";
            Q_EMIT resourceGateTriggered(
                QStringLiteral("CPU sustained above %1% for %2 ticks").arg(m_gate.cpuThresholdPercent, 0, 'f', 0).arg(m_gate.currentCpuExceedCount));
        }
    } else {
        // CPU back below threshold — reset debounce
        if (m_gate.currentCpuExceedCount > 0) {
            m_gate.currentCpuExceedCount = 0;
        }
        if (m_gate.gateTriggered && usage.cpuPercent < m_gate.cpuThresholdPercent) {
            // Only clear gate if RSS is also OK
            quint64 rssThreshold = m_gate.autoRssThreshold();
            if (usage.rssBytes < rssThreshold) {
                m_gate.gateTriggered = false;
                m_yoloPausedByGate = false;
                qDebug() << "BudgetController: Resource gate cleared";
                Q_EMIT resourceGateCleared();
            }
        }
    }

    // RSS gate
    quint64 rssThreshold = m_gate.autoRssThreshold();
    if (usage.rssBytes >= rssThreshold) {
        if (!m_gate.gateTriggered) {
            m_gate.gateTriggered = true;
            m_yoloPausedByGate = true;
            qDebug() << "BudgetController: RSS gate triggered -" << usage.rssBytes << ">=" << rssThreshold;
            Q_EMIT resourceGateTriggered(QStringLiteral("RSS %1 bytes exceeds threshold %2 bytes").arg(usage.rssBytes).arg(rssThreshold));
        }
    }
}

void BudgetController::checkTimeBudget()
{
    if (m_budget.timeLimitMinutes > 0 && m_budget.startedAt.isValid()) {
        int elapsed = m_budget.elapsedMinutes();
        double percent = (static_cast<double>(elapsed) / static_cast<double>(m_budget.timeLimitMinutes)) * 100.0;

        if (elapsed >= m_budget.timeLimitMinutes) {
            if (!m_budget.timeExceeded) {
                m_budget.timeExceeded = true;
                if (!m_timeExceededEmitted) {
                    m_timeExceededEmitted = true;
                    qDebug() << "BudgetController: Time budget exceeded -" << elapsed << ">=" << m_budget.timeLimitMinutes << "minutes";
                    Q_EMIT budgetExceeded(QStringLiteral("time"));
                }
            }
        } else if (percent >= m_budget.warningThresholdPercent && !m_timeWarningEmitted) {
            m_timeWarningEmitted = true;
            qDebug() << "BudgetController: Time warning at" << percent << "%";
            Q_EMIT budgetWarning(QStringLiteral("time"), percent);
        }
    }

    Q_EMIT velocityUpdated();
}

bool BudgetController::shouldBlockYolo() const
{
    return m_budget.timeExceeded || m_budget.costExceeded || m_budget.tokenExceeded || m_gate.gateTriggered;
}

} // namespace Konsolai
