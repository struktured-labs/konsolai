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
    if (!taskDescription.isEmpty()) {
        obj[QStringLiteral("taskDescription")] = taskDescription;
    }
    obj[QStringLiteral("isAttached")] = isAttached;
    if (isRemote) {
        obj[QStringLiteral("isRemote")] = true;
        obj[QStringLiteral("sshHost")] = sshHost;
        obj[QStringLiteral("sshUsername")] = sshUsername;
        obj[QStringLiteral("sshPort")] = sshPort;
    }
    if (!autoContinuePrompt.isEmpty()) {
        obj[QStringLiteral("autoContinuePrompt")] = autoContinuePrompt;
    }
    obj[QStringLiteral("yoloMode")] = yoloMode;
    obj[QStringLiteral("doubleYoloMode")] = doubleYoloMode;
    obj[QStringLiteral("tripleYoloMode")] = tripleYoloMode;
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
    state.taskDescription = obj.value(QStringLiteral("taskDescription")).toString();
    state.isAttached = obj.value(QStringLiteral("isAttached")).toBool();
    state.isRemote = obj.value(QStringLiteral("isRemote")).toBool();
    state.sshHost = obj.value(QStringLiteral("sshHost")).toString();
    state.sshUsername = obj.value(QStringLiteral("sshUsername")).toString();
    state.sshPort = obj.value(QStringLiteral("sshPort")).toInt(22);
    state.autoContinuePrompt = obj.value(QStringLiteral("autoContinuePrompt")).toString();
    state.yoloMode = obj.value(QStringLiteral("yoloMode")).toBool();
    state.doubleYoloMode = obj.value(QStringLiteral("doubleYoloMode")).toBool();
    state.tripleYoloMode = obj.value(QStringLiteral("tripleYoloMode")).toBool();
    return state;
}

} // namespace Konsolai
