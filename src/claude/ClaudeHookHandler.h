/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    Based on Bobcat's AIIntegration by İsmail Yılmaz
*/

#ifndef CLAUDEHOOKHANDLER_H
#define CLAUDEHOOKHANDLER_H

#include "konsoleprivate_export.h"

#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace Konsolai
{

class ClaudeSession;

/**
 * ClaudeHookHandler manages a server for receiving Claude hook events.
 *
 * Supports two modes:
 * - UnixSocket: Uses QLocalServer at ~/.konsolai/sessions/{session-id}.sock
 *   Best for local sessions.
 * - TCP: Uses QTcpServer on a dynamic port, accessible via SSH reverse tunnel
 *   Required for remote SSH sessions.
 *
 * Claude hooks are configured to call the konsolai-hook-handler binary,
 * which connects to this server and sends JSON-encoded events.
 *
 * Hook events:
 * - Notification: Permission prompts, idle prompts, etc.
 * - Stop: Claude finished responding
 * - PreToolUse: Claude is about to use a tool
 * - PostToolUse: Claude finished using a tool
 */
class KONSOLEPRIVATE_EXPORT ClaudeHookHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * Server mode - Unix socket (local) or TCP (remote via SSH tunnel)
     */
    enum Mode {
        UnixSocket, ///< Local Unix socket at ~/.konsolai/sessions/{id}.sock
        TCP ///< TCP server on dynamic port, tunneled via SSH -R
    };

    /**
     * Create a hook handler for a Claude session
     *
     * @param sessionId The session ID (used for socket path)
     * @param parent Parent object
     */
    explicit ClaudeHookHandler(const QString &sessionId, QObject *parent = nullptr);
    ~ClaudeHookHandler() override;

    /**
     * Set the server mode (must be called before start())
     */
    void setMode(Mode mode)
    {
        m_mode = mode;
    }

    /**
     * Get the current server mode
     */
    Mode mode() const
    {
        return m_mode;
    }

    /**
     * Get the socket path for this handler (UnixSocket mode)
     */
    QString socketPath() const { return m_socketPath; }

    /**
     * Get the TCP port (TCP mode only, 0 if not started)
     */
    quint16 tcpPort() const
    {
        return m_tcpPort;
    }

    /**
     * Start the hook handler server
     *
     * @return true if started successfully
     */
    bool start();

    /**
     * Stop the hook handler server
     */
    void stop();

    /**
     * Whether the server is running
     */
    bool isRunning() const;

    /**
     * Get connection string for remote hooks config
     * In TCP mode: "localhost:PORT"
     * In UnixSocket mode: the socket path
     */
    QString connectionString() const;

    /**
     * Get the path to the hook handler binary
     */
    static QString hookHandlerPath();

    /**
     * Generate Claude hooks configuration JSON for this session
     *
     * This returns JSON that can be used in Claude's hooks.json to
     * configure hooks to call the konsolai-hook-handler.
     */
    QString generateHooksConfig() const;

    /**
     * Generate remote hooks script for SSH sessions
     *
     * Returns a shell script that the remote machine can use to send
     * hook events back via the SSH reverse tunnel.
     *
     * @param tunnelPort The port on remote that tunnels back to local TCP server
     */
    QString generateRemoteHookScript(quint16 tunnelPort) const;

    /**
     * Generate remote hooks configuration for SSH sessions
     *
     * Returns hooks.json content that calls a netcat-based script
     * to send events through the SSH tunnel.
     *
     * @param tunnelPort The port on remote that tunnels back to local TCP server
     * @param scriptPath Path to the hook script on the remote machine
     */
    QString generateRemoteHooksConfig(quint16 tunnelPort, const QString &scriptPath) const;

    /**
     * Get the base directory for Konsolai session data
     */
    static QString sessionDataDir();

Q_SIGNALS:
    /**
     * Emitted when a hook event is received
     *
     * @param eventType Type of event (Stop, Notification, PreToolUse, PostToolUse)
     * @param eventData JSON data associated with the event
     */
    void hookEventReceived(const QString &eventType, const QString &eventData);

    /**
     * Emitted when a client connects
     */
    void clientConnected();

    /**
     * Emitted when a client disconnects
     */
    void clientDisconnected();

    /**
     * Emitted when an error occurs
     */
    void errorOccurred(const QString &message);

private Q_SLOTS:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onTcpNewConnection();
    void onTcpClientReadyRead();
    void onTcpClientDisconnected();

private:
    void handleMessage(const QByteArray &data);
    void processJsonMessage(const QJsonObject &obj);
    void ensureDirectoryExists();
    bool startUnixSocket();
    bool startTcp();

    Mode m_mode = UnixSocket;
    QString m_sessionId;
    QString m_socketPath;
    quint16 m_tcpPort = 0;

    // Unix socket mode
    QLocalServer *m_server = nullptr;
    QSet<QLocalSocket *> m_clients;

    // TCP mode
    QTcpServer *m_tcpServer = nullptr;
    QSet<QTcpSocket *> m_tcpClients;
};

/**
 * ClaudeHookClient is used by the hook handler binary to connect to Konsolai.
 *
 * This is a simple client that connects to the Unix socket and sends a single
 * message, then disconnects.
 */
class KONSOLEPRIVATE_EXPORT ClaudeHookClient : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeHookClient(QObject *parent = nullptr);
    ~ClaudeHookClient() override;

    /**
     * Send a hook event to Konsolai
     *
     * @param socketPath Path to the Unix socket
     * @param eventType Type of hook event
     * @param eventData JSON data for the event
     * @param timeoutMs Timeout in milliseconds
     * @return true if sent successfully
     */
    bool sendEvent(const QString &socketPath,
                   const QString &eventType,
                   const QJsonObject &eventData,
                   int timeoutMs = 5000);

private:
    QLocalSocket *m_socket = nullptr;
};

} // namespace Konsolai

#endif // CLAUDEHOOKHANDLER_H
