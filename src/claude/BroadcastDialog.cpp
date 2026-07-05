/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BroadcastDialog.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace Konsolai
{

namespace
{

// Display the row text in the recipient list as `<category>/<suffix> — "<displayName>"`,
// where `<suffix>` is the workdir's basename with a leading `<category>-` stripped
// off (e.g. category="cowir", basename="cowir-pcc-base" → suffix "pcc-base"). If the
// basename equals the category (or there's no useful suffix), drop the slash.
QString rowText(const BroadcastRecipient &r)
{
    const QString basename = QDir(r.workingDirectory).dirName();
    const QString category = r.category;
    QString header;
    if (basename.isEmpty()) {
        header = category;
    } else if (basename == category) {
        header = category;
    } else if (!category.isEmpty() && basename.startsWith(category + QLatin1Char('-'))) {
        header = category + QLatin1Char('/') + basename.mid(category.size() + 1);
    } else if (category.isEmpty()) {
        header = basename;
    } else {
        header = category + QLatin1Char('/') + basename;
    }
    return QStringLiteral("%1 — \"%2\"").arg(header, r.displayName);
}

} // namespace

BroadcastDialog::BroadcastDialog(const QList<BroadcastRecipient> &candidates, QWidget *parent)
    : QDialog(parent)
    , m_candidates(candidates)
{
    setObjectName(QStringLiteral("broadcastDialog"));
    setWindowTitle(i18n("Broadcast Message"));
    buildUi();
    populateRecipients();
    updatePreview();
}

QStringList BroadcastDialog::selectedSessionIds() const
{
    QStringList ids;
    if (!m_recipientsList) {
        return ids;
    }
    for (int i = 0; i < m_recipientsList->count(); ++i) {
        auto *item = m_recipientsList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            ids.append(item->data(Qt::UserRole).toString());
        }
    }
    return ids;
}

QString BroadcastDialog::messageTemplate() const
{
    return m_messageEdit ? m_messageEdit->toPlainText() : QString();
}

bool BroadcastDialog::pressEnterAfterEach() const
{
    return m_pressEnterCheck && m_pressEnterCheck->isChecked();
}

QStringList BroadcastDialog::substitutedMessages() const
{
    QStringList out;
    if (!m_recipientsList) {
        return out;
    }
    // Build vars per checked recipient using its position among checked items.
    QList<int> checkedCandidateIndexes;
    for (int i = 0; i < m_recipientsList->count(); ++i) {
        auto *item = m_recipientsList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            checkedCandidateIndexes.append(i);
        }
    }
    const QString tmpl = messageTemplate();
    const int total = checkedCandidateIndexes.size();
    for (int pos = 0; pos < total; ++pos) {
        const int candIdx = checkedCandidateIndexes[pos];
        const BroadcastVars vars = varsForIndex(candIdx, pos + 1, total);
        out.append(substituteTemplate(tmpl, vars));
    }
    return out;
}

void BroadcastDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_recipientsHeader = new QLabel(this);
    m_recipientsHeader->setObjectName(QStringLiteral("broadcastRecipientsHeader"));
    root->addWidget(m_recipientsHeader);

    m_recipientsList = new QListWidget(this);
    m_recipientsList->setObjectName(QStringLiteral("broadcastRecipientsList"));
    m_recipientsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_recipientsList->setMinimumHeight(100);
    root->addWidget(m_recipientsList);

    // Message label + help button row
    auto *messageHeaderRow = new QHBoxLayout();
    auto *messageLabel = new QLabel(i18n("Message (use {session_name}, {project}, ... — ? for full list):"), this);
    messageHeaderRow->addWidget(messageLabel, /*stretch=*/1);
    m_helpButton = new QToolButton(this);
    m_helpButton->setObjectName(QStringLiteral("broadcastHelpButton"));
    m_helpButton->setText(QStringLiteral("?"));
    m_helpButton->setToolTip(i18n("Insert a template variable"));
    m_helpButton->setPopupMode(QToolButton::InstantPopup);
    messageHeaderRow->addWidget(m_helpButton);
    root->addLayout(messageHeaderRow);

    m_messageEdit = new QPlainTextEdit(this);
    m_messageEdit->setObjectName(QStringLiteral("broadcastMessageEdit"));
    m_messageEdit->setPlaceholderText(i18n("Type a message — {session_name} substitutes per recipient."));
    // Aim for ~5 lines minimum.
    const QFontMetrics fm(m_messageEdit->font());
    m_messageEdit->setMinimumHeight(fm.lineSpacing() * 5 + 16);
    root->addWidget(m_messageEdit);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setObjectName(QStringLiteral("broadcastPreviewLabel"));
    root->addWidget(m_previewLabel);

    m_previewView = new QTextEdit(this);
    m_previewView->setObjectName(QStringLiteral("broadcastPreviewView"));
    m_previewView->setReadOnly(true);
    m_previewView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_previewView->setMinimumHeight(fm.lineSpacing() * 3 + 16);
    root->addWidget(m_previewView);

    m_pressEnterCheck = new QCheckBox(i18n("Press Enter after sending each (recommended on)"), this);
    m_pressEnterCheck->setObjectName(QStringLiteral("broadcastPressEnterCheck"));
    m_pressEnterCheck->setChecked(true);
    root->addWidget(m_pressEnterCheck);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancelButton = new QPushButton(i18n("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("broadcastCancelButton"));
    m_broadcastButton = new QPushButton(i18n("Broadcast"), this);
    m_broadcastButton->setObjectName(QStringLiteral("broadcastConfirmButton"));
    m_broadcastButton->setDefault(true);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_broadcastButton);
    root->addLayout(buttons);

    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_broadcastButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_messageEdit, &QPlainTextEdit::textChanged, this, &BroadcastDialog::updatePreview);
    connect(m_recipientsList, &QListWidget::itemChanged, this, &BroadcastDialog::updatePreview);
    connect(m_helpButton, &QToolButton::clicked, this, &BroadcastDialog::onHelpButtonClicked);

    // Build the help menu eagerly so the popup is available on first click.
    auto *menu = new QMenu(m_helpButton);
    menu->setObjectName(QStringLiteral("broadcastHelpMenu"));
    const QStringList names = templateVariableNames();
    for (const QString &name : names) {
        QAction *act = menu->addAction(QStringLiteral("{%1}").arg(name));
        connect(act, &QAction::triggered, this, [this, name]() {
            insertVariableAtCursor(name);
        });
    }
    m_helpButton->setMenu(menu);
}

void BroadcastDialog::populateRecipients()
{
    if (!m_recipientsList) {
        return;
    }
    m_recipientsList->clear();
    for (const auto &r : m_candidates) {
        auto *item = new QListWidgetItem(rowText(r));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, r.sessionId);
        item->setToolTip(r.workingDirectory);
        m_recipientsList->addItem(item);
    }
}

int BroadcastDialog::countChecked() const
{
    if (!m_recipientsList) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < m_recipientsList->count(); ++i) {
        auto *item = m_recipientsList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            ++n;
        }
    }
    return n;
}

int BroadcastDialog::firstCheckedIndex() const
{
    if (!m_recipientsList) {
        return -1;
    }
    for (int i = 0; i < m_recipientsList->count(); ++i) {
        auto *item = m_recipientsList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            return i;
        }
    }
    return -1;
}

BroadcastVars BroadcastDialog::varsForIndex(int candidateIndex, int recipientIndex, int recipientCount) const
{
    if (candidateIndex < 0 || candidateIndex >= m_candidates.size()) {
        return {};
    }
    const BroadcastRecipient &r = m_candidates[candidateIndex];
    BroadcastVars v;
    v.sessionId = r.sessionId;
    v.sessionName = r.displayName;
    v.project = r.category;
    v.workingDirectory = r.workingDirectory;
    v.tmuxSession = r.tmuxSession;
    v.index = recipientIndex;
    v.count = recipientCount;
    return v;
}

void BroadcastDialog::updatePreview()
{
    if (!m_recipientsHeader || !m_previewLabel || !m_previewView || !m_broadcastButton) {
        return;
    }

    const int total = countChecked();
    m_recipientsHeader->setText(i18n("Broadcast to %1 active session(s)", total));

    if (total == 0) {
        m_previewLabel->setText(i18n("Preview (no recipients selected):"));
        m_previewView->setPlainText(i18n("(no recipients selected)"));
        m_broadcastButton->setEnabled(false);
        return;
    }

    const int firstIdx = firstCheckedIndex();
    if (firstIdx < 0 || firstIdx >= m_candidates.size()) {
        m_previewView->setPlainText(QString());
        m_broadcastButton->setEnabled(false);
        return;
    }

    const BroadcastRecipient &first = m_candidates[firstIdx];
    m_previewLabel->setText(i18n("Preview (recipient 1/%1, \"%2\"):", total, first.displayName));

    const BroadcastVars vars = varsForIndex(firstIdx, /*recipientIndex=*/1, total);
    m_previewView->setPlainText(substituteTemplate(messageTemplate(), vars));
    m_broadcastButton->setEnabled(true);
}

void BroadcastDialog::onHelpButtonClicked()
{
    // QToolButton::InstantPopup already shows the menu; this slot exists as a
    // defensive fallback if the popup mode is overridden in tests.
    if (m_helpButton && m_helpButton->menu()) {
        m_helpButton->showMenu();
    }
}

void BroadcastDialog::insertVariableAtCursor(const QString &name)
{
    if (!m_messageEdit) {
        return;
    }
    const QString token = QStringLiteral("{%1}").arg(name);
    m_messageEdit->insertPlainText(token);
    m_messageEdit->setFocus();
}

} // namespace Konsolai
