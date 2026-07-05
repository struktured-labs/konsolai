/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ReorganizeTreeDialogTest.h"

#include "../claude/ClaudeAssistant.h"
#include "../claude/ClaudeAssistantPromptBuilder.h"
#include "../claude/ReorganizeTreeDialog.h"

#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTextEdit>
#include <QTimer>

using namespace Konsolai;

namespace
{

/**
 * A minimal ClaudeAssistant stub. On ask(), schedules an emit of a canned
 * response (or a canned failure) on the event loop.
 */
class StubAssistant : public ClaudeAssistant
{
    Q_OBJECT
public:
    explicit StubAssistant(QObject *parent = nullptr)
        : ClaudeAssistant(parent)
    {
    }

    QString cannedOutput;
    QString cannedFailure;
    int cannedExitCode = 0;
    int askCallCount = 0;
    QString lastPrompt;

    void ask(const QString &prompt, bool jsonOutput = false) override
    {
        Q_UNUSED(jsonOutput);
        ++askCallCount;
        lastPrompt = prompt;
        QTimer::singleShot(0, this, [this]() {
            if (!cannedFailure.isEmpty()) {
                Q_EMIT failed(cannedFailure);
            } else {
                Q_EMIT finished(cannedOutput, cannedExitCode);
            }
        });
    }
};

TreeInventory sampleInventory(int projectCount = 5)
{
    TreeInventory inv;
    for (int i = 0; i < projectCount; ++i) {
        TreeInventory::Project p;
        p.workingDirectory = QStringLiteral("/home/u/projects/proj-%1").arg(i);
        p.basename = QStringLiteral("proj-%1").arg(i);
        p.sessionCount = 1;
        inv.projects << p;
    }
    return inv;
}

} // namespace

void ReorganizeTreeDialogTest::testShowsAllProjectsInInventory()
{
    ReorganizeTreeDialog dlg(sampleInventory(5));
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("reorganizeProjectsList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 5);
    for (int i = 0; i < list->count(); ++i) {
        QCOMPARE(list->item(i)->checkState(), Qt::Checked);
    }
}

void ReorganizeTreeDialogTest::testUncheckingProjectExcludesFromPrompt()
{
    ReorganizeTreeDialog dlg(sampleInventory(3));
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("reorganizeProjectsList"));
    QVERIFY(list);

    // Uncheck the middle one.
    list->item(1)->setCheckState(Qt::Unchecked);

    const QStringList included = dlg.intentIncludedWorkdirs();
    QCOMPARE(included.size(), 2);
    QVERIFY(included.contains(QStringLiteral("/home/u/projects/proj-0")));
    QVERIFY(!included.contains(QStringLiteral("/home/u/projects/proj-1")));
    QVERIFY(included.contains(QStringLiteral("/home/u/projects/proj-2")));

    // Drive an ask to verify the excluded workdir is NOT in the prompt.
    auto *stub = new StubAssistant();
    stub->cannedOutput = QStringLiteral("{\"rationale\":\"ok\"}");
    dlg.setAssistantForTesting(stub);
    auto *intentEdit = dlg.findChild<QPlainTextEdit *>(QStringLiteral("reorganizeIntentEdit"));
    QVERIFY(intentEdit);
    intentEdit->setPlainText(QStringLiteral("please help"));
    dlg.askClaude();

    QCOMPARE(stub->askCallCount, 1);
    QVERIFY(stub->lastPrompt.contains(QStringLiteral("/home/u/projects/proj-0")));
    QVERIFY(!stub->lastPrompt.contains(QStringLiteral("/home/u/projects/proj-1")));
    QVERIFY(stub->lastPrompt.contains(QStringLiteral("/home/u/projects/proj-2")));
    stub->deleteLater();
}

void ReorganizeTreeDialogTest::testAskButtonDisabledWhenIntentEmpty()
{
    ReorganizeTreeDialog dlg(sampleInventory(2));
    auto *askBtn = dlg.findChild<QPushButton *>(QStringLiteral("reorganizeAskButton"));
    QVERIFY(askBtn);
    QVERIFY(!askBtn->isEnabled());
}

void ReorganizeTreeDialogTest::testAskButtonRunsWithNonEmptyIntent()
{
    ReorganizeTreeDialog dlg(sampleInventory(2));
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    auto *intentEdit = dlg.findChild<QPlainTextEdit *>(QStringLiteral("reorganizeIntentEdit"));
    auto *askBtn = dlg.findChild<QPushButton *>(QStringLiteral("reorganizeAskButton"));
    QVERIFY(intentEdit);
    QVERIFY(askBtn);

    intentEdit->setPlainText(QStringLiteral("please help"));
    QVERIFY(askBtn->isEnabled());

    auto *stub = new StubAssistant();
    stub->cannedOutput = QStringLiteral("{\"categoryAliases\":{\"a\":\"b\"},\"rationale\":\"ok\"}");
    dlg.setAssistantForTesting(stub);
    dlg.askClaude();

    // Spin the event loop for the queued finished() to arrive.
    QTRY_COMPARE(stub->askCallCount, 1);
    auto *proposalView = dlg.findChild<QTextEdit *>(QStringLiteral("reorganizeProposalView"));
    QVERIFY(proposalView);
    QTRY_VERIFY(proposalView->isVisible());
    QVERIFY(proposalView->toPlainText().contains(QStringLiteral("a")));
    QVERIFY(proposalView->toPlainText().contains(QStringLiteral("b")));
    stub->deleteLater();
}

void ReorganizeTreeDialogTest::testApplyButtonStoresProposal()
{
    ReorganizeTreeDialog dlg(sampleInventory(2));
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));

    ReorganizeProposal canned;
    canned.categoryAliases.insert(QStringLiteral("foo"), QStringLiteral("bar"));
    canned.suppressedCategories << QStringLiteral("legacy");
    canned.userCategories << QStringLiteral("mystuff");
    canned.rationale = QStringLiteral("test rationale");
    dlg.applyStubProposal(canned);

    auto *applyBtn = dlg.findChild<QPushButton *>(QStringLiteral("reorganizeApplyButton"));
    QVERIFY(applyBtn);
    QVERIFY(applyBtn->isVisible());
    QVERIFY(applyBtn->isEnabled());

    QSignalSpy acceptedSpy(&dlg, &QDialog::accepted);
    applyBtn->click();

    QCOMPARE(acceptedSpy.count(), 1);
    const ReorganizeProposal got = dlg.proposal();
    QCOMPARE(got.categoryAliases.value(QStringLiteral("foo")), QStringLiteral("bar"));
    QVERIFY(got.suppressedCategories.contains(QStringLiteral("legacy")));
    QVERIFY(got.userCategories.contains(QStringLiteral("mystuff")));
    QCOMPARE(got.rationale, QStringLiteral("test rationale"));
}

void ReorganizeTreeDialogTest::testEmptyProposalDisablesApply()
{
    ReorganizeTreeDialog dlg(sampleInventory(2));
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));

    ReorganizeProposal empty; // isEmpty() == true
    empty.rationale = QStringLiteral("cannot fulfill: no changes needed");
    dlg.applyStubProposal(empty);

    auto *applyBtn = dlg.findChild<QPushButton *>(QStringLiteral("reorganizeApplyButton"));
    QVERIFY(applyBtn);
    QVERIFY(applyBtn->isVisible());
    QVERIFY(!applyBtn->isEnabled());

    auto *rationaleLabel = dlg.findChild<QLabel *>(QStringLiteral("reorganizeRationaleLabel"));
    QVERIFY(rationaleLabel);
    QVERIFY(rationaleLabel->text().contains(QStringLiteral("cannot fulfill")));
}

void ReorganizeTreeDialogTest::testFailureShowsRetryButton()
{
    ReorganizeTreeDialog dlg(sampleInventory(2));
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    auto *intentEdit = dlg.findChild<QPlainTextEdit *>(QStringLiteral("reorganizeIntentEdit"));
    intentEdit->setPlainText(QStringLiteral("please help"));

    auto *stub = new StubAssistant();
    stub->cannedFailure = QStringLiteral("boom");
    dlg.setAssistantForTesting(stub);
    dlg.askClaude();

    auto *errorLabel = dlg.findChild<QLabel *>(QStringLiteral("reorganizeErrorLabel"));
    auto *retryBtn = dlg.findChild<QPushButton *>(QStringLiteral("reorganizeRetryButton"));
    QVERIFY(errorLabel);
    QVERIFY(retryBtn);
    QTRY_VERIFY(errorLabel->isVisible());
    QTRY_VERIFY(retryBtn->isVisible());
    QVERIFY(errorLabel->text().contains(QStringLiteral("boom")));
    stub->deleteLater();
}

QTEST_MAIN(Konsolai::ReorganizeTreeDialogTest)
#include "ReorganizeTreeDialogTest.moc"
#include "moc_ReorganizeTreeDialogTest.cpp"
