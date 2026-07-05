/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LettaSendMessageDialog.h"
#include "LettaApiClient.h"

#include <KLocalizedString>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QUuid>
#include <QVBoxLayout>

namespace Konsolai
{

LettaSendMessageDialog::LettaSendMessageDialog(LettaApiClient *client, const QString &agentId, const QString &agentName, QWidget *parent)
    : QDialog(parent)
    , m_agentId(agentId)
    , m_client(client)
{
    setWindowTitle(i18n("Send Message — %1", agentName.isEmpty() ? agentId : agentName));
    resize(540, 480);

    auto *layout = new QVBoxLayout(this);

    auto *promptLabel = new QLabel(i18n("Message:"), this);
    layout->addWidget(promptLabel);

    m_promptEdit = new QPlainTextEdit(this);
    m_promptEdit->setObjectName(QStringLiteral("lettaPromptEdit"));
    m_promptEdit->setPlaceholderText(i18n("Type a message to send to the Letta agent..."));
    layout->addWidget(m_promptEdit, 1);

    auto *responseLabel = new QLabel(i18n("Response:"), this);
    layout->addWidget(responseLabel);

    m_responseView = new QTextBrowser(this);
    m_responseView->setObjectName(QStringLiteral("lettaResponseView"));
    layout->addWidget(m_responseView, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("lettaStatusLabel"));
    layout->addWidget(m_statusLabel);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_sendButton = m_buttons->addButton(i18n("Send"), QDialogButtonBox::ActionRole);
    m_sendButton->setObjectName(QStringLiteral("lettaSendButton"));
    m_sendButton->setDefault(true);
    connect(m_sendButton, &QPushButton::clicked, this, &LettaSendMessageDialog::onSendClicked);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    if (m_client) {
        connect(m_client, &LettaApiClient::messageReplied, this, &LettaSendMessageDialog::onMessageReplied);
        connect(m_client, &LettaApiClient::requestFailed, this, &LettaSendMessageDialog::onRequestFailed);
    }
}

LettaSendMessageDialog::~LettaSendMessageDialog() = default;

void LettaSendMessageDialog::onSendClicked()
{
    if (!m_client) {
        m_statusLabel->setText(i18n("No API client available."));
        return;
    }
    const QString text = m_promptEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_statusLabel->setText(i18n("Message is empty."));
        return;
    }
    m_pendingTag = QStringLiteral("dlg:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sendButton->setEnabled(false);
    m_statusLabel->setText(i18n("Sending..."));
    m_client->sendMessage(m_agentId, text, m_pendingTag);
}

void LettaSendMessageDialog::onMessageReplied(const QString &tag, const QString &assistantText)
{
    if (tag != m_pendingTag) {
        return;
    }
    m_pendingTag.clear();
    m_sendButton->setEnabled(true);
    m_statusLabel->setText(i18n("Response received."));
    m_responseView->setPlainText(assistantText);
}

void LettaSendMessageDialog::onRequestFailed(const QString &tag, const QString &endpoint, const QString &error)
{
    if (tag != m_pendingTag) {
        return;
    }
    m_pendingTag.clear();
    m_sendButton->setEnabled(true);
    m_statusLabel->setText(i18n("Error (%1): %2", endpoint, error));
}

} // namespace Konsolai

#include "moc_LettaSendMessageDialog.cpp"
