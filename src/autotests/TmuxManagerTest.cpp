/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "TmuxManagerTest.h"

// Qt
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/TmuxManager.h"

using namespace Konsolai;

void TmuxManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TmuxManagerTest::cleanupTestCase()
{
}

void TmuxManagerTest::testGenerateSessionId()
{
    // Session ID should be 8 hex characters
    QString id1 = TmuxManager::generateSessionId();
    QCOMPARE(id1.length(), 8);

    // Should only contain hex characters
    QRegularExpression hexRegex(QStringLiteral("^[0-9a-f]{8}$"));
    QVERIFY(hexRegex.match(id1).hasMatch());

    // Generate another and verify uniqueness
    QString id2 = TmuxManager::generateSessionId();
    QCOMPARE(id2.length(), 8);
    QVERIFY(hexRegex.match(id2).hasMatch());

    // IDs should be different (probabilistically)
    QVERIFY(id1 != id2);
}

void TmuxManagerTest::testBuildSessionName()
{
    QString sessionId = QStringLiteral("a1b2c3d4");

    // Default profile name
    QString name = TmuxManager::buildSessionName(QStringLiteral("default"), sessionId);
    QCOMPARE(name, QStringLiteral("konsolai-default-a1b2c3d4"));

    // Custom profile name
    name = TmuxManager::buildSessionName(QStringLiteral("MyProfile"), sessionId);
    QCOMPARE(name, QStringLiteral("konsolai-MyProfile-a1b2c3d4"));

    // Empty profile name should use "default"
    name = TmuxManager::buildSessionName(QString(), sessionId);
    QVERIFY(name.contains(sessionId));
}

void TmuxManagerTest::testBuildSessionNameWithTemplate()
{
    QString sessionId = QStringLiteral("deadbeef");
    QString profileName = QStringLiteral("test");

    // Using default template
    QString name = TmuxManager::buildSessionName(profileName, sessionId);
    QVERIFY(name.contains(QStringLiteral("konsolai")));
    QVERIFY(name.contains(profileName));
    QVERIFY(name.contains(sessionId));
}

void TmuxManagerTest::testBuildSessionNameCustomTemplate()
{
    QString sessionId = QStringLiteral("12345678");
    QString profileName = QStringLiteral("myprofile");

    // Custom template
    QString customTemplate = QStringLiteral("claude-{profile}-session-{id}");
    QString name = TmuxManager::buildSessionName(profileName, sessionId, customTemplate);
    QCOMPARE(name, QStringLiteral("claude-myprofile-session-12345678"));
}

void TmuxManagerTest::testBuildNewSessionCommand()
{
    TmuxManager manager;

    QString cmd = manager.buildNewSessionCommand(
        QStringLiteral("konsolai-test-12345678"),
        QStringLiteral("claude")
    );

    // Should contain tmux new-session
    QVERIFY(cmd.contains(QStringLiteral("tmux")));
    QVERIFY(cmd.contains(QStringLiteral("new-session")));
    QVERIFY(cmd.contains(QStringLiteral("konsolai-test-12345678")));
    QVERIFY(cmd.contains(QStringLiteral("claude")));
}

void TmuxManagerTest::testBuildNewSessionCommandWithWorkingDir()
{
    TmuxManager manager;

    QString cmd = manager.buildNewSessionCommand(
        QStringLiteral("konsolai-test-12345678"),
        QStringLiteral("claude"),
        true,
        QStringLiteral("/home/user/project")
    );

    // Should contain working directory option
    QVERIFY(cmd.contains(QStringLiteral("/home/user/project")));
    QVERIFY(cmd.contains(QStringLiteral("-c")));
}

void TmuxManagerTest::testBuildAttachCommand()
{
    TmuxManager manager;

    QString cmd = manager.buildAttachCommand(QStringLiteral("konsolai-test-12345678"));

    QVERIFY(cmd.contains(QStringLiteral("tmux")));
    QVERIFY(cmd.contains(QStringLiteral("attach")));
    QVERIFY(cmd.contains(QStringLiteral("konsolai-test-12345678")));
}

void TmuxManagerTest::testBuildKillCommand()
{
    TmuxManager manager;

    QString cmd = manager.buildKillCommand(QStringLiteral("konsolai-test-12345678"));

    QVERIFY(cmd.contains(QStringLiteral("tmux")));
    QVERIFY(cmd.contains(QStringLiteral("kill-session")));
    QVERIFY(cmd.contains(QStringLiteral("konsolai-test-12345678")));
}

void TmuxManagerTest::testBuildDetachCommand()
{
    TmuxManager manager;

    QString cmd = manager.buildDetachCommand(QStringLiteral("konsolai-test-12345678"));

    QVERIFY(cmd.contains(QStringLiteral("tmux")));
    QVERIFY(cmd.contains(QStringLiteral("detach")));
    QVERIFY(cmd.contains(QStringLiteral("konsolai-test-12345678")));
}

void TmuxManagerTest::testIsAvailable()
{
    // Just verify the function doesn't crash
    // The actual result depends on system configuration
    bool available = TmuxManager::isAvailable();
    Q_UNUSED(available)
}

void TmuxManagerTest::testVersion()
{
    // If tmux is available, version should return a non-empty string
    if (TmuxManager::isAvailable()) {
        QString version = TmuxManager::version();
        QVERIFY(!version.isEmpty());
    }
}

QTEST_GUILESS_MAIN(TmuxManagerTest)

#include "moc_TmuxManagerTest.cpp"
