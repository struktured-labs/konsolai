/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeConversationPicker.h"
#include "ClaudeSessionRegistry.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Konsolai
{

ClaudeConversationPicker::ClaudeConversationPicker(const QList<ClaudeConversation> &conversations, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Resume Claude Conversation"));
    setupUi(conversations);
}

ClaudeConversationPicker::~ClaudeConversationPicker() = default;

void ClaudeConversationPicker::setupUi(const QList<ClaudeConversation> &conversations)
{
    auto *layout = new QVBoxLayout(this);

    auto *label = new QLabel(QStringLiteral("Select a previous conversation to resume, or start fresh:"), this);
    layout->addWidget(label);

    // Check if any conversations have projectPath set (multi-project mode)
    bool hasProjectPaths = false;
    for (const ClaudeConversation &conv : conversations) {
        if (!conv.projectPath.isEmpty()) {
            hasProjectPaths = true;
            break;
        }
    }

    // Search filter
    auto *filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText(QStringLiteral("Filter conversations..."));
    filterEdit->setClearButtonEnabled(true);
    layout->addWidget(filterEdit);

    // Tree widget with conversation list
    m_tree = new QTreeWidget(this);
    if (hasProjectPaths) {
        m_tree->setHeaderLabels({QStringLiteral("Summary"), QStringLiteral("Project"), QStringLiteral("Messages"), QStringLiteral("Last Modified")});
    } else {
        m_tree->setHeaderLabels({QStringLiteral("Summary"), QStringLiteral("Messages"), QStringLiteral("Last Modified")});
    }
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSortingEnabled(true);

    // Column indices shift when project column is present
    const int colProject = hasProjectPaths ? 1 : -1;
    const int colMessages = hasProjectPaths ? 2 : 1;
    const int colModified = hasProjectPaths ? 3 : 2;

    for (const ClaudeConversation &conv : conversations) {
        auto *item = new QTreeWidgetItem(m_tree);

        // Use summary if available, otherwise first prompt, otherwise "(no summary)"
        QString displayText = conv.summary;
        if (displayText.isEmpty()) {
            displayText = conv.firstPrompt;
        }
        if (displayText.isEmpty()) {
            displayText = QStringLiteral("(no summary)");
        }
        // Collapse whitespace/newlines
        displayText = displayText.simplified();
        // Truncate long summaries for display
        if (displayText.length() > 120) {
            displayText = displayText.left(117) + QStringLiteral("...");
        }

        item->setText(0, displayText);
        if (hasProjectPaths) {
            // Show the project directory basename from the hashed dir name.
            // Use projectDir (the raw hash) to extract the project name portion
            // since rpath() can produce wrong results for ambiguous paths.
            QString projectLabel = QDir(conv.projectPath).dirName();
            if (projectLabel.isEmpty() || projectLabel == QStringLiteral(".")) {
                projectLabel = conv.projectPath;
            }
            item->setText(colProject, projectLabel);
            item->setToolTip(colProject, conv.projectPath);
        }
        item->setText(colMessages, QString::number(conv.messageCount));
        item->setTextAlignment(colMessages, Qt::AlignRight | Qt::AlignVCenter);
        // Store raw message count for correct numeric sorting
        item->setData(colMessages, Qt::UserRole + 10, conv.messageCount);
        item->setText(colModified, conv.modified.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        item->setData(0, Qt::UserRole, conv.sessionId);
        item->setData(0, Qt::UserRole + 1, conv.projectPath);
    }

    // Default sort by last modified descending (most recent first)
    m_tree->sortByColumn(colModified, Qt::DescendingOrder);

    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    if (hasProjectPaths) {
        m_tree->header()->setSectionResizeMode(colProject, QHeaderView::ResizeToContents);
    }
    m_tree->header()->setSectionResizeMode(colMessages, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(colModified, QHeaderView::ResizeToContents);

    // Select first item by default
    if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }

    layout->addWidget(m_tree);

    // Filter: hide items that don't match the search text
    connect(filterEdit, &QLineEdit::textChanged, this, [this, colProject](const QString &text) {
        QString filter = text.trimmed();
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto *item = m_tree->topLevelItem(i);
            if (filter.isEmpty()) {
                item->setHidden(false);
                continue;
            }
            // Search across summary, project name, and project path
            bool matches = item->text(0).contains(filter, Qt::CaseInsensitive);
            if (!matches && colProject > 0) {
                matches = item->text(colProject).contains(filter, Qt::CaseInsensitive)
                       || item->toolTip(colProject).contains(filter, Qt::CaseInsensitive);
            }
            item->setHidden(!matches);
        }
        // Select first visible item
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto *item = m_tree->topLevelItem(i);
            if (!item->isHidden()) {
                m_tree->setCurrentItem(item);
                break;
            }
        }
    });

    // Buttons
    auto *buttonBox = new QDialogButtonBox(this);
    auto *freshButton = buttonBox->addButton(QStringLiteral("Start Fresh"), QDialogButtonBox::RejectRole);
    auto *resumeButton = buttonBox->addButton(QStringLiteral("Resume Selected"), QDialogButtonBox::AcceptRole);
    resumeButton->setDefault(true);

    layout->addWidget(buttonBox);

    // Double-click to resume
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item) {
            m_selectedId = item->data(0, Qt::UserRole).toString();
            m_selectedProjectPath = item->data(0, Qt::UserRole + 1).toString();
            accept();
        }
    });

    connect(resumeButton, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem();
        if (item) {
            m_selectedId = item->data(0, Qt::UserRole).toString();
            m_selectedProjectPath = item->data(0, Qt::UserRole + 1).toString();
        }
        accept();
    });

    connect(freshButton, &QPushButton::clicked, this, [this]() {
        m_selectedId.clear();
        m_selectedProjectPath.clear();
        reject();
    });

    resize(hasProjectPaths ? 800 : 600, 400);
}

QString ClaudeConversationPicker::pick(const QList<ClaudeConversation> &conversations, QWidget *parent)
{
    if (conversations.isEmpty()) {
        return QString();
    }

    ClaudeConversationPicker dialog(conversations, parent);
    dialog.exec();
    return dialog.selectedSessionId();
}

} // namespace Konsolai
