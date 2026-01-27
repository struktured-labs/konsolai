/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeProcess.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace Konsolai
{

ClaudeProcess::ClaudeProcess(QObject *parent)
    : QObject(parent)
{
}

ClaudeProcess::~ClaudeProcess() = default;

QString ClaudeProcess::buildCommand(Model model,
                                     const QString &workingDir,
                                     const QStringList &additionalArgs)
{
    QStringList args;
    args << QStringLiteral("claude");

    // Add model if not default
    if (model != Model::Default) {
        args << QStringLiteral("--model") << modelName(model);
    }

    // Add any additional arguments
    args << additionalArgs;

    return args.join(QLatin1Char(' '));
}

bool ClaudeProcess::isAvailable()
{
    return !executablePath().isEmpty();
}

QString ClaudeProcess::executablePath()
{
    // First check if claude is in PATH
    QString path = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!path.isEmpty()) {
        return path;
    }

    // Check common installation locations
    const QStringList commonPaths = {
        QStringLiteral("/usr/local/bin/claude"),
        QStringLiteral("/usr/bin/claude"),
        QDir::homePath() + QStringLiteral("/.local/bin/claude"),
        QDir::homePath() + QStringLiteral("/.claude/local/claude"),
    };

    for (const QString &p : commonPaths) {
        if (QFile::exists(p)) {
            return p;
        }
    }

    return QString();
}

QString ClaudeProcess::modelName(Model model)
{
    switch (model) {
    case Model::Opus:
        return QStringLiteral("claude-opus-4-5");
    case Model::Sonnet:
        return QStringLiteral("claude-sonnet-4");
    case Model::Haiku:
        return QStringLiteral("claude-haiku");
    case Model::Default:
    default:
        return QString();
    }
}

ClaudeProcess::Model ClaudeProcess::parseModel(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QStringLiteral("opus"))) {
        return Model::Opus;
    }
    if (lower.contains(QStringLiteral("sonnet"))) {
        return Model::Sonnet;
    }
    if (lower.contains(QStringLiteral("haiku"))) {
        return Model::Haiku;
    }
    return Model::Default;
}

void ClaudeProcess::handleHookEvent(const QString &eventType, const QString &eventData)
{
    QJsonDocument doc = QJsonDocument::fromJson(eventData.toUtf8());
    QJsonObject obj = doc.object();

    if (eventType == QStringLiteral("Stop")) {
        // Claude finished responding
        setState(State::Idle);
        Q_EMIT taskFinished();
    }
    else if (eventType == QStringLiteral("PreToolUse")) {
        // Claude is about to use a tool
        setState(State::Working);
        QString toolName = obj.value(QStringLiteral("tool_name")).toString();
        setCurrentTask(QStringLiteral("Using tool: %1").arg(toolName));
    }
    else if (eventType == QStringLiteral("PostToolUse")) {
        // Claude finished using a tool
        // Stay in Working state, tool completed
    }
    else if (eventType == QStringLiteral("Notification")) {
        QString notificationType = obj.value(QStringLiteral("type")).toString();
        QString message = obj.value(QStringLiteral("message")).toString();

        // Handle permission-related notifications (various naming conventions)
        if (notificationType == QStringLiteral("permission_request") || notificationType == QStringLiteral("permission")
            || notificationType == QStringLiteral("permission_required")) {
            setState(State::WaitingInput);
            QString action = obj.value(QStringLiteral("action")).toString();
            QString description = obj.value(QStringLiteral("description")).toString();
            Q_EMIT permissionRequested(action, description);
        } else if (notificationType == QStringLiteral("idle_prompt") || notificationType == QStringLiteral("idle")) {
            setState(State::WaitingInput);
        }

        Q_EMIT notificationReceived(notificationType, message);
    }
}

void ClaudeProcess::setCurrentTask(const QString &task)
{
    m_currentTask = task;
    if (!task.isEmpty()) {
        Q_EMIT taskStarted(task);
    }
}

void ClaudeProcess::clearTask()
{
    m_currentTask.clear();
    Q_EMIT taskFinished();
}

void ClaudeProcess::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        Q_EMIT stateChanged(newState);
    }
}

} // namespace Konsolai

#include "moc_ClaudeProcess.cpp"
