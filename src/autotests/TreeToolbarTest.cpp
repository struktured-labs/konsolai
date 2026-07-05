/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TreeToolbarTest.h"

#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "../claude/TreeToolbar.h"

using namespace Konsolai;

namespace
{
void buildThreeLevelTree(QTreeWidget *tree)
{
    auto *top = new QTreeWidgetItem(tree);
    top->setText(0, QStringLiteral("top"));
    auto *mid = new QTreeWidgetItem(top);
    mid->setText(0, QStringLiteral("mid"));
    auto *leaf = new QTreeWidgetItem(mid);
    leaf->setText(0, QStringLiteral("leaf"));

    auto *top2 = new QTreeWidgetItem(tree);
    top2->setText(0, QStringLiteral("top2"));
    auto *mid2 = new QTreeWidgetItem(top2);
    mid2->setText(0, QStringLiteral("mid2"));
}
} // namespace

void TreeToolbarTest::testButtonsHaveObjectNames()
{
    QTreeWidget tree;
    TreeToolbar toolbar(&tree);
    QVERIFY(toolbar.expandButton());
    QVERIFY(toolbar.collapseButton());
    QCOMPARE(toolbar.expandButton()->objectName(), QStringLiteral("expandAllButton"));
    QCOMPARE(toolbar.collapseButton()->objectName(), QStringLiteral("collapseAllButton"));
}

void TreeToolbarTest::testExpandAll_RevealsNestedItems()
{
    QTreeWidget tree;
    buildThreeLevelTree(&tree);
    TreeToolbar toolbar(&tree);

    // Sanity: tree starts collapsed.
    QVERIFY(!tree.topLevelItem(0)->isExpanded());

    toolbar.expandButton()->click();

    QVERIFY(tree.topLevelItem(0)->isExpanded());
    QVERIFY(tree.topLevelItem(0)->child(0)->isExpanded());
    QVERIFY(tree.topLevelItem(1)->isExpanded());
    QVERIFY(tree.topLevelItem(1)->child(0)->isExpanded());
}

void TreeToolbarTest::testCollapseAll_HidesNestedItems()
{
    QTreeWidget tree;
    buildThreeLevelTree(&tree);
    tree.expandAll();
    QVERIFY(tree.topLevelItem(0)->isExpanded());

    TreeToolbar toolbar(&tree);
    toolbar.collapseButton()->click();

    QVERIFY(!tree.topLevelItem(0)->isExpanded());
    QVERIFY(!tree.topLevelItem(1)->isExpanded());
}

void TreeToolbarTest::testButtonsAreNoOps_AfterTreeDeleted()
{
    auto *tree = new QTreeWidget();
    buildThreeLevelTree(tree);
    TreeToolbar toolbar(tree);
    delete tree;

    // Should not crash. Buttons hold a QPointer so click is a no-op.
    toolbar.expandButton()->click();
    toolbar.collapseButton()->click();
}

QTEST_MAIN(TreeToolbarTest)

#include "moc_TreeToolbarTest.cpp"
