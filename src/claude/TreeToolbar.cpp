/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TreeToolbar.h"

#include <KLocalizedString>
#include <QHBoxLayout>
#include <QIcon>
#include <QToolButton>
#include <QTreeWidget>

namespace Konsolai
{

TreeToolbar::TreeToolbar(QTreeWidget *tree, QWidget *parent)
    : QWidget(parent)
    , m_tree(tree)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    m_expandButton = new QToolButton(this);
    m_expandButton->setObjectName(QStringLiteral("expandAllButton"));
    m_expandButton->setIcon(QIcon::fromTheme(QStringLiteral("view-list-tree")));
    m_expandButton->setToolTip(i18n("Expand All"));
    m_expandButton->setAutoRaise(true);
    m_expandButton->setFixedSize(22, 22);
    connect(m_expandButton, &QToolButton::clicked, this, &TreeToolbar::onExpandClicked);
    layout->addWidget(m_expandButton);

    m_collapseButton = new QToolButton(this);
    m_collapseButton->setObjectName(QStringLiteral("collapseAllButton"));
    m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("view-list-text")));
    m_collapseButton->setToolTip(i18n("Collapse All"));
    m_collapseButton->setAutoRaise(true);
    m_collapseButton->setFixedSize(22, 22);
    connect(m_collapseButton, &QToolButton::clicked, this, &TreeToolbar::onCollapseClicked);
    layout->addWidget(m_collapseButton);

    layout->addStretch();
}

TreeToolbar::~TreeToolbar() = default;

void TreeToolbar::onExpandClicked()
{
    if (m_tree) {
        m_tree->expandAll();
    }
}

void TreeToolbar::onCollapseClicked()
{
    if (m_tree) {
        m_tree->collapseAll();
    }
}

} // namespace Konsolai

#include "moc_TreeToolbar.cpp"
