/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ReorganizeTreeDialog.h"

#include "ClaudeAssistant.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QTextEdit>
#include <QVBoxLayout>

namespace Konsolai
{

ReorganizeTreeDialog::ReorganizeTreeDialog(const TreeInventory &inventory, QWidget *parent)
    : QDialog(parent)
    , m_inventory(inventory)
{
    setObjectName(QStringLiteral("reorganizeTreeDialog"));
    setWindowTitle(i18n("Reorganize Session Tree with AI"));
    buildUi();
    populateInventory();
    enterComposeState();
    updateInventoryTitle();
    updateAskButtonEnabled();
    resize(600, 500);
}

ReorganizeTreeDialog::~ReorganizeTreeDialog()
{
    if (m_ownsAssistant && m_assistant) {
        m_assistant->deleteLater();
        m_assistant = nullptr;
    }
}

void ReorganizeTreeDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *headerLabel = new QLabel(i18n("Ask Claude to reorganize your session tree"), this);
    QFont f = headerLabel->font();
    f.setBold(true);
    headerLabel->setFont(f);
    root->addWidget(headerLabel);

    auto *muted = new QLabel(i18n("Describe what you want. Claude will propose changes; you review and apply."), this);
    muted->setWordWrap(true);
    muted->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(muted);

    // Intent editor.
    m_intentEdit = new QPlainTextEdit(this);
    m_intentEdit->setObjectName(QStringLiteral("reorganizeIntentEdit"));
    m_intentEdit->setPlaceholderText(
        i18n("Describe what you want. E.g., 'Put all AI projects together', 'Group my Konsolai-related "
             "work separately', 'Rename cowir to Corridor Games'…"));
    const int line = QFontMetrics(m_intentEdit->font()).lineSpacing();
    m_intentEdit->setMinimumHeight(line * 4 + 16);
    connect(m_intentEdit, &QPlainTextEdit::textChanged, this, &ReorganizeTreeDialog::onIntentTextChanged);
    root->addWidget(m_intentEdit);

    // Inventory checklist.
    m_inventoryBox = new QGroupBox(i18n("Include projects"), this);
    m_inventoryBox->setObjectName(QStringLiteral("reorganizeInventoryBox"));
    m_inventoryBox->setCheckable(false);
    auto *invLayout = new QVBoxLayout(m_inventoryBox);
    invLayout->setContentsMargins(6, 6, 6, 6);
    m_projectsList = new QListWidget(m_inventoryBox);
    m_projectsList->setObjectName(QStringLiteral("reorganizeProjectsList"));
    m_projectsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_projectsList->setUniformItemSizes(true);
    m_projectsList->setMaximumHeight(150);
    connect(m_projectsList, &QListWidget::itemChanged, this, &ReorganizeTreeDialog::onProjectItemChanged);
    invLayout->addWidget(m_projectsList);
    root->addWidget(m_inventoryBox);

    // Busy indicator (hidden until Ask).
    m_busySpinner = new QLabel(i18n("Asking Claude…"), this);
    m_busySpinner->setObjectName(QStringLiteral("reorganizeBusySpinner"));
    m_busySpinner->setVisible(false);
    root->addWidget(m_busySpinner);

    // Review state widgets.
    m_rationaleLabel = new QLabel(this);
    m_rationaleLabel->setObjectName(QStringLiteral("reorganizeRationaleLabel"));
    m_rationaleLabel->setWordWrap(true);
    m_rationaleLabel->setVisible(false);
    root->addWidget(m_rationaleLabel);

    m_proposalView = new QTextEdit(this);
    m_proposalView->setObjectName(QStringLiteral("reorganizeProposalView"));
    m_proposalView->setReadOnly(true);
    m_proposalView->setVisible(false);
    m_proposalView->setMinimumHeight(150);
    root->addWidget(m_proposalView, /*stretch=*/1);

    // Error state.
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("reorganizeErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));
    m_errorLabel->setVisible(false);
    root->addWidget(m_errorLabel);

    // Button row — assembled up front, individual buttons toggled per state.
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    m_retryButton = new QPushButton(i18n("Retry"), this);
    m_retryButton->setObjectName(QStringLiteral("reorganizeRetryButton"));
    m_retryButton->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    connect(m_retryButton, &QPushButton::clicked, this, &ReorganizeTreeDialog::askClaude);
    m_retryButton->setVisible(false);
    buttonRow->addWidget(m_retryButton);

    m_cancelButton = new QPushButton(i18n("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("reorganizeCancelButton"));
    connect(m_cancelButton, &QPushButton::clicked, this, &ReorganizeTreeDialog::reject);
    buttonRow->addWidget(m_cancelButton);

    m_askButton = new QPushButton(i18n("Ask Claude"), this);
    m_askButton->setObjectName(QStringLiteral("reorganizeAskButton"));
    m_askButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    m_askButton->setDefault(true);
    connect(m_askButton, &QPushButton::clicked, this, &ReorganizeTreeDialog::askClaude);
    buttonRow->addWidget(m_askButton);

    m_applyButton = new QPushButton(i18n("Apply Proposal"), this);
    m_applyButton->setObjectName(QStringLiteral("reorganizeApplyButton"));
    m_applyButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
    m_applyButton->setDefault(true);
    m_applyButton->setVisible(false);
    connect(m_applyButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(m_applyButton);

    root->addLayout(buttonRow);
}

void ReorganizeTreeDialog::populateInventory()
{
    m_projectsList->blockSignals(true);
    m_projectsList->clear();
    for (const TreeInventory::Project &p : m_inventory.projects) {
        auto *item = new QListWidgetItem(p.workingDirectory, m_projectsList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, p.workingDirectory);
        if (!p.description.trimmed().isEmpty()) {
            item->setToolTip(p.description.trimmed());
        }
    }
    m_projectsList->blockSignals(false);
}

void ReorganizeTreeDialog::enterComposeState()
{
    m_intentEdit->setVisible(true);
    m_inventoryBox->setVisible(true);
    m_busySpinner->setVisible(false);
    m_rationaleLabel->setVisible(false);
    m_proposalView->setVisible(false);
    m_errorLabel->setVisible(false);
    m_retryButton->setVisible(false);
    m_askButton->setVisible(true);
    m_applyButton->setVisible(false);
    m_cancelButton->setVisible(true);
    m_askButton->setEnabled(true);
    updateAskButtonEnabled();
}

void ReorganizeTreeDialog::enterBusyState()
{
    m_intentEdit->setEnabled(false);
    m_projectsList->setEnabled(false);
    m_askButton->setVisible(true);
    m_askButton->setEnabled(false);
    m_busySpinner->setVisible(true);
    m_rationaleLabel->setVisible(false);
    m_proposalView->setVisible(false);
    m_errorLabel->setVisible(false);
    m_retryButton->setVisible(false);
    m_applyButton->setVisible(false);
}

void ReorganizeTreeDialog::enterReviewState(const ReorganizeProposal &proposal)
{
    m_proposal = proposal;
    m_intentEdit->setEnabled(false);
    m_projectsList->setEnabled(false);
    m_askButton->setVisible(false);
    m_busySpinner->setVisible(false);
    m_errorLabel->setVisible(false);
    m_retryButton->setVisible(false);

    if (!proposal.rationale.isEmpty()) {
        m_rationaleLabel->setText(proposal.rationale);
        m_rationaleLabel->setVisible(true);
    } else {
        m_rationaleLabel->setVisible(false);
    }

    m_proposalView->setPlainText(buildDiffText(proposal));
    m_proposalView->setVisible(true);

    m_applyButton->setVisible(true);
    // Empty proposals cannot be applied — hide Apply, keep Cancel.
    m_applyButton->setEnabled(!proposal.isEmpty());
}

void ReorganizeTreeDialog::enterErrorState(const QString &message)
{
    m_intentEdit->setEnabled(true);
    m_projectsList->setEnabled(true);
    m_askButton->setVisible(false);
    m_busySpinner->setVisible(false);
    m_rationaleLabel->setVisible(false);
    m_proposalView->setVisible(false);
    m_applyButton->setVisible(false);
    m_retryButton->setVisible(true);
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);
}

QString ReorganizeTreeDialog::buildDiffText(const ReorganizeProposal &proposal) const
{
    if (proposal.isEmpty()) {
        return i18n("(no changes proposed)");
    }
    QString out;
    for (auto it = proposal.categoryAliases.cbegin(); it != proposal.categoryAliases.cend(); ++it) {
        out += QStringLiteral("+ Alias: %1 → %2\n").arg(it.key(), it.value());
    }
    for (auto it = proposal.workdirOverrides.cbegin(); it != proposal.workdirOverrides.cend(); ++it) {
        out += QStringLiteral("+ Workdir override: %1 → %2\n").arg(it.key(), it.value());
    }
    for (const QString &s : proposal.suppressedCategories) {
        out += QStringLiteral("~ Suppress: %1\n").arg(s);
    }
    for (const QString &u : proposal.userCategories) {
        out += QStringLiteral("+ New empty category: %1\n").arg(u);
    }
    return out;
}

void ReorganizeTreeDialog::updateInventoryTitle()
{
    if (m_inventoryBox) {
        m_inventoryBox->setTitle(i18n("Include (%1 projects)", m_inventory.projects.size()));
    }
}

void ReorganizeTreeDialog::onIntentTextChanged()
{
    updateAskButtonEnabled();
}

void ReorganizeTreeDialog::onProjectItemChanged()
{
    // No-op for now — projects can all be unchecked and Claude receives a
    // shrunk inventory. Ask button remains enabled unless intent is empty.
}

void ReorganizeTreeDialog::updateAskButtonEnabled()
{
    if (!m_askButton) {
        return;
    }
    const QString trimmed = m_intentEdit ? m_intentEdit->toPlainText().trimmed() : QString();
    m_askButton->setEnabled(!trimmed.isEmpty());
}

QStringList ReorganizeTreeDialog::intentIncludedWorkdirs() const
{
    QStringList out;
    if (!m_projectsList) {
        return out;
    }
    for (int i = 0; i < m_projectsList->count(); ++i) {
        QListWidgetItem *it = m_projectsList->item(i);
        if (it && it->checkState() == Qt::Checked) {
            const QString wd = it->data(Qt::UserRole).toString();
            if (!wd.isEmpty()) {
                out.append(wd);
            }
        }
    }
    return out;
}

void ReorganizeTreeDialog::ensureAssistant()
{
    if (m_assistant) {
        return;
    }
    m_assistant = new ClaudeAssistant(this);
    m_ownsAssistant = true;
    connect(m_assistant, &ClaudeAssistant::finished, this, &ReorganizeTreeDialog::onAssistantFinished);
    connect(m_assistant, &ClaudeAssistant::failed, this, &ReorganizeTreeDialog::onAssistantFailed);
}

void ReorganizeTreeDialog::setAssistantForTesting(ClaudeAssistant *stub)
{
    if (m_assistant && m_ownsAssistant) {
        m_assistant->deleteLater();
    }
    m_assistant = stub;
    m_ownsAssistant = false;
    if (m_assistant) {
        // Take child ownership for cleanup while keeping m_ownsAssistant=false so
        // the destructor doesn't double-delete a stub owned by a test harness.
        connect(m_assistant, &ClaudeAssistant::finished, this, &ReorganizeTreeDialog::onAssistantFinished);
        connect(m_assistant, &ClaudeAssistant::failed, this, &ReorganizeTreeDialog::onAssistantFailed);
    }
}

void ReorganizeTreeDialog::applyStubProposal(const ReorganizeProposal &proposal)
{
    enterReviewState(proposal);
}

void ReorganizeTreeDialog::askClaude()
{
    const QString intent = m_intentEdit->toPlainText().trimmed();
    if (intent.isEmpty()) {
        return; // guarded by button state, but defensive.
    }

    // Build a filtered inventory using only the checked projects. Categories
    // still keep their full spec — Claude may still reason about them — but
    // the projects list narrows.
    TreeInventory filtered = m_inventory;
    const QStringList includedList = intentIncludedWorkdirs();
    const QSet<QString> included(includedList.begin(), includedList.end());
    filtered.projects.clear();
    for (const TreeInventory::Project &p : m_inventory.projects) {
        if (included.contains(p.workingDirectory)) {
            filtered.projects.append(p);
        }
    }

    m_lastPrompt = buildReorganizePrompt(filtered, intent);
    enterBusyState();
    ensureAssistant();
    m_assistant->ask(m_lastPrompt, /*jsonOutput=*/false);
}

void ReorganizeTreeDialog::onAssistantFinished(const QString &output, int exitCode)
{
    if (exitCode != 0) {
        enterErrorState(i18n("Claude returned exit code %1.", exitCode));
        return;
    }
    QString parseError;
    const ReorganizeProposal proposal = parseReorganizeResponse(output, &parseError);
    if (!parseError.isEmpty() && proposal.isEmpty() && proposal.rationale.isEmpty()) {
        enterErrorState(i18n("Could not parse Claude's response: %1", parseError));
        return;
    }
    enterReviewState(proposal);
}

void ReorganizeTreeDialog::onAssistantFailed(const QString &err)
{
    enterErrorState(i18n("Claude request failed: %1", err));
}

} // namespace Konsolai

#include "moc_ReorganizeTreeDialog.cpp"
