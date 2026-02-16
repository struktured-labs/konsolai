/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    Based on Bobcat's AIIntegration by İsmail Yılmaz
*/

#include "ClaudeHookHandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QStandardPaths>

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
        qDebug() << "ClaudeHookHandler: Found hook handler in PATH:" << installed;
        return installed;
    }

    // Check relative to application
    QString appDir = QCoreApplication::applicationDirPath();
    QString relative = appDir + QStringLiteral("/konsolai-hook-handler");
    qDebug() << "ClaudeHookHandler: Checking for hook handler at:" << relative;
    if (QFile::exists(relative)) {
        qDebug() << "ClaudeHookHandler: Found hook handler relative to app:" << relative;
        return relative;
    }

    // Check common install locations
    const QStringList commonPaths = {
        QStringLiteral("/usr/local/bin/konsolai-hook-handler"),
        QStringLiteral("/usr/bin/konsolai-hook-handler"),
    };

    for (const QString &path : commonPaths) {
        if (QFile::exists(path)) {
            qDebug() << "ClaudeHookHandler: Found hook handler at:" << path;
            return path;
        }
    }

    qWarning() << "ClaudeHookHandler: Hook handler binary not found!";
    qWarning() << "  Checked: QStandardPaths, appDir=" << appDir << ", /usr/local/bin, /usr/bin";
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
    qDebug() << "  Mode:" << (m_mode == TCP ? "TCP" : "UnixSocket");

    if (m_mode == TCP) {
        return startTcp();
    } else {
        return startUnixSocket();
    }
}

bool ClaudeHookHandler::startUnixSocket()
{
    qDebug() << "ClaudeHookHandler::startUnixSocket() - path:" << m_socketPath;

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

    connect(m_server, &QLocalServer::newConnection, this, &ClaudeHookHandler::onNewConnection);

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

bool ClaudeHookHandler::startTcp()
{
    qDebug() << "ClaudeHookHandler::startTcp()";

    if (m_tcpServer && m_tcpServer->isListening()) {
        qDebug() << "  Already listening on port" << m_tcpPort;
        return true;
    }

    m_tcpServer = new QTcpServer(this);

    connect(m_tcpServer, &QTcpServer::newConnection, this, &ClaudeHookHandler::onTcpNewConnection);

    // Listen on localhost only (will be tunneled via SSH -R)
    if (!m_tcpServer->listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "ClaudeHookHandler: Failed to start TCP server:" << m_tcpServer->errorString();
        Q_EMIT errorOccurred(QStringLiteral("Failed to start TCP hook server: ") + m_tcpServer->errorString());
        delete m_tcpServer;
        m_tcpServer = nullptr;
        return false;
    }

    m_tcpPort = m_tcpServer->serverPort();
    qDebug() << "ClaudeHookHandler: Started TCP server on port" << m_tcpPort;
    return true;
}

bool ClaudeHookHandler::isRunning() const
{
    if (m_mode == TCP) {
        return m_tcpServer && m_tcpServer->isListening();
    } else {
        return m_server && m_server->isListening();
    }
}

QString ClaudeHookHandler::connectionString() const
{
    if (m_mode == TCP) {
        return QStringLiteral("localhost:%1").arg(m_tcpPort);
    } else {
        return m_socketPath;
    }
}

void ClaudeHookHandler::stop()
{
    // Stop Unix socket server
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }

    // Close all Unix socket client connections
    for (QLocalSocket *client : std::as_const(m_clients)) {
        client->disconnectFromServer();
        client->deleteLater();
    }
    m_clients.clear();

    // Remove socket file
    if (QFile::exists(m_socketPath)) {
        QFile::remove(m_socketPath);
    }

    // Stop TCP server
    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    // Close all TCP client connections
    for (QTcpSocket *client : std::as_const(m_tcpClients)) {
        client->disconnectFromHost();
        client->deleteLater();
    }
    m_tcpClients.clear();
    m_tcpPort = 0;
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
    QLocalSocket *client = qobject_cast<QLocalSocket *>(sender());
    if (client) {
        m_clients.remove(client);
        client->deleteLater();
        Q_EMIT clientDisconnected();
    }
}

void ClaudeHookHandler::onTcpNewConnection()
{
    qDebug() << "ClaudeHookHandler: New TCP connection received";
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket *client = m_tcpServer->nextPendingConnection();
        if (client) {
            m_tcpClients.insert(client);

            connect(client, &QTcpSocket::readyRead, this, &ClaudeHookHandler::onTcpClientReadyRead);
            connect(client, &QTcpSocket::disconnected, this, &ClaudeHookHandler::onTcpClientDisconnected);

            qDebug() << "ClaudeHookHandler: TCP client connected, total clients:" << m_tcpClients.size();
            Q_EMIT clientConnected();
        }
    }
}

void ClaudeHookHandler::onTcpClientReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) {
        return;
    }

    while (client->canReadLine()) {
        QByteArray line = client->readLine();
        handleMessage(line);
    }
}

void ClaudeHookHandler::onTcpClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (client) {
        m_tcpClients.remove(client);
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
        qWarning() << "ClaudeHookHandler: Cannot generate hooks config - hook handler not found";
        return QString();
    }

    qDebug() << "ClaudeHookHandler: Generating hooks config with handler:" << handlerPath;
    qDebug() << "  Socket path:" << m_socketPath;

    QJsonObject hooks;

    // Build command string for each hook type
    // Claude Code expects: "command": "string" (not an array)
    // and "matcher": {} (empty object for match-all, not a string)
    auto makeHookEntry = [&](const QString &eventType) -> QJsonArray {
        // Quote paths in case they contain spaces
        QString cmdStr = QStringLiteral("'%1' --socket '%2' --event '%3'").arg(handlerPath, m_socketPath, eventType);
        QJsonObject hookDef;
        hookDef[QStringLiteral("type")] = QStringLiteral("command");
        hookDef[QStringLiteral("command")] = cmdStr;

        QJsonObject entry;
        entry[QStringLiteral("matcher")] = QStringLiteral("*"); // string = match all
        entry[QStringLiteral("hooks")] = QJsonArray{hookDef};
        return QJsonArray{entry};
    };

    hooks[QStringLiteral("Notification")] = makeHookEntry(QStringLiteral("Notification"));
    hooks[QStringLiteral("Stop")] = makeHookEntry(QStringLiteral("Stop"));
    hooks[QStringLiteral("PreToolUse")] = makeHookEntry(QStringLiteral("PreToolUse"));
    hooks[QStringLiteral("PostToolUse")] = makeHookEntry(QStringLiteral("PostToolUse"));
    hooks[QStringLiteral("PermissionRequest")] = makeHookEntry(QStringLiteral("PermissionRequest"));
    hooks[QStringLiteral("SubagentStart")] = makeHookEntry(QStringLiteral("SubagentStart"));
    hooks[QStringLiteral("SubagentStop")] = makeHookEntry(QStringLiteral("SubagentStop"));
    hooks[QStringLiteral("TeammateIdle")] = makeHookEntry(QStringLiteral("TeammateIdle"));
    hooks[QStringLiteral("TaskCompleted")] = makeHookEntry(QStringLiteral("TaskCompleted"));

    // Wrap in "hooks" key as expected by Claude Code
    QJsonObject root;
    root[QStringLiteral("hooks")] = hooks;

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QString ClaudeHookHandler::generateRemoteHookScript(quint16 tunnelPort) const
{
    // Generate a shell script that sends hook events via netcat to the SSH tunnel
    // This script runs on the remote machine and communicates back to local Konsolai
    QString script = QStringLiteral(R"(#!/bin/bash
# Konsolai remote hook handler - sends events through SSH reverse tunnel
# Usage: konsolai-remote-hook --event <event_type>
# Reads hook data from stdin, forwards to local Konsolai via SSH tunnel

EVENT_TYPE=""
TUNNEL_PORT=%1

while [[ $# -gt 0 ]]; do
    case $1 in
        --event)
            EVENT_TYPE="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

if [[ -z "$EVENT_TYPE" ]]; then
    echo "Error: --event required" >&2
    exit 0  # Always exit 0 to not break Claude
fi

# Read hook data from stdin
HOOK_DATA=$(cat)

# Build JSON message
JSON_MSG=$(cat <<EOF
{"event_type":"${EVENT_TYPE}","data":${HOOK_DATA}}
EOF
)

# Send via netcat to the SSH tunnel port (timeout 2s, suppress errors)
# Falls back to bash /dev/tcp if nc is not available
if command -v nc &>/dev/null; then
    echo "$JSON_MSG" | nc -w 2 localhost $TUNNEL_PORT 2>/dev/null || true
else
    (exec 3<>/dev/tcp/localhost/$TUNNEL_PORT && echo "$JSON_MSG" >&3 && exec 3>&-) 2>/dev/null || true
fi

# Always exit 0 to not break Claude
exit 0
)")
                         .arg(tunnelPort);

    return script;
}

QString ClaudeHookHandler::generateRemoteHooksConfig(quint16 tunnelPort, const QString &scriptPath) const
{
    Q_UNUSED(tunnelPort); // Port is embedded in the script

    qDebug() << "ClaudeHookHandler: Generating remote hooks config";
    qDebug() << "  Script path:" << scriptPath;

    QJsonObject hooks;

    // Build command string for each hook type
    auto makeHookEntry = [&](const QString &eventType) -> QJsonArray {
        QString cmdStr = QStringLiteral("'%1' --event '%2'").arg(scriptPath, eventType);
        QJsonObject hookDef;
        hookDef[QStringLiteral("type")] = QStringLiteral("command");
        hookDef[QStringLiteral("command")] = cmdStr;

        QJsonObject entry;
        entry[QStringLiteral("matcher")] = QStringLiteral("*");
        entry[QStringLiteral("hooks")] = QJsonArray{hookDef};
        return QJsonArray{entry};
    };

    hooks[QStringLiteral("Notification")] = makeHookEntry(QStringLiteral("Notification"));
    hooks[QStringLiteral("Stop")] = makeHookEntry(QStringLiteral("Stop"));
    hooks[QStringLiteral("PreToolUse")] = makeHookEntry(QStringLiteral("PreToolUse"));
    hooks[QStringLiteral("PostToolUse")] = makeHookEntry(QStringLiteral("PostToolUse"));
    hooks[QStringLiteral("PermissionRequest")] = makeHookEntry(QStringLiteral("PermissionRequest"));
    hooks[QStringLiteral("SubagentStart")] = makeHookEntry(QStringLiteral("SubagentStart"));
    hooks[QStringLiteral("SubagentStop")] = makeHookEntry(QStringLiteral("SubagentStop"));
    hooks[QStringLiteral("TeammateIdle")] = makeHookEntry(QStringLiteral("TeammateIdle"));
    hooks[QStringLiteral("TaskCompleted")] = makeHookEntry(QStringLiteral("TaskCompleted"));

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
