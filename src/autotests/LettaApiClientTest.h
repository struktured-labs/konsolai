/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LETTAAPICLIENTTEST_H
#define LETTAAPICLIENTTEST_H

#include <QObject>
#include <QTcpServer>

namespace Konsolai
{

class LettaApiClientTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void testEnvBaseUrlPrecedence();
    void testApiKeyPrecedence();
    void testListAgents_Success();
    void testListAgents_ParsesNameAndModel();
    void testListAgents_HandlesAgentsKeyWrap();
    void testListAgents_ServerError();
    void testListAgents_MalformedJson();
    void testGetMemoryBlocks_Success();
    void testSendMessage_ExtractsAssistantText();
    void testSendMessage_HandlesContentArray();
    void testAuthHeaderPresent();
    void testAuthHeaderAbsentWhenNoKey();

private:
    void startServer(const QByteArray &responseBody, int statusCode = 200);
    QString baseUrl() const;

    QTcpServer *m_server = nullptr;
    QByteArray m_lastRequest;
    QByteArray m_responseBody;
    int m_statusCode = 200;
};

}

#endif // LETTAAPICLIENTTEST_H
