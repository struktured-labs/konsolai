/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LETTAAGENTPROVIDERTEST_H
#define LETTAAGENTPROVIDERTEST_H

#include <QObject>
#include <QTcpServer>

namespace Konsolai
{

class LettaAgentProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testIdentity();
    void testInterfaceVersion();
    void testIsAvailableAlwaysTrue();
    void testAgents_EmptyOnConstruction();
    void testAgents_PopulatedAfterPoll();
    void testAgentsReloaded_OnRefresh();
    void testTriggerRun_PostsMessage();
    void testTriggerRun_EmptyTaskRejected();
    void testLastError_PopulatedOnFailure();
    void testReadOnlyMutations_AllReturnFalse();

private:
    QString baseUrl() const;

    QTcpServer *m_server = nullptr;
    QByteArray m_responseBody;
    QByteArray m_lastRequest;
    int m_statusCode = 200;
};

}

#endif // LETTAAGENTPROVIDERTEST_H
