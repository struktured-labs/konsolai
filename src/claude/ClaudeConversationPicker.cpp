/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeConversationPicker.h"
#include "ClaudeSessionRegistry.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
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

    // Tree widget with conversation list
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("Summary"), QStringLiteral("Messages"), QStringLiteral("Last Modified")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

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
        // Truncate long summaries for display
        if (displayText.length() > 120) {
            displayText = displayText.left(117) + QStringLiteral("...");
        }

        item->setText(0, displayText);
        item->setText(1, QString::number(conv.messageCount));
        item->setText(2, conv.modified.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        item->setData(0, Qt::UserRole, conv.sessionId);
    }

    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    // Select first item by default
    if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }

    layout->addWidget(m_tree);

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
            accept();
        }
    });

    connect(resumeButton, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem();
        if (item) {
            m_selectedId = item->data(0, Qt::UserRole).toString();
        }
        accept();
    });

    connect(freshButton, &QPushButton::clicked, this, [this]() {
        m_selectedId.clear();
        accept();
    });

    resize(600, 400);
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
