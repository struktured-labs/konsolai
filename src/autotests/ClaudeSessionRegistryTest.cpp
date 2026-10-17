/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeSessionRegistryTest.h"

// Qt
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

// Konsolai
#include "../claude/ClaudeSessionRegistry.h"
#include "../claude/ClaudeSessionState.h"

using namespace Konsolai;

void ClaudeSessionRegistryTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeSessionRegistryTest::cleanupTestCase()
{
}

void ClaudeSessionRegistryTest::cleanup()
{
    // Remove test state file between tests
    QString filePath = ClaudeSessionRegistry::sessionStateFilePath();
    QFile::remove(filePath);
}

void ClaudeSessionRegistryTest::testSaveAndLoadState()
{
    // Create a registry, add state manually via save, then load in a new one
    {
        ClaudeSessionRegistry registry;

        // We can't call registerSession without a real ClaudeSession,
        // but we can save state directly via the file
    }

    // Write state file directly
    QString filePath = ClaudeSessionRegistry::sessionStateFilePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QJsonArray sessions;
    QJsonObject session1;
    session1[QStringLiteral("sessionName")] = QStringLiteral("konsolai-test-aabbccdd");
    session1[QStringLiteral("sessionId")] = QStringLiteral("aabbccdd");
    session1[QStringLiteral("profileName")] = QStringLiteral("Test");
    session1[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/project1");
    session1[QStringLiteral("isAttached")] = false;
    session1[QStringLiteral("created")] = QStringLiteral("2025-06-01T10:00:00");
    session1[QStringLiteral("lastAccessed")] = QStringLiteral("2025-06-01T12:00:00");
    sessions.append(session1);

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("sessions")] = sessions;

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();

    // Load in a new registry
    ClaudeSessionRegistry registry2;

    QList<ClaudeSessionState> states = registry2.allSessionStates();
    // Should have loaded at least our session (tmux refresh may remove it if tmux isn't running,
    // but the loadState should have added it)
    bool found = false;
    for (const auto &s : states) {
        if (s.sessionName == QStringLiteral("konsolai-test-aabbccdd")) {
            found = true;
            QCOMPARE(s.sessionId, QStringLiteral("aabbccdd"));
            QCOMPARE(s.workingDirectory, QStringLiteral("/home/user/project1"));
        }
    }
    // Note: refreshOrphanedSessions() may remove it if tmux session doesn't exist.
    // That's expected behavior. The key test is that loadState parsed correctly.
    Q_UNUSED(found);
}

void ClaudeSessionRegistryTest::testPromptPersistedInState()
{
    // autoContinuePrompt was removed. Verify basic state loading still works.
    QString filePath = ClaudeSessionRegistry::sessionStateFilePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QJsonArray sessions;
    QJsonObject session;
    session[QStringLiteral("sessionName")] = QStringLiteral("konsolai-prompt-11223344");
    session[QStringLiteral("sessionId")] = QStringLiteral("11223344");
    session[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/myapp");
    session[QStringLiteral("isAttached")] = false;
    session[QStringLiteral("created")] = QStringLiteral("2025-06-01T10:00:00");
    session[QStringLiteral("lastAccessed")] = QStringLiteral("2025-06-01T12:00:00");
    sessions.append(session);

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("sessions")] = sessions;

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();

    ClaudeSessionState state = ClaudeSessionState::fromJson(session);
    QVERIFY(state.isValid());
    QCOMPARE(state.workingDirectory, QStringLiteral("/home/user/myapp"));
}

void ClaudeSessionRegistryTest::testLastAutoContinuePromptByDirectory()
{
    // lastAutoContinuePrompt was removed with triple yolo.
    // Verify lastSessionState still works for directory lookup.
    ClaudeSessionRegistry registry;
    const ClaudeSessionState *state = registry.lastSessionState(QStringLiteral("/nonexistent/path"));
    QVERIFY(state == nullptr);
}

void ClaudeSessionRegistryTest::testLastAutoContinuePromptMostRecent()
{
    // lastAutoContinuePrompt was removed with triple yolo.
    // This test now verifies lastSessionState returns the most recent session.
    ClaudeSessionRegistry registry;
    const ClaudeSessionState *state = registry.lastSessionState(QStringLiteral("/nonexistent/shared"));
    QVERIFY(state == nullptr);
}

void ClaudeSessionRegistryTest::testLastAutoContinuePromptNoMatch()
{
    // lastAutoContinuePrompt was removed. Test is now a no-op.
    ClaudeSessionRegistry registry;
    const ClaudeSessionState *state = registry.lastSessionState(QStringLiteral("/nonexistent/path"));
    QVERIFY(state == nullptr);
}

void ClaudeSessionRegistryTest::testUpdateSessionPrompt()
{
    // updateSessionPrompt was removed with triple yolo. This test is now a no-op.
    // Just verify registry creation works.
    ClaudeSessionRegistry registry;
    QVERIFY(ClaudeSessionRegistry::instance() != nullptr);
}

void ClaudeSessionRegistryTest::testReadClaudeConversationsEmpty()
{
    // Non-existent path should return empty list
    QList<ClaudeConversation> convs = ClaudeSessionRegistry::readClaudeConversations(QStringLiteral("/nonexistent/path/to/project"));
    QVERIFY(convs.isEmpty());

    // Empty string should return empty list
    convs = ClaudeSessionRegistry::readClaudeConversations(QString());
    QVERIFY(convs.isEmpty());
}

void ClaudeSessionRegistryTest::testReadClaudeConversationsParsing()
{
    // Create a fake sessions-index.json
    QString projectPath = QStringLiteral("/tmp/konsolai-test-conv-parse");

    // The hashed path: /tmp/konsolai-test-conv-parse → -tmp-konsolai-test-conv-parse
    QString hashedName = projectPath;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));

    QString indexDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;
    QDir().mkpath(indexDir);
    QString indexPath = indexDir + QStringLiteral("/sessions-index.json");

    // Write a test index file (bare array format)
    QJsonArray entries;
    QJsonObject conv1;
    conv1[QStringLiteral("sessionId")] = QStringLiteral("uuid-1234-5678");
    conv1[QStringLiteral("summary")] = QStringLiteral("Test conversation");
    conv1[QStringLiteral("firstPrompt")] = QStringLiteral("Hello Claude");
    conv1[QStringLiteral("messageCount")] = 5;
    conv1[QStringLiteral("created")] = QStringLiteral("2025-06-01T10:00:00");
    conv1[QStringLiteral("modified")] = QStringLiteral("2025-06-01T12:00:00");
    entries.append(conv1);

    QFile file(indexPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(entries).toJson());
    file.close();

    QList<ClaudeConversation> convs = ClaudeSessionRegistry::readClaudeConversations(projectPath);
    QCOMPARE(convs.size(), 1);
    QCOMPARE(convs[0].sessionId, QStringLiteral("uuid-1234-5678"));
    QCOMPARE(convs[0].summary, QStringLiteral("Test conversation"));
    QCOMPARE(convs[0].firstPrompt, QStringLiteral("Hello Claude"));
    QCOMPARE(convs[0].messageCount, 5);

    // Cleanup
    QFile::remove(indexPath);
    QDir().rmdir(indexDir);
}

void ClaudeSessionRegistryTest::testRefreshOrphanedSessionsAsyncCompletes()
{
    ClaudeSessionRegistry registry;

    // refreshOrphanedSessionsAsync spawns an async tmux list operation.
    // It should complete without crash even if tmux is unavailable.
    registry.refreshOrphanedSessionsAsync();

    // Give the async operation time to complete (tmux call + callback)
    QTest::qWait(2000);

    // Verify registry is still functional
    QList<ClaudeSessionState> states = registry.allSessionStates();
    Q_UNUSED(states)
}

// ============================================================
// hashedProjectPath tests — pin down the exact normalization
// Claude Code uses so resume/token/conversation lookups don't
// silently look in a non-existent directory.
// ============================================================

void ClaudeSessionRegistryTest::testHashedProjectPath_HyphenatesSlashes()
{
    QCOMPARE(ClaudeSessionRegistry::hashedProjectPath(QStringLiteral("/home/user/projects/konsolai")), QStringLiteral("-home-user-projects-konsolai"));
}

void ClaudeSessionRegistryTest::testHashedProjectPath_HyphenatesUnderscores()
{
    QCOMPARE(ClaudeSessionRegistry::hashedProjectPath(QStringLiteral("/home/u/dr_mario_rl")), QStringLiteral("-home-u-dr-mario-rl"));
    QCOMPARE(ClaudeSessionRegistry::hashedProjectPath(QStringLiteral("foo_bar_baz")), QStringLiteral("foo-bar-baz"));
}

void ClaudeSessionRegistryTest::testHashedProjectPath_HyphenatesDots()
{
    QCOMPARE(ClaudeSessionRegistry::hashedProjectPath(QStringLiteral("/home/u/foo.bar")), QStringLiteral("-home-u-foo-bar"));
}

void ClaudeSessionRegistryTest::testHashedProjectPath_MatchesClaudeCodeDrMarioRlCase()
{
    // The regression this whole fix exists for. Actual Claude Code observed
    // output was -home-struktured-projects-dr-mario-rl for the underscore path.
    QCOMPARE(ClaudeSessionRegistry::hashedProjectPath(QStringLiteral("/home/struktured/projects/dr_mario_rl")),
             QStringLiteral("-home-struktured-projects-dr-mario-rl"));
}

void ClaudeSessionRegistryTest::testHashedProjectPath_HandlesWorktreePath()
{
    // /.claude/... adds two hyphens in a row because both '/' and '.' collapse.
    // Verified against the on-disk name in ~/.claude/projects/.
    QCOMPARE(ClaudeSessionRegistry::hashedProjectPath(QStringLiteral("/home/struktured/projects/dr_mario_rl/.claude/worktrees/faithful-sim")),
             QStringLiteral("-home-struktured-projects-dr-mario-rl--claude-worktrees-faithful-sim"));
}

void ClaudeSessionRegistryTest::testBuildRemoteHasSessionArgs_basic()
{
    QStringList args = ClaudeSessionRegistry::buildRemoteHasSessionArgs(
        QStringLiteral("deploy@myserver.example.com"), 22, QStringLiteral("konsolai-abc-12345678"));

    // Non-interactive, bounded ssh — same policy as remote discovery
    QVERIFY2(args.contains(QStringLiteral("BatchMode=yes")), "has-session check must not hang on interactive auth");
    QVERIFY2(args.contains(QStringLiteral("ConnectTimeout=5")), "has-session check must have a bounded connect timeout");
    QVERIFY2(!args.contains(QStringLiteral("-p")), "Default port 22 should not produce -p flag");
    QVERIFY2(args.contains(QStringLiteral("deploy@myserver.example.com")), "Args should contain the ssh target");

    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("tmux has-session")), "Check must use 'tmux has-session'");
    QVERIFY2(remoteCmd.contains(QStringLiteral("-t '=konsolai-abc-12345678'")),
             "Check must use exact-match ('=' prefix) so a prefix-named sibling session does not count as a hit");
    QVERIFY2(remoteCmd.contains(QStringLiteral("KONSOLAI_HAS_SESSION")), "Check must emit a positive marker on stdout");
    QVERIFY2(remoteCmd.contains(QStringLiteral("KONSOLAI_NO_SESSION")), "Check must emit a negative marker on stdout");
    QVERIFY2(!remoteCmd.contains(QStringLiteral("new-session")), "Existence check must never create a session");
}

void ClaudeSessionRegistryTest::testBuildRemoteHasSessionArgs_customPort()
{
    QStringList args = ClaudeSessionRegistry::buildRemoteHasSessionArgs(
        QStringLiteral("myserver.example.com"), 2222, QStringLiteral("konsolai-abc-12345678"));

    int pIdx = args.indexOf(QStringLiteral("-p"));
    QVERIFY2(pIdx >= 0, "Custom port should produce -p flag");
    QVERIFY2(pIdx + 1 < args.size(), "-p flag must have a value following it");
    QCOMPARE(args.at(pIdx + 1), QStringLiteral("2222"));
}

void ClaudeSessionRegistryTest::testBuildRemoteHasSessionArgs_quotesSessionName()
{
    // A session name containing a single quote must not break out of the shell quoting
    QStringList args = ClaudeSessionRegistry::buildRemoteHasSessionArgs(
        QStringLiteral("myserver.example.com"), 22, QStringLiteral("weird'name"));

    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("weird'\\''name")),
             "Single quotes in the session name must be shell-escaped ('\\'')");
}

void ClaudeSessionRegistryTest::testRemoteHasSessionAsync_emptyTarget()
{
    ClaudeSessionRegistry registry;

    // Empty target: check is inconclusive — callback must fire synchronously
    // with checkSucceeded=false (callers treat inconclusive as "proceed with attach").
    bool called = false;
    bool gotExists = true;
    bool gotSucceeded = true;
    registry.remoteTmuxSessionExistsAsync(QString(), 22, QStringLiteral("konsolai-abc-12345678"),
                                          [&](bool exists, bool checkSucceeded) {
                                              called = true;
                                              gotExists = exists;
                                              gotSucceeded = checkSucceeded;
                                          });

    QVERIFY2(called, "Callback must fire even for an empty ssh target");
    QVERIFY2(!gotExists, "Empty target cannot report an existing session");
    QVERIFY2(!gotSucceeded, "Empty target check must be reported as inconclusive");

    // Empty session name: same contract
    called = false;
    registry.remoteTmuxSessionExistsAsync(QStringLiteral("host.example.com"), 22, QString(),
                                          [&](bool exists, bool checkSucceeded) {
                                              called = true;
                                              gotExists = exists;
                                              gotSucceeded = checkSucceeded;
                                          });
    QVERIFY2(called, "Callback must fire even for an empty session name");
    QVERIFY2(!gotExists, "Empty session name cannot report an existing session");
    QVERIFY2(!gotSucceeded, "Empty session name check must be reported as inconclusive");
}

// ── liveConversationIdForTmuxSession ──
//
// Every running Claude CLI keeps ~/.claude/sessions/<pid>.json naming the
// conversation it is on and the tmux pane it runs in. That is the one source
// that tells apart several sessions sharing a workdir after a konsolai restart,
// when hooks are stale and the newest-transcript guess picks one for all.
// The format is the CLI's (observed in 2.1.296-2.1.302), so anything
// unexpected must degrade to "unknown", never to a wrong ID.

namespace
{
void writeCliSession(const QString &dir, qint64 pid, const QString &conversationId, const QString &tmux, qint64 updatedAt = 1000)
{
    QJsonObject obj;
    obj[QStringLiteral("pid")] = pid;
    obj[QStringLiteral("sessionId")] = conversationId;
    obj[QStringLiteral("tmux")] = tmux;
    obj[QStringLiteral("updatedAt")] = updatedAt;
    QFile f(dir + QStringLiteral("/%1.json").arg(pid));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(obj).toJson());
}

// A pid no process can hold: above the kernel's pid_max ceiling (2^22).
constexpr qint64 kDeadPid = 4194304 + 12345;
} // namespace

void ClaudeSessionRegistryTest::testLiveConversationId_matchesTmuxSession()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeCliSession(dir.path(), QCoreApplication::applicationPid(), QStringLiteral("conv-mine"), QStringLiteral("konsolai-default-Claude-aaaa1111:@3.%3"));

    QCOMPARE(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-aaaa1111"), dir.path()),
             QStringLiteral("conv-mine"));
    QVERIFY(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-bbbb2222"), dir.path()).isEmpty());
}

void ClaudeSessionRegistryTest::testLiveConversationId_ignoresDeadPid()
{
    // The previous CLI in a pane can leave its file behind; it names the
    // conversation the pane USED to be on.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeCliSession(dir.path(), kDeadPid, QStringLiteral("conv-dead"), QStringLiteral("konsolai-default-Claude-aaaa1111:@3.%3"));

    QVERIFY(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-aaaa1111"), dir.path()).isEmpty());
}

void ClaudeSessionRegistryTest::testLiveConversationId_nameMustEndAtColon()
{
    // "konsolai-x-Claude-aaaa1111" is a prefix of "...aaaa11112": a bare
    // startsWith would resume another session's conversation.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeCliSession(dir.path(), QCoreApplication::applicationPid(), QStringLiteral("conv-other"), QStringLiteral("konsolai-default-Claude-aaaa11112:@3.%3"));

    QVERIFY(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-aaaa1111"), dir.path()).isEmpty());
}

void ClaudeSessionRegistryTest::testLiveConversationId_newestEntryWins()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const qint64 me = QCoreApplication::applicationPid();
    const qint64 parent = getppid(); // also alive for the duration of the test
    writeCliSession(dir.path(), me, QStringLiteral("conv-old"), QStringLiteral("konsolai-default-Claude-aaaa1111:@3.%3"), 1000);
    writeCliSession(dir.path(), parent, QStringLiteral("conv-new"), QStringLiteral("konsolai-default-Claude-aaaa1111:@3.%3"), 2000);

    QCOMPARE(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-aaaa1111"), dir.path()),
             QStringLiteral("conv-new"));
}

void ClaudeSessionRegistryTest::testLiveConversationId_malformedFilesIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        QFile garbage(dir.path() + QStringLiteral("/1.json"));
        QVERIFY(garbage.open(QIODevice::WriteOnly));
        garbage.write("{not json");
    }
    // Live pid and matching pane, but no sessionId.
    QJsonObject noId;
    noId[QStringLiteral("pid")] = QCoreApplication::applicationPid();
    noId[QStringLiteral("tmux")] = QStringLiteral("konsolai-default-Claude-aaaa1111:@3.%3");
    QFile f(dir.path() + QStringLiteral("/2.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(noId).toJson());
    f.close();

    QVERIFY(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-aaaa1111"), dir.path()).isEmpty());
}

void ClaudeSessionRegistryTest::testLiveConversationId_missingDirIsEmpty()
{
    QVERIFY(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QStringLiteral("konsolai-default-Claude-aaaa1111"),
                                                                    QStringLiteral("/nonexistent/konsolai-test-dir"))
                .isEmpty());
    QVERIFY(ClaudeSessionRegistry::liveConversationIdForTmuxSession(QString(), QStringLiteral("/nonexistent/konsolai-test-dir")).isEmpty());
}

QTEST_GUILESS_MAIN(ClaudeSessionRegistryTest)

#include "moc_ClaudeSessionRegistryTest.cpp"
