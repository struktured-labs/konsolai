/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    Based on Bobcat's AIIntegration by İsmail Yılmaz
*/

#include "ClaudeHookHandler.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QCoreApplication>

namespace Konsolai
{

// ============================================================================
// ClaudeHookHandler
// ============================================================================

ClaudeHookHandler::ClaudeHookHandler(const QString &sessionId, QObject *parent)
    : QObject(parent)
    , m_sessionId(sessionId)
{
    // Socket path: ~/.konsolai/sessions/{session-id}.sock
    m_socketPath = sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
}

ClaudeHookHandler::~ClaudeHookHandler()
{
    stop();
}

QString ClaudeHookHandler::sessionDataDir()
{
    QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataHome + QStringLiteral("/konsolai");
}

QString ClaudeHookHandler::hookHandlerPath()
{
    // First check if installed
    QString installed = QStandardPaths::findExecutable(QStringLiteral("konsolai-hook-handler"));
    if (!installed.isEmpty()) {
        return installed;
    }

    // Check relative to application
    QString appDir = QCoreApplication::applicationDirPath();
    QString relative = appDir + QStringLiteral("/konsolai-hook-handler");
    if (QFile::exists(relative)) {
        return relative;
    }

    // Check common install locations
    const QStringList commonPaths = {
        QStringLiteral("/usr/local/bin/konsolai-hook-handler"),
        QStringLiteral("/usr/bin/konsolai-hook-handler"),
    };

    for (const QString &path : commonPaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }

    return QString();
}

void ClaudeHookHandler::ensureDirectoryExists()
{
    QString dir = sessionDataDir() + QStringLiteral("/sessions");
    QDir d(dir);
    if (!d.exists()) {
        bool created = d.mkpath(dir);
        qDebug() << "ClaudeHookHandler: Created sessions directory:" << dir << "result:" << created;
    } else {
        qDebug() << "ClaudeHookHandler: Sessions directory already exists:" << dir;
    }
}

bool ClaudeHookHandler::start()
{
    qDebug() << "ClaudeHookHandler::start() called for session:" << m_sessionId;
    qDebug() << "  Socket path:" << m_socketPath;

    if (m_server && m_server->isListening()) {
        qDebug() << "  Already listening";
        return true;
    }

    ensureDirectoryExists();
    qDebug() << "  Directory ensured:" << sessionDataDir() + QStringLiteral("/sessions");

    // Remove old socket file if exists
    if (QFile::exists(m_socketPath)) {
        QFile::remove(m_socketPath);
        qDebug() << "  Removed old socket file";
    }

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    connect(m_server, &QLocalServer::newConnection,
            this, &ClaudeHookHandler::onNewConnection);

    if (!m_server->listen(m_socketPath)) {
        qWarning() << "ClaudeHookHandler: Failed to start hook server:" << m_server->errorString();
        Q_EMIT errorOccurred(QStringLiteral("Failed to start hook server: ") + m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }

    qDebug() << "ClaudeHookHandler: Started listening on" << m_socketPath;
    return true;
}

void ClaudeHookHandler::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }

    // Close all client connections
    for (QLocalSocket *client : std::as_const(m_clients)) {
        client->disconnectFromServer();
        client->deleteLater();
    }
    m_clients.clear();

    // Remove socket file
    if (QFile::exists(m_socketPath)) {
        QFile::remove(m_socketPath);
    }
}

void ClaudeHookHandler::onNewConnection()
{
    qDebug() << "ClaudeHookHandler: New connection received";
    while (m_server->hasPendingConnections()) {
        QLocalSocket *client = m_server->nextPendingConnection();
        if (client) {
            m_clients.insert(client);

            connect(client, &QLocalSocket::readyRead,
                    this, &ClaudeHookHandler::onClientReadyRead);
            connect(client, &QLocalSocket::disconnected,
                    this, &ClaudeHookHandler::onClientDisconnected);

            qDebug() << "ClaudeHookHandler: Client connected, total clients:" << m_clients.size();
            Q_EMIT clientConnected();
        }
    }
}

void ClaudeHookHandler::onClientReadyRead()
{
    QLocalSocket *client = qobject_cast<QLocalSocket*>(sender());
    if (!client) {
        return;
    }

    while (client->canReadLine()) {
        QByteArray line = client->readLine();
        handleMessage(line);
    }
}

void ClaudeHookHandler::onClientDisconnected()
{
    QLocalSocket *client = qobject_cast<QLocalSocket*>(sender());
    if (client) {
        m_clients.remove(client);
        client->deleteLater();
        Q_EMIT clientDisconnected();
    }
}

void ClaudeHookHandler::handleMessage(const QByteArray &data)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        Q_EMIT errorOccurred(QStringLiteral("Failed to parse hook message: ") + error.errorString());
        return;
    }

    if (!doc.isObject()) {
        Q_EMIT errorOccurred(QStringLiteral("Hook message is not a JSON object"));
        return;
    }

    processJsonMessage(doc.object());
}

void ClaudeHookHandler::processJsonMessage(const QJsonObject &obj)
{
    QString eventType = obj.value(QStringLiteral("event_type")).toString();
    if (eventType.isEmpty()) {
        qWarning() << "ClaudeHookHandler: Hook message missing event_type";
        Q_EMIT errorOccurred(QStringLiteral("Hook message missing event_type"));
        return;
    }

    // Convert the data portion to a string for the signal
    QJsonObject eventData = obj.value(QStringLiteral("data")).toObject();
    QJsonDocument dataDoc(eventData);
    QString dataString = QString::fromUtf8(dataDoc.toJson(QJsonDocument::Compact));

    qDebug() << "ClaudeHookHandler: Received hook event:" << eventType;
    qDebug() << "  Data:" << dataString.left(200);

    Q_EMIT hookEventReceived(eventType, dataString);
}

QString ClaudeHookHandler::generateHooksConfig() const
{
    // Generate hooks configuration JSON for Claude
    // This creates a hooks.json that calls konsolai-hook-handler for each event type

    QString handlerPath = hookHandlerPath();
    if (handlerPath.isEmpty()) {
        return QString();
    }

    QJsonObject hooks;

    // Notification hook
    QJsonArray notificationHooks;
    QJsonObject notificationHook;
    notificationHook[QStringLiteral("matcher")] = QStringLiteral("*");
    QJsonArray notificationCmd;
    notificationCmd.append(handlerPath);
    notificationCmd.append(QStringLiteral("--socket"));
    notificationCmd.append(m_socketPath);
    notificationCmd.append(QStringLiteral("--event"));
    notificationCmd.append(QStringLiteral("Notification"));
    notificationHook[QStringLiteral("hooks")] = QJsonArray{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                    {QStringLiteral("command"), notificationCmd}}
    };
    notificationHooks.append(notificationHook);
    hooks[QStringLiteral("Notification")] = notificationHooks;

    // Stop hook
    QJsonArray stopHooks;
    QJsonObject stopHook;
    stopHook[QStringLiteral("matcher")] = QStringLiteral("*");
    QJsonArray stopCmd;
    stopCmd.append(handlerPath);
    stopCmd.append(QStringLiteral("--socket"));
    stopCmd.append(m_socketPath);
    stopCmd.append(QStringLiteral("--event"));
    stopCmd.append(QStringLiteral("Stop"));
    stopHook[QStringLiteral("hooks")] = QJsonArray{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                    {QStringLiteral("command"), stopCmd}}
    };
    stopHooks.append(stopHook);
    hooks[QStringLiteral("Stop")] = stopHooks;

    // PreToolUse hook
    QJsonArray preToolHooks;
    QJsonObject preToolHook;
    preToolHook[QStringLiteral("matcher")] = QStringLiteral("*");
    QJsonArray preToolCmd;
    preToolCmd.append(handlerPath);
    preToolCmd.append(QStringLiteral("--socket"));
    preToolCmd.append(m_socketPath);
    preToolCmd.append(QStringLiteral("--event"));
    preToolCmd.append(QStringLiteral("PreToolUse"));
    preToolHook[QStringLiteral("hooks")] = QJsonArray{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                    {QStringLiteral("command"), preToolCmd}}
    };
    preToolHooks.append(preToolHook);
    hooks[QStringLiteral("PreToolUse")] = preToolHooks;

    // PostToolUse hook
    QJsonArray postToolHooks;
    QJsonObject postToolHook;
    postToolHook[QStringLiteral("matcher")] = QStringLiteral("*");
    QJsonArray postToolCmd;
    postToolCmd.append(handlerPath);
    postToolCmd.append(QStringLiteral("--socket"));
    postToolCmd.append(m_socketPath);
    postToolCmd.append(QStringLiteral("--event"));
    postToolCmd.append(QStringLiteral("PostToolUse"));
    postToolHook[QStringLiteral("hooks")] = QJsonArray{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                    {QStringLiteral("command"), postToolCmd}}
    };
    postToolHooks.append(postToolHook);
    hooks[QStringLiteral("PostToolUse")] = postToolHooks;

    // Wrap in "hooks" key as expected by Claude Code
    QJsonObject root;
    root[QStringLiteral("hooks")] = hooks;

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}


// ============================================================================
// ClaudeHookClient
// ============================================================================

ClaudeHookClient::ClaudeHookClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
{
}

ClaudeHookClient::~ClaudeHookClient() = default;

bool ClaudeHookClient::sendEvent(const QString &socketPath,
                                  const QString &eventType,
                                  const QJsonObject &eventData,
                                  int timeoutMs)
{
    m_socket->connectToServer(socketPath);
    if (!m_socket->waitForConnected(timeoutMs)) {
        return false;
    }

    // Build message
    QJsonObject msg;
    msg[QStringLiteral("event_type")] = eventType;
    msg[QStringLiteral("data")] = eventData;

    QJsonDocument doc(msg);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

    m_socket->write(data);
    if (!m_socket->waitForBytesWritten(timeoutMs)) {
        m_socket->disconnectFromServer();
        return false;
    }

    m_socket->disconnectFromServer();
    return true;
}

} // namespace Konsolai

#include "moc_ClaudeHookHandler.cpp"
