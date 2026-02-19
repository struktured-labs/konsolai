/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeHookHandlerTest.h"

// Qt
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTest>

// Konsolai
#include "../claude/ClaudeHookHandler.h"

using namespace Konsolai;

void ClaudeHookHandlerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeHookHandlerTest::cleanupTestCase()
{
}

void ClaudeHookHandlerTest::cleanup()
{
    // Drain deferred deletions from TCP socket cleanup to prevent segfault at exit
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void ClaudeHookHandlerTest::testSocketPath()
{
    ClaudeHookHandler handler(QStringLiteral("test1234"));

    QString path = handler.socketPath();
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QStringLiteral("test1234")));
    QVERIFY(path.endsWith(QStringLiteral(".sock")));
}

void ClaudeHookHandlerTest::testSessionDataDir()
{
    QString dir = ClaudeHookHandler::sessionDataDir();

    QVERIFY(!dir.isEmpty());
    QVERIFY(dir.contains(QStringLiteral("konsolai")));
}

void ClaudeHookHandlerTest::testStartStop()
{
    ClaudeHookHandler handler(QStringLiteral("startstop1"));

    QVERIFY(!handler.isRunning());

    // Start the server
    bool started = handler.start();
    QVERIFY(started);
    QVERIFY(handler.isRunning());

    // Verify socket file exists
    QVERIFY(QFile::exists(handler.socketPath()));

    // Stop the server
    handler.stop();
    QVERIFY(!handler.isRunning());

    // Socket file should be cleaned up
    QVERIFY(!QFile::exists(handler.socketPath()));
}

void ClaudeHookHandlerTest::testIsRunning()
{
    ClaudeHookHandler handler(QStringLiteral("isrunning1"));

    QVERIFY(!handler.isRunning());

    handler.start();
    QVERIFY(handler.isRunning());

    handler.stop();
    QVERIFY(!handler.isRunning());
}

void ClaudeHookHandlerTest::testStartTwice()
{
    ClaudeHookHandler handler(QStringLiteral("starttwice1"));

    QVERIFY(handler.start());
    QVERIFY(handler.isRunning());

    // Starting again should succeed (or be a no-op)
    bool secondStart = handler.start();
    QVERIFY(secondStart || handler.isRunning());

    handler.stop();
}

void ClaudeHookHandlerTest::testGenerateHooksConfig()
{
    ClaudeHookHandler handler(QStringLiteral("hooksconfig1"));

    QString config = handler.generateHooksConfig();

    QVERIFY(!config.isEmpty());

    // Should be valid JSON
    QJsonDocument doc = QJsonDocument::fromJson(config.toUtf8());
    QVERIFY(!doc.isNull());
    QVERIFY(doc.isObject());
}

void ClaudeHookHandlerTest::testHookHandlerPath()
{
    QString path = ClaudeHookHandler::hookHandlerPath();

    // Path should be set (may or may not exist in test environment)
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QStringLiteral("konsolai-hook-handler")));
}

void ClaudeHookHandlerTest::testClientConnection()
{
    ClaudeHookHandler handler(QStringLiteral("clientconn1"));
    QVERIFY(handler.start());

    QSignalSpy connectedSpy(&handler, &ClaudeHookHandler::clientConnected);

    // Connect a client
    QLocalSocket client;
    client.connectToServer(handler.socketPath());

    // Wait for connection
    QVERIFY(client.waitForConnected(1000));
    QVERIFY(QTest::qWaitFor([&]() { return connectedSpy.count() > 0; }, 1000));

    QCOMPARE(connectedSpy.count(), 1);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerTest::testReceiveHookEvent()
{
    ClaudeHookHandler handler(QStringLiteral("recvevent1"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    // Connect and send an event
    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    QJsonObject event;
    event[QStringLiteral("event_type")] = QStringLiteral("Stop");
    event[QStringLiteral("data")] = QJsonObject();

    QByteArray message = QJsonDocument(event).toJson(QJsonDocument::Compact);
    message.append('\n');  // Newline delimiter

    client.write(message);
    client.flush();

    // Wait for event to be received
    QVERIFY(QTest::qWaitFor([&]() { return eventSpy.count() > 0; }, 1000));

    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("Stop"));

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerTest::testReceiveMultipleEvents()
{
    ClaudeHookHandler handler(QStringLiteral("multievent1"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    // Send multiple events
    QStringList eventTypes = {
        QStringLiteral("PreToolUse"),
        QStringLiteral("PostToolUse"),
        QStringLiteral("Stop")
    };

    for (const QString &type : eventTypes) {
        QJsonObject event;
        event[QStringLiteral("event_type")] = type;
        event[QStringLiteral("data")] = QJsonObject();

        QByteArray message = QJsonDocument(event).toJson(QJsonDocument::Compact);
        message.append('\n');

        client.write(message);
    }
    client.flush();

    // Wait for all events
    QVERIFY(QTest::qWaitFor([&]() { return eventSpy.count() >= 3; }, 2000));

    QCOMPARE(eventSpy.count(), 3);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerTest::testMalformedJson()
{
    ClaudeHookHandler handler(QStringLiteral("malformed1"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);
    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    // Send malformed JSON
    client.write("not valid json\n");
    client.flush();

    // Should not crash; may emit error signal
    QTest::qWait(100);

    // No valid event should be received
    QCOMPARE(eventSpy.count(), 0);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerTest::testHookEventReceivedSignal()
{
    ClaudeHookHandler handler(QStringLiteral("signal1"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    QJsonObject eventData;
    eventData[QStringLiteral("tool")] = QStringLiteral("Read");
    eventData[QStringLiteral("file")] = QStringLiteral("/test/path");

    QJsonObject event;
    event[QStringLiteral("event_type")] = QStringLiteral("PreToolUse");
    event[QStringLiteral("data")] = eventData;

    QByteArray message = QJsonDocument(event).toJson(QJsonDocument::Compact);
    message.append('\n');

    client.write(message);
    client.flush();

    QVERIFY(QTest::qWaitFor([&]() { return eventSpy.count() > 0; }, 1000));

    QCOMPARE(eventSpy.count(), 1);

    QList<QVariant> args = eventSpy.at(0);
    QCOMPARE(args.at(0).toString(), QStringLiteral("PreToolUse"));

    // Verify event data is passed
    QString dataStr = args.at(1).toString();
    QVERIFY(dataStr.contains(QStringLiteral("Read")) || dataStr.contains(QStringLiteral("tool")));

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerTest::testClientConnectedSignal()
{
    ClaudeHookHandler handler(QStringLiteral("connsig1"));
    QVERIFY(handler.start());

    QSignalSpy connectedSpy(&handler, &ClaudeHookHandler::clientConnected);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    QVERIFY(QTest::qWaitFor([&]() { return connectedSpy.count() > 0; }, 1000));
    QCOMPARE(connectedSpy.count(), 1);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerTest::testClientDisconnectedSignal()
{
    ClaudeHookHandler handler(QStringLiteral("disconnsig1"));
    QVERIFY(handler.start());

    QSignalSpy disconnectedSpy(&handler, &ClaudeHookHandler::clientDisconnected);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    // Wait a bit then disconnect
    QTest::qWait(50);
    client.disconnectFromServer();

    QVERIFY(QTest::qWaitFor([&]() { return disconnectedSpy.count() > 0; }, 1000));
    QCOMPARE(disconnectedSpy.count(), 1);

    handler.stop();
}

void ClaudeHookHandlerTest::testErrorSignal()
{
    // Test that error signal is properly defined and connectable
    ClaudeHookHandler handler(QStringLiteral("errorsig1"));

    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    // Error signal should be connectable
    QVERIFY(errorSpy.isValid());
}

// ============================================================
// TCP mode tests
// ============================================================

void ClaudeHookHandlerTest::testModeDefaultIsUnixSocket()
{
    ClaudeHookHandler handler(QStringLiteral("modedefault1"));
    QCOMPARE(handler.mode(), ClaudeHookHandler::UnixSocket);
    QCOMPARE(handler.tcpPort(), quint16(0));
}

void ClaudeHookHandlerTest::testTcpStartStop()
{
    ClaudeHookHandler handler(QStringLiteral("tcpstartstop1"));
    handler.setMode(ClaudeHookHandler::TCP);

    QVERIFY(!handler.isRunning());
    QCOMPARE(handler.tcpPort(), quint16(0));

    QVERIFY(handler.start());
    QVERIFY(handler.isRunning());
    QVERIFY(handler.tcpPort() > 0);

    quint16 port = handler.tcpPort();
    Q_UNUSED(port)

    handler.stop();
    QVERIFY(!handler.isRunning());
    QCOMPARE(handler.tcpPort(), quint16(0));
}

void ClaudeHookHandlerTest::testTcpClientConnection()
{
    ClaudeHookHandler handler(QStringLiteral("tcpclient1"));
    handler.setMode(ClaudeHookHandler::TCP);
    QVERIFY(handler.start());

    QSignalSpy connectedSpy(&handler, &ClaudeHookHandler::clientConnected);

    auto *client = new QTcpSocket;
    client->connectToHost(QStringLiteral("127.0.0.1"), handler.tcpPort());
    QVERIFY(client->waitForConnected(1000));
    QVERIFY(QTest::qWaitFor(
        [&]() {
            return connectedSpy.count() > 0;
        },
        1000));

    QCOMPARE(connectedSpy.count(), 1);

    client->disconnectFromHost();
    if (client->state() != QAbstractSocket::UnconnectedState) {
        client->waitForDisconnected(500);
    }
    delete client;
    handler.stop();
    QCoreApplication::processEvents();
}

void ClaudeHookHandlerTest::testTcpReceiveEvent()
{
    ClaudeHookHandler handler(QStringLiteral("tcprecv1"));
    handler.setMode(ClaudeHookHandler::TCP);
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    auto *client = new QTcpSocket;
    client->connectToHost(QStringLiteral("127.0.0.1"), handler.tcpPort());
    QVERIFY(client->waitForConnected(1000));

    QJsonObject event;
    event[QStringLiteral("event_type")] = QStringLiteral("Stop");
    event[QStringLiteral("data")] = QJsonObject();

    QByteArray message = QJsonDocument(event).toJson(QJsonDocument::Compact);
    message.append('\n');

    client->write(message);
    client->flush();

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));

    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("Stop"));

    client->disconnectFromHost();
    if (client->state() != QAbstractSocket::UnconnectedState) {
        client->waitForDisconnected(500);
    }
    delete client;
    handler.stop();
    QCoreApplication::processEvents();
}

void ClaudeHookHandlerTest::testTcpConnectionString()
{
    ClaudeHookHandler handler(QStringLiteral("tcpconnstr1"));
    handler.setMode(ClaudeHookHandler::TCP);
    QVERIFY(handler.start());

    QString connStr = handler.connectionString();
    QVERIFY(connStr.startsWith(QStringLiteral("localhost:")));
    QVERIFY(connStr.contains(QString::number(handler.tcpPort())));

    handler.stop();
}

void ClaudeHookHandlerTest::testTcpMultipleClients()
{
    ClaudeHookHandler handler(QStringLiteral("tcpmulti1"));
    handler.setMode(ClaudeHookHandler::TCP);
    QVERIFY(handler.start());

    QSignalSpy connectedSpy(&handler, &ClaudeHookHandler::clientConnected);
    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    auto *client1 = new QTcpSocket;
    client1->connectToHost(QStringLiteral("127.0.0.1"), handler.tcpPort());
    QVERIFY(client1->waitForConnected(1000));

    auto *client2 = new QTcpSocket;
    client2->connectToHost(QStringLiteral("127.0.0.1"), handler.tcpPort());
    QVERIFY(client2->waitForConnected(1000));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return connectedSpy.count() >= 2;
        },
        1000));
    QCOMPARE(connectedSpy.count(), 2);

    // Send event from each client
    auto sendEvent = [](QTcpSocket *sock, const QString &type) {
        QJsonObject event;
        event[QStringLiteral("event_type")] = type;
        event[QStringLiteral("data")] = QJsonObject();
        QByteArray msg = QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n";
        sock->write(msg);
        sock->flush();
    };

    sendEvent(client1, QStringLiteral("PreToolUse"));
    sendEvent(client2, QStringLiteral("PostToolUse"));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() >= 2;
        },
        1000));
    QCOMPARE(eventSpy.count(), 2);

    client1->disconnectFromHost();
    client2->disconnectFromHost();
    if (client1->state() != QAbstractSocket::UnconnectedState) {
        client1->waitForDisconnected(500);
    }
    if (client2->state() != QAbstractSocket::UnconnectedState) {
        client2->waitForDisconnected(500);
    }
    delete client1;
    delete client2;
    handler.stop();
    QCoreApplication::processEvents();
}

void ClaudeHookHandlerTest::testTcpMalformedJson()
{
    ClaudeHookHandler handler(QStringLiteral("tcpmalformed1"));
    handler.setMode(ClaudeHookHandler::TCP);
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);
    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    auto *client = new QTcpSocket;
    client->connectToHost(QStringLiteral("127.0.0.1"), handler.tcpPort());
    QVERIFY(client->waitForConnected(1000));

    client->write("not valid json\n");
    client->flush();

    QTest::qWait(100);

    // No valid event should be received
    QCOMPARE(eventSpy.count(), 0);
    // Error should be reported
    QVERIFY(errorSpy.count() > 0);

    client->disconnectFromHost();
    if (client->state() != QAbstractSocket::UnconnectedState) {
        client->waitForDisconnected(500);
    }
    delete client;
    handler.stop();
    QCoreApplication::processEvents();
}

void ClaudeHookHandlerTest::testTcpClientDisconnect()
{
    ClaudeHookHandler handler(QStringLiteral("tcpdisconn1"));
    handler.setMode(ClaudeHookHandler::TCP);
    QVERIFY(handler.start());

    QSignalSpy disconnectedSpy(&handler, &ClaudeHookHandler::clientDisconnected);

    auto *client = new QTcpSocket;
    client->connectToHost(QStringLiteral("127.0.0.1"), handler.tcpPort());
    QVERIFY(client->waitForConnected(1000));

    QTest::qWait(50);
    client->disconnectFromHost();

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return disconnectedSpy.count() > 0;
        },
        1000));
    QCOMPARE(disconnectedSpy.count(), 1);

    delete client;
    handler.stop();
    QCoreApplication::processEvents();
}

void ClaudeHookHandlerTest::testConnectionStringUnixSocket()
{
    ClaudeHookHandler handler(QStringLiteral("unixconnstr1"));
    // Default mode is UnixSocket
    QCOMPARE(handler.mode(), ClaudeHookHandler::UnixSocket);

    QString connStr = handler.connectionString();
    // Should be the socket path
    QCOMPARE(connStr, handler.socketPath());
    QVERIFY(connStr.endsWith(QStringLiteral(".sock")));
}

// ============================================================
// Remote hook config tests
// ============================================================

void ClaudeHookHandlerTest::testGenerateRemoteHookScript()
{
    ClaudeHookHandler handler(QStringLiteral("remotescript1"));

    QString script = handler.generateRemoteHookScript(12345);

    QVERIFY(!script.isEmpty());
    QVERIFY(script.contains(QStringLiteral("#!/bin/bash")));
    QVERIFY(script.contains(QStringLiteral("12345")));
    QVERIFY(script.contains(QStringLiteral("TUNNEL_PORT")));
    QVERIFY(script.contains(QStringLiteral("event_type")));
    // Must always exit 0
    QVERIFY(script.contains(QStringLiteral("exit 0")));
}

void ClaudeHookHandlerTest::testGenerateRemoteHooksConfig()
{
    ClaudeHookHandler handler(QStringLiteral("remoteconfig1"));

    QString config = handler.generateRemoteHooksConfig(12345, QStringLiteral("/usr/local/bin/konsolai-remote-hook"));

    QVERIFY(!config.isEmpty());

    QJsonDocument doc = QJsonDocument::fromJson(config.toUtf8());
    QVERIFY(!doc.isNull());
    QVERIFY(doc.isObject());

    QJsonObject root = doc.object();
    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();

    // Should have entries for all event types
    QVERIFY(hooks.contains(QStringLiteral("Notification")));
    QVERIFY(hooks.contains(QStringLiteral("Stop")));
    QVERIFY(hooks.contains(QStringLiteral("PreToolUse")));
    QVERIFY(hooks.contains(QStringLiteral("PostToolUse")));
    QVERIFY(hooks.contains(QStringLiteral("PermissionRequest")));
    QVERIFY(hooks.contains(QStringLiteral("SubagentStart")));
    QVERIFY(hooks.contains(QStringLiteral("SubagentStop")));
    QVERIFY(hooks.contains(QStringLiteral("TeammateIdle")));
    QVERIFY(hooks.contains(QStringLiteral("TaskCompleted")));

    // Each entry should reference the script path
    QVERIFY(config.contains(QStringLiteral("konsolai-remote-hook")));
}

// ============================================================
// Missing event_type field
// ============================================================

void ClaudeHookHandlerTest::testMissingEventType()
{
    ClaudeHookHandler handler(QStringLiteral("missingtype1"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);
    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    // JSON without event_type field
    QJsonObject event;
    event[QStringLiteral("data")] = QJsonObject();

    QByteArray message = QJsonDocument(event).toJson(QJsonDocument::Compact);
    message.append('\n');

    client.write(message);
    client.flush();

    QTest::qWait(100);

    // No hookEventReceived should fire — missing event_type
    QCOMPARE(eventSpy.count(), 0);
    // Error should be reported
    QVERIFY(errorSpy.count() > 0);

    client.disconnectFromServer();
    handler.stop();
}

// ============================================================
// ClaudeHookClient tests
// ============================================================

void ClaudeHookHandlerTest::testHookClientSendEvent()
{
    // Start a handler server
    ClaudeHookHandler handler(QStringLiteral("hookclient1"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    // Use ClaudeHookClient to send an event
    ClaudeHookClient hookClient;

    QJsonObject eventData;
    eventData[QStringLiteral("tool_name")] = QStringLiteral("Read");

    bool sent = hookClient.sendEvent(handler.socketPath(), QStringLiteral("PreToolUse"), eventData);
    QVERIFY(sent);

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));

    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("PreToolUse"));

    handler.stop();
}

void ClaudeHookHandlerTest::testHookClientConnectionFailure()
{
    ClaudeHookClient hookClient;

    // Try sending to a non-existent socket
    QJsonObject eventData;
    bool sent = hookClient.sendEvent(QStringLiteral("/tmp/nonexistent-socket-12345.sock"), QStringLiteral("Stop"), eventData, 500);
    QVERIFY(!sent);
}

QTEST_GUILESS_MAIN(ClaudeHookHandlerTest)

#include "moc_ClaudeHookHandlerTest.cpp"
