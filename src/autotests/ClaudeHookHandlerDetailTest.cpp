/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeHookHandlerDetailTest.h"

// Qt
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeHookHandler.h"

using namespace Konsolai;

void ClaudeHookHandlerDetailTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeHookHandlerDetailTest::cleanupTestCase()
{
}

void ClaudeHookHandlerDetailTest::cleanup()
{
    // Drain deferred deletions to prevent segfault at exit
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

// ============================================================
// generateHooksConfig() tests
// ============================================================

void ClaudeHookHandlerDetailTest::testGenerateHooksConfigIsValidJson()
{
    ClaudeHookHandler handler(QStringLiteral("detail-valid-json"));

    QString config = handler.generateHooksConfig();

    // May be empty if hook handler binary not found — skip gracefully
    if (config.isEmpty()) {
        QSKIP("Hook handler binary not found in test environment");
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(config.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());
}

void ClaudeHookHandlerDetailTest::testGenerateHooksConfigContainsAllEventTypes()
{
    ClaudeHookHandler handler(QStringLiteral("detail-event-types"));

    QString config = handler.generateHooksConfig();
    if (config.isEmpty()) {
        QSKIP("Hook handler binary not found in test environment");
    }

    QJsonDocument doc = QJsonDocument::fromJson(config.toUtf8());
    QJsonObject root = doc.object();
    QJsonObject hooks = root.value(QStringLiteral("hooks")).toObject();

    // Must contain PreToolUse, PostToolUse, Stop (core hooks)
    QVERIFY(hooks.contains(QStringLiteral("PreToolUse")));
    QVERIFY(hooks.contains(QStringLiteral("PostToolUse")));
    QVERIFY(hooks.contains(QStringLiteral("Stop")));

    // Must also contain extended hooks
    QVERIFY(hooks.contains(QStringLiteral("Notification")));
    QVERIFY(hooks.contains(QStringLiteral("PermissionRequest")));
    QVERIFY(hooks.contains(QStringLiteral("SubagentStart")));
    QVERIFY(hooks.contains(QStringLiteral("SubagentStop")));
    QVERIFY(hooks.contains(QStringLiteral("TeammateIdle")));
    QVERIFY(hooks.contains(QStringLiteral("TaskCompleted")));
}

void ClaudeHookHandlerDetailTest::testGenerateHooksConfigReferencesSocketPath()
{
    ClaudeHookHandler handler(QStringLiteral("detail-socket-ref"));

    QString config = handler.generateHooksConfig();
    if (config.isEmpty()) {
        QSKIP("Hook handler binary not found in test environment");
    }

    // The config must reference the handler's socket path
    QString socketPath = handler.socketPath();
    QVERIFY(!socketPath.isEmpty());
    QVERIFY2(config.contains(socketPath), qPrintable(QStringLiteral("Config should contain socket path: ") + socketPath));
}

void ClaudeHookHandlerDetailTest::testGenerateHooksConfigReferencesHandlerBinary()
{
    ClaudeHookHandler handler(QStringLiteral("detail-handler-ref"));

    QString config = handler.generateHooksConfig();
    if (config.isEmpty()) {
        QSKIP("Hook handler binary not found in test environment");
    }

    // The config must reference konsolai-hook-handler
    QVERIFY(config.contains(QStringLiteral("konsolai-hook-handler")));
}

void ClaudeHookHandlerDetailTest::testGenerateHooksConfigHasCorrectStructure()
{
    ClaudeHookHandler handler(QStringLiteral("detail-structure"));

    QString config = handler.generateHooksConfig();
    if (config.isEmpty()) {
        QSKIP("Hook handler binary not found in test environment");
    }

    QJsonDocument doc = QJsonDocument::fromJson(config.toUtf8());
    QJsonObject root = doc.object();

    // Root must have "hooks" key
    QVERIFY(root.contains(QStringLiteral("hooks")));

    QJsonObject hooks = root[QStringLiteral("hooks")].toObject();

    // Each hook type entry should be a JSON array
    for (const QString &key : hooks.keys()) {
        QVERIFY2(hooks[key].isArray(), qPrintable(QStringLiteral("Hook entry '%1' should be an array").arg(key)));

        QJsonArray entries = hooks[key].toArray();
        QVERIFY(entries.size() >= 1);

        // Each entry should have "matcher" and "hooks" keys
        QJsonObject entry = entries[0].toObject();
        QVERIFY2(entry.contains(QStringLiteral("matcher")), qPrintable(QStringLiteral("Entry for '%1' should have 'matcher'").arg(key)));
        QVERIFY2(entry.contains(QStringLiteral("hooks")), qPrintable(QStringLiteral("Entry for '%1' should have 'hooks'").arg(key)));

        // The inner hooks array should have command entries
        QJsonArray hookDefs = entry[QStringLiteral("hooks")].toArray();
        QVERIFY(hookDefs.size() >= 1);

        QJsonObject hookDef = hookDefs[0].toObject();
        QCOMPARE(hookDef[QStringLiteral("type")].toString(), QStringLiteral("command"));
        QVERIFY(!hookDef[QStringLiteral("command")].toString().isEmpty());
    }
}

// ============================================================
// Hook event parsing tests
// ============================================================

static void sendEventToHandler(ClaudeHookHandler &handler, const QString &eventType, const QJsonObject &data = QJsonObject())
{
    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    QJsonObject event;
    event[QStringLiteral("event_type")] = eventType;
    event[QStringLiteral("data")] = data;

    QByteArray message = QJsonDocument(event).toJson(QJsonDocument::Compact);
    message.append('\n');

    client.write(message);
    client.flush();
    client.waitForBytesWritten(1000);

    // Small wait to allow signal processing
    QTest::qWait(50);

    client.disconnectFromServer();
}

void ClaudeHookHandlerDetailTest::testParsePreToolUseEvent()
{
    ClaudeHookHandler handler(QStringLiteral("detail-pretool"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Read");
    data[QStringLiteral("file_path")] = QStringLiteral("/some/file.txt");

    sendEventToHandler(handler, QStringLiteral("PreToolUse"), data);

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("PreToolUse"));

    // Verify data is included
    QString dataStr = eventSpy.at(0).at(1).toString();
    QVERIFY(dataStr.contains(QStringLiteral("Read")));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testParsePostToolUseEvent()
{
    ClaudeHookHandler handler(QStringLiteral("detail-posttool"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    sendEventToHandler(handler, QStringLiteral("PostToolUse"));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("PostToolUse"));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testParseStopEvent()
{
    ClaudeHookHandler handler(QStringLiteral("detail-stop"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    sendEventToHandler(handler, QStringLiteral("Stop"));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("Stop"));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testParseNotificationEvent()
{
    ClaudeHookHandler handler(QStringLiteral("detail-notif"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    sendEventToHandler(handler, QStringLiteral("Notification"));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("Notification"));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testParsePermissionRequestEvent()
{
    ClaudeHookHandler handler(QStringLiteral("detail-perm"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    sendEventToHandler(handler, QStringLiteral("PermissionRequest"));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("PermissionRequest"));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testParseSubagentStartEvent()
{
    ClaudeHookHandler handler(QStringLiteral("detail-substart"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    sendEventToHandler(handler, QStringLiteral("SubagentStart"));

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("SubagentStart"));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testMalformedJsonEmitsError()
{
    ClaudeHookHandler handler(QStringLiteral("detail-malformed"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);
    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    client.write("this is not valid json at all\n");
    client.flush();

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return errorSpy.count() > 0;
        },
        1000));

    // No valid event should fire
    QCOMPARE(eventSpy.count(), 0);
    // Error should mention parse failure
    QVERIFY(errorSpy.count() > 0);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerDetailTest::testNonObjectJsonEmitsError()
{
    ClaudeHookHandler handler(QStringLiteral("detail-nonobj"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);
    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    // Valid JSON but not an object (it's an array)
    client.write("[1, 2, 3]\n");
    client.flush();

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return errorSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.count(), 0);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerDetailTest::testEmptyEventTypeEmitsError()
{
    ClaudeHookHandler handler(QStringLiteral("detail-emptytype"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);
    QSignalSpy errorSpy(&handler, &ClaudeHookHandler::errorOccurred);

    QLocalSocket client;
    client.connectToServer(handler.socketPath());
    QVERIFY(client.waitForConnected(1000));

    // Valid JSON object but missing event_type
    QJsonObject event;
    event[QStringLiteral("data")] = QJsonObject();
    QByteArray msg = QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n";
    client.write(msg);
    client.flush();

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return errorSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.count(), 0);

    client.disconnectFromServer();
    handler.stop();
}

void ClaudeHookHandlerDetailTest::testEventDataPreserved()
{
    ClaudeHookHandler handler(QStringLiteral("detail-datapreserve"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Write");
    data[QStringLiteral("file_path")] = QStringLiteral("/home/user/important.cpp");
    data[QStringLiteral("approved")] = true;

    sendEventToHandler(handler, QStringLiteral("PreToolUse"), data);

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));

    QString dataStr = eventSpy.at(0).at(1).toString();
    // Parse it back to verify data integrity
    QJsonDocument parsedData = QJsonDocument::fromJson(dataStr.toUtf8());
    QVERIFY(parsedData.isObject());
    QJsonObject parsedObj = parsedData.object();
    QCOMPARE(parsedObj[QStringLiteral("tool_name")].toString(), QStringLiteral("Write"));
    QCOMPARE(parsedObj[QStringLiteral("file_path")].toString(), QStringLiteral("/home/user/important.cpp"));
    QCOMPARE(parsedObj[QStringLiteral("approved")].toBool(), true);

    handler.stop();
}

// ============================================================
// Socket path generation tests
// ============================================================

void ClaudeHookHandlerDetailTest::testSocketPathContainsSessionId()
{
    ClaudeHookHandler handler(QStringLiteral("abc12345"));
    QVERIFY(handler.socketPath().contains(QStringLiteral("abc12345")));
}

void ClaudeHookHandlerDetailTest::testSocketPathEndsSock()
{
    ClaudeHookHandler handler(QStringLiteral("endsock1"));
    QVERIFY(handler.socketPath().endsWith(QStringLiteral(".sock")));
}

void ClaudeHookHandlerDetailTest::testSocketPathUnique()
{
    ClaudeHookHandler handler1(QStringLiteral("unique1"));
    ClaudeHookHandler handler2(QStringLiteral("unique2"));

    QVERIFY(handler1.socketPath() != handler2.socketPath());
}

void ClaudeHookHandlerDetailTest::testSocketPathInSessionsDir()
{
    ClaudeHookHandler handler(QStringLiteral("sessdir1"));

    // Socket should be in the sessions subdirectory
    QVERIFY(handler.socketPath().contains(QStringLiteral("/sessions/")));
}

// ============================================================
// generateRemoteHookScript() tests
// ============================================================

void ClaudeHookHandlerDetailTest::testRemoteHookScriptContainsTunnelPort()
{
    ClaudeHookHandler handler(QStringLiteral("remotescript-port"));

    QString script = handler.generateRemoteHookScript(54321);

    QVERIFY(!script.isEmpty());
    QVERIFY(script.contains(QStringLiteral("54321")));
}

void ClaudeHookHandlerDetailTest::testRemoteHookScriptIsValidShell()
{
    ClaudeHookHandler handler(QStringLiteral("remotescript-shell"));

    QString script = handler.generateRemoteHookScript(12345);

    QVERIFY(script.startsWith(QStringLiteral("#!/bin/bash")));
}

void ClaudeHookHandlerDetailTest::testRemoteHookScriptAlwaysExitsZero()
{
    ClaudeHookHandler handler(QStringLiteral("remotescript-exit0"));

    QString script = handler.generateRemoteHookScript(12345);

    // Critical: script must always exit 0 to not break Claude CLI
    QVERIFY(script.contains(QStringLiteral("exit 0")));
}

void ClaudeHookHandlerDetailTest::testRemoteHookScriptContainsEventHandling()
{
    ClaudeHookHandler handler(QStringLiteral("remotescript-event"));

    QString script = handler.generateRemoteHookScript(12345);

    // Should parse --event argument
    QVERIFY(script.contains(QStringLiteral("--event")));
    QVERIFY(script.contains(QStringLiteral("EVENT_TYPE")));
    QVERIFY(script.contains(QStringLiteral("event_type")));
}

// ============================================================
// ClaudeHookClient round-trip tests
// ============================================================

void ClaudeHookHandlerDetailTest::testClientServerRoundTripPreToolUse()
{
    ClaudeHookHandler handler(QStringLiteral("roundtrip-pretool"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    ClaudeHookClient client;
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");

    bool sent = client.sendEvent(handler.socketPath(), QStringLiteral("PreToolUse"), data);
    QVERIFY(sent);

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));
    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("PreToolUse"));

    handler.stop();
}

void ClaudeHookHandlerDetailTest::testClientServerRoundTripWithData()
{
    ClaudeHookHandler handler(QStringLiteral("roundtrip-data"));
    QVERIFY(handler.start());

    QSignalSpy eventSpy(&handler, &ClaudeHookHandler::hookEventReceived);

    ClaudeHookClient client;
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Edit");
    data[QStringLiteral("file_path")] = QStringLiteral("/src/main.cpp");
    data[QStringLiteral("line_count")] = 42;

    bool sent = client.sendEvent(handler.socketPath(), QStringLiteral("PostToolUse"), data);
    QVERIFY(sent);

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return eventSpy.count() > 0;
        },
        1000));

    QCOMPARE(eventSpy.at(0).at(0).toString(), QStringLiteral("PostToolUse"));

    QString dataStr = eventSpy.at(0).at(1).toString();
    QJsonDocument parsedData = QJsonDocument::fromJson(dataStr.toUtf8());
    QJsonObject parsedObj = parsedData.object();
    QCOMPARE(parsedObj[QStringLiteral("tool_name")].toString(), QStringLiteral("Edit"));
    QCOMPARE(parsedObj[QStringLiteral("line_count")].toInt(), 42);

    handler.stop();
}

QTEST_GUILESS_MAIN(ClaudeHookHandlerDetailTest)

#include "moc_ClaudeHookHandlerDetailTest.cpp"
