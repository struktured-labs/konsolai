/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_REORGANIZETREEDIALOG_H
#define KONSOLAI_REORGANIZETREEDIALOG_H

#include "konsoleprivate_export.h"

#include "ClaudeAssistantPromptBuilder.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QGroupBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;

namespace Konsolai
{

class ClaudeAssistant;

/**
 * Dialog for the "Reorganize Tree with AI" feature. Two states:
 *
 *  1. Compose state (initial) — user enters an intent, may uncheck projects
 *     they don't want Claude to touch, clicks "Ask Claude".
 *  2. Review state — after Claude replies, shows a rationale + diff-style
 *     summary of the proposed changes; user clicks Apply or Cancel.
 *
 * Failures land the dialog in a small error state with a Retry action.
 *
 * The dialog owns its ClaudeAssistant by default. Tests can inject a stub
 * via setAssistantForTesting() before calling ask().
 */
class KONSOLEPRIVATE_EXPORT ReorganizeTreeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReorganizeTreeDialog(const TreeInventory &inventory, QWidget *parent = nullptr);
    ~ReorganizeTreeDialog() override;

    /** Returned only when the user hit "Apply". Empty on cancel. */
    ReorganizeProposal proposal() const
    {
        return m_proposal;
    }

    /** Workdirs currently checked in the inventory list. Used by the prompt. */
    QStringList intentIncludedWorkdirs() const;

    /**
     * Testing hook — inject a stub ClaudeAssistant. Takes ownership.
     * Must be called BEFORE the user clicks "Ask Claude".
     */
    void setAssistantForTesting(ClaudeAssistant *stub);

    /**
     * Testing hook — pre-populate the review state with a known proposal so
     * tests can exercise the Apply button without spinning up a real process.
     * Callable regardless of current state — swaps the dialog into review mode.
     */
    void applyStubProposal(const ReorganizeProposal &proposal);

    /**
     * Testing hook — return the last prompt sent to the assistant (empty if
     * the assistant hasn't been asked yet).
     */
    QString lastPromptForTesting() const
    {
        return m_lastPrompt;
    }

public Q_SLOTS:
    /** Called by the "Ask Claude" button. Public so tests can drive it. */
    void askClaude();

private Q_SLOTS:
    void onAssistantFinished(const QString &output, int exitCode);
    void onAssistantFailed(const QString &err);
    void onIntentTextChanged();
    void onProjectItemChanged();

private:
    void buildUi();
    void populateInventory();
    void enterComposeState();
    void enterBusyState();
    void enterReviewState(const ReorganizeProposal &proposal);
    void enterErrorState(const QString &message);
    QString buildDiffText(const ReorganizeProposal &proposal) const;
    void ensureAssistant();
    void updateInventoryTitle();
    void updateAskButtonEnabled();

    TreeInventory m_inventory;

    // Compose state widgets.
    QPlainTextEdit *m_intentEdit = nullptr;
    QGroupBox *m_inventoryBox = nullptr;
    QListWidget *m_projectsList = nullptr;
    QPushButton *m_askButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    // Busy / review state widgets.
    QLabel *m_busySpinner = nullptr;
    QLabel *m_rationaleLabel = nullptr;
    QTextEdit *m_proposalView = nullptr;
    QPushButton *m_applyButton = nullptr;

    // Error state.
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_retryButton = nullptr;

    ClaudeAssistant *m_assistant = nullptr;
    bool m_ownsAssistant = true;
    ReorganizeProposal m_proposal;
    QString m_lastPrompt;
};

} // namespace Konsolai

#endif // KONSOLAI_REORGANIZETREEDIALOG_H
