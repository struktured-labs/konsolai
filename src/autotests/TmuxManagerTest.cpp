/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "TmuxManagerTest.h"

// Qt
#include <QRegularExpression>
#include <QSignalSpy>
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

// ============================================================
// Session name sanitization
// ============================================================

void TmuxManagerTest::testBuildSessionNameSanitizesChars()
{
    // Dots and colons should be replaced with hyphens
    QString name = TmuxManager::buildSessionName(QStringLiteral("my.profile"), QStringLiteral("a1b2c3d4"));
    QVERIFY(!name.contains(QLatin1Char('.')));
    QCOMPARE(name, QStringLiteral("konsolai-my-profile-a1b2c3d4"));

    QString name2 = TmuxManager::buildSessionName(QStringLiteral("host:8080"), QStringLiteral("deadbeef"));
    QVERIFY(!name2.contains(QLatin1Char(':')));
    QCOMPARE(name2, QStringLiteral("konsolai-host-8080-deadbeef"));
}

// ============================================================
// Session ID uniqueness
// ============================================================

void TmuxManagerTest::testSessionIdUniqueness()
{
    QSet<QString> ids;
    const int count = 100;

    for (int i = 0; i < count; ++i) {
        ids.insert(TmuxManager::generateSessionId());
    }

    // All should be unique (probabilistically — 2^32 space, 100 samples)
    QCOMPARE(ids.size(), count);
}

// ============================================================
// Command building edge cases
// ============================================================

void TmuxManagerTest::testBuildNewSessionCommandNoAttach()
{
    TmuxManager manager;

    QString cmd = manager.buildNewSessionCommand(QStringLiteral("konsolai-test-12345678"),
                                                 QStringLiteral("claude"),
                                                 false // attachExisting = false
    );

    QVERIFY(cmd.contains(QStringLiteral("tmux")));
    QVERIFY(cmd.contains(QStringLiteral("new-session")));
    // Should NOT contain -A flag
    QVERIFY(!cmd.contains(QStringLiteral(" -A ")));
    QVERIFY(!cmd.contains(QStringLiteral(" -A\n")));
}

void TmuxManagerTest::testBuildNewSessionCommandPassthrough()
{
    TmuxManager manager;

    QString cmd = manager.buildNewSessionCommand(QStringLiteral("konsolai-test-12345678"), QStringLiteral("claude"));

    // Should suppress DCS passthrough
    QVERIFY(cmd.contains(QStringLiteral("allow-passthrough")));
    QVERIFY(cmd.contains(QStringLiteral("off")));
}

// ============================================================
// Execution tests (require tmux)
// ============================================================

void TmuxManagerTest::testSessionExistsNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    QVERIFY(!manager.sessionExists(QStringLiteral("konsolai-nonexistent-99999999")));
}

void TmuxManagerTest::testListSessionsWhenAvailable()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    // Should not crash even if no sessions exist
    QList<TmuxManager::SessionInfo> sessions = manager.listSessions();
    Q_UNUSED(sessions)

    // Also test listKonsolaiSessions
    QList<TmuxManager::SessionInfo> konsolaiSessions = manager.listKonsolaiSessions();
    // All returned sessions should match the konsolai- prefix
    for (const auto &s : konsolaiSessions) {
        QVERIFY(s.name.startsWith(QStringLiteral("konsolai-")));
    }
}

void TmuxManagerTest::testKillNonexistentSession()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    QSignalSpy errorSpy(&manager, &TmuxManager::errorOccurred);

    bool result = manager.killSession(QStringLiteral("konsolai-nonexistent-99999999"));
    QVERIFY(!result);
}

void TmuxManagerTest::testCapturePaneNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    QString output = manager.capturePane(QStringLiteral("konsolai-nonexistent-99999999"));
    QVERIFY(output.isEmpty());
}

void TmuxManagerTest::testSendKeysNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    bool result = manager.sendKeys(QStringLiteral("konsolai-nonexistent-99999999"), QStringLiteral("hello\n"));
    QVERIFY(!result);
}

void TmuxManagerTest::testSendKeySequenceNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    bool result = manager.sendKeySequence(QStringLiteral("konsolai-nonexistent-99999999"), QStringLiteral("Enter"));
    QVERIFY(!result);
}

void TmuxManagerTest::testGetPaneWorkingDirNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    QString dir = manager.getPaneWorkingDirectory(QStringLiteral("konsolai-nonexistent-99999999"));
    QVERIFY(dir.isEmpty());
}

void TmuxManagerTest::testGetPanePidNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    qint64 pid = manager.getPanePid(QStringLiteral("konsolai-nonexistent-99999999"));
    QCOMPARE(pid, qint64(0));
}

// ============================================================
// Async execution tests (require tmux)
// ============================================================

void TmuxManagerTest::testSessionExistsAsyncNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    bool callbackCalled = false;
    bool exists = true;

    manager.sessionExistsAsync(QStringLiteral("konsolai-nonexistent-99999999"), [&](bool result) {
        callbackCalled = true;
        exists = result;
    });

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return callbackCalled;
        },
        5000));
    QVERIFY(!exists);
}

void TmuxManagerTest::testCapturePaneAsyncNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    bool callbackCalled = false;
    bool ok = true;

    manager.capturePaneAsync(QStringLiteral("konsolai-nonexistent-99999999"), -10, 10, [&](bool success, const QString &output) {
        callbackCalled = true;
        ok = success;
        Q_UNUSED(output)
    });

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return callbackCalled;
        },
        5000));
    QVERIFY(!ok);
}

void TmuxManagerTest::testListKonsolaiSessionsAsync()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    bool callbackCalled = false;
    QList<TmuxManager::SessionInfo> result;

    manager.listKonsolaiSessionsAsync([&](const QList<TmuxManager::SessionInfo> &sessions) {
        callbackCalled = true;
        result = sessions;
    });

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return callbackCalled;
        },
        5000));
    // All results should match konsolai- prefix
    for (const auto &s : result) {
        QVERIFY(s.name.startsWith(QStringLiteral("konsolai-")));
    }
}

void TmuxManagerTest::testGetPanePidAsyncNonexistent()
{
    if (!TmuxManager::isAvailable()) {
        QSKIP("tmux not available");
    }

    TmuxManager manager;
    bool callbackCalled = false;
    qint64 pid = -1;

    manager.getPanePidAsync(QStringLiteral("konsolai-nonexistent-99999999"), [&](qint64 result) {
        callbackCalled = true;
        pid = result;
    });

    QVERIFY(QTest::qWaitFor(
        [&]() {
            return callbackCalled;
        },
        5000));
    QCOMPARE(pid, qint64(0));
}

QTEST_GUILESS_MAIN(TmuxManagerTest)

#include "moc_TmuxManagerTest.cpp"
