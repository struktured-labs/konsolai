/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LettaAgentProvider.h"

#include <QTimer>

namespace Konsolai
{

namespace
{
constexpr int kPollIntervalMs = 30000;
constexpr const char *kListTag = "list-agents";
constexpr const char *kMemoryTagPrefix = "memory:";
constexpr const char *kAgentDetailTagPrefix = "detail:";
constexpr const char *kSendTagPrefix = "send:";
} // namespace

LettaAgentProvider::LettaAgentProvider(const QString &baseUrl, const QString &apiKey, QObject *parent)
    : AgentProvider(parent)
    , m_client(new LettaApiClient(baseUrl, apiKey, this))
    , m_pollTimer(new QTimer(this))
{
    connect(m_client, &LettaApiClient::agentsListed, this, &LettaAgentProvider::onAgentsListed);
    connect(m_client, &LettaApiClient::memoryFetched, this, &LettaAgentProvider::onMemoryFetched);
    connect(m_client, &LettaApiClient::agentFetched, this, &LettaAgentProvider::onAgentFetched);
    connect(m_client, &LettaApiClient::messageReplied, this, &LettaAgentProvider::onMessageReplied);
    connect(m_client, &LettaApiClient::requestFailed, this, &LettaAgentProvider::onRequestFailed);

    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &LettaAgentProvider::reload);
    m_pollTimer->start();

    // Kick off the first poll on the next event-loop tick — gives the panel
    // a chance to subscribe to agentsReloaded before the first reply arrives.
    QTimer::singleShot(0, this, &LettaAgentProvider::reload);
}

LettaAgentProvider::~LettaAgentProvider() = default;

void LettaAgentProvider::reload()
{
    m_client->listAgents(QString::fromLatin1(kListTag));
}

void LettaAgentProvider::refresh()
{
    reload();
}

AgentInfo LettaAgentProvider::toAgentInfo(const LettaAgentSummary &summary) const
{
    AgentInfo info;
    info.id = summary.id;
    info.name = summary.name.isEmpty() ? summary.id : summary.name;
    info.project = QString();
    info.schedule = QString();
    // Use the system prompt as the goal text, truncated to the first 200 chars
    // so the tree row stays compact. Detail children carry the full text.
    info.goal = summary.system.left(200);
    info.provider = name();
    info.budget.model = summary.model;
    return info;
}

QList<AgentInfo> LettaAgentProvider::agents() const
{
    QList<AgentInfo> result;
    result.reserve(m_agentOrder.size());
    for (const QString &id : m_agentOrder) {
        auto it = m_agents.constFind(id);
        if (it != m_agents.constEnd()) {
            result.append(toAgentInfo(it.value()));
        }
    }
    return result;
}

AgentStatus LettaAgentProvider::agentStatus(const QString &id) const
{
    AgentStatus status;
    status.state = AgentStatus::Idle;
    auto it = m_lastResults.constFind(id);
    if (it != m_lastResults.constEnd()) {
        status.lastRun = it.value().timestamp;
        status.lastSummary = it.value().summary;
        status.runCount = 1;
    }
    return status;
}

bool LettaAgentProvider::triggerRun(const QString &id, const QString &task)
{
    if (id.isEmpty() || task.isEmpty()) {
        return false;
    }
    m_client->sendMessage(id, task, QString::fromLatin1(kSendTagPrefix) + id);
    return true;
}

bool LettaAgentProvider::setBrief(const QString &, const QString &)
{
    return false;
}

bool LettaAgentProvider::addSteeringNote(const QString &, const QString &)
{
    return false;
}

bool LettaAgentProvider::markBriefDone(const QString &)
{
    return false;
}

QList<AgentReport> LettaAgentProvider::recentReports(const QString &, int) const
{
    return {};
}

QList<AgentRunResult> LettaAgentProvider::recentResults(const QString &id, int) const
{
    QList<AgentRunResult> result;
    auto it = m_lastResults.constFind(id);
    if (it != m_lastResults.constEnd()) {
        result.append(it.value());
    }
    return result;
}

AgentRunResult LettaAgentProvider::lastResult(const QString &id) const
{
    auto it = m_lastResults.constFind(id);
    if (it != m_lastResults.constEnd()) {
        return it.value();
    }
    return {};
}

AgentAttachInfo LettaAgentProvider::attachInfo(const QString &) const
{
    AgentAttachInfo info;
    info.canAttach = false;
    return info;
}

bool LettaAgentProvider::createAgent(const AgentConfig &)
{
    return false;
}

bool LettaAgentProvider::updateAgent(const QString &, const AgentConfig &)
{
    return false;
}

bool LettaAgentProvider::deleteAgent(const QString &)
{
    return false;
}

bool LettaAgentProvider::resetSession(const QString &)
{
    return false;
}

QList<LettaMemoryBlock> LettaAgentProvider::cachedMemoryBlocks(const QString &agentId) const
{
    return m_memoryCache.value(agentId);
}

LettaAgentSummary LettaAgentProvider::cachedSummary(const QString &agentId) const
{
    return m_agents.value(agentId);
}

void LettaAgentProvider::onAgentsListed(const QString &tag, const QList<LettaAgentSummary> &agents)
{
    Q_UNUSED(tag);

    m_lastError.clear();

    QHash<QString, LettaAgentSummary> newAgents;
    QStringList newOrder;
    newOrder.reserve(agents.size());
    bool anyChanged = newAgents.size() != m_agents.size();

    for (const LettaAgentSummary &a : agents) {
        if (a.id.isEmpty()) {
            continue;
        }
        newOrder.append(a.id);
        newAgents.insert(a.id, a);
        auto prev = m_agents.constFind(a.id);
        if (prev == m_agents.constEnd() || prev.value().name != a.name || prev.value().model != a.model) {
            anyChanged = true;
        }
    }

    if (newOrder != m_agentOrder) {
        anyChanged = true;
    }

    m_agents = newAgents;
    m_agentOrder = newOrder;

    if (anyChanged) {
        Q_EMIT agentsReloaded();
    }

    // Lazily refresh memory for each known agent. Cheap GETs; spaced naturally
    // by the network. Errors per agent are folded into m_lastError as they arrive.
    for (const QString &id : m_agentOrder) {
        m_client->getMemoryBlocks(id, QString::fromLatin1(kMemoryTagPrefix) + id);
    }
}

void LettaAgentProvider::onMemoryFetched(const QString &tag, const QList<LettaMemoryBlock> &blocks)
{
    if (!tag.startsWith(QString::fromLatin1(kMemoryTagPrefix))) {
        return;
    }
    const QString agentId = tag.mid(qstrlen(kMemoryTagPrefix));
    m_memoryCache.insert(agentId, blocks);
    Q_EMIT agentChanged(agentId);
}

void LettaAgentProvider::onAgentFetched(const QString &tag, const LettaAgentSummary &agent)
{
    if (!tag.startsWith(QString::fromLatin1(kAgentDetailTagPrefix))) {
        return;
    }
    if (agent.id.isEmpty()) {
        return;
    }
    m_agents.insert(agent.id, agent);
    Q_EMIT agentChanged(agent.id);
}

void LettaAgentProvider::onMessageReplied(const QString &tag, const QString &assistantText)
{
    if (!tag.startsWith(QString::fromLatin1(kSendTagPrefix))) {
        return;
    }
    const QString agentId = tag.mid(qstrlen(kSendTagPrefix));
    AgentRunResult result;
    result.status = AgentRunResult::Ok;
    result.summary = assistantText.left(200);
    result.fullOutput = assistantText;
    result.timestamp = QDateTime::currentDateTime();
    m_lastResults.insert(agentId, result);
    Q_EMIT agentChanged(agentId);
}

void LettaAgentProvider::onRequestFailed(const QString &tag, const QString &endpoint, const QString &error)
{
    Q_UNUSED(tag);
    m_lastError = QStringLiteral("%1: %2").arg(endpoint, error);
    // Failed list refresh clears the agent cache so the UI shows an empty
    // group with the error tooltip rather than stale data.
    if (endpoint == QStringLiteral("listAgents")) {
        if (!m_agents.isEmpty() || !m_agentOrder.isEmpty()) {
            m_agents.clear();
            m_agentOrder.clear();
            Q_EMIT agentsReloaded();
        } else {
            // Still notify so the panel can refresh the error tooltip.
            Q_EMIT agentsReloaded();
        }
    }
}

} // namespace Konsolai

#include "moc_LettaAgentProvider.cpp"
