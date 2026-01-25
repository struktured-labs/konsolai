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

QTEST_GUILESS_MAIN(ClaudeSessionStateTest)

#include "moc_ClaudeSessionStateTest.cpp"
