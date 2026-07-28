/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QTest>

#include "../claude/ClaudeSession.h"
#include "../claude/ClaudeSessionWizard.h"
#include "../claude/CodexProcess.h"
#include "../claude/KonsolaiSettings.h"

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
    void testGitUiIsGone();
    void testCodexModelDoesNotOverwriteClaudeDefault();
    void testWizardBudgetValuesAreReadable();
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

void ClaudeSessionWizardAgentTest::testGitUiIsGone()
{
    ClaudeSessionWizard wizard;
    // The git panel was removed entirely: konsolai does not init repos or
    // create worktrees. Its absence must not break the accessors, which the
    // create path still calls.
    QVERIFY2(!wizard.findChild<QComboBox *>(QStringLiteral("wizardGitModeCombo")), "git mode combo must be gone");
    QVERIFY(!wizard.shouldInitGit());
    QVERIFY(wizard.worktreeBranch().isEmpty());
}

void ClaudeSessionWizardAgentTest::testCodexModelDoesNotOverwriteClaudeDefault()
{
    KonsolaiSettings settings;
    const QString claudeBefore = settings.defaultModel();
    QVERIFY(claudeBefore.startsWith(QStringLiteral("claude-")));

    ClaudeSessionWizard wizard;
    auto *agent = wizard.findChild<QComboBox *>(QStringLiteral("wizardAgentCombo"));
    QVERIFY(agent);
    agent->setCurrentIndex(1); // Codex

    wizard.saveSelectedModel(&settings);

    // The Codex slug must land in the Codex key. Writing it to DefaultModel
    // would also break `claude -p` one-shots, which read the same setting.
    QCOMPARE(settings.defaultModel(), claudeBefore);
    QVERIFY(settings.codexModel().startsWith(QStringLiteral("gpt-")));
}
void ClaudeSessionWizardAgentTest::testWizardBudgetValuesAreReadable()
{
    ClaudeSessionWizard wizard;
    auto *group = wizard.findChild<QGroupBox *>(QStringLiteral("wizardBudgetGroup"));
    auto *cost = wizard.findChild<QDoubleSpinBox *>(QStringLiteral("wizardCostCeilingSpin"));
    QVERIFY2(group, "budget group must be findable");
    QVERIFY2(cost, "cost ceiling spin must be findable");

    // Budgets are opt-in: the accessors report 0 until the group is checked.
    QVERIFY(group->isCheckable());
    group->setChecked(true);
    cost->setValue(5.0);
    // The accessor must reflect the widget: MainWindow now reads these at
    // session creation, so a value that doesn't round-trip means a budget the
    // user set is silently dropped.
    QCOMPARE(wizard.budgetCostCeilingUSD(), 5.0);
}
}

QTEST_MAIN(Konsolai::ClaudeSessionWizardAgentTest)
#include "ClaudeSessionWizardAgentTest.moc"
