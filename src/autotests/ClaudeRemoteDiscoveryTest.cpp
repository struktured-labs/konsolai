/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeRemoteDiscoveryTest.h"

// Qt
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeSessionRegistry.h"
#include "../claude/ClaudeSessionState.h"
#include "../claude/KonsolaiSettings.h"

using namespace Konsolai;

void ClaudeRemoteDiscoveryTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeRemoteDiscoveryTest::testSshDiscoveryHostsPersistence()
{
    KonsolaiSettings settings;

    // Initially empty
    QVERIFY(settings.sshDiscoveryHosts().isEmpty());

    // Set some hosts
    QStringList hosts = {QStringLiteral("user@host1"), QStringLiteral("admin@host2:2222")};
    settings.setSshDiscoveryHosts(hosts);

    // Read them back
    QStringList result = settings.sshDiscoveryHosts();
    QCOMPARE(result.size(), 2);
    QCOMPARE(result[0], QStringLiteral("user@host1"));
    QCOMPARE(result[1], QStringLiteral("admin@host2:2222"));

    // Clear
    settings.setSshDiscoveryHosts({});
    QVERIFY(settings.sshDiscoveryHosts().isEmpty());
}

void ClaudeRemoteDiscoveryTest::testParseRemoteDiscoveryOutputBasic()
{
    QString output = QStringLiteral("/home/user/projects/myapp/.claude\n");

    auto results = ClaudeSessionRegistry::parseRemoteDiscoveryOutput(
        output, QStringLiteral("blackmage"), QStringLiteral("user"), 22);

    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].workingDirectory, QStringLiteral("/home/user/projects/myapp"));
    QCOMPARE(results[0].sshHost, QStringLiteral("blackmage"));
    QCOMPARE(results[0].sshUsername, QStringLiteral("user"));
    QCOMPARE(results[0].sshPort, 22);
    QVERIFY(results[0].isRemote);
    QVERIFY(results[0].sessionName.contains(QStringLiteral("blackmage")));
}

void ClaudeRemoteDiscoveryTest::testParseRemoteDiscoveryOutputEmpty()
{
    auto results = ClaudeSessionRegistry::parseRemoteDiscoveryOutput(
        QString(), QStringLiteral("host"), QStringLiteral("user"), 22);
    QVERIFY(results.isEmpty());

    results = ClaudeSessionRegistry::parseRemoteDiscoveryOutput(
        QStringLiteral("\n\n"), QStringLiteral("host"), QStringLiteral("user"), 22);
    QVERIFY(results.isEmpty());
}

void ClaudeRemoteDiscoveryTest::testParseRemoteDiscoveryOutputMultiple()
{
    QString output = QStringLiteral(
        "/home/user/projects/app1/.claude\n"
        "/home/user/projects/app2/.claude\n"
        "/home/user/projects/app3/.claude\n");

    auto results = ClaudeSessionRegistry::parseRemoteDiscoveryOutput(
        output, QStringLiteral("server"), QStringLiteral("admin"), 2222);

    QCOMPARE(results.size(), 3);
    QCOMPARE(results[0].workingDirectory, QStringLiteral("/home/user/projects/app1"));
    QCOMPARE(results[1].workingDirectory, QStringLiteral("/home/user/projects/app2"));
    QCOMPARE(results[2].workingDirectory, QStringLiteral("/home/user/projects/app3"));

    for (const auto &state : results) {
        QVERIFY(state.isRemote);
        QCOMPARE(state.sshHost, QStringLiteral("server"));
        QCOMPARE(state.sshUsername, QStringLiteral("admin"));
        QCOMPARE(state.sshPort, 2222);
    }
}

void ClaudeRemoteDiscoveryTest::testParseRemoteDiscoveryOutputExtraWhitespace()
{
    QString output = QStringLiteral("  /home/user/projects/app/.claude  \n  \n");

    auto results = ClaudeSessionRegistry::parseRemoteDiscoveryOutput(
        output, QStringLiteral("host"), QStringLiteral("user"), 22);

    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].workingDirectory, QStringLiteral("/home/user/projects/app"));
}

void ClaudeRemoteDiscoveryTest::testSessionStateRemoteFieldsSerialization()
{
    ClaudeSessionState state;
    state.sessionName = QStringLiteral("konsolai-remote-aabbccdd");
    state.sessionId = QStringLiteral("aabbccdd");
    state.workingDirectory = QStringLiteral("/home/user/project");
    state.isRemote = true;
    state.sshHost = QStringLiteral("blackmage");
    state.sshUsername = QStringLiteral("carm");
    state.sshPort = 2222;

    QJsonObject json = state.toJson();
    QVERIFY(json[QStringLiteral("isRemote")].toBool());
    QCOMPARE(json[QStringLiteral("sshHost")].toString(), QStringLiteral("blackmage"));
    QCOMPARE(json[QStringLiteral("sshUsername")].toString(), QStringLiteral("carm"));
    QCOMPARE(json[QStringLiteral("sshPort")].toInt(), 2222);

    // Round-trip
    ClaudeSessionState restored = ClaudeSessionState::fromJson(json);
    QVERIFY(restored.isRemote);
    QCOMPARE(restored.sshHost, QStringLiteral("blackmage"));
    QCOMPARE(restored.sshUsername, QStringLiteral("carm"));
    QCOMPARE(restored.sshPort, 2222);

    // Non-remote state should not have isRemote in JSON
    ClaudeSessionState localState;
    localState.sessionName = QStringLiteral("konsolai-local-11223344");
    localState.sessionId = QStringLiteral("11223344");
    QJsonObject localJson = localState.toJson();
    QVERIFY(!localJson.contains(QStringLiteral("isRemote")));
}

QTEST_GUILESS_MAIN(ClaudeRemoteDiscoveryTest)

#include "moc_ClaudeRemoteDiscoveryTest.cpp"
