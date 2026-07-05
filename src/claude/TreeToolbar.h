/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_TREETOOLBAR_H
#define KONSOLAI_TREETOOLBAR_H

#include "konsoleprivate_export.h"

#include <QPointer>
#include <QWidget>

class QToolButton;
class QTreeWidget;

namespace Konsolai
{

/**
 * Small toolbar row with Expand All / Collapse All buttons that act on a
 * QTreeWidget. Designed to sit directly above the tree in panels like
 * SessionManagerPanel and AgentManagerPanel.
 *
 * The toolbar keeps a QPointer to the tree so deletion of the tree before
 * the toolbar will not crash; buttons become no-ops in that case.
 */
class KONSOLEPRIVATE_EXPORT TreeToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit TreeToolbar(QTreeWidget *tree, QWidget *parent = nullptr);
    ~TreeToolbar() override;

    QToolButton *expandButton() const
    {
        return m_expandButton;
    }
    QToolButton *collapseButton() const
    {
        return m_collapseButton;
    }

private Q_SLOTS:
    void onExpandClicked();
    void onCollapseClicked();

private:
    QPointer<QTreeWidget> m_tree;
    QToolButton *m_expandButton = nullptr;
    QToolButton *m_collapseButton = nullptr;
};

} // namespace Konsolai

#endif // KONSOLAI_TREETOOLBAR_H
