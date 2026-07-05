/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeSessionStateTest.h"

// Qt
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeSessionState.h"

using namespace Konsolai;

void ClaudeSessionStateTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeSessionStateTest::cleanupTestCase()
{
}

void ClaudeSessionStateTest::testDefaultStateInvalid()
{
    ClaudeSessionState state;

    QVERIFY(!state.isValid());
    QVERIFY(state.sessionName.isEmpty());
    QVERIFY(state.sessionId.isEmpty());
}

void ClaudeSessionStateTest::testValidState()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-test-12345678");
    state.sessionId = QStringLiteral("12345678");

    QVERIFY(state.isValid());
}

void ClaudeSessionStateTest::testInvalidWithEmptySessionName()
{
    ClaudeSessionState state;
    state.sessionId = QStringLiteral("12345678");

    QVERIFY(!state.isValid());
}

void ClaudeSessionStateTest::testInvalidWithEmptySessionId()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-test-12345678");

    QVERIFY(!state.isValid());
}

void ClaudeSessionStateTest::testToJson()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-default-a1b2c3d4");
    state.sessionId = QStringLiteral("a1b2c3d4");
    state.profileName = QStringLiteral("default");
    state.workingDirectory = QStringLiteral("/home/user/project");
    state.claudeModel = QStringLiteral("claude-sonnet-4");
    state.isAttached = true;
    state.created = QDateTime::fromString(QStringLiteral("2025-01-15T10:00:00"), Qt::ISODate);
    state.lastAccessed = QDateTime::fromString(QStringLiteral("2025-01-15T12:00:00"), Qt::ISODate);

    QJsonObject json = state.toJson();

    QCOMPARE(json[QStringLiteral("sessionName")].toString(), state.sessionName);
    QCOMPARE(json[QStringLiteral("sessionId")].toString(), state.sessionId);
    QCOMPARE(json[QStringLiteral("profileName")].toString(), state.profileName);
    QCOMPARE(json[QStringLiteral("workingDirectory")].toString(), state.workingDirectory);
    QCOMPARE(json[QStringLiteral("claudeModel")].toString(), state.claudeModel);
    QCOMPARE(json[QStringLiteral("isAttached")].toBool(), state.isAttached);
}

void ClaudeSessionStateTest::testFromJson()
{
    QJsonObject json;
    json[QStringLiteral("sessionName")] = QStringLiteral("konsolai-test-deadbeef");
    json[QStringLiteral("sessionId")] = QStringLiteral("deadbeef");
    json[QStringLiteral("profileName")] = QStringLiteral("MyProfile");
    json[QStringLiteral("workingDirectory")] = QStringLiteral("/tmp/test");
    json[QStringLiteral("claudeModel")] = QStringLiteral("claude-haiku");
    json[QStringLiteral("isAttached")] = false;
    json[QStringLiteral("created")] = QStringLiteral("2025-01-10T08:00:00");
    json[QStringLiteral("lastAccessed")] = QStringLiteral("2025-01-10T09:30:00");

    ClaudeSessionState state = ClaudeSessionState::fromJson(json);

    QVERIFY(state.isValid());
    QCOMPARE(state.sessionName, QStringLiteral("konsolai-test-deadbeef"));
    QCOMPARE(state.sessionId, QStringLiteral("deadbeef"));
    QCOMPARE(state.profileName, QStringLiteral("MyProfile"));
    QCOMPARE(state.workingDirectory, QStringLiteral("/tmp/test"));
    QCOMPARE(state.claudeModel, QStringLiteral("claude-haiku"));
    QCOMPARE(state.isAttached, false);
}

void ClaudeSessionStateTest::testJsonRoundTrip()
{
    ClaudeSessionState original;
    original.sessionName = QStringLiteral("konsolai-roundtrip-abcd1234");
    original.sessionId = QStringLiteral("abcd1234");
    original.profileName = QStringLiteral("RoundTrip");
    original.workingDirectory = QStringLiteral("/var/data");
    original.claudeModel = QStringLiteral("claude-opus-4-5");
    original.isAttached = true;
    original.created = QDateTime::currentDateTime();
    original.lastAccessed = QDateTime::currentDateTime();

    // Serialize
    QJsonObject json = original.toJson();

    // Deserialize
    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);

    // Verify
    QVERIFY(restored.isValid());
    QCOMPARE(restored.sessionName, original.sessionName);
    QCOMPARE(restored.sessionId, original.sessionId);
    QCOMPARE(restored.profileName, original.profileName);
    QCOMPARE(restored.workingDirectory, original.workingDirectory);
    QCOMPARE(restored.claudeModel, original.claudeModel);
    QCOMPARE(restored.isAttached, original.isAttached);
}

void ClaudeSessionStateTest::testFromJsonMissingFields()
{
    // Empty object
    QJsonObject emptyJson;
    ClaudeSessionState emptyState = ClaudeSessionState::fromJson(emptyJson);
    QVERIFY(!emptyState.isValid());

    // Partial object (missing sessionId)
    QJsonObject partialJson;
    partialJson[QStringLiteral("sessionName")] = QStringLiteral("test");
    ClaudeSessionState partialState = ClaudeSessionState::fromJson(partialJson);
    QVERIFY(!partialState.isValid());
}

void ClaudeSessionStateTest::testFromJsonInvalidData()
{
    // Wrong types
    QJsonObject json;
    json[QStringLiteral("sessionName")] = 12345;  // Should be string
    json[QStringLiteral("sessionId")] = true;      // Should be string

    ClaudeSessionState state = ClaudeSessionState::fromJson(json);
    // Should handle gracefully (either invalid or empty strings)
    QVERIFY(!state.isValid() || state.sessionName.isEmpty());
}

void ClaudeSessionStateTest::testEquality()
{
    ClaudeSessionState state1;
    state1.sessionName = QStringLiteral("konsolai-test-12345678");
    state1.sessionId = QStringLiteral("12345678");

    ClaudeSessionState state2;
    state2.sessionName = QStringLiteral("konsolai-test-12345678");
    state2.sessionId = QStringLiteral("87654321");  // Different ID

    // Equality is based on sessionName only
    QVERIFY(state1 == state2);
}

void ClaudeSessionStateTest::testInequalityByName()
{
    ClaudeSessionState state1;
    state1.sessionName = QStringLiteral("konsolai-test-12345678");
    state1.sessionId = QStringLiteral("12345678");

    ClaudeSessionState state2;
    state2.sessionName = QStringLiteral("konsolai-other-12345678");
    state2.sessionId = QStringLiteral("12345678");

    QVERIFY(!(state1 == state2));
}

void ClaudeSessionStateTest::testTimestamps()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("test");
    state.sessionId = QStringLiteral("12345678");

    // Default timestamps should be null/invalid
    QVERIFY(!state.created.isValid() || state.created.isNull());

    // Set timestamps
    QDateTime now = QDateTime::currentDateTime();
    state.created = now;
    state.lastAccessed = now.addSecs(3600);  // 1 hour later

    QVERIFY(state.created.isValid());
    QVERIFY(state.lastAccessed.isValid());
    QVERIFY(state.lastAccessed > state.created);
}

void ClaudeSessionStateTest::testAttachmentStatus()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("test");
    state.sessionId = QStringLiteral("12345678");

    // Default should be false
    QCOMPARE(state.isAttached, false);

    state.isAttached = true;
    QCOMPARE(state.isAttached, true);

    // Verify it persists through serialization
    QJsonObject json = state.toJson();
    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);
    QCOMPARE(restored.isAttached, true);
}

void ClaudeSessionStateTest::testAutoContinuePromptSerialization()
{
    // autoContinuePrompt has been removed from ClaudeSessionState.
    // This test now verifies the field is no longer serialized.
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-test-prompt01");
    state.sessionId = QStringLiteral("prompt01");

    QJsonObject json = state.toJson();
    QVERIFY(!json.contains(QStringLiteral("autoContinuePrompt")));
}

void ClaudeSessionStateTest::testAutoContinuePromptRoundTrip()
{
    // autoContinuePrompt has been removed. Verify basic round trip still works.
    ClaudeSessionState original;
    original.sessionName = QStringLiteral("konsolai-test-prompt02");
    original.sessionId = QStringLiteral("prompt02");
    original.workingDirectory = QStringLiteral("/home/user/myproject");

    QJsonObject json = original.toJson();
    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);

    QCOMPARE(restored.workingDirectory, original.workingDirectory);
}

void ClaudeSessionStateTest::testAutoContinuePromptEmptyNotInJson()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-test-prompt03");
    state.sessionId = QStringLiteral("prompt03");

    QJsonObject json = state.toJson();
    QVERIFY(!json.contains(QStringLiteral("autoContinuePrompt")));
}

void ClaudeSessionStateTest::testAutoContinuePromptMissingFromJson()
{
    QJsonObject json;
    json[QStringLiteral("sessionName")] = QStringLiteral("konsolai-test-prompt04");
    json[QStringLiteral("sessionId")] = QStringLiteral("prompt04");

    ClaudeSessionState state = ClaudeSessionState::fromJson(json);
    QVERIFY(state.isValid());
}

void ClaudeSessionStateTest::testYoloModeSerialization()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-yolo-aabbccdd");
    state.sessionId = QStringLiteral("aabbccdd");
    state.yoloMode = true;
    state.doubleYoloMode = true;

    QJsonObject json = state.toJson();

    QCOMPARE(json[QStringLiteral("yoloMode")].toBool(), true);
    QCOMPARE(json[QStringLiteral("doubleYoloMode")].toBool(), true);
}

void ClaudeSessionStateTest::testYoloModeRoundTrip()
{
    ClaudeSessionState original;
    original.sessionName = QStringLiteral("konsolai-yolo-eeff0011");
    original.sessionId = QStringLiteral("eeff0011");
    original.yoloMode = true;
    original.doubleYoloMode = false;

    QJsonObject json = original.toJson();
    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);

    QCOMPARE(restored.yoloMode, true);
    QCOMPARE(restored.doubleYoloMode, false);
}

void ClaudeSessionStateTest::testYoloModeDefaultsFalse()
{
    // Missing yolo keys in JSON should default to false
    QJsonObject json;
    json[QStringLiteral("sessionName")] = QStringLiteral("konsolai-noyolo-22334455");
    json[QStringLiteral("sessionId")] = QStringLiteral("22334455");

    ClaudeSessionState state = ClaudeSessionState::fromJson(json);

    QCOMPARE(state.yoloMode, false);
    QCOMPARE(state.doubleYoloMode, false);
}

void ClaudeSessionStateTest::testTaskDescriptionRoundTrip()
{
    ClaudeSessionState original;
    original.sessionName = QStringLiteral("konsolai-test-task0001");
    original.sessionId = QStringLiteral("task0001");
    original.workingDirectory = QStringLiteral("/home/user/myproject");
    original.taskDescription = QStringLiteral("cc3dfs claude wrapper");

    QJsonObject json = original.toJson();
    QVERIFY(json.contains(QStringLiteral("taskDescription")));
    QCOMPARE(json[QStringLiteral("taskDescription")].toString(), QStringLiteral("cc3dfs claude wrapper"));

    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);
    QCOMPARE(restored.taskDescription, original.taskDescription);
}

void ClaudeSessionStateTest::testTaskDescriptionEmptyNotInJson()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-test-task0002");
    state.sessionId = QStringLiteral("task0002");
    // taskDescription left empty

    QJsonObject json = state.toJson();

    // Empty task description should NOT be serialized (saves space)
    QVERIFY(!json.contains(QStringLiteral("taskDescription")));
}

void ClaudeSessionStateTest::testTaskDescriptionMissingFromJson()
{
    QJsonObject json;
    json[QStringLiteral("sessionName")] = QStringLiteral("konsolai-test-task0003");
    json[QStringLiteral("sessionId")] = QStringLiteral("task0003");
    // No taskDescription key

    ClaudeSessionState state = ClaudeSessionState::fromJson(json);

    QVERIFY(state.isValid());
    QVERIFY(state.taskDescription.isEmpty());
}

// ============================================================
// Remote/SSH persistence — the actual bug behind "resume made
// a new remote session": ClaudeSessionState dropped the ssh
// fields on save, so a remote session loaded as "local" and
// there was nothing to reattach to.
// ============================================================

void ClaudeSessionStateTest::testRemoteSshFields_defaultLocal()
{
    ClaudeSessionState state;

    // Fresh state must NOT accidentally look remote — a stale local
    // session must never be treated as an SSH target.
    QCOMPARE(state.isRemote, false);
    QVERIFY(state.sshHost.isEmpty());
    QVERIFY(state.sshUsername.isEmpty());
    QCOMPARE(state.sshPort, 22);
}

void ClaudeSessionStateTest::testRemoteSshFields_roundTrip()
{
    ClaudeSessionState original;
    original.sessionName = QStringLiteral("konsolai-default-Claude-1d957d8f");
    original.sessionId = QStringLiteral("1d957d8f");
    original.workingDirectory = QStringLiteral("/home/struktured/projects/cowir-main");
    original.isRemote = true;
    original.sshHost = QStringLiteral("blackmage.tailecb0b5.ts.net");
    original.sshUsername = QStringLiteral("struktured");
    original.sshPort = 2222;

    QJsonObject json = original.toJson();
    QVERIFY2(json.contains(QStringLiteral("isRemote")), "toJson must persist isRemote");
    QVERIFY2(json.contains(QStringLiteral("sshHost")), "toJson must persist sshHost");
    QVERIFY2(json.contains(QStringLiteral("sshUsername")), "toJson must persist sshUsername");
    QVERIFY2(json.contains(QStringLiteral("sshPort")), "toJson must persist sshPort");

    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);
    QCOMPARE(restored.isRemote, true);
    QCOMPARE(restored.sshHost, original.sshHost);
    QCOMPARE(restored.sshUsername, original.sshUsername);
    QCOMPARE(restored.sshPort, 2222);
}

void ClaudeSessionStateTest::testRemoteSshFields_localSessionOmitsSshKeys()
{
    // A local session should NOT bloat every state record with empty ssh keys.
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-local-abcd1234");
    state.sessionId = QStringLiteral("abcd1234");
    // isRemote left false, ssh fields left empty

    QJsonObject json = state.toJson();

    QVERIFY2(!json.contains(QStringLiteral("sshHost")),
             "Local session must not serialize an empty sshHost");
    QVERIFY2(!json.contains(QStringLiteral("sshUsername")),
             "Local session must not serialize an empty sshUsername");
    // isRemote may or may not be persisted when false — either is fine, but must round-trip.
}

void ClaudeSessionStateTest::testRemoteSshFields_missingFromJsonDefaultLocal()
{
    // Old state files (pre-fix) have no ssh keys. They must load as LOCAL,
    // never as a remote session with garbage/empty ssh info.
    QJsonObject json;
    json[QStringLiteral("sessionName")] = QStringLiteral("konsolai-legacy-00112233");
    json[QStringLiteral("sessionId")] = QStringLiteral("00112233");
    json[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/oldproject");

    ClaudeSessionState state = ClaudeSessionState::fromJson(json);

    QVERIFY(state.isValid());
    QCOMPARE(state.isRemote, false);
    QVERIFY(state.sshHost.isEmpty());
    QVERIFY(state.sshUsername.isEmpty());
    QCOMPARE(state.sshPort, 22);
}

QTEST_GUILESS_MAIN(ClaudeSessionStateTest)

#include "moc_ClaudeSessionStateTest.cpp"
