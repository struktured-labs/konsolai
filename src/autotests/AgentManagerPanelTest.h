/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AGENTMANAGERPANELTEST_H
#define AGENTMANAGERPANELTEST_H

#include <QObject>
#include <QTemporaryDir>

namespace Konsolai
{

class AgentManagerPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    // Panel construction
    void testCreatePanel();
    void testTreeWidgetExists();
    void testFooterExists();

    // Provider management
    void testAddProvider();
    void testRemoveProvider();
    void testProviderNames();
    void testGetProvider();
    void testGetProvider_NotFound();

    // Tree rendering
    void testEmptyTree();
    void testTreeWithAgents();
    void testTreeAgentColumns();
    void testTreeChildItems();
    void testTreeStatusBadge_Idle();
    void testTreeStatusBadge_Running();
    void testTreeStatusBadge_Error();
    void testTreeStatusBadge_Budget();

    // Footer
    void testFooterNoSpend();
    void testFooterWithBudget();

    // Context menu
    void testContextMenuItemData();

    // Refresh
    void testRefresh();

    // Signals
    void testSpendChangedSignal();

    // Aggregate calculations
    void testTotalDailySpend();
    void testTotalDailyBudget();

private:
    void writeGoalYaml(const QString &name, const QString &content);

    QTemporaryDir *m_tempDir = nullptr;
    QString m_fleetPath;
};

}

#endif // AGENTMANAGERPANELTEST_H
