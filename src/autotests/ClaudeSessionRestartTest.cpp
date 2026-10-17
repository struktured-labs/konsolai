/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionRestartTest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "../claude/ClaudeProcess.h"
#include "../claude/ClaudeSession.h"
#include "../claude/CodexProcess.h"

using namespace Konsolai;

// restart() respawns the pane with restartCommand(). It used to build that
// command itself — always a claude command, always the default model — so a
// bulk restart turned Codex sessions into Claude ones and Sonnet sessions into
// Opus ones. These pin it to the same construction the launch path uses.
//
// HOME points at an empty temp dir for the whole run, so the CLI registry
// and newest-JSONL fallbacks find nothing unless a test plants it.

void ClaudeSessionRestartTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_home.isValid());
    m_realHome = qgetenv("HOME");
    qputenv("HOME", m_home.path().toLocal8Bit());
    QCOMPARE(QDir::homePath(), m_home.path());
}

void ClaudeSessionRestartTest::cleanupTestCase()
{
    qputenv("HOME", m_realHome);
}

void ClaudeSessionRestartTest::testRestartCommandCodexStaysCodex()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    ClaudeSession session(QStringLiteral("test"), workdir.path());
    session.setAgentKind(ClaudeSession::AgentKind::Codex);
    session.setResumeSessionId(QStringLiteral("019fa0a5-00a5-7cf0-a5ea-8a083d4e9ca3"));

    const QString cmd = session.restartCommand();
    const QString expected = CodexProcess::launchBinary() + QStringLiteral(" resume 019fa0a5-00a5-7cf0-a5ea-8a083d4e9ca3");
    QVERIFY2(cmd.contains(expected), qPrintable(cmd));
    QVERIFY2(!cmd.startsWith(QStringLiteral("claude ")), qPrintable(cmd));
    QVERIFY2(!cmd.contains(QStringLiteral("--resume")), qPrintable(cmd));
}

void ClaudeSessionRestartTest::testRestartCommandHonoursModelOverride()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    ClaudeSession session(QStringLiteral("test"), workdir.path());
    session.setModelOverride(QStringLiteral("claude-sonnet-4-6[1m]"));

    const QString cmd = session.restartCommand();
    QVERIFY2(cmd.contains(QStringLiteral("--model ") + ClaudeProcess::modelName(ClaudeProcess::Model::Sonnet)), qPrintable(cmd));
}

void ClaudeSessionRestartTest::testRestartCommandResumesHookObservedConversation()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    ClaudeSession session(QStringLiteral("test"), workdir.path());
    auto *process = session.findChild<ClaudeProcess *>();
    QVERIFY(process);

    process->handleHookEvent(QStringLiteral("Stop"), QStringLiteral(R"({"session_id": "7d1c2f0a-1111-4222-8333-944455556666"})"));

    QCOMPARE(session.resumeSessionId(), QStringLiteral("7d1c2f0a-1111-4222-8333-944455556666"));
    const QString cmd = session.restartCommand();
    QVERIFY2(cmd.contains(QStringLiteral("--resume 7d1c2f0a-1111-4222-8333-944455556666")), qPrintable(cmd));
}

void ClaudeSessionRestartTest::testRestartCommandFollowsClearToNewConversation()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    ClaudeSession session(QStringLiteral("test"), workdir.path());
    // Launched resuming A; the user then ran /clear and is now on B.
    session.setResumeSessionId(QStringLiteral("aaaaaaaa-1111-4222-8333-944455556666"));
    auto *process = session.findChild<ClaudeProcess *>();
    QVERIFY(process);

    process->handleHookEvent(QStringLiteral("Stop"), QStringLiteral(R"({"session_id": "bbbbbbbb-1111-4222-8333-944455556666"})"));

    const QString cmd = session.restartCommand();
    QVERIFY2(cmd.contains(QStringLiteral("--resume bbbbbbbb-1111-4222-8333-944455556666")), qPrintable(cmd));
    QVERIFY2(!cmd.contains(QStringLiteral("aaaaaaaa-1111-4222-8333-944455556666")), qPrintable(cmd));
}

void ClaudeSessionRestartTest::testRestartCommandIsNotWrappedInTmux()
{
    // respawn-pane runs this INSIDE the existing pane. Handing it the launch
    // wrapper (tmux new-session -A ...) would nest a tmux client in the pane.
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    ClaudeSession session(QStringLiteral("test"), workdir.path());

    const QString cmd = session.restartCommand();
    QVERIFY2(cmd.startsWith(QStringLiteral("claude ")), qPrintable(cmd));
    QVERIFY2(!cmd.contains(QStringLiteral("tmux")), qPrintable(cmd));
}

void ClaudeSessionRestartTest::testRestartCommandPrefersLiveCliRegistry()
{
    // After a konsolai restart a reattached session has no hook-observed ID
    // (the CLI snapshotted the old hook socket), but the running CLI still
    // records its conversation in ~/.claude/sessions/<pid>.json, keyed to its
    // tmux pane. That record is the CLI's own truth and outranks our copy.
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    ClaudeSession session(QStringLiteral("test"), workdir.path());
    session.setResumeSessionId(QStringLiteral("aaaaaaaa-1111-4222-8333-944455556666")); // stale

    const QString sessionsDir = m_home.path() + QStringLiteral("/.claude/sessions");
    QVERIFY(QDir().mkpath(sessionsDir));
    const qint64 pid = QCoreApplication::applicationPid(); // a pid that is certainly alive
    QJsonObject entry;
    entry[QStringLiteral("pid")] = pid;
    entry[QStringLiteral("sessionId")] = QStringLiteral("cccccccc-1111-4222-8333-944455556666");
    entry[QStringLiteral("tmux")] = QString(session.sessionName() + QStringLiteral(":@4.%4"));
    entry[QStringLiteral("updatedAt")] = 1000;
    QFile f(sessionsDir + QStringLiteral("/%1.json").arg(pid));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(entry).toJson());
    f.close();

    const QString cmd = session.restartCommand();
    QFile::remove(f.fileName());
    QVERIFY2(cmd.contains(QStringLiteral("--resume cccccccc-1111-4222-8333-944455556666")), qPrintable(cmd));
}

QTEST_GUILESS_MAIN(ClaudeSessionRestartTest)

#include "moc_ClaudeSessionRestartTest.cpp"
