/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_AGENTFLEETPROVIDER_H
#define KONSOLAI_AGENTFLEETPROVIDER_H

#include "AgentProvider.h"

#include <QFileSystemWatcher>
#include <QTimer>

namespace Konsolai
{

/**
 * Agent provider for the agent-fleet framework.
 *
 * Read path (filesystem, no subprocess):
 *   - Goals: {fleetPath}/goals/{name}.yaml
 *   - State: ~/.config/agent-fleet/sessions/{agent}.json
 *   - Budgets: ~/.config/agent-fleet/budgets/{date}/{agent}.json
 *   - Reports: {project}/reports/{name}.md
 *   - Briefs: ~/.config/agent-fleet/briefs/{agent}.json
 *
 * Write path (subprocess):
 *   - agent-fleet trigger <agent> [task]
 *   - agent-fleet brief <agent> <direction>
 *   - agent-fleet steer <agent> <note>
 *   - agent-fleet done <agent>
 */
class KONSOLEPRIVATE_EXPORT AgentFleetProvider : public AgentProvider
{
    Q_OBJECT

public:
    explicit AgentFleetProvider(const QString &fleetPath = QString(), QObject *parent = nullptr);
    ~AgentFleetProvider() override;

    int interfaceVersion() const override;
    QString name() const override;
    bool isAvailable() const override;

    QList<AgentInfo> agents() const override;
    AgentStatus agentStatus(const QString &id) const override;

    bool triggerRun(const QString &id, const QString &task = QString()) override;
    bool setBrief(const QString &id, const QString &direction) override;
    bool addSteeringNote(const QString &id, const QString &note) override;
    bool markBriefDone(const QString &id) override;

    QList<AgentReport> recentReports(const QString &id, int count = 5) const override;
    QList<AgentRunResult> recentResults(const QString &id, int count = 10) const override;
    AgentRunResult lastResult(const QString &id) const override;
    AgentAttachInfo attachInfo(const QString &id) const override;

    bool createAgent(const AgentConfig &config) override;
    bool updateAgent(const QString &id, const AgentConfig &config) override;
    bool deleteAgent(const QString &id) override;

    bool pauseSchedule(const QString &id) override;
    bool resumeSchedule(const QString &id) override;
    bool resetSession(const QString &id) override;

    /** Path to the agent-fleet installation. */
    QString fleetPath() const;

    /** Auto-detect fleet path from common locations. */
    static QString detectFleetPath();

    /** Override config directory (for testing). */
    void setConfigDir(const QString &dir);

    /** Aggregate daily spend across all agents. */
    double totalDailySpendUSD() const;

    /** Aggregate daily budget across all agents. */
    double totalDailyBudgetUSD() const;

public Q_SLOTS:
    void reloadAgents();

private Q_SLOTS:
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);

private:
    AgentInfo parseGoalYaml(const QString &filePath) const;
    AgentStatus parseSessionState(const QString &agentId) const;
    AgentBrief parseBrief(const QString &agentId) const;
    double parseDailyBudgetSpend(const QString &agentId) const;
    QList<AgentReport> parseReports(const QString &projectDir, int count) const;
    QList<AgentRunResult> parseRunResults(const QString &agentId, int count) const;
    bool runFleetCommand(const QStringList &args, int timeoutMs = 30000) const;

    QString configDir() const;
    QString sessionsDir() const;
    QString budgetsDir() const;
    QString briefsDir() const;
    QString goalsDir() const;

    QString m_fleetPath;
    QString m_configDirOverride;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_fallbackTimer = nullptr;
    mutable QList<AgentInfo> m_cachedAgents;
    mutable bool m_cacheValid = false;
};

} // namespace Konsolai

#endif // KONSOLAI_AGENTFLEETPROVIDER_H
