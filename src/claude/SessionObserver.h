/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONOBSERVER_H
#define SESSIONOBSERVER_H

#include "konsoleprivate_export.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace Konsolai
{

class ClaudeSession; // forward declaration only

/**
 * Stuck pattern types detected by SessionObserver
 */
enum class KONSOLEPRIVATE_EXPORT StuckPattern {
    IdleLoop, // Working->Idle cycles with little progress
    ErrorLoop, // Same error repeated
    CostSpiral, // High token/cost burn with no code changes
    ContextRot, // Input tokens very high, output shrinking
    PermissionStorm, // Too many permission approvals in short window
    SubagentChurn, // Agents starting/stopping without completing
};

/**
 * Intervention types (escalating severity)
 */
enum class KONSOLEPRIVATE_EXPORT InterventionType {
    Notify, // Desktop notification + tree item badge
    Pause, // Save yolo state, disable relevant levels
    Adjust, // Downgrade yolo level
    Redirect, // Pause L3, send corrective prompt, re-enable
    Restart, // Kill session, create new with carry-forward (needs user confirm)
};

/**
 * Observer configuration with per-pattern enables and thresholds
 */
struct KONSOLEPRIVATE_EXPORT ObserverConfig {
    // Per-pattern enable flags
    bool idleLoopEnabled = true;
    bool errorLoopEnabled = true;
    bool costSpiralEnabled = true;
    bool contextRotEnabled = true;
    bool permissionStormEnabled = true;
    bool subagentChurnEnabled = true;

    // Intervention policy
    enum InterventionPolicy {
        NotifyOnly,
        AutoDowngrade,
        AutoRedirect,
        FullAuto
    };
    InterventionPolicy policy = AutoRedirect;

    // IdleLoop thresholds
    int idleLoopCycleThreshold = 3;
    int idleLoopMinWorkSeconds = 15;
    quint64 idleLoopMinTokens = 5000;

    // ErrorLoop thresholds
    int errorLoopCount = 3;
    int errorLoopWindowSeconds = 300;

    // CostSpiral thresholds
    quint64 costSpiralTokenThreshold = 100000;
    double costSpiralCostThreshold = 1.0;
    int costSpiralWindowSeconds = 300;

    // ContextRot thresholds
    quint64 contextRotInputThreshold = 800000;
    double contextRotOutputRatio = 0.5;

    // PermissionStorm thresholds
    int permStormCount = 10;
    int permStormWindowSeconds = 30;
    double permStormSameToolPercent = 60.0;

    // SubagentChurn thresholds
    int subagentChurnCount = 5;
    int subagentChurnWindowSeconds = 600;
    double subagentChurnCompletionPercent = 20.0;

    // Cooldown between interventions for the same pattern
    int interventionCooldownSecs = 120;
};

/**
 * A detected stuck event with pattern, severity, and suggested intervention
 */
struct KONSOLEPRIVATE_EXPORT StuckEvent {
    StuckPattern pattern;
    int severity = 1; // 1-3
    QString description;
    InterventionType suggestedIntervention;
    QDateTime detectedAt;
};

/**
 * SessionObserver monitors a Claude session for stuck patterns and suggests interventions.
 *
 * Design: L4 session-level supervisor -- zero additional token cost (pure heuristic).
 * Watches existing signals, does not send keystrokes directly.
 * Interventions go through existing setYoloMode() / sendPrompt() APIs on the session.
 */
class KONSOLEPRIVATE_EXPORT SessionObserver : public QObject
{
    Q_OBJECT

public:
    explicit SessionObserver(QObject *parent = nullptr);
    ~SessionObserver() override;

    void setConfig(const ObserverConfig &config);
    const ObserverConfig &config() const;

    int composedSeverity() const;
    QList<StuckEvent> activeEvents() const;
    void reset();

    static QString correctivePrompt(StuckPattern pattern);

public Q_SLOTS:
    void onStateChanged(int state);
    void onTokenUsageChanged(quint64 inputTokens, quint64 outputTokens, quint64 totalTokens, double costUSD);
    void onApprovalLogged(const QString &toolName, int yoloLevel, const QDateTime &timestamp);
    void onSubagentStarted(const QString &agentId);
    void onSubagentStopped(const QString &agentId);

Q_SIGNALS:
    void stuckDetected(int pattern, int severity, const QString &description);
    void stuckCleared(int pattern);
    void interventionSuggested(int interventionType, const QString &description);

private:
    void periodicCheck();
    void checkIdleLoop();
    void checkErrorLoop();
    void checkCostSpiral();
    void checkContextRot();
    void checkPermissionStorm();
    void checkSubagentChurn();

    void activatePattern(StuckPattern pattern, int severity, const QString &description);
    void clearPattern(StuckPattern pattern);
    bool isPatternActive(StuckPattern pattern) const;
    bool isCooldownActive(StuckPattern pattern) const;
    InterventionType suggestIntervention(StuckPattern pattern, int severity) const;
    void cleanupOldEntries();

    // State constants (avoids including ClaudeProcess.h)
    static constexpr int StateIdle = 2;
    static constexpr int StateWorking = 3;
    static constexpr int StateError = 5;

    ObserverConfig m_config;
    QList<StuckEvent> m_activeEvents;
    QMap<StuckPattern, QDateTime> m_lastInterventionTime;
    QTimer *m_periodicTimer = nullptr;

    // General state
    int m_lastState = 0;

    // Token/cost tracking
    quint64 m_currentTotalTokens = 0;
    quint64 m_currentInputTokens = 0;
    quint64 m_currentOutputTokens = 0;
    double m_currentCostUSD = 0.0;

    // IdleLoop tracking
    QDateTime m_workingStartTime;
    quint64 m_tokensAtWorkingStart = 0;
    struct WorkCycle {
        QDateTime endTime;
        int durationSecs;
        quint64 tokenDelta;
    };
    QVector<WorkCycle> m_workCycles;

    // ErrorLoop tracking
    QVector<QPair<QDateTime, QString>> m_errorSignatures;

    // CostSpiral tracking
    QDateTime m_costWindowStart;
    quint64 m_costWindowStartTokens = 0;
    double m_costWindowStartCost = 0.0;

    // ContextRot tracking
    double m_initialOutputRatio = -1.0;
    int m_outputRatioSamples = 0;
    static constexpr int kOutputRatioSampleCount = 3;

    // PermissionStorm tracking
    QVector<QPair<QDateTime, QString>> m_recentApprovals;

    // SubagentChurn tracking
    QSet<QString> m_activeSubagents;
    QMap<QString, QDateTime> m_subagentStartTimes;
    QVector<QPair<QDateTime, bool>> m_subagentLifecycles;
    static constexpr int kSubagentCompletionMinSecs = 30;
};

} // namespace Konsolai

#endif // SESSIONOBSERVER_H
