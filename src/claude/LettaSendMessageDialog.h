/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_LETTASENDMESSAGEDIALOG_H
#define KONSOLAI_LETTASENDMESSAGEDIALOG_H

#include "konsoleprivate_export.h"

#include <QDialog>
#include <QPointer>

class QDialogButtonBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace Konsolai
{

class LettaApiClient;

/**
 * Modal dialog for sending a single message to a Letta agent. The dialog
 * captures the user prompt, POSTs it through LettaApiClient::sendMessage,
 * and shows the assistant reply inline. Closing the dialog discards any
 * in-flight response.
 */
class KONSOLEPRIVATE_EXPORT LettaSendMessageDialog : public QDialog
{
    Q_OBJECT

public:
    LettaSendMessageDialog(LettaApiClient *client, const QString &agentId, const QString &agentName, QWidget *parent = nullptr);
    ~LettaSendMessageDialog() override;

private Q_SLOTS:
    void onSendClicked();
    void onMessageReplied(const QString &tag, const QString &assistantText);
    void onRequestFailed(const QString &tag, const QString &endpoint, const QString &error);

private:
    QString m_pendingTag;
    QString m_agentId;
    QPointer<LettaApiClient> m_client;

    QPlainTextEdit *m_promptEdit = nullptr;
    QTextBrowser *m_responseView = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_sendButton = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace Konsolai

#endif // KONSOLAI_LETTASENDMESSAGEDIALOG_H
