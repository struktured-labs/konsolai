/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionObserver.h"

#include <algorithm>

namespace Konsolai
{

SessionObserver::SessionObserver(QObject *parent)
    : QObject(parent)
    , m_periodicTimer(new QTimer(this))
{
    m_periodicTimer->setInterval(60000); // 60s
    connect(m_periodicTimer, &QTimer::timeout, this, &SessionObserver::periodicCheck);
    m_periodicTimer->start();
}

SessionObserver::~SessionObserver()
{
    m_periodicTimer->stop();
}

void SessionObserver::setConfig(const ObserverConfig &config)
{
    m_config = config;
}

const ObserverConfig &SessionObserver::config() const
{
    return m_config;
}

int SessionObserver::composedSeverity() const
{
    int total = 0;
    for (const auto &event : m_activeEvents) {
        total += event.severity;
    }
    return total;
}

QList<StuckEvent> SessionObserver::activeEvents() const
{
    return m_activeEvents;
}

void SessionObserver::reset()
{
    m_activeEvents.clear();
    // Note: m_lastInterventionTime is NOT cleared — cooldowns persist through reset

    m_lastState = 0;
    m_workingStartTime = QDateTime();
    m_tokensAtWorkingStart = 0;
    m_workCycles.clear();

    m_errorSignatures.clear();

    m_currentTotalTokens = 0;
    m_currentInputTokens = 0;
    m_currentOutputTokens = 0;
    m_currentCostUSD = 0.0;

    m_costWindowStart = QDateTime();
    m_costWindowStartTokens = 0;
    m_costWindowStartCost = 0.0;

    m_initialOutputRatio = -1.0;
    m_outputRatioSamples = 0;

    m_recentApprovals.clear();

    m_activeSubagents.clear();
    m_subagentStartTimes.clear();
    m_subagentLifecycles.clear();
}

QString SessionObserver::correctivePrompt(StuckPattern pattern)
{
    switch (pattern) {
    case StuckPattern::IdleLoop:
        return QStringLiteral("Summarize progress, identify blockers, try a different approach");
    case StuckPattern::ErrorLoop:
        return QStringLiteral("Same error N times. Read it carefully, search for root cause, check prerequisites");
    case StuckPattern::CostSpiral:
        return QStringLiteral("Consumed too many tokens with no code changes. Commit progress, plan next actions");
    case StuckPattern::ContextRot:
        return QStringLiteral("Responses degrading. Summarize learnings, create concise action plan");
    case StuckPattern::PermissionStorm:
        return QStringLiteral("Too many permission requests. Consider using --dangerously-skip-permissions or restructuring approach");
    case StuckPattern::SubagentChurn:
        return QStringLiteral("Many agents starting and stopping without completing. Focus on a single approach");
    }
    return QString();
}

// --- Event handlers ---

void SessionObserver::onStateChanged(int state)
{
    const auto now = QDateTime::currentDateTime();

    // Working -> Idle: record work cycle and error signature
    if (m_lastState == StateWorking && state == StateIdle) {
        int durationSecs = 0;
        quint64 tokenDelta = 0;

        if (m_workingStartTime.isValid()) {
            durationSecs = static_cast<int>(m_workingStartTime.secsTo(now));
            tokenDelta = m_currentTotalTokens - m_tokensAtWorkingStart;
        }

        WorkCycle cycle;
        cycle.endTime = now;
        cycle.durationSecs = durationSecs;
        cycle.tokenDelta = tokenDelta;
        m_workCycles.append(cycle);

        // Also record error signature for ErrorLoop
        m_errorSignatures.append({now, QStringLiteral("Working->Idle")});

        checkIdleLoop();
        checkErrorLoop();
    }

    // Transition to Error state: record for ErrorLoop
    if (state == StateError) {
        m_errorSignatures.append({QDateTime::currentDateTime(), QStringLiteral("Error")});
        checkErrorLoop();
    }

    // Entering Working: record start time and token baseline
    if (state == StateWorking) {
        m_workingStartTime = now;
        m_tokensAtWorkingStart = m_currentTotalTokens;
    }

    m_lastState = state;
}

void SessionObserver::onTokenUsageChanged(quint64 inputTokens, quint64 outputTokens, quint64 totalTokens, double costUSD)
{
    m_currentInputTokens = inputTokens;
    m_currentOutputTokens = outputTokens;
    m_currentTotalTokens = totalTokens;
    m_currentCostUSD = costUSD;

    // Sample initial output ratio from the first few updates
    if (m_outputRatioSamples < kOutputRatioSampleCount && inputTokens > 0) {
        double ratio = static_cast<double>(outputTokens) / static_cast<double>(inputTokens);
        if (m_initialOutputRatio < 0.0) {
            m_initialOutputRatio = ratio;
        } else {
            // Running average
            m_initialOutputRatio = (m_initialOutputRatio * m_outputRatioSamples + ratio) / (m_outputRatioSamples + 1);
        }
        ++m_outputRatioSamples;
    }

    checkCostSpiral();
    checkContextRot();
}

void SessionObserver::onApprovalLogged(const QString &toolName, int yoloLevel, const QDateTime &timestamp)
{
    Q_UNUSED(yoloLevel)
    m_recentApprovals.append({timestamp, toolName});
    checkPermissionStorm();
}

void SessionObserver::onSubagentStarted(const QString &agentId)
{
    m_activeSubagents.insert(agentId);
    m_subagentStartTimes[agentId] = QDateTime::currentDateTime();
}

void SessionObserver::onSubagentStopped(const QString &agentId)
{
    const auto now = QDateTime::currentDateTime();
    bool completed = false;

    if (m_subagentStartTimes.contains(agentId)) {
        int durationSecs = static_cast<int>(m_subagentStartTimes[agentId].secsTo(now));
        completed = (durationSecs >= kSubagentCompletionMinSecs);
        m_subagentStartTimes.remove(agentId);
    }

    m_activeSubagents.remove(agentId);
    m_subagentLifecycles.append({now, completed});

    checkSubagentChurn();
}

// --- Pattern checks ---

void SessionObserver::checkIdleLoop()
{
    if (!m_config.idleLoopEnabled)
        return;

    const int threshold = m_config.idleLoopCycleThreshold;
    if (m_workCycles.size() < threshold)
        return;

    // Check the most recent 'threshold' cycles — all must be unproductive
    bool allUnproductive = true;
    for (int i = m_workCycles.size() - threshold; i < m_workCycles.size(); ++i) {
        const auto &cycle = m_workCycles[i];
        if (cycle.durationSecs >= m_config.idleLoopMinWorkSeconds || cycle.tokenDelta >= m_config.idleLoopMinTokens) {
            allUnproductive = false;
            break;
        }
    }

    if (allUnproductive) {
        activatePattern(StuckPattern::IdleLoop, 1, QStringLiteral("Agent completed %1 consecutive idle cycles with minimal work").arg(threshold));
    } else if (isPatternActive(StuckPattern::IdleLoop)) {
        clearPattern(StuckPattern::IdleLoop);
    }
}

void SessionObserver::checkErrorLoop()
{
    if (!m_config.errorLoopEnabled)
        return;

    const auto now = QDateTime::currentDateTime();
    const auto windowStart = now.addSecs(-m_config.errorLoopWindowSeconds);

    // Count error signatures within window
    int count = 0;
    for (const auto &entry : m_errorSignatures) {
        if (entry.first >= windowStart) {
            ++count;
        }
    }

    if (count >= m_config.errorLoopCount) {
        activatePattern(StuckPattern::ErrorLoop,
                        2,
                        QStringLiteral("Detected %1 error-like transitions in %2 seconds").arg(count).arg(m_config.errorLoopWindowSeconds));
    } else if (isPatternActive(StuckPattern::ErrorLoop)) {
        clearPattern(StuckPattern::ErrorLoop);
    }
}

void SessionObserver::checkCostSpiral()
{
    if (!m_config.costSpiralEnabled)
        return;

    const auto now = QDateTime::currentDateTime();

    // Initialize or reset window if expired
    if (!m_costWindowStart.isValid() || m_costWindowStart.secsTo(now) > m_config.costSpiralWindowSeconds) {
        m_costWindowStart = now;
        m_costWindowStartTokens = m_currentTotalTokens;
        m_costWindowStartCost = m_currentCostUSD;

        if (isPatternActive(StuckPattern::CostSpiral)) {
            clearPattern(StuckPattern::CostSpiral);
        }
        return;
    }

    quint64 tokenDelta = m_currentTotalTokens - m_costWindowStartTokens;
    double costDelta = m_currentCostUSD - m_costWindowStartCost;

    if (tokenDelta >= m_config.costSpiralTokenThreshold && costDelta >= m_config.costSpiralCostThreshold) {
        activatePattern(StuckPattern::CostSpiral,
                        2,
                        QStringLiteral("Consumed %1 tokens ($%2) in %3 seconds")
                            .arg(tokenDelta)
                            .arg(costDelta, 0, 'f', 2)
                            .arg(static_cast<int>(m_costWindowStart.secsTo(now))));
    } else if (isPatternActive(StuckPattern::CostSpiral)) {
        clearPattern(StuckPattern::CostSpiral);
    }
}

void SessionObserver::checkContextRot()
{
    if (!m_config.contextRotEnabled)
        return;

    if (m_currentInputTokens < m_config.contextRotInputThreshold)
        return;

    if (m_initialOutputRatio <= 0.0 || m_outputRatioSamples < kOutputRatioSampleCount)
        return;

    double currentRatio = (m_currentInputTokens > 0) ? static_cast<double>(m_currentOutputTokens) / static_cast<double>(m_currentInputTokens) : 0.0;

    if (currentRatio < m_initialOutputRatio * m_config.contextRotOutputRatio) {
        activatePattern(StuckPattern::ContextRot,
                        2,
                        QStringLiteral("Output ratio degraded to %1 (initial: %2, threshold: %3)")
                            .arg(currentRatio, 0, 'f', 3)
                            .arg(m_initialOutputRatio, 0, 'f', 3)
                            .arg(m_initialOutputRatio * m_config.contextRotOutputRatio, 0, 'f', 3));
    } else if (isPatternActive(StuckPattern::ContextRot)) {
        clearPattern(StuckPattern::ContextRot);
    }
}

void SessionObserver::checkPermissionStorm()
{
    if (!m_config.permissionStormEnabled)
        return;

    const auto now = QDateTime::currentDateTime();
    const auto windowStart = now.addSecs(-m_config.permStormWindowSeconds);

    // Count approvals in window and track tool frequencies
    QMap<QString, int> toolCounts;
    int totalInWindow = 0;

    for (const auto &entry : m_recentApprovals) {
        if (entry.first >= windowStart) {
            ++totalInWindow;
            ++toolCounts[entry.second];
        }
    }

    if (totalInWindow >= m_config.permStormCount) {
        // Find the most common tool
        int maxToolCount = 0;
        for (auto it = toolCounts.constBegin(); it != toolCounts.constEnd(); ++it) {
            maxToolCount = qMax(maxToolCount, it.value());
        }

        double sameToolPercent = (totalInWindow > 0) ? (static_cast<double>(maxToolCount) / totalInWindow * 100.0) : 0.0;

        if (sameToolPercent >= m_config.permStormSameToolPercent) {
            activatePattern(StuckPattern::PermissionStorm,
                            1,
                            QStringLiteral("%1 approvals in %2s, dominant tool at %3%")
                                .arg(totalInWindow)
                                .arg(m_config.permStormWindowSeconds)
                                .arg(sameToolPercent, 0, 'f', 1));
        } else if (isPatternActive(StuckPattern::PermissionStorm)) {
            clearPattern(StuckPattern::PermissionStorm);
        }
    } else if (isPatternActive(StuckPattern::PermissionStorm)) {
        clearPattern(StuckPattern::PermissionStorm);
    }
}

void SessionObserver::checkSubagentChurn()
{
    if (!m_config.subagentChurnEnabled)
        return;

    const auto now = QDateTime::currentDateTime();
    const auto windowStart = now.addSecs(-m_config.subagentChurnWindowSeconds);

    int totalStopped = 0;
    int completedCount = 0;

    for (const auto &entry : m_subagentLifecycles) {
        if (entry.first >= windowStart) {
            ++totalStopped;
            if (entry.second) {
                ++completedCount;
            }
        }
    }

    if (totalStopped >= m_config.subagentChurnCount) {
        double completionPercent = (totalStopped > 0) ? (static_cast<double>(completedCount) / totalStopped * 100.0) : 0.0;

        if (completionPercent < m_config.subagentChurnCompletionPercent) {
            activatePattern(StuckPattern::SubagentChurn,
                            1,
                            QStringLiteral("%1 agents stopped, only %2% completed tasks").arg(totalStopped).arg(completionPercent, 0, 'f', 1));
        } else if (isPatternActive(StuckPattern::SubagentChurn)) {
            clearPattern(StuckPattern::SubagentChurn);
        }
    } else if (isPatternActive(StuckPattern::SubagentChurn)) {
        clearPattern(StuckPattern::SubagentChurn);
    }
}

// --- Internal helpers ---

void SessionObserver::activatePattern(StuckPattern pattern, int severity, const QString &description)
{
    if (isCooldownActive(pattern))
        return;

    if (isPatternActive(pattern))
        return;

    InterventionType intervention = suggestIntervention(pattern, severity);

    StuckEvent event;
    event.pattern = pattern;
    event.severity = severity;
    event.description = description;
    event.suggestedIntervention = intervention;
    event.detectedAt = QDateTime::currentDateTime();

    m_activeEvents.append(event);
    m_lastInterventionTime[pattern] = QDateTime::currentDateTime();

    Q_EMIT stuckDetected(static_cast<int>(pattern), severity, description);
    Q_EMIT interventionSuggested(static_cast<int>(intervention), description);
}

void SessionObserver::clearPattern(StuckPattern pattern)
{
    for (int i = m_activeEvents.size() - 1; i >= 0; --i) {
        if (m_activeEvents[i].pattern == pattern) {
            m_activeEvents.removeAt(i);
            Q_EMIT stuckCleared(static_cast<int>(pattern));
            return;
        }
    }
}

bool SessionObserver::isPatternActive(StuckPattern pattern) const
{
    for (const auto &event : m_activeEvents) {
        if (event.pattern == pattern)
            return true;
    }
    return false;
}

bool SessionObserver::isCooldownActive(StuckPattern pattern) const
{
    auto it = m_lastInterventionTime.constFind(pattern);
    if (it == m_lastInterventionTime.constEnd())
        return false;
    return it.value().secsTo(QDateTime::currentDateTime()) < m_config.interventionCooldownSecs;
}

InterventionType SessionObserver::suggestIntervention(StuckPattern pattern, int severity) const
{
    Q_UNUSED(pattern)

    switch (m_config.policy) {
    case ObserverConfig::NotifyOnly:
        return InterventionType::Notify;
    case ObserverConfig::AutoDowngrade:
        return (severity >= 2) ? InterventionType::Pause : InterventionType::Adjust;
    case ObserverConfig::AutoRedirect:
        return InterventionType::Redirect;
    case ObserverConfig::FullAuto:
        return (severity >= 3) ? InterventionType::Restart : InterventionType::Redirect;
    }
    return InterventionType::Notify;
}

void SessionObserver::cleanupOldEntries()
{
    const auto now = QDateTime::currentDateTime();

    // Clean error signatures older than window
    const auto errorCutoff = now.addSecs(-m_config.errorLoopWindowSeconds);
    m_errorSignatures.erase(std::remove_if(m_errorSignatures.begin(),
                                           m_errorSignatures.end(),
                                           [&errorCutoff](const QPair<QDateTime, QString> &entry) {
                                               return entry.first < errorCutoff;
                                           }),
                            m_errorSignatures.end());

    // Clean approvals older than window
    const auto approvalCutoff = now.addSecs(-m_config.permStormWindowSeconds);
    m_recentApprovals.erase(std::remove_if(m_recentApprovals.begin(),
                                           m_recentApprovals.end(),
                                           [&approvalCutoff](const QPair<QDateTime, QString> &entry) {
                                               return entry.first < approvalCutoff;
                                           }),
                            m_recentApprovals.end());

    // Clean subagent lifecycles older than window
    const auto subagentCutoff = now.addSecs(-m_config.subagentChurnWindowSeconds);
    m_subagentLifecycles.erase(std::remove_if(m_subagentLifecycles.begin(),
                                              m_subagentLifecycles.end(),
                                              [&subagentCutoff](const QPair<QDateTime, bool> &entry) {
                                                  return entry.first < subagentCutoff;
                                              }),
                               m_subagentLifecycles.end());

    // Cap work cycles to last 20
    while (m_workCycles.size() > 20) {
        m_workCycles.removeFirst();
    }
}

void SessionObserver::periodicCheck()
{
    cleanupOldEntries();
    checkIdleLoop();
    checkErrorLoop();
    checkCostSpiral();
    checkContextRot();
    checkPermissionStorm();
    checkSubagentChurn();
}

} // namespace Konsolai
