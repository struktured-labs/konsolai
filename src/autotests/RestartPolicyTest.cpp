/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RestartPolicyTest.h"

#include <QTest>

#include "../claude/RestartPolicy.h"

using namespace Konsolai;

namespace
{

RestartCandidate candidate(const QString &id, bool hasTab, bool isRemote = false)
{
    RestartCandidate c;
    c.sessionId = id;
    c.sessionName = QStringLiteral("konsolai-default-Claude-") + id;
    c.hasTab = hasTab;
    c.isRemote = isRemote;
    return c;
}

} // namespace

void RestartPolicyTest::testTabbedSessionsRestartInPlace()
{
    const RestartPlan plan = planRestart({candidate(QStringLiteral("a"), true), candidate(QStringLiteral("b"), true)});
    QCOMPARE(plan.restartInPlace, QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY(plan.reopenThenRestart.isEmpty());
    QVERIFY(plan.skippedRemote.isEmpty());
    QCOMPARE(plan.restartCount(), 2);
}

void RestartPolicyTest::testTablessSessionsAreReopenedFirst()
{
    // The old group action counted these in its "(N)" label, then looked up a
    // ClaudeSession that didn't exist and skipped them without a word.
    const RestartPlan plan = planRestart({candidate(QStringLiteral("a"), true), candidate(QStringLiteral("b"), false)});
    QCOMPARE(plan.restartInPlace, QStringList({QStringLiteral("a")}));
    QCOMPARE(plan.reopenThenRestart, QStringList({QStringLiteral("b")}));
    QCOMPARE(plan.restartCount(), 2);
}

void RestartPolicyTest::testRemoteSessionsAreSkipped()
{
    // restart() respawns through the LOCAL tmux server; a remote session's
    // tmux lives on the SSH host, so the respawn would miss it (or hit a local
    // session that happens to share the name). Skip, and say so.
    const RestartPlan plan =
        planRestart({candidate(QStringLiteral("r1"), true, true), candidate(QStringLiteral("r2"), false, true), candidate(QStringLiteral("a"), true)});
    QCOMPARE(plan.restartInPlace, QStringList({QStringLiteral("a")}));
    QVERIFY(plan.reopenThenRestart.isEmpty());
    QCOMPARE(plan.skippedRemote, QStringList({QStringLiteral("r1"), QStringLiteral("r2")}));
    QCOMPARE(plan.restartCount(), 1);
}

void RestartPolicyTest::testDuplicateIdsCountedOnce()
{
    // A category walk can reach a session twice (e.g. via a "+N older" tuck
    // node). Restarting it twice would respawn the pane mid-startup.
    const RestartPlan plan = planRestart({candidate(QStringLiteral("a"), true), candidate(QStringLiteral("a"), true)});
    QCOMPARE(plan.restartInPlace, QStringList({QStringLiteral("a")}));
    QCOMPARE(plan.restartCount(), 1);
}

void RestartPolicyTest::testEmptyInputIsEmptyPlan()
{
    const RestartPlan plan = planRestart({});
    QCOMPARE(plan.restartCount(), 0);
    QVERIFY(plan.skippedRemote.isEmpty());
}

void RestartPolicyTest::testOrderIsPreserved()
{
    const RestartPlan plan = planRestart({candidate(QStringLiteral("c"), true), candidate(QStringLiteral("a"), true), candidate(QStringLiteral("b"), true)});
    QCOMPARE(plan.restartInPlace, QStringList({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
}

QTEST_GUILESS_MAIN(RestartPolicyTest)

#include "moc_RestartPolicyTest.cpp"
