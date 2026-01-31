/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDE_CONVERSATION_PICKER_H
#define CLAUDE_CONVERSATION_PICKER_H

#include "konsoleprivate_export.h"

#include <QDialog>
#include <QList>

class QTreeWidget;

namespace Konsolai
{

struct ClaudeConversation;

/**
 * Dialog for picking a Claude CLI conversation to resume.
 *
 * Shows a list of previous conversations with summary, message count,
 * and last modified date. The user can choose to resume one or start fresh.
 *
 * Use the static pick() helper for the common case.
 */
class KONSOLEPRIVATE_EXPORT ClaudeConversationPicker : public QDialog
{
    Q_OBJECT

public:
    explicit ClaudeConversationPicker(const QList<ClaudeConversation> &conversations, QWidget *parent = nullptr);
    ~ClaudeConversationPicker() override;

    /**
     * Get the selected conversation session ID.
     * Empty string means "Start Fresh".
     */
    QString selectedSessionId() const
    {
        return m_selectedId;
    }

    /**
     * Show the picker and return the chosen conversation UUID.
     *
     * @param conversations List of available conversations
     * @param parent Parent widget
     * @return Session UUID to resume, or empty string for fresh start.
     *         If no conversations exist, returns empty immediately (no dialog).
     */
    static QString pick(const QList<ClaudeConversation> &conversations, QWidget *parent = nullptr);

private:
    void setupUi(const QList<ClaudeConversation> &conversations);

    QTreeWidget *m_tree = nullptr;
    QString m_selectedId;
};

} // namespace Konsolai

#endif // CLAUDE_CONVERSATION_PICKER_H
