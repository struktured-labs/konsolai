/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionState.h"

#include <QJsonDocument>

namespace Konsolai
{

QJsonObject ClaudeSessionState::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("sessionName")] = sessionName;
    obj[QStringLiteral("sessionId")] = sessionId;
    obj[QStringLiteral("profileName")] = profileName;
    obj[QStringLiteral("created")] = created.toString(Qt::ISODate);
    obj[QStringLiteral("lastAccessed")] = lastAccessed.toString(Qt::ISODate);
    obj[QStringLiteral("workingDirectory")] = workingDirectory;
    obj[QStringLiteral("claudeModel")] = claudeModel;
    obj[QStringLiteral("isAttached")] = isAttached;
    return obj;
}

ClaudeSessionState ClaudeSessionState::fromJson(const QJsonObject &obj)
{
    ClaudeSessionState state;
    state.sessionName = obj.value(QStringLiteral("sessionName")).toString();
    state.sessionId = obj.value(QStringLiteral("sessionId")).toString();
    state.profileName = obj.value(QStringLiteral("profileName")).toString();
    state.created = QDateTime::fromString(obj.value(QStringLiteral("created")).toString(), Qt::ISODate);
    state.lastAccessed = QDateTime::fromString(obj.value(QStringLiteral("lastAccessed")).toString(), Qt::ISODate);
    state.workingDirectory = obj.value(QStringLiteral("workingDirectory")).toString();
    state.claudeModel = obj.value(QStringLiteral("claudeModel")).toString();
    state.isAttached = obj.value(QStringLiteral("isAttached")).toBool();
    return state;
}

} // namespace Konsolai
