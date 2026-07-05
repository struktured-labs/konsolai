/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LettaApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace
{
constexpr const char *kDefaultBaseUrl = "http://localhost:8283";
} // namespace

namespace Konsolai
{

static QString resolveBaseUrl(const QString &override)
{
    const QString envUrl = qEnvironmentVariable("LETTA_BASE_URL");
    if (!envUrl.isEmpty()) {
        return envUrl;
    }
    if (!override.isEmpty()) {
        return override;
    }
    return QString::fromLatin1(kDefaultBaseUrl);
}

static QString resolveApiKey(const QString &override)
{
    const QString envKey = qEnvironmentVariable("LETTA_API_KEY");
    if (!envKey.isEmpty()) {
        return envKey;
    }
    return override;
}

LettaApiClient::LettaApiClient(const QString &baseUrl, const QString &apiKey, QObject *parent)
    : QObject(parent)
    , m_baseUrl(resolveBaseUrl(baseUrl))
    , m_apiKey(resolveApiKey(apiKey))
    , m_nam(new QNetworkAccessManager(this))
{
    qRegisterMetaType<LettaAgentSummary>("LettaAgentSummary");
    qRegisterMetaType<LettaMemoryBlock>("LettaMemoryBlock");
    qRegisterMetaType<QList<LettaAgentSummary>>("QList<LettaAgentSummary>");
    qRegisterMetaType<QList<LettaMemoryBlock>>("QList<LettaMemoryBlock>");
}

LettaApiClient::~LettaApiClient() = default;

void LettaApiClient::setBaseUrl(const QString &url)
{
    m_baseUrl = QUrl(url);
}

void LettaApiClient::setApiKey(const QString &key)
{
    m_apiKey = key;
}

QNetworkRequest LettaApiClient::buildRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    }
    // Short timeouts — the panel polls and we don't want to stall the UI.
    req.setTransferTimeout(8000);
    return req;
}

void LettaApiClient::listAgents(const QString &requestTag)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/v1/agents"));
    auto *reply = m_nam->get(buildRequest(url));
    m_pending.insert(reply, PendingRequest{Endpoint::ListAgents, requestTag, QString()});
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void LettaApiClient::getAgent(const QString &agentId, const QString &requestTag)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/v1/agents/") + agentId);
    auto *reply = m_nam->get(buildRequest(url));
    m_pending.insert(reply, PendingRequest{Endpoint::GetAgent, requestTag, agentId});
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void LettaApiClient::getMemoryBlocks(const QString &agentId, const QString &requestTag)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/v1/agents/") + agentId + QStringLiteral("/core-memory/blocks"));
    auto *reply = m_nam->get(buildRequest(url));
    m_pending.insert(reply, PendingRequest{Endpoint::GetMemory, requestTag, agentId});
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void LettaApiClient::sendMessage(const QString &agentId, const QString &text, const QString &requestTag)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/v1/agents/") + agentId + QStringLiteral("/messages"));

    QJsonObject body;
    QJsonArray messages;
    QJsonObject msg;
    msg[QStringLiteral("role")] = QStringLiteral("user");
    msg[QStringLiteral("content")] = text;
    messages.append(msg);
    body[QStringLiteral("messages")] = messages;

    auto *reply = m_nam->post(buildRequest(url), QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_pending.insert(reply, PendingRequest{Endpoint::SendMessage, requestTag, agentId});
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

static LettaAgentSummary parseAgent(const QJsonObject &obj)
{
    LettaAgentSummary summary;
    summary.id = obj.value(QStringLiteral("id")).toString();
    summary.name = obj.value(QStringLiteral("name")).toString();
    summary.agentType = obj.value(QStringLiteral("agent_type")).toString();

    // Model is reported under several keys depending on Letta version.
    if (obj.contains(QStringLiteral("model"))) {
        summary.model = obj.value(QStringLiteral("model")).toString();
    } else if (obj.contains(QStringLiteral("llm_config"))) {
        summary.model = obj.value(QStringLiteral("llm_config")).toObject().value(QStringLiteral("model")).toString();
    }

    summary.system = obj.value(QStringLiteral("system")).toString();

    const QJsonArray toolsArr = obj.value(QStringLiteral("tools")).toArray();
    for (const QJsonValue &v : toolsArr) {
        if (v.isString()) {
            summary.tools.append(v.toString());
        } else if (v.isObject()) {
            const QString name = v.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                summary.tools.append(name);
            }
        }
    }
    return summary;
}

void LettaApiClient::handleReply(QNetworkReply *reply)
{
    auto it = m_pending.find(reply);
    if (it == m_pending.end()) {
        reply->deleteLater();
        return;
    }
    const PendingRequest pending = it.value();
    m_pending.erase(it);

    const QString endpointStr = [&pending]() {
        switch (pending.endpoint) {
        case Endpoint::ListAgents:
            return QStringLiteral("listAgents");
        case Endpoint::GetAgent:
            return QStringLiteral("getAgent");
        case Endpoint::GetMemory:
            return QStringLiteral("getMemory");
        case Endpoint::SendMessage:
            return QStringLiteral("sendMessage");
        }
        return QString();
    }();

    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT requestFailed(pending.tag, endpointStr, reply->errorString());
        reply->deleteLater();
        return;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &jsonErr);
    if (jsonErr.error != QJsonParseError::NoError) {
        Q_EMIT requestFailed(pending.tag, endpointStr, QStringLiteral("Invalid JSON: %1").arg(jsonErr.errorString()));
        return;
    }

    switch (pending.endpoint) {
    case Endpoint::ListAgents: {
        QList<LettaAgentSummary> result;
        const QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("agents")).toArray();
        for (const QJsonValue &v : arr) {
            result.append(parseAgent(v.toObject()));
        }
        Q_EMIT agentsListed(pending.tag, result);
        break;
    }
    case Endpoint::GetAgent: {
        Q_EMIT agentFetched(pending.tag, parseAgent(doc.object()));
        break;
    }
    case Endpoint::GetMemory: {
        QList<LettaMemoryBlock> blocks;
        const QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("blocks")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            LettaMemoryBlock blk;
            blk.label = obj.value(QStringLiteral("label")).toString();
            blk.value = obj.value(QStringLiteral("value")).toString();
            blk.limit = obj.value(QStringLiteral("limit")).toInt();
            blocks.append(blk);
        }
        Q_EMIT memoryFetched(pending.tag, blocks);
        break;
    }
    case Endpoint::SendMessage: {
        QString text;
        const QJsonObject obj = doc.object();
        // Letta returns {"messages":[{"role":"assistant","content":"..."}, ...]}
        // We collect every assistant message's content into a single string.
        const QJsonArray msgs = obj.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue &m : msgs) {
            const QJsonObject mo = m.toObject();
            if (mo.value(QStringLiteral("role")).toString() != QStringLiteral("assistant")) {
                continue;
            }
            const QJsonValue contentVal = mo.value(QStringLiteral("content"));
            if (contentVal.isString()) {
                if (!text.isEmpty()) {
                    text += QStringLiteral("\n\n");
                }
                text += contentVal.toString();
            } else if (contentVal.isArray()) {
                // Some responses use list-of-parts; concatenate text parts.
                const QJsonArray parts = contentVal.toArray();
                for (const QJsonValue &p : parts) {
                    const QJsonObject po = p.toObject();
                    if (po.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                        if (!text.isEmpty()) {
                            text += QStringLiteral("\n\n");
                        }
                        text += po.value(QStringLiteral("text")).toString();
                    }
                }
            }
        }
        Q_EMIT messageReplied(pending.tag, text);
        break;
    }
    }
}

} // namespace Konsolai

#include "moc_LettaApiClient.cpp"
