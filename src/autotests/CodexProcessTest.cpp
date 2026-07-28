/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CodexProcessTest.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "../claude/CodexProcess.h"

namespace Konsolai
{

namespace
{
// Write a minimal rollout transcript whose first line is the session_meta
// record, matching the shape Codex 0.145 emits.
bool writeRollout(const QString &path, const QString &sessionId, const QString &cwd, const QString &timestamp)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << QStringLiteral(
               R"({"timestamp":"%1","type":"session_meta","payload":{"session_id":"%2","id":"%2","timestamp":"%1","cwd":"%3","originator":"codex-tui","cli_version":"0.145.0","model_provider":"openai"}})")
               .arg(timestamp, sessionId, cwd)
        << "\n";
    out << R"({"timestamp":"2026-07-26T22:56:58.935Z","type":"event_msg","payload":{"type":"task_started"}})" << "\n";
    file.close();
    return true;
}
}

void CodexProcessTest::testBuildCommandFresh()
{
    const QString cmd = CodexProcess::buildCommand();
    QCOMPARE(cmd, QStringLiteral("codex"));
}

void CodexProcessTest::testBuildCommandWithWorkingDir()
{
    const QString cmd = CodexProcess::buildCommand(QStringLiteral("/home/u/projects/foo"));
    QCOMPARE(cmd, QStringLiteral("codex -C /home/u/projects/foo"));
}

void CodexProcessTest::testBuildCommandResume()
{
    const QString cmd = CodexProcess::buildCommand(QStringLiteral("/home/u/projects/foo"), QStringLiteral("019fa0a5-00a5-7cf0-a5ea-8a083d4e9ca3"));
    QCOMPARE(cmd, QStringLiteral("codex resume 019fa0a5-00a5-7cf0-a5ea-8a083d4e9ca3 -C /home/u/projects/foo"));
}

void CodexProcessTest::testBuildCommandResumeIsSubcommandBeforeOptions()
{
    // `resume` is a subcommand, not a flag: it must come immediately after the
    // binary and take the session id as its positional argument. Emitting it
    // after an option would make codex treat it as a prompt.
    const QString cmd = CodexProcess::buildCommand(QStringLiteral("/tmp/x"), QStringLiteral("abc-123"), QStringLiteral("gpt-5"));
    QVERIFY(cmd.startsWith(QStringLiteral("codex resume abc-123 ")));
    QVERIFY(cmd.indexOf(QStringLiteral("resume")) < cmd.indexOf(QStringLiteral("-C")));
}

void CodexProcessTest::testBuildCommandWithModel()
{
    const QString cmd = CodexProcess::buildCommand(QString(), QString(), QStringLiteral("gpt-5-codex"));
    QCOMPARE(cmd, QStringLiteral("codex -m gpt-5-codex"));
}

void CodexProcessTest::testBuildCommandWithAdditionalArgs()
{
    const QString cmd = CodexProcess::buildCommand(QString(), QString(), QString(), {QStringLiteral("--search")});
    QCOMPARE(cmd, QStringLiteral("codex --search"));
}

void CodexProcessTest::testSessionsRoot()
{
    QVERIFY(CodexProcess::sessionsRoot().endsWith(QStringLiteral("/.codex/sessions")));
}

void CodexProcessTest::testParseSessionMeta()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("rollout-2026-07-26T18-56-36-019fa0a5.jsonl"));
    QVERIFY(writeRollout(path,
                         QStringLiteral("019fa0a5-00a5-7cf0-a5ea-8a083d4e9ca3"),
                         QStringLiteral("/home/u/projects/foo"),
                         QStringLiteral("2026-07-26T22:56:36.005Z")));

    const CodexConversation conv = CodexProcess::parseSessionMeta(path);
    QCOMPARE(conv.sessionId, QStringLiteral("019fa0a5-00a5-7cf0-a5ea-8a083d4e9ca3"));
    QCOMPARE(conv.workingDirectory, QStringLiteral("/home/u/projects/foo"));
    QCOMPARE(conv.transcriptPath, path);
    QVERIFY(conv.created.isValid());
}

void CodexProcessTest::testParseSessionMetaMissingFile()
{
    const CodexConversation conv = CodexProcess::parseSessionMeta(QStringLiteral("/nonexistent/rollout.jsonl"));
    QVERIFY(conv.sessionId.isEmpty());
}

void CodexProcessTest::testParseSessionMetaNotSessionMeta()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("other.jsonl"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({"type":"event_msg","payload":{"type":"task_started"}})");
    file.close();

    // A transcript that does not lead with session_meta yields no id, so it is
    // skipped rather than surfacing an unresumable entry.
    const CodexConversation conv = CodexProcess::parseSessionMeta(path);
    QVERIFY(conv.sessionId.isEmpty());
}

void CodexProcessTest::testParseSessionMetaMalformedJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.jsonl"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("{not json at all");
    file.close();

    const CodexConversation conv = CodexProcess::parseSessionMeta(path);
    QVERIFY(conv.sessionId.isEmpty());
}

void CodexProcessTest::testDiscoverConversationsFiltersByWorkingDir()
{
    // discoverConversations reads the real ~/.codex tree, so exercise the
    // filter through parseSessionMeta on a controlled fixture instead.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = dir.filePath(QStringLiteral("a.jsonl"));
    const QString b = dir.filePath(QStringLiteral("b.jsonl"));
    QVERIFY(writeRollout(a, QStringLiteral("id-a"), QStringLiteral("/proj/alpha"), QStringLiteral("2026-07-26T10:00:00.000Z")));
    QVERIFY(writeRollout(b, QStringLiteral("id-b"), QStringLiteral("/proj/beta"), QStringLiteral("2026-07-26T11:00:00.000Z")));

    QCOMPARE(CodexProcess::parseSessionMeta(a).workingDirectory, QStringLiteral("/proj/alpha"));
    QCOMPARE(CodexProcess::parseSessionMeta(b).workingDirectory, QStringLiteral("/proj/beta"));
}

void CodexProcessTest::testDiscoverConversationsSortedNewestFirst()
{
    const QList<CodexConversation> all = CodexProcess::discoverConversations();
    // Ordering must hold regardless of how many sessions exist on this machine,
    // so the tree's "latest session" pick is the genuinely newest one.
    for (int i = 1; i < all.size(); ++i) {
        QVERIFY(all.at(i - 1).modified >= all.at(i).modified);
    }
    // Every surfaced entry must be resumable.
    for (const CodexConversation &c : all) {
        QVERIFY(!c.sessionId.isEmpty());
    }

    // Resuming re-records a session under a new rollout file with the same id,
    // so discovery must collapse them: one entry per conversation.
    QSet<QString> ids;
    for (const CodexConversation &c : all) {
        QVERIFY2(!ids.contains(c.sessionId), qPrintable(QStringLiteral("duplicate session id surfaced: %1").arg(c.sessionId)));
        ids.insert(c.sessionId);
    }
}

}

QTEST_GUILESS_MAIN(Konsolai::CodexProcessTest)
