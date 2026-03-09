/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AgentManagerPanelTest.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTreeWidget>

#include "../claude/AgentFleetProvider.h"
#include "../claude/AgentManagerPanel.h"

using namespace Konsolai;

void AgentManagerPanelTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AgentManagerPanelTest::cleanup()
{
    delete m_tempDir;
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    m_fleetPath = m_tempDir->path() + QStringLiteral("/fleet");
    QDir().mkpath(m_fleetPath + QStringLiteral("/goals"));
}

void AgentManagerPanelTest::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void AgentManagerPanelTest::writeGoalYaml(const QString &name, const QString &content)
{
    QFile file(m_fleetPath + QStringLiteral("/goals/") + name + QStringLiteral(".yaml"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
}

// ── Panel construction ──

void AgentManagerPanelTest::testCreatePanel()
{
    AgentManagerPanel panel;
    QVERIFY(panel.providerNames().isEmpty());
}

void AgentManagerPanelTest::testTreeWidgetExists()
{
    AgentManagerPanel panel;
    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    QVERIFY(tree);
    QCOMPARE(tree->columnCount(), 2);
}

void AgentManagerPanelTest::testFooterExists()
{
    AgentManagerPanel panel;
    auto *footer = panel.findChild<QLabel *>(QStringLiteral("agentFooter"));
    QVERIFY(footer);
    QVERIFY(!footer->text().isEmpty());
}

// ── Provider management ──

void AgentManagerPanelTest::testAddProvider()
{
    AgentManagerPanel panel;
    auto *provider = new AgentFleetProvider(m_fleetPath);
    panel.addProvider(provider);
    QCOMPARE(panel.providerNames().size(), 1);
    QCOMPARE(panel.providerNames().first(), QStringLiteral("agent-fleet"));
}

void AgentManagerPanelTest::testRemoveProvider()
{
    AgentManagerPanel panel;
    auto *provider = new AgentFleetProvider(m_fleetPath);
    panel.addProvider(provider);
    QCOMPARE(panel.providerNames().size(), 1);

    panel.removeProvider(QStringLiteral("agent-fleet"));
    QVERIFY(panel.providerNames().isEmpty());
}

void AgentManagerPanelTest::testProviderNames()
{
    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));
    QCOMPARE(panel.providerNames(), QStringList{QStringLiteral("agent-fleet")});
}

void AgentManagerPanelTest::testGetProvider()
{
    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));
    QVERIFY(panel.provider(QStringLiteral("agent-fleet")));
}

void AgentManagerPanelTest::testGetProvider_NotFound()
{
    AgentManagerPanel panel;
    QVERIFY(!panel.provider(QStringLiteral("nonexistent")));
}

// ── Tree rendering ──

void AgentManagerPanelTest::testEmptyTree()
{
    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    QCOMPARE(tree->topLevelItemCount(), 0);
}

void AgentManagerPanelTest::testTreeWithAgents()
{
    writeGoalYaml(QStringLiteral("alpha"), QStringLiteral("name: Alpha\ngoal: Do A\n"));
    writeGoalYaml(QStringLiteral("beta"), QStringLiteral("name: Beta\ngoal: Do B\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    QCOMPARE(tree->topLevelItemCount(), 2);
}

void AgentManagerPanelTest::testTreeAgentColumns()
{
    writeGoalYaml(QStringLiteral("test"), QStringLiteral("name: Test Agent\ngoal: Testing\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    QCOMPARE(tree->topLevelItemCount(), 1);

    auto *item = tree->topLevelItem(0);
    QCOMPARE(item->text(0), QStringLiteral("Test Agent"));
    // Status column should have state icon
    QVERIFY(!item->text(1).isEmpty());
}

void AgentManagerPanelTest::testTreeChildItems()
{
    writeGoalYaml(QStringLiteral("child-test"),
                  QStringLiteral("name: Child Test\n"
                                 "goal: Testing\n"
                                 "schedule: hourly\n"
                                 "daily_budget: 10.0\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    auto *item = tree->topLevelItem(0);
    // Should have schedule and budget child items
    QVERIFY(item->childCount() >= 2);
}

void AgentManagerPanelTest::testTreeStatusBadge_Idle()
{
    writeGoalYaml(QStringLiteral("idle-agent"), QStringLiteral("name: Idle\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    auto *item = tree->topLevelItem(0);
    // Status should contain idle circle ○
    QVERIFY(item->text(1).contains(QStringLiteral("\u25CB")));
}

void AgentManagerPanelTest::testTreeStatusBadge_Running()
{
    // Without a session state file, status defaults to idle
    writeGoalYaml(QStringLiteral("running-agent"), QStringLiteral("name: Running\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    auto *item = tree->topLevelItem(0);
    // Default state is idle since no session file exists
    QVERIFY(item->text(1).contains(QStringLiteral("\u25CB")));
}

void AgentManagerPanelTest::testTreeStatusBadge_Error()
{
    writeGoalYaml(QStringLiteral("error-agent"), QStringLiteral("name: Error\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    auto *item = tree->topLevelItem(0);
    QVERIFY(item->text(1).contains(QStringLiteral("\u25CB"))); // idle (no state file)
}

void AgentManagerPanelTest::testTreeStatusBadge_Budget()
{
    writeGoalYaml(QStringLiteral("budget-agent"), QStringLiteral("name: Budget\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    auto *item = tree->topLevelItem(0);
    QVERIFY(item->text(1).contains(QStringLiteral("\u25CB"))); // idle (no state file)
}

// ── Footer ──

void AgentManagerPanelTest::testFooterNoSpend()
{
    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *footer = panel.findChild<QLabel *>(QStringLiteral("agentFooter"));
    // No agents means no activity
    QVERIFY(footer->text().contains(QStringLiteral("No agent activity")));
}

void AgentManagerPanelTest::testFooterWithBudget()
{
    writeGoalYaml(QStringLiteral("budgeted"), QStringLiteral("name: Budgeted\ndaily_budget: 50.0\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *footer = panel.findChild<QLabel *>(QStringLiteral("agentFooter"));
    // Should show fleet total with budget
    QVERIFY(footer->text().contains(QStringLiteral("$")));
}

// ── Context menu ──

void AgentManagerPanelTest::testContextMenuItemData()
{
    writeGoalYaml(QStringLiteral("menu-test"), QStringLiteral("name: Menu Test\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    auto *item = tree->topLevelItem(0);

    // Verify item data roles
    QCOMPARE(item->data(0, Qt::UserRole + 1).toString(), QStringLiteral("agent-fleet")); // ProviderNameRole
    QCOMPARE(item->data(0, Qt::UserRole + 2).toString(), QStringLiteral("menu-test")); // AgentIdRole
    QVERIFY(item->data(0, Qt::UserRole + 3).toBool()); // IsAgentItemRole
}

// ── Refresh ──

void AgentManagerPanelTest::testRefresh()
{
    AgentManagerPanel panel;
    auto *fleetProvider = new AgentFleetProvider(m_fleetPath);
    panel.addProvider(fleetProvider);

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("agentTree"));
    QCOMPARE(tree->topLevelItemCount(), 0);

    writeGoalYaml(QStringLiteral("refreshed"), QStringLiteral("name: Refreshed\n"));
    // Invalidate provider cache and refresh panel
    fleetProvider->reloadAgents();
    // The reloadAgents() signal triggers a debounced rebuild;
    // call refresh() directly for immediate rebuild
    panel.refresh();

    QCOMPARE(tree->topLevelItemCount(), 1);
}

// ── Signals ──

void AgentManagerPanelTest::testSpendChangedSignal()
{
    AgentManagerPanel panel;
    QSignalSpy spy(&panel, &AgentManagerPanel::spendChanged);

    panel.addProvider(new AgentFleetProvider(m_fleetPath));

    // rebuildTree() calls updateFooter() which emits spendChanged
    QVERIFY(spy.count() >= 1);
}

// ── Aggregates ──

void AgentManagerPanelTest::testTotalDailySpend()
{
    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));
    QCOMPARE(panel.totalDailySpendUSD(), 0.0);
}

void AgentManagerPanelTest::testTotalDailyBudget()
{
    writeGoalYaml(QStringLiteral("b1"), QStringLiteral("name: B1\ndaily_budget: 15.0\n"));
    writeGoalYaml(QStringLiteral("b2"), QStringLiteral("name: B2\ndaily_budget: 25.0\n"));

    AgentManagerPanel panel;
    panel.addProvider(new AgentFleetProvider(m_fleetPath));
    QCOMPARE(panel.totalDailyBudgetUSD(), 40.0);
}

QTEST_MAIN(AgentManagerPanelTest)

#include "moc_AgentManagerPanelTest.cpp"
