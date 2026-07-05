/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_LETTAAPICLIENT_H
#define KONSOLAI_LETTAAPICLIENT_H

#include "konsoleprivate_export.h"

#include <QHash>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace Konsolai
{

/**
 * Plain data describing a Letta agent. Subset of the /v1/agents response —
 * just the fields the panel renders.
 */
struct LettaAgentSummary {
    QString id;
    QString name;
    QString agentType; // e.g. "memgpt_v2_agent"
    QString model;
    QString system;
    QStringList tools;
};

/**
 * One core-memory block returned by /v1/agents/{id}/core-memory/blocks.
 */
struct LettaMemoryBlock {
    QString label;
    QString value;
    int limit = 0;
};

/**
 * Thin HTTP client for a subset of the Letta server API. All requests are
 * asynchronous; results arrive as signals keyed by an opaque QString request
 * tag so multiple in-flight calls can be matched to their handlers.
 *
 * Configuration:
 *  - Base URL: LETTA_BASE_URL env var, falling back to constructor argument,
 *    falling back to http://localhost:8283.
 *  - Auth: LETTA_API_KEY env var sent as "Authorization: Bearer <key>".
 *    If unset, no auth header is sent.
 */
class KONSOLEPRIVATE_EXPORT LettaApiClient : public QObject
{
    Q_OBJECT

public:
    explicit LettaApiClient(const QString &baseUrl = QString(), const QString &apiKey = QString(), QObject *parent = nullptr);
    ~LettaApiClient() override;

    QUrl baseUrl() const
    {
        return m_baseUrl;
    }
    bool hasAuth() const
    {
        return !m_apiKey.isEmpty();
    }

    /** Override base URL at runtime (e.g. for tests). */
    void setBaseUrl(const QString &url);

    /** Override API key at runtime. */
    void setApiKey(const QString &key);

    /** GET /v1/agents — full agent list. Replies via agentsListed. */
    void listAgents(const QString &requestTag = QString());

    /** GET /v1/agents/{id} — single agent detail. Replies via agentFetched. */
    void getAgent(const QString &agentId, const QString &requestTag = QString());

    /** GET /v1/agents/{id}/core-memory/blocks — memory. Replies via memoryFetched. */
    void getMemoryBlocks(const QString &agentId, const QString &requestTag = QString());

    /** POST /v1/agents/{id}/messages — send a user message. Replies via messageReplied. */
    void sendMessage(const QString &agentId, const QString &text, const QString &requestTag = QString());

Q_SIGNALS:
    void agentsListed(const QString &requestTag, const QList<LettaAgentSummary> &agents);
    void agentFetched(const QString &requestTag, const LettaAgentSummary &agent);
    void memoryFetched(const QString &requestTag, const QList<LettaMemoryBlock> &blocks);
    void messageReplied(const QString &requestTag, const QString &assistantText);
    void requestFailed(const QString &requestTag, const QString &endpoint, const QString &errorString);

private:
    enum class Endpoint {
        ListAgents,
        GetAgent,
        GetMemory,
        SendMessage,
    };

    struct PendingRequest {
        Endpoint endpoint;
        QString tag;
        QString contextId; // agent id, etc.
    };

    QNetworkRequest buildRequest(const QUrl &url) const;
    void handleReply(QNetworkReply *reply);

    QUrl m_baseUrl;
    QString m_apiKey;
    QNetworkAccessManager *m_nam = nullptr;
    QHash<QNetworkReply *, PendingRequest> m_pending;
};

} // namespace Konsolai

Q_DECLARE_METATYPE(Konsolai::LettaAgentSummary)
Q_DECLARE_METATYPE(Konsolai::LettaMemoryBlock)
Q_DECLARE_METATYPE(QList<Konsolai::LettaAgentSummary>)
Q_DECLARE_METATYPE(QList<Konsolai::LettaMemoryBlock>)

#endif // KONSOLAI_LETTAAPICLIENT_H
