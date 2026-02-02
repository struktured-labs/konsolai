/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeHookHandlerTest.h"

// Qt
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QStandardPaths>
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

QTEST_MAIN(ClaudeHookHandlerTest)

#include "moc_ClaudeHookHandlerTest.cpp"
