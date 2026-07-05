/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LettaAgentProviderTest.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

#include "../claude/LettaAgentProvider.h"

using namespace Konsolai;

void LettaAgentProviderTest::initTestCase()
{
    qunsetenv("LETTA_BASE_URL");
    qunsetenv("LETTA_API_KEY");
}

void LettaAgentProviderTest::init()
{
    m_server = new QTcpServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));
    m_responseBody = QByteArrayLiteral("[]");
    m_statusCode = 200;

    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            m_lastRequest = socket->readAll();
            const QByteArray statusLine = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(m_statusCode) + " OK\r\n";
            QByteArray response = statusLine;
            response += "Content-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(m_responseBody.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += m_responseBody;
            socket->write(response);
            socket->disconnectFromHost();
        });
    });
}

void LettaAgentProviderTest::cleanup()
{
    delete m_server;
    m_server = nullptr;
    m_lastRequest.clear();
    m_responseBody.clear();
}

QString LettaAgentProviderTest::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());
}

void LettaAgentProviderTest::testIdentity()
{
    LettaAgentProvider provider(baseUrl());
    QCOMPARE(provider.name(), QStringLiteral("letta"));
}

void LettaAgentProviderTest::testInterfaceVersion()
{
    LettaAgentProvider provider(baseUrl());
    QCOMPARE(provider.interfaceVersion(), 2);
}

void LettaAgentProviderTest::testIsAvailableAlwaysTrue()
{
    // Provider should advertise as available regardless of server state so the
    // group node is visible. Error surfaces via lastError().
    LettaAgentProvider provider(QStringLiteral("http://127.0.0.1:1"));
    QVERIFY(provider.isAvailable());
}

void LettaAgentProviderTest::testAgents_EmptyOnConstruction()
{
    LettaAgentProvider provider(baseUrl());
    QCOMPARE(provider.agents().size(), 0);
}

void LettaAgentProviderTest::testAgents_PopulatedAfterPoll()
{
    m_responseBody = R"([{"id":"a1","name":"foo","llm_config":{"model":"claude-haiku-4-5"}}])";
    LettaAgentProvider provider(baseUrl());
    QSignalSpy spy(&provider, &LettaAgentProvider::agentsReloaded);
    QVERIFY(spy.wait(3000));
    const auto agents = provider.agents();
    QCOMPARE(agents.size(), 1);
    QCOMPARE(agents[0].id, QStringLiteral("a1"));
    QCOMPARE(agents[0].name, QStringLiteral("foo"));
    QCOMPARE(agents[0].budget.model, QStringLiteral("claude-haiku-4-5"));
}

void LettaAgentProviderTest::testAgentsReloaded_OnRefresh()
{
    m_responseBody = R"([{"id":"a1","name":"foo"}])";
    LettaAgentProvider provider(baseUrl());
    QSignalSpy spy(&provider, &LettaAgentProvider::agentsReloaded);
    QVERIFY(spy.wait(2000));
    spy.clear();

    m_responseBody = R"([{"id":"a1","name":"foo"},{"id":"a2","name":"bar"}])";
    provider.refresh();
    QVERIFY(spy.wait(2000));
    QCOMPARE(provider.agents().size(), 2);
}

void LettaAgentProviderTest::testTriggerRun_PostsMessage()
{
    m_responseBody = R"({"messages":[{"role":"assistant","content":"ack"}]})";
    LettaAgentProvider provider(baseUrl());
    QSignalSpy changeSpy(&provider, &LettaAgentProvider::agentChanged);
    QVERIFY(provider.triggerRun(QStringLiteral("a1"), QStringLiteral("hello")));
    QVERIFY(changeSpy.wait(2000));
    QVERIFY(m_lastRequest.contains("POST"));
    QVERIFY(m_lastRequest.contains("/v1/agents/a1/messages"));
    const AgentRunResult result = provider.lastResult(QStringLiteral("a1"));
    QCOMPARE(result.fullOutput, QStringLiteral("ack"));
}

void LettaAgentProviderTest::testTriggerRun_EmptyTaskRejected()
{
    LettaAgentProvider provider(baseUrl());
    QVERIFY(!provider.triggerRun(QStringLiteral("a1"), QString()));
    QVERIFY(!provider.triggerRun(QString(), QStringLiteral("hi")));
}

void LettaAgentProviderTest::testLastError_PopulatedOnFailure()
{
    m_statusCode = 500;
    m_responseBody = QByteArrayLiteral("server error");
    LettaAgentProvider provider(baseUrl());
    QSignalSpy spy(&provider, &LettaAgentProvider::agentsReloaded);
    QVERIFY(spy.wait(2000));
    QVERIFY(!provider.lastError().isEmpty());
    QVERIFY(provider.lastError().startsWith(QStringLiteral("listAgents")));
}

void LettaAgentProviderTest::testReadOnlyMutations_AllReturnFalse()
{
    LettaAgentProvider provider(baseUrl());
    QVERIFY(!provider.setBrief(QStringLiteral("a"), QStringLiteral("d")));
    QVERIFY(!provider.addSteeringNote(QStringLiteral("a"), QStringLiteral("n")));
    QVERIFY(!provider.markBriefDone(QStringLiteral("a")));
    QVERIFY(!provider.deleteAgent(QStringLiteral("a")));
    QVERIFY(!provider.resetSession(QStringLiteral("a")));

    AgentConfig cfg;
    QVERIFY(!provider.createAgent(cfg));
    QVERIFY(!provider.updateAgent(QStringLiteral("a"), cfg));
}

QTEST_GUILESS_MAIN(LettaAgentProviderTest)

#include "moc_LettaAgentProviderTest.cpp"
