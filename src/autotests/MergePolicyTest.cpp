/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MergePolicyTest.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QTest>
#include <QVector>

#include "../claude/ClaudeSession.h"
#include "../claude/MergePolicy.h"
#include "../claude/SessionManagerPanel.h"

using namespace Konsolai;

namespace
{

SessionMetadata makeMeta(const QString &id, const QString &workdir = QStringLiteral("/proj"))
{
    SessionMetadata m;
    m.sessionId = id;
    m.sessionName = QStringLiteral("konsolai-x-") + id;
    m.profileName = QStringLiteral("Test");
    m.workingDirectory = workdir;
    m.createdAt = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00"), Qt::ISODate);
    return m;
}

ApprovalLogEntry makeEntry(const QString &iso, const QString &action)
{
    ApprovalLogEntry e;
    e.timestamp = QDateTime::fromString(iso, Qt::ISODate);
    e.toolName = QStringLiteral("Bash");
    e.action = action;
    e.yoloLevel = 1;
    return e;
}

} // namespace

void MergePolicyTest::testInheritsLongestDescription()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.description = QStringLiteral("short");

    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.description = QStringLiteral("a longer description than primary");

    SessionMetadata b = makeMeta(QStringLiteral("b"));
    b.description = QStringLiteral("mid");

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, {});
    QCOMPARE(merged.description, QStringLiteral("a longer description than primary"));
}

void MergePolicyTest::testInheritsLongestDescriptionEmptyAllowed()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    SessionMetadata b = makeMeta(QStringLiteral("b"));

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, {});
    QVERIFY(merged.description.isEmpty());
}

void MergePolicyTest::testPinnedIsOrOfSources()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.isPinned = false;
    SessionMetadata other = makeMeta(QStringLiteral("o"));
    other.isPinned = true;

    const SessionMetadata merged = applyMerge(primary, {other}, MergeFieldChoices{}, {});
    QVERIFY(merged.isPinned);

    SessionMetadata other2 = makeMeta(QStringLiteral("o2"));
    other2.isPinned = false;
    const SessionMetadata merged2 = applyMerge(primary, {other2}, MergeFieldChoices{}, {});
    QVERIFY(!merged2.isPinned);
}

void MergePolicyTest::testYoloIsOrOfSources()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.yoloMode = true;
    SessionMetadata b = makeMeta(QStringLiteral("b"));
    b.doubleYoloMode = true;

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, {});
    QVERIFY(merged.yoloMode);
    QVERIFY(merged.doubleYoloMode);
}

void MergePolicyTest::testLastAccessedTakesMax()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.lastAccessed = QDateTime::fromString(QStringLiteral("2026-01-01T10:00:00"), Qt::ISODate);
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.lastAccessed = QDateTime::fromString(QStringLiteral("2026-03-15T08:00:00"), Qt::ISODate);
    SessionMetadata b = makeMeta(QStringLiteral("b"));
    b.lastAccessed = QDateTime::fromString(QStringLiteral("2026-02-01T12:00:00"), Qt::ISODate);

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, {});
    QCOMPARE(merged.lastAccessed, a.lastAccessed);
}

void MergePolicyTest::testResumeIdPicksLargestJsonl()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.lastResumeSessionId = QStringLiteral("resume-p");
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.lastResumeSessionId = QStringLiteral("resume-a");
    SessionMetadata b = makeMeta(QStringLiteral("b"));
    b.lastResumeSessionId = QStringLiteral("resume-b");

    QHash<QString, qint64> sizes;
    sizes.insert(QStringLiteral("resume-p"), 100);
    sizes.insert(QStringLiteral("resume-a"), 5000);
    sizes.insert(QStringLiteral("resume-b"), 250);

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, sizes);
    QCOMPARE(merged.lastResumeSessionId, QStringLiteral("resume-a"));
}

void MergePolicyTest::testResumeIdFallsBackToPrimaryOnTie()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.lastResumeSessionId = QStringLiteral("resume-p");
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.lastResumeSessionId = QStringLiteral("resume-a");

    QHash<QString, qint64> sizes;
    sizes.insert(QStringLiteral("resume-p"), 1000);
    sizes.insert(QStringLiteral("resume-a"), 1000);

    const SessionMetadata merged = applyMerge(primary, {a}, MergeFieldChoices{}, sizes);
    QCOMPARE(merged.lastResumeSessionId, QStringLiteral("resume-p"));

    // When neither has a resume id, result stays empty
    SessionMetadata p2 = makeMeta(QStringLiteral("p2"));
    SessionMetadata a2 = makeMeta(QStringLiteral("a2"));
    const SessionMetadata merged2 = applyMerge(p2, {a2}, MergeFieldChoices{}, {});
    QVERIFY(merged2.lastResumeSessionId.isEmpty());
}

void MergePolicyTest::testApprovalLogConcatenatedSorted()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T10:00:00"), QStringLiteral("p1")));
    primary.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T12:00:00"), QStringLiteral("p2")));

    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T11:00:00"), QStringLiteral("a1")));

    const SessionMetadata merged = applyMerge(primary, {a}, MergeFieldChoices{}, {});
    QCOMPARE(merged.approvalLog.size(), 3);
    QCOMPARE(merged.approvalLog[0].action, QStringLiteral("p1"));
    QCOMPARE(merged.approvalLog[1].action, QStringLiteral("a1"));
    QCOMPARE(merged.approvalLog[2].action, QStringLiteral("p2"));
}

void MergePolicyTest::testApprovalLogDedupesByTimestampAction()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T10:00:00"), QStringLiteral("approve")));
    primary.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T11:00:00"), QStringLiteral("approve")));

    SessionMetadata a = makeMeta(QStringLiteral("a"));
    // duplicate (timestamp + action) of primary's first entry
    a.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T10:00:00"), QStringLiteral("approve")));
    // unique (different action at same timestamp)
    a.approvalLog.append(makeEntry(QStringLiteral("2026-03-01T10:00:00"), QStringLiteral("deny")));

    const SessionMetadata merged = applyMerge(primary, {a}, MergeFieldChoices{}, {});
    // Expect 3: p@10 approve, a@10 deny, p@11 approve
    QCOMPARE(merged.approvalLog.size(), 3);
}

void MergePolicyTest::testApprovalCountsSummed()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.yoloApprovalCount = 5;
    primary.doubleYoloApprovalCount = 2;
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.yoloApprovalCount = 7;
    a.doubleYoloApprovalCount = 3;
    SessionMetadata b = makeMeta(QStringLiteral("b"));
    b.yoloApprovalCount = 1;

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, {});
    QCOMPARE(merged.yoloApprovalCount, 13);
    QCOMPARE(merged.doubleYoloApprovalCount, 5);
}

void MergePolicyTest::testPromptRoundTakesMax()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.currentPromptRound = 4;
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.currentPromptRound = 9;
    SessionMetadata b = makeMeta(QStringLiteral("b"));
    b.currentPromptRound = 1;

    const SessionMetadata merged = applyMerge(primary, {a, b}, MergeFieldChoices{}, {});
    QCOMPARE(merged.currentPromptRound, 9);
}

void MergePolicyTest::testBudgetUnchangedByDefault()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.budgetTimeLimitMinutes = 30;
    primary.budgetCostCeilingUSD = 5.0;
    primary.budgetTokenCeiling = 1000;
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.budgetTimeLimitMinutes = 999;
    a.budgetCostCeilingUSD = 1000.0;
    a.budgetTokenCeiling = 9999999;

    MergeFieldChoices choices; // inheritBudget = false
    const SessionMetadata merged = applyMerge(primary, {a}, choices, {});
    QCOMPARE(merged.budgetTimeLimitMinutes, 30);
    QCOMPARE(merged.budgetCostCeilingUSD, 5.0);
    QCOMPARE(merged.budgetTokenCeiling, quint64(1000));
}

void MergePolicyTest::testBudgetMergesWhenOptedIn()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.budgetTimeLimitMinutes = 30;
    primary.budgetCostCeilingUSD = 2.0;
    primary.budgetTokenCeiling = 1000;
    SessionMetadata a = makeMeta(QStringLiteral("a"));
    a.budgetTimeLimitMinutes = 120;
    a.budgetCostCeilingUSD = 1.0;
    a.budgetTokenCeiling = 5000;

    MergeFieldChoices choices;
    choices.inheritBudget = true;
    const SessionMetadata merged = applyMerge(primary, {a}, choices, {});
    QCOMPARE(merged.budgetTimeLimitMinutes, 120);
    QCOMPARE(merged.budgetCostCeilingUSD, 2.0);
    QCOMPARE(merged.budgetTokenCeiling, quint64(5000));
}

void MergePolicyTest::testIdentityFieldsNeverChange()
{
    SessionMetadata primary = makeMeta(QStringLiteral("primary-id"), QStringLiteral("/proj/primary"));
    primary.sessionName = QStringLiteral("konsolai-primary");
    primary.profileName = QStringLiteral("Primary");
    primary.createdAt = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00"), Qt::ISODate);
    primary.isRemote = true;
    primary.sshHost = QStringLiteral("primary-host");
    primary.sshUsername = QStringLiteral("primary-user");
    primary.sshPort = 2222;

    SessionMetadata other = makeMeta(QStringLiteral("other-id"), QStringLiteral("/proj/primary"));
    other.sessionName = QStringLiteral("konsolai-other");
    other.profileName = QStringLiteral("Other");
    other.createdAt = QDateTime::fromString(QStringLiteral("2026-02-01T00:00:00"), Qt::ISODate);
    other.isRemote = false;
    other.sshHost = QStringLiteral("other-host");
    other.sshUsername = QStringLiteral("other-user");
    other.sshPort = 22;
    other.description = QStringLiteral("very long description that would otherwise win");

    MergeFieldChoices choices;
    choices.inheritBudget = true; // turn everything on
    const SessionMetadata merged = applyMerge(primary, {other}, choices, {});

    QCOMPARE(merged.sessionId, QStringLiteral("primary-id"));
    QCOMPARE(merged.sessionName, QStringLiteral("konsolai-primary"));
    QCOMPARE(merged.profileName, QStringLiteral("Primary"));
    QCOMPARE(merged.workingDirectory, QStringLiteral("/proj/primary"));
    QCOMPARE(merged.createdAt, primary.createdAt);
    QCOMPARE(merged.isRemote, true);
    QCOMPARE(merged.sshHost, QStringLiteral("primary-host"));
    QCOMPARE(merged.sshUsername, QStringLiteral("primary-user"));
    QCOMPARE(merged.sshPort, 2222);

    // Confirm a mergeable field DID change so the test isn't a no-op false-positive.
    QCOMPARE(merged.description, other.description);
}

void MergePolicyTest::testEmptyOthersIsNoOp()
{
    SessionMetadata primary = makeMeta(QStringLiteral("p"));
    primary.description = QStringLiteral("kept");
    primary.isPinned = true;
    primary.yoloApprovalCount = 7;

    const SessionMetadata merged = applyMerge(primary, {}, MergeFieldChoices{}, {});
    QCOMPARE(merged.description, QStringLiteral("kept"));
    QVERIFY(merged.isPinned);
    QCOMPARE(merged.yoloApprovalCount, 7);
}

void MergePolicyTest::testJsonlSizesReturnsZeroForMissing()
{
    // Pass a workingDirectory that almost certainly does not host any conv file
    // for these synthetic UUIDs — every entry should report 0.
    const QStringList ids = {QStringLiteral("00000000-aaaa-bbbb-cccc-dddddddddddd"), QStringLiteral("11111111-eeee-ffff-1111-222222222222")};
    const auto sizes = jsonlSizesForResumeIds(QStringLiteral("/this/path/does/not/exist/__xyz"), ids);
    QCOMPARE(sizes.value(ids[0], -1), qint64(0));
    QCOMPARE(sizes.value(ids[1], -1), qint64(0));

    // Empty resume ids are dropped silently.
    const auto sizesEmpty = jsonlSizesForResumeIds(QStringLiteral("/x"), {QString()});
    QCOMPARE(sizesEmpty.size(), 0);
}

QTEST_GUILESS_MAIN(MergePolicyTest)
#include "moc_MergePolicyTest.cpp"
