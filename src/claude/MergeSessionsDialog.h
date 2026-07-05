/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_MERGESESSIONSDIALOG_H
#define KONSOLAI_MERGESESSIONSDIALOG_H

#include "konsoleprivate_export.h"

#include "MergePolicy.h"
#include "SessionManagerPanel.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;

namespace Konsolai
{

/**
 * Modal dialog for merging N same-project sessions into a single primary.
 * Lets the user pick which session is the primary and which fields the
 * primary inherits from the others. Shows a live summary preview.
 */
class KONSOLEPRIVATE_EXPORT MergeSessionsDialog : public QDialog
{
    Q_OBJECT

public:
    MergeSessionsDialog(const QList<SessionMetadata> &candidates, const QHash<QString, qint64> &jsonlSizes, QWidget *parent = nullptr);

    /** Selected primary session id (after exec() returns Accepted). */
    QString primarySessionId() const;

    /** Inheritance choices (after exec() returns Accepted). */
    MergeFieldChoices choices() const;

private:
    void buildUi();
    void rebuildPrimaryCombo();
    void refreshSummary();
    MergeFieldChoices readChoices() const;
    QString displayNameFor(const SessionMetadata &meta) const;

    QList<SessionMetadata> m_candidates;
    QHash<QString, qint64> m_jsonlSizes;

    QComboBox *m_primaryCombo = nullptr;
    QCheckBox *m_inheritDescription = nullptr;
    QCheckBox *m_inheritPinned = nullptr;
    QCheckBox *m_inheritYolo = nullptr;
    QCheckBox *m_inheritLastAccessed = nullptr;
    QCheckBox *m_inheritResumeConversation = nullptr;
    QCheckBox *m_inheritApprovalLog = nullptr;
    QCheckBox *m_inheritApprovalCounts = nullptr;
    QCheckBox *m_inheritPromptRound = nullptr;
    QCheckBox *m_inheritBudget = nullptr;

    QPlainTextEdit *m_summary = nullptr;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace Konsolai

#endif // KONSOLAI_MERGESESSIONSDIALOG_H
