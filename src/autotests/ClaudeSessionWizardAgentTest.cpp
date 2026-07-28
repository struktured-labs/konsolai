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

}

QTEST_MAIN(Konsolai::ClaudeSessionWizardAgentTest)
#include "ClaudeSessionWizardAgentTest.moc"
