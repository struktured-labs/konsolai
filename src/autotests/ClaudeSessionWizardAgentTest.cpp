/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QComboBox>
#include <QTest>

#include "../claude/ClaudeSession.h"
#include "../claude/ClaudeSessionWizard.h"
#include "../claude/CodexProcess.h"

namespace Konsolai
{

class ClaudeSessionWizardAgentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testAgentComboExists();
    void testDefaultsToClaude();
    void testSelectingCodexChangesAgentKind();
    void testCodexEntryDisabledWhenBinaryMissing();
    void testModelComboSwapsWithAgent();
    void testGitComboHasSingleNothingEntryAndDefaultsToIt();
};

void ClaudeSessionWizardAgentTest::testAgentComboExists()
{
    ClaudeSessionWizard wizard;
    auto *combo = wizard.findChild<QComboBox *>(QStringLiteral("wizardAgentCombo"));
    QVERIFY2(combo, "wizard must expose an agent picker for AT-SPI/GUI tests");
    QCOMPARE(combo->count(), 2);
}

void ClaudeSessionWizardAgentTest::testDefaultsToClaude()
{
    ClaudeSessionWizard wizard;
    // Codex is opt-in: an unchanged wizard must still produce a Claude session.
    QCOMPARE(wizard.agentKind(), ClaudeSession::AgentKind::Claude);
}

void ClaudeSessionWizardAgentTest::testSelectingCodexChangesAgentKind()
{
    ClaudeSessionWizard wizard;
    auto *combo = wizard.findChild<QComboBox *>(QStringLiteral("wizardAgentCombo"));
    QVERIFY(combo);

    combo->setCurrentIndex(1);
    QCOMPARE(wizard.agentKind(), ClaudeSession::AgentKind::Codex);

    combo->setCurrentIndex(0);
    QCOMPARE(wizard.agentKind(), ClaudeSession::AgentKind::Claude);
}

void ClaudeSessionWizardAgentTest::testCodexEntryDisabledWhenBinaryMissing()
{
    ClaudeSessionWizard wizard;
    auto *combo = wizard.findChild<QComboBox *>(QStringLiteral("wizardAgentCombo"));
    QVERIFY(combo);

    // The Codex row is only selectable when the binary resolves, so the picker
    // can never hand back a kind that would fail to launch.
    const bool selectable = combo->model()->flags(combo->model()->index(1, 0)).testFlag(Qt::ItemIsEnabled);
    QCOMPARE(selectable, CodexProcess::isAvailable());
}

void ClaudeSessionWizardAgentTest::testModelComboSwapsWithAgent()
{
    ClaudeSessionWizard wizard;
    auto *agent = wizard.findChild<QComboBox *>(QStringLiteral("wizardAgentCombo"));
    auto *model = wizard.findChild<QComboBox *>(QStringLiteral("wizardModelCombo"));
    QVERIFY(agent);
    QVERIFY(model);

    // Claude selected: only Claude slugs on offer.
    QVERIFY(model->currentText().startsWith(QStringLiteral("claude-")));

    agent->setCurrentIndex(1);
    // Offering claude-* to Codex would be rejected by the CLI, so the model
    // vocabulary has to follow the agent.
    for (int i = 0; i < model->count(); ++i) {
        QVERIFY2(!model->itemText(i).startsWith(QStringLiteral("claude-")),
                 qPrintable(QStringLiteral("Claude model offered for Codex: %1").arg(model->itemText(i))));
    }
    QVERIFY(model->currentText().startsWith(QStringLiteral("gpt-")));

    agent->setCurrentIndex(0);
    QVERIFY(model->currentText().startsWith(QStringLiteral("claude-")));
}

void ClaudeSessionWizardAgentTest::testGitComboHasSingleNothingEntryAndDefaultsToIt()
{
    ClaudeSessionWizard wizard;
    auto *git = wizard.findChild<QComboBox *>(QStringLiteral("wizardGitModeCombo"));
    QVERIFY2(git, "git mode combo must be findable");

    // Exactly one do-nothing option — two was confusing — and it is the
    // default, so the wizard never pre-creates a repo or worktree.
    int nothingCount = 0;
    for (int i = 0; i < git->count(); ++i) {
        if (git->itemText(i).startsWith(QStringLiteral("Nothing"))) {
            ++nothingCount;
        }
    }
    QCOMPARE(nothingCount, 1);
    QCOMPARE(git->count(), 3);
    QVERIFY(git->currentText().startsWith(QStringLiteral("Nothing")));
}
}

QTEST_MAIN(Konsolai::ClaudeSessionWizardAgentTest)
#include "ClaudeSessionWizardAgentTest.moc"
