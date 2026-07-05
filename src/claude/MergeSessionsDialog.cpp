/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MergeSessionsDialog.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace Konsolai
{

namespace
{

QString formatBytes(qint64 size)
{
    if (size <= 0) {
        return QStringLiteral("0 B");
    }
    if (size < 1024) {
        return QStringLiteral("%1 B").arg(size);
    }
    if (size < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(QString::number(size / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(size / (1024.0 * 1024.0), 'f', 1));
}

QString relativeTimeText(const QDateTime &when)
{
    if (!when.isValid()) {
        return QStringLiteral("never");
    }
    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return QStringLiteral("just now");
    }
    if (secs < 3600) {
        return QStringLiteral("%1m ago").arg(secs / 60);
    }
    if (secs < 86400) {
        return QStringLiteral("%1h ago").arg(secs / 3600);
    }
    return QStringLiteral("%1d ago").arg(secs / 86400);
}

} // namespace

MergeSessionsDialog::MergeSessionsDialog(const QList<SessionMetadata> &candidates, const QHash<QString, qint64> &jsonlSizes, QWidget *parent)
    : QDialog(parent)
    , m_candidates(candidates)
    , m_jsonlSizes(jsonlSizes)
{
    setObjectName(QStringLiteral("mergeSessionsDialog"));
    setWindowTitle(i18n("Merge Sessions"));
    buildUi();
    rebuildPrimaryCombo();
    refreshSummary();
}

QString MergeSessionsDialog::primarySessionId() const
{
    if (!m_primaryCombo) {
        return {};
    }
    return m_primaryCombo->currentData().toString();
}

MergeFieldChoices MergeSessionsDialog::choices() const
{
    return readChoices();
}

QString MergeSessionsDialog::displayNameFor(const SessionMetadata &meta) const
{
    QString name;
    if (!meta.description.trimmed().isEmpty()) {
        name = meta.description.trimmed();
    } else if (!meta.workingDirectory.isEmpty()) {
        name = QDir(meta.workingDirectory).dirName();
    }
    if (name.isEmpty()) {
        name = meta.sessionName;
    }
    if (name.length() > 60) {
        name = name.left(57) + QStringLiteral("...");
    }
    return name;
}

void MergeSessionsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(i18n("Merge these sessions into a single primary. Other sessions will be dismissed but reversible."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    // Primary chooser
    auto *primaryRow = new QHBoxLayout();
    auto *primaryLabel = new QLabel(i18n("Primary:"), this);
    m_primaryCombo = new QComboBox(this);
    m_primaryCombo->setObjectName(QStringLiteral("mergeDialogPrimaryCombo"));
    primaryRow->addWidget(primaryLabel);
    primaryRow->addWidget(m_primaryCombo, /*stretch=*/1);
    root->addLayout(primaryRow);

    connect(m_primaryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshSummary();
    });

    // Inheritance choices
    auto *group = new QGroupBox(i18n("Inherit from others"), this);
    auto *grid = new QVBoxLayout(group);

    auto addCheck = [this, grid](QCheckBox *&target, const QString &objName, const QString &label, bool defaultChecked) {
        target = new QCheckBox(label, this);
        target->setObjectName(objName);
        target->setChecked(defaultChecked);
        grid->addWidget(target);
        connect(target, &QCheckBox::toggled, this, [this](bool) {
            refreshSummary();
        });
    };

    const MergeFieldChoices defaults;
    addCheck(m_inheritDescription, QStringLiteral("mergeDialogInheritDescription"), i18n("Description (longest wins)"), defaults.inheritDescription);
    addCheck(m_inheritPinned, QStringLiteral("mergeDialogInheritPinned"), i18n("Pinned (OR across sessions)"), defaults.inheritPinned);
    addCheck(m_inheritYolo, QStringLiteral("mergeDialogInheritYolo"), i18n("Yolo mode (OR; double-yolo follows)"), defaults.inheritYolo);
    addCheck(m_inheritLastAccessed, QStringLiteral("mergeDialogInheritLastAccessed"), i18n("Last accessed (max)"), defaults.inheritLastAccessed);
    addCheck(m_inheritResumeConversation,
             QStringLiteral("mergeDialogInheritResumeConversation"),
             i18n("Resume conversation (pick largest .jsonl)"),
             defaults.inheritResumeConversation);
    addCheck(m_inheritApprovalLog, QStringLiteral("mergeDialogInheritApprovalLog"), i18n("Approval log (merge + dedupe)"), defaults.inheritApprovalLog);
    addCheck(m_inheritApprovalCounts, QStringLiteral("mergeDialogInheritApprovalCounts"), i18n("Approval counts (sum)"), defaults.inheritApprovalCounts);
    addCheck(m_inheritPromptRound, QStringLiteral("mergeDialogInheritPromptRound"), i18n("Current prompt round (max)"), defaults.inheritPromptRound);
    addCheck(m_inheritBudget, QStringLiteral("mergeDialogInheritBudget"), i18n("Budget caps (take max — off by default)"), defaults.inheritBudget);

    root->addWidget(group);

    // Summary
    auto *summaryLabel = new QLabel(i18n("Preview:"), this);
    root->addWidget(summaryLabel);
    m_summary = new QPlainTextEdit(this);
    m_summary->setObjectName(QStringLiteral("mergeDialogSummaryView"));
    m_summary->setReadOnly(true);
    m_summary->setMinimumHeight(160);
    root->addWidget(m_summary, /*stretch=*/1);

    // Buttons
    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancelButton = new QPushButton(i18n("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("mergeDialogCancelButton"));
    m_confirmButton = new QPushButton(i18n("Merge"), this);
    m_confirmButton->setObjectName(QStringLiteral("mergeDialogConfirmButton"));
    m_confirmButton->setDefault(true);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_confirmButton);
    root->addLayout(buttons);

    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_confirmButton, &QPushButton::clicked, this, &QDialog::accept);
}

void MergeSessionsDialog::rebuildPrimaryCombo()
{
    // Default selection: candidate with the most recent lastAccessed.
    int defaultIndex = 0;
    QDateTime bestTime;
    for (int i = 0; i < m_candidates.size(); ++i) {
        const auto &meta = m_candidates[i];
        const QString label = QStringLiteral("%1  —  %2").arg(displayNameFor(meta), relativeTimeText(meta.lastAccessed));
        m_primaryCombo->addItem(label, meta.sessionId);
        if (meta.lastAccessed.isValid() && (!bestTime.isValid() || meta.lastAccessed > bestTime)) {
            bestTime = meta.lastAccessed;
            defaultIndex = i;
        }
    }
    m_primaryCombo->setCurrentIndex(defaultIndex);
}

MergeFieldChoices MergeSessionsDialog::readChoices() const
{
    MergeFieldChoices c;
    c.inheritDescription = m_inheritDescription->isChecked();
    c.inheritPinned = m_inheritPinned->isChecked();
    c.inheritYolo = m_inheritYolo->isChecked();
    c.inheritLastAccessed = m_inheritLastAccessed->isChecked();
    c.inheritResumeConversation = m_inheritResumeConversation->isChecked();
    c.inheritApprovalLog = m_inheritApprovalLog->isChecked();
    c.inheritApprovalCounts = m_inheritApprovalCounts->isChecked();
    c.inheritPromptRound = m_inheritPromptRound->isChecked();
    c.inheritBudget = m_inheritBudget->isChecked();
    return c;
}

void MergeSessionsDialog::refreshSummary()
{
    if (!m_summary || m_candidates.isEmpty()) {
        return;
    }

    const QString primaryId = primarySessionId();
    SessionMetadata primary;
    QList<SessionMetadata> others;
    QStringList otherNames;
    for (const auto &meta : m_candidates) {
        if (meta.sessionId == primaryId) {
            primary = meta;
        } else {
            others.append(meta);
            otherNames.append(displayNameFor(meta));
        }
    }

    const MergeFieldChoices ch = readChoices();
    const SessionMetadata merged = applyMerge(primary, others, ch, m_jsonlSizes);

    QString text;
    text += QStringLiteral("Primary: %1\n").arg(displayNameFor(primary));
    text += QStringLiteral("Description: %1\n").arg(merged.description.isEmpty() ? QStringLiteral("(none)") : merged.description);
    text += QStringLiteral("Pinned: %1\n").arg(merged.isPinned ? QStringLiteral("yes") : QStringLiteral("no"));
    text += QStringLiteral("Yolo: %1%2\n")
                .arg(merged.yoloMode ? QStringLiteral("yes") : QStringLiteral("no"), merged.doubleYoloMode ? QStringLiteral(" (double)") : QString());
    text += QStringLiteral("Last accessed: %1\n")
                .arg(merged.lastAccessed.isValid() ? merged.lastAccessed.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("never"));
    {
        const qint64 size = m_jsonlSizes.value(merged.lastResumeSessionId, 0);
        text += QStringLiteral("Resume conv: %1 (%2)\n")
                    .arg(merged.lastResumeSessionId.isEmpty() ? QStringLiteral("(none)") : merged.lastResumeSessionId, formatBytes(size));
    }
    text += QStringLiteral("Approval log: %1 entries\n").arg(merged.approvalLog.size());
    text += QStringLiteral("Approval counts: %1 / %2 (yolo / double)\n").arg(merged.yoloApprovalCount).arg(merged.doubleYoloApprovalCount);
    text += QStringLiteral("Prompt round: %1\n").arg(merged.currentPromptRound);
    if (ch.inheritBudget) {
        text += QStringLiteral("Budget: time=%1m cost=$%2 tokens=%3\n")
                    .arg(merged.budgetTimeLimitMinutes)
                    .arg(QString::number(merged.budgetCostCeilingUSD, 'f', 2))
                    .arg(merged.budgetTokenCeiling);
    } else {
        text += QStringLiteral("Budget: primary's stays\n");
    }

    text += QStringLiteral("\nOthers (%1): %2\n").arg(otherNames.size()).arg(otherNames.join(QStringLiteral(", ")));
    text += QStringLiteral("\xE2\x86\x92 marked dismissed, mergedInto = %1\n").arg(primary.sessionId);

    m_summary->setPlainText(text);
}

} // namespace Konsolai
