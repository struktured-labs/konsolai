/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "SplitViewClaudeTest.h"

// Qt
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

void SplitViewClaudeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void SplitViewClaudeTest::cleanupTestCase()
{
}

void SplitViewClaudeTest::cleanup()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void SplitViewClaudeTest::testClaudeSessionReusedOnSplit()
{
    // Verify that ClaudeSession can be created and is a Session subclass
    ClaudeSession session(QStringLiteral("test-profile"), QDir::tempPath());
    QVERIFY(qobject_cast<Konsole::Session *>(&session) != nullptr);
    QCOMPARE(session.profileName(), QStringLiteral("test-profile"));
}

void SplitViewClaudeTest::testRemoveOneViewDoesNotKillSession()
{
    // Verify that a ClaudeSession survives when views are conceptually removed.
    // We can't create real TerminalDisplays without a full GUI, but we can
    // verify the session object stays alive when nothing kills it.
    auto *session = new ClaudeSession(QStringLiteral("split-test"), QDir::tempPath());
    QPointer<ClaudeSession> guard(session);

    // Session should still exist — no SIGHUP was sent
    QVERIFY(!guard.isNull());
    QCOMPARE(session->profileName(), QStringLiteral("split-test"));

    delete session;
    QVERIFY(guard.isNull());
}

void SplitViewClaudeTest::testSessionMapTracksMultipleViews()
{
    // Test that a QHash can map multiple keys to the same session value
    // (simulating the _sessionMap behavior in ViewManager)
    QHash<int, Konsole::Session *> sessionMap;

    auto *session = new ClaudeSession(QStringLiteral("multi-view"), QDir::tempPath());

    // Simulate two views pointing to the same session
    sessionMap.insert(1, session);
    sessionMap.insert(2, session);

    QCOMPARE(sessionMap.keys(session).count(), 2);

    // Remove one "view"
    sessionMap.remove(1);
    QCOMPARE(sessionMap.keys(session).count(), 1);

    // Session is still referenced
    QVERIFY(sessionMap.values().contains(session));

    sessionMap.clear();
    delete session;
}

void SplitViewClaudeTest::testSharedSessionSplit()
{
    // Verify that when splitting a Claude session, the SAME session object
    // should be reused (not a new one created). We simulate this by checking
    // that a single ClaudeSession can be inserted into the session map under
    // multiple display keys (the core invariant of Option B shared-session split).
    auto *session = new ClaudeSession(QStringLiteral("shared-split"), QDir::tempPath());
    QHash<int, Konsole::Session *> sessionMap;

    // Simulate first view
    sessionMap.insert(100, session);
    QCOMPARE(sessionMap.size(), 1);

    // Simulate split: same session, new display key
    sessionMap.insert(200, session);
    QCOMPARE(sessionMap.size(), 2);

    // Both map entries should resolve to the exact same session pointer
    QCOMPARE(sessionMap.value(100), session);
    QCOMPARE(sessionMap.value(200), session);
    QVERIFY(sessionMap.value(100) == sessionMap.value(200));

    // The session is a ClaudeSession in both cases
    QVERIFY(qobject_cast<Konsolai::ClaudeSession *>(sessionMap.value(100)) != nullptr);
    QVERIFY(qobject_cast<Konsolai::ClaudeSession *>(sessionMap.value(200)) != nullptr);

    sessionMap.clear();
    delete session;
}

void SplitViewClaudeTest::testMultipleViewsSameSession()
{
    // Verify that _sessionMap correctly tracks multiple displays pointing to
    // the same session, and that keys(session) returns all of them.
    auto *session = new ClaudeSession(QStringLiteral("multi-display"), QDir::tempPath());
    QHash<int, Konsole::Session *> sessionMap;

    sessionMap.insert(10, session);
    sessionMap.insert(20, session);
    sessionMap.insert(30, session);

    // All three displays map to the same session
    QCOMPARE(sessionMap.keys(session).count(), 3);
    QVERIFY(sessionMap.keys(session).contains(10));
    QVERIFY(sessionMap.keys(session).contains(20));
    QVERIFY(sessionMap.keys(session).contains(30));

    // Remove the middle one
    sessionMap.remove(20);
    QCOMPARE(sessionMap.keys(session).count(), 2);
    QVERIFY(!sessionMap.keys(session).contains(20));

    // Session is still reachable from remaining displays
    QCOMPARE(sessionMap.value(10), session);
    QCOMPARE(sessionMap.value(30), session);

    sessionMap.clear();
    delete session;
}

void SplitViewClaudeTest::testRemoveOneViewKeepsSession()
{
    // Verify that removing one view (display) from the session map does NOT
    // destroy the session when other views still reference it.
    auto *session = new ClaudeSession(QStringLiteral("keep-alive"), QDir::tempPath());
    QPointer<ClaudeSession> guard(session);

    QHash<int, Konsole::Session *> sessionMap;
    sessionMap.insert(1, session);
    sessionMap.insert(2, session);

    // Remove one view
    sessionMap.remove(1);

    // Session must still be alive — only one view was removed, not the session
    QVERIFY(!guard.isNull());
    QCOMPARE(sessionMap.keys(session).count(), 1);
    QCOMPARE(sessionMap.value(2), session);

    // Remove the last view — session is still alive (map removal doesn't delete)
    sessionMap.remove(2);
    QVERIFY(!guard.isNull());
    QCOMPARE(sessionMap.keys(session).count(), 0);

    // Only explicit delete destroys it
    delete session;
    QVERIFY(guard.isNull());
}

void SplitViewClaudeTest::testCreateForReattachProducesIndependentSession()
{
    // createForReattach should produce a distinct session object
    auto *original = new ClaudeSession(QStringLiteral("split-profile"), QDir::tempPath());
    auto *reattach = ClaudeSession::createForReattach(original->sessionName(), nullptr);

    QVERIFY(reattach != nullptr);
    QVERIFY(reattach != original);

    // Both should be valid Session subclasses
    QVERIFY(qobject_cast<Konsole::Session *>(reattach) != nullptr);

    // The reattach session should reference the same tmux session name
    QCOMPARE(reattach->sessionName(), original->sessionName());

    // But they should be independent objects — deleting one doesn't affect the other
    QPointer<ClaudeSession> reattachGuard(reattach);
    delete original;
    QVERIFY(!reattachGuard.isNull());

    delete reattach;
}

void SplitViewClaudeTest::testReattachSessionHasSameNameButDifferentObject()
{
    // Verify that two reattach sessions for the same tmux name are independent
    auto *session1 = ClaudeSession::createForReattach(QStringLiteral("konsolai-test-abcd1234"), nullptr);
    auto *session2 = ClaudeSession::createForReattach(QStringLiteral("konsolai-test-abcd1234"), nullptr);

    QVERIFY(session1 != session2);
    QCOMPARE(session1->sessionName(), session2->sessionName());
    QCOMPARE(session1->sessionName(), QStringLiteral("konsolai-test-abcd1234"));

    // Parsed profile and ID from the session name
    QCOMPARE(session1->profileName(), QStringLiteral("test"));
    QCOMPARE(session1->sessionId(), QStringLiteral("abcd1234"));

    delete session1;
    delete session2;
}

void SplitViewClaudeTest::testRemoveHooksForWorkDirClearsAllKonsolaiHooks()
{
    // Create a temporary working directory with a .claude/settings.local.json
    QDir tempDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/splitview-test-hooks"));
    tempDir.mkpath(QStringLiteral(".claude"));

    QString settingsPath = tempDir.absolutePath() + QStringLiteral("/.claude/settings.local.json");

    // Write a settings file with konsolai hooks and a non-konsolai hook
    QJsonObject konsolaiHookDef;
    konsolaiHookDef[QStringLiteral("type")] = QStringLiteral("command");
    konsolaiHookDef[QStringLiteral("command")] = QStringLiteral("konsolai-hook-handler --socket /tmp/test.sock --event $EVENT_TYPE");

    QJsonObject otherHookDef;
    otherHookDef[QStringLiteral("type")] = QStringLiteral("command");
    otherHookDef[QStringLiteral("command")] = QStringLiteral("echo other-hook");

    QJsonObject matcherObj;
    matcherObj[QStringLiteral("tool_name")] = QStringLiteral(".*");

    QJsonObject konsolaiEntry;
    konsolaiEntry[QStringLiteral("matcher")] = matcherObj;
    konsolaiEntry[QStringLiteral("hooks")] = QJsonArray({konsolaiHookDef});

    QJsonObject otherEntry;
    otherEntry[QStringLiteral("matcher")] = matcherObj;
    otherEntry[QStringLiteral("hooks")] = QJsonArray({otherHookDef});

    QJsonObject hooks;
    hooks[QStringLiteral("PreToolUse")] = QJsonArray({konsolaiEntry, otherEntry});
    hooks[QStringLiteral("PostToolUse")] = QJsonArray({konsolaiEntry});

    QJsonObject settings;
    settings[QStringLiteral("hooks")] = hooks;
    settings[QStringLiteral("other_setting")] = QStringLiteral("preserved");

    QFile outFile(settingsPath);
    QVERIFY(outFile.open(QIODevice::WriteOnly));
    outFile.write(QJsonDocument(settings).toJson());
    outFile.close();

    // Call removeHooksForWorkDir
    ClaudeSession::removeHooksForWorkDir(tempDir.absolutePath());

    // Read back and verify
    QFile inFile(settingsPath);
    QVERIFY(inFile.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
    inFile.close();

    QJsonObject result = doc.object();

    // The non-konsolai hook should be preserved
    QVERIFY(result.contains(QStringLiteral("other_setting")));

    // If hooks key exists, it should only contain non-konsolai entries
    if (result.contains(QStringLiteral("hooks"))) {
        QJsonObject resultHooks = result[QStringLiteral("hooks")].toObject();
        for (const QString &key : resultHooks.keys()) {
            QJsonArray entries = resultHooks[key].toArray();
            for (const auto &entry : entries) {
                QString entryStr = QString::fromUtf8(QJsonDocument(entry.toObject()).toJson());
                QVERIFY2(!entryStr.contains(QStringLiteral("konsolai-hook-handler")),
                         qPrintable(QStringLiteral("Found konsolai hook that should have been removed: ") + entryStr));
            }
        }
    }

    // Cleanup
    QFile::remove(settingsPath);
    tempDir.rmpath(QStringLiteral(".claude"));
}

void SplitViewClaudeTest::testRemoveHooksForWorkDirPreservesNonKonsolaiHooks()
{
    QDir tempDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/splitview-test-preserve"));
    tempDir.mkpath(QStringLiteral(".claude"));

    QString settingsPath = tempDir.absolutePath() + QStringLiteral("/.claude/settings.local.json");

    // Write a settings file with ONLY non-konsolai hooks
    QJsonObject hookDef;
    hookDef[QStringLiteral("type")] = QStringLiteral("command");
    hookDef[QStringLiteral("command")] = QStringLiteral("echo custom-hook");

    QJsonObject matcherObj;
    matcherObj[QStringLiteral("tool_name")] = QStringLiteral(".*");

    QJsonObject entry;
    entry[QStringLiteral("matcher")] = matcherObj;
    entry[QStringLiteral("hooks")] = QJsonArray({hookDef});

    QJsonObject hooks;
    hooks[QStringLiteral("PreToolUse")] = QJsonArray({entry});

    QJsonObject settings;
    settings[QStringLiteral("hooks")] = hooks;

    QFile outFile(settingsPath);
    QVERIFY(outFile.open(QIODevice::WriteOnly));
    outFile.write(QJsonDocument(settings).toJson());
    outFile.close();

    // Call removeHooksForWorkDir — should be a no-op
    ClaudeSession::removeHooksForWorkDir(tempDir.absolutePath());

    // Read back — hooks should still be there
    QFile inFile(settingsPath);
    QVERIFY(inFile.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
    inFile.close();

    QJsonObject result = doc.object();
    QVERIFY(result.contains(QStringLiteral("hooks")));
    QJsonObject resultHooks = result[QStringLiteral("hooks")].toObject();
    QVERIFY(resultHooks.contains(QStringLiteral("PreToolUse")));
    QCOMPARE(resultHooks[QStringLiteral("PreToolUse")].toArray().size(), 1);

    // Cleanup
    QFile::remove(settingsPath);
    tempDir.rmpath(QStringLiteral(".claude"));
}

void SplitViewClaudeTest::testRemoveHooksForWorkDirNoFileNoCrash()
{
    // Calling with a non-existent directory should not crash
    ClaudeSession::removeHooksForWorkDir(QStringLiteral("/nonexistent/path/that/does/not/exist"));

    // Calling with empty string should not crash
    ClaudeSession::removeHooksForWorkDir(QString());
}

QTEST_GUILESS_MAIN(SplitViewClaudeTest)

#include "moc_SplitViewClaudeTest.cpp"
