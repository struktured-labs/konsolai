/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_LETTAAGENTPROVIDER_H
#define KONSOLAI_LETTAAGENTPROVIDER_H

#include "AgentProvider.h"
#include "LettaApiClient.h"

class QTimer;

namespace Konsolai
{

/**
 * Agent provider backed by a running Letta server.
 *
 * Read-only model (per project decision): the provider lists agents and
 * surfaces model/tool/memory metadata as detail children in the tree. The
 * only mutation supported is "send message", exposed through triggerRun().
 *
 * Configuration is picked up from the environment first (LETTA_BASE_URL /
 * LETTA_API_KEY); the constructor arguments are used as fallbacks. See
 * LettaApiClient for details.
 *
 * Reachability handling: when the server is unreachable, isAvailable()
 * stays true (so the panel still shows our group) and the cached agent
 * list is empty until a successful refresh. The last error string is
 * available via lastError() so the panel can surface it on the group node.
 */
class KONSOLEPRIVATE_EXPORT LettaAgentProvider : public AgentProvider
{
    Q_OBJECT

public:
    explicit LettaAgentProvider(const QString &baseUrl = QString(), const QString &apiKey = QString(), QObject *parent = nullptr);
    ~LettaAgentProvider() override;

    int interfaceVersion() const override
    {
        return 2;
    }
    QString name() const override
    {
        return QStringLiteral("letta");
    }
    bool isAvailable() const override
    {
        return true;
    }

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
    bool resetSession(const QString &id) override;

    /** Force an immediate poll. */
    void refresh();

    /** Most recent error string from the API, or empty if the last poll succeeded. */
    QString lastError() const
    {
        return m_lastError;
    }

    /** Access to the underlying HTTP client (used by detail/send-message UI). */
    LettaApiClient *apiClient() const
    {
        return m_client;
    }

    /** Detail data cached for a given agent (memory blocks, full system prompt). */
    QList<LettaMemoryBlock> cachedMemoryBlocks(const QString &agentId) const;
    LettaAgentSummary cachedSummary(const QString &agentId) const;

public Q_SLOTS:
    /** Re-fetch the agent list. Equivalent to refresh(). */
    void reload();

private Q_SLOTS:
    void onAgentsListed(const QString &tag, const QList<LettaAgentSummary> &agents);
    void onMemoryFetched(const QString &tag, const QList<LettaMemoryBlock> &blocks);
    void onAgentFetched(const QString &tag, const LettaAgentSummary &agent);
    void onMessageReplied(const QString &tag, const QString &assistantText);
    void onRequestFailed(const QString &tag, const QString &endpoint, const QString &error);

private:
    AgentInfo toAgentInfo(const LettaAgentSummary &summary) const;

    LettaApiClient *m_client = nullptr;
    QTimer *m_pollTimer = nullptr;

    QHash<QString, LettaAgentSummary> m_agents; // id -> summary
    QStringList m_agentOrder; // preserved order from server
    QHash<QString, QList<LettaMemoryBlock>> m_memoryCache; // id -> blocks
    QHash<QString, AgentRunResult> m_lastResults; // id -> last send-message result
    QString m_lastError;
};

} // namespace Konsolai

#endif // KONSOLAI_LETTAAGENTPROVIDER_H
