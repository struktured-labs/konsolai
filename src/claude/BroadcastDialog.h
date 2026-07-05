/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_BROADCASTDIALOG_H
#define KONSOLAI_BROADCASTDIALOG_H

#include "konsoleprivate_export.h"

#include "BroadcastPolicy.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;
class QToolButton;

namespace Konsolai
{

/**
 * One row passed in by the caller. Caller resolves display name + category
 * key from the panel's data model so the dialog stays UI-agnostic about
 * the smart-category logic.
 */
struct KONSOLEPRIVATE_EXPORT BroadcastRecipient {
    QString sessionId;
    QString displayName;
    QString workingDirectory;
    QString tmuxSession;
    QString category;
};

class KONSOLEPRIVATE_EXPORT BroadcastDialog : public QDialog
{
    Q_OBJECT

public:
    BroadcastDialog(const QList<BroadcastRecipient> &candidates, QWidget *parent = nullptr);

    QStringList selectedSessionIds() const;
    QString messageTemplate() const;
    bool pressEnterAfterEach() const;

    /** Compute the per-recipient substituted text for what will be sent.
     *  Returns parallel list to selectedSessionIds() — same order. */
    QStringList substitutedMessages() const;

private Q_SLOTS:
    void updatePreview();
    void onHelpButtonClicked();

private:
    void buildUi();
    void populateRecipients();
    int countChecked() const;
    int firstCheckedIndex() const;
    BroadcastVars varsForIndex(int candidateIndex, int recipientIndex, int recipientCount) const;
    void insertVariableAtCursor(const QString &name);

    QList<BroadcastRecipient> m_candidates;

    QLabel *m_recipientsHeader = nullptr;
    QListWidget *m_recipientsList = nullptr; // checkable items, sessionId in Qt::UserRole
    QPlainTextEdit *m_messageEdit = nullptr;
    QLabel *m_previewLabel = nullptr;
    QTextEdit *m_previewView = nullptr; // read-only
    QCheckBox *m_pressEnterCheck = nullptr;
    QPushButton *m_broadcastButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QToolButton *m_helpButton = nullptr;
};

} // namespace Konsolai

#endif // KONSOLAI_BROADCASTDIALOG_H
