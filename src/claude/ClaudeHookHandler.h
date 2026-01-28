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
#include <QTimer>

namespace Konsolai
{

class ClaudeSession;

/**
 * ClaudeHookHandler manages a Unix socket server for receiving Claude hook events.
 *
 * Claude hooks are configured to call the konsolai-hook-handler binary,
 * which connects to this server and sends JSON-encoded events.
 *
 * Hook events:
 * - Notification: Permission prompts, idle prompts, etc.
 * - Stop: Claude finished responding
 * - PreToolUse: Claude is about to use a tool
 * - PostToolUse: Claude finished using a tool
 *
 * Socket path: ~/.konsolai/sessions/{session-id}.sock
 */
class KONSOLEPRIVATE_EXPORT ClaudeHookHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * Create a hook handler for a Claude session
     *
     * @param sessionId The session ID (used for socket path)
     * @param parent Parent object
     */
    explicit ClaudeHookHandler(const QString &sessionId, QObject *parent = nullptr);
    ~ClaudeHookHandler() override;

    /**
     * Get the socket path for this handler
     */
    QString socketPath() const { return m_socketPath; }

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
    bool isRunning() const { return m_server && m_server->isListening(); }

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

private:
    void handleMessage(const QByteArray &data);
    void processJsonMessage(const QJsonObject &obj);
    void ensureDirectoryExists();

    QString m_sessionId;
    QString m_socketPath;
    QLocalServer *m_server = nullptr;
    QSet<QLocalSocket*> m_clients;
};

/**
 * ClaudeHookClient is used by the hook handler binary to connect to Konsolai.
 *
 * This is a simple client that connects to the Unix socket and sends a single
 * message, then disconnects.
 */
class ClaudeHookClient : public QObject
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
