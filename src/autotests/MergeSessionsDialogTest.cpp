/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MergeSessionsDialogTest.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>

#include "../claude/MergePolicy.h"
#include "../claude/MergeSessionsDialog.h"
#include "../claude/SessionManagerPanel.h"

using namespace Konsolai;

namespace
{

SessionMetadata makeMeta(const QString &id, const QDateTime &lastAccessed, const QString &desc = QString())
{
    SessionMetadata m;
    m.sessionId = id;
    m.sessionName = QStringLiteral("konsolai-x-") + id;
    m.profileName = QStringLiteral("Test");
    m.workingDirectory = QStringLiteral("/proj");
    m.lastAccessed = lastAccessed;
    m.description = desc;
    return m;
}

QList<SessionMetadata> sampleCandidates()
{
    QList<SessionMetadata> v;
    v.append(makeMeta(QStringLiteral("oldest"), QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00"), Qt::ISODate), QStringLiteral("old")));
    v.append(makeMeta(QStringLiteral("middle"), QDateTime::fromString(QStringLiteral("2026-03-01T00:00:00"), Qt::ISODate), QStringLiteral("middle")));
    v.append(makeMeta(QStringLiteral("newest"), QDateTime::fromString(QStringLiteral("2026-06-01T00:00:00"), Qt::ISODate), QStringLiteral("recent")));
    return v;
}

} // namespace

void MergeSessionsDialogTest::testPrimaryDefaultsToMostRecent()
{
    MergeSessionsDialog dlg(sampleCandidates(), {}, nullptr);
    QCOMPARE(dlg.primarySessionId(), QStringLiteral("newest"));
}

void MergeSessionsDialogTest::testSummaryUpdatesOnPrimaryChange()
{
    MergeSessionsDialog dlg(sampleCandidates(), {}, nullptr);
    auto *summary = dlg.findChild<QPlainTextEdit *>(QStringLiteral("mergeDialogSummaryView"));
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("mergeDialogPrimaryCombo"));
    QVERIFY(summary);
    QVERIFY(combo);

    const QString before = summary->toPlainText();
    QVERIFY(before.contains(QStringLiteral("recent")));

    // Switch primary to oldest by data
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toString() == QStringLiteral("oldest")) {
            combo->setCurrentIndex(i);
            break;
        }
    }
    const QString after = summary->toPlainText();
    QVERIFY(after != before);
    QVERIFY(after.contains(QStringLiteral("old")));
}

void MergeSessionsDialogTest::testSummaryUpdatesOnCheckboxToggle()
{
    MergeSessionsDialog dlg(sampleCandidates(), {}, nullptr);
    auto *summary = dlg.findChild<QPlainTextEdit *>(QStringLiteral("mergeDialogSummaryView"));
    auto *budgetCheck = dlg.findChild<QCheckBox *>(QStringLiteral("mergeDialogInheritBudget"));
    QVERIFY(summary);
    QVERIFY(budgetCheck);

    const QString before = summary->toPlainText();
    QVERIFY(before.contains(QStringLiteral("primary's stays")));

    budgetCheck->setChecked(true);
    const QString after = summary->toPlainText();
    QVERIFY(after != before);
    QVERIFY(after.contains(QStringLiteral("time=")));
}

void MergeSessionsDialogTest::testReturnsChoicesAfterAccept()
{
    MergeSessionsDialog dlg(sampleCandidates(), {}, nullptr);
    auto *budgetCheck = dlg.findChild<QCheckBox *>(QStringLiteral("mergeDialogInheritBudget"));
    auto *yoloCheck = dlg.findChild<QCheckBox *>(QStringLiteral("mergeDialogInheritYolo"));
    QVERIFY(budgetCheck);
    QVERIFY(yoloCheck);

    budgetCheck->setChecked(true);
    yoloCheck->setChecked(false);

    const MergeFieldChoices ch = dlg.choices();
    QVERIFY(ch.inheritBudget);
    QVERIFY(!ch.inheritYolo);
    // Description defaults to true and we didn't touch it
    QVERIFY(ch.inheritDescription);
}

QTEST_MAIN(MergeSessionsDialogTest)
#include "moc_MergeSessionsDialogTest.cpp"
