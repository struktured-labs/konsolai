/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LettaApiClientTest.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

#include "../claude/LettaApiClient.h"

using namespace Konsolai;

void LettaApiClientTest::initTestCase()
{
}

void LettaApiClientTest::init()
{
    m_server = new QTcpServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));

    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            m_lastRequest += socket->readAll();

            // Wait for end of headers — auth headers may arrive in a later TCP packet.
            int headerEnd = m_lastRequest.indexOf("\r\n\r\n");
            while (headerEnd < 0 && socket->waitForReadyRead(500)) {
                m_lastRequest += socket->readAll();
                headerEnd = m_lastRequest.indexOf("\r\n\r\n");
            }
            if (headerEnd < 0) {
                return; // give up; test will fail loudly
            }

            // If Content-Length present, wait for the body too.
            const QByteArray headers = m_lastRequest.left(headerEnd);
            const int idx = headers.indexOf("Content-Length:");
            if (idx >= 0) {
                const int valStart = idx + int(strlen("Content-Length:"));
                const int lineEnd = headers.indexOf("\r\n", valStart);
                const int contentLen = headers.mid(valStart, lineEnd - valStart).trimmed().toInt();
                while ((m_lastRequest.size() - headerEnd - 4) < contentLen && socket->waitForReadyRead(500)) {
                    m_lastRequest += socket->readAll();
                }
            }

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

void LettaApiClientTest::cleanup()
{
    delete m_server;
    m_server = nullptr;
    m_lastRequest.clear();
    m_responseBody.clear();
    m_statusCode = 200;
}

void LettaApiClientTest::cleanupTestCase()
{
}

void LettaApiClientTest::startServer(const QByteArray &responseBody, int statusCode)
{
    m_responseBody = responseBody;
    m_statusCode = statusCode;
}

QString LettaApiClientTest::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());
}

void LettaApiClientTest::testEnvBaseUrlPrecedence()
{
    qputenv("LETTA_BASE_URL", "http://env.example.com:9999");
    LettaApiClient client(QStringLiteral("http://ctor.example.com"));
    QCOMPARE(client.baseUrl().toString(), QStringLiteral("http://env.example.com:9999"));
    qunsetenv("LETTA_BASE_URL");

    LettaApiClient client2(QStringLiteral("http://ctor.example.com"));
    QCOMPARE(client2.baseUrl().toString(), QStringLiteral("http://ctor.example.com"));

    LettaApiClient client3;
    QCOMPARE(client3.baseUrl().toString(), QStringLiteral("http://localhost:8283"));
}

void LettaApiClientTest::testApiKeyPrecedence()
{
    qputenv("LETTA_API_KEY", "env-key");
    LettaApiClient client(QString(), QStringLiteral("ctor-key"));
    QVERIFY(client.hasAuth());
    qunsetenv("LETTA_API_KEY");

    LettaApiClient client2(QString(), QStringLiteral("ctor-key"));
    QVERIFY(client2.hasAuth());

    LettaApiClient client3;
    QVERIFY(!client3.hasAuth());
}

void LettaApiClientTest::testListAgents_Success()
{
    startServer(R"([{"id":"a1","name":"first"},{"id":"a2","name":"second"}])");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::agentsListed);
    client.listAgents();
    QVERIFY(spy.wait(2000));
    const auto agents = spy.first().at(1).value<QList<LettaAgentSummary>>();
    QCOMPARE(agents.size(), 2);
    QCOMPARE(agents[0].id, QStringLiteral("a1"));
    QCOMPARE(agents[1].name, QStringLiteral("second"));
}

void LettaApiClientTest::testListAgents_ParsesNameAndModel()
{
    startServer(
        R"([{"id":"a1","name":"foo","llm_config":{"model":"claude-sonnet-4-6"},"agent_type":"memgpt_v2_agent","tools":[{"name":"send_message"},{"name":"core_memory_append"}]}])");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::agentsListed);
    client.listAgents();
    QVERIFY(spy.wait(2000));
    const auto agents = spy.first().at(1).value<QList<LettaAgentSummary>>();
    QCOMPARE(agents.size(), 1);
    QCOMPARE(agents[0].model, QStringLiteral("claude-sonnet-4-6"));
    QCOMPARE(agents[0].agentType, QStringLiteral("memgpt_v2_agent"));
    QCOMPARE(agents[0].tools.size(), 2);
    QCOMPARE(agents[0].tools[0], QStringLiteral("send_message"));
}

void LettaApiClientTest::testListAgents_HandlesAgentsKeyWrap()
{
    // Some Letta versions wrap the array in {"agents": [...]}.
    startServer(R"({"agents":[{"id":"a1","name":"only"}]})");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::agentsListed);
    client.listAgents();
    QVERIFY(spy.wait(2000));
    const auto agents = spy.first().at(1).value<QList<LettaAgentSummary>>();
    QCOMPARE(agents.size(), 1);
    QCOMPARE(agents[0].id, QStringLiteral("a1"));
}

void LettaApiClientTest::testListAgents_ServerError()
{
    startServer("Internal Error", 500);
    LettaApiClient client(baseUrl());
    QSignalSpy okSpy(&client, &LettaApiClient::agentsListed);
    QSignalSpy errSpy(&client, &LettaApiClient::requestFailed);
    client.listAgents();
    QVERIFY(errSpy.wait(2000));
    QCOMPARE(okSpy.count(), 0);
    QCOMPARE(errSpy.first().at(1).toString(), QStringLiteral("listAgents"));
}

void LettaApiClientTest::testListAgents_MalformedJson()
{
    startServer(QByteArrayLiteral("{not json"));
    LettaApiClient client(baseUrl());
    QSignalSpy errSpy(&client, &LettaApiClient::requestFailed);
    client.listAgents();
    QVERIFY(errSpy.wait(2000));
    QVERIFY(errSpy.first().at(2).toString().startsWith(QStringLiteral("Invalid JSON")));
}

void LettaApiClientTest::testGetMemoryBlocks_Success()
{
    startServer(R"([{"label":"persona","value":"a friendly bot","limit":2000},{"label":"human","value":"the user","limit":2000}])");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::memoryFetched);
    client.getMemoryBlocks(QStringLiteral("ag1"));
    QVERIFY(spy.wait(2000));
    const auto blocks = spy.first().at(1).value<QList<LettaMemoryBlock>>();
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].label, QStringLiteral("persona"));
    QCOMPARE(blocks[0].limit, 2000);
    QCOMPARE(blocks[1].value, QStringLiteral("the user"));
}

void LettaApiClientTest::testSendMessage_ExtractsAssistantText()
{
    startServer(R"({"messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"hello!"}]})");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::messageReplied);
    client.sendMessage(QStringLiteral("ag1"), QStringLiteral("hi"));
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("hello!"));
    QVERIFY(m_lastRequest.contains("\"role\":\"user\""));
    QVERIFY(m_lastRequest.contains("\"content\":\"hi\""));
}

void LettaApiClientTest::testSendMessage_HandlesContentArray()
{
    startServer(R"({"messages":[{"role":"assistant","content":[{"type":"text","text":"part one"},{"type":"text","text":"part two"}]}]})");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::messageReplied);
    client.sendMessage(QStringLiteral("ag1"), QStringLiteral("hi"));
    QVERIFY(spy.wait(2000));
    const QString reply = spy.first().at(1).toString();
    QVERIFY(reply.contains(QStringLiteral("part one")));
    QVERIFY(reply.contains(QStringLiteral("part two")));
}

void LettaApiClientTest::testAuthHeaderPresent()
{
    qunsetenv("LETTA_API_KEY");
    startServer("[]");
    LettaApiClient client(baseUrl(), QStringLiteral("sk-test-1234"));
    QVERIFY(client.hasAuth());
    QSignalSpy spy(&client, &LettaApiClient::agentsListed);
    client.listAgents();
    QVERIFY(spy.wait(2000));
    // Qt 6 normalizes header names to lowercase on the wire.
    const QByteArray lower = m_lastRequest.toLower();
    QVERIFY(lower.contains("authorization: bearer sk-test-1234"));
}

void LettaApiClientTest::testAuthHeaderAbsentWhenNoKey()
{
    qunsetenv("LETTA_API_KEY");
    startServer("[]");
    LettaApiClient client(baseUrl());
    QSignalSpy spy(&client, &LettaApiClient::agentsListed);
    client.listAgents();
    QVERIFY(spy.wait(2000));
    QVERIFY(!m_lastRequest.toLower().contains("authorization:"));
}

QTEST_GUILESS_MAIN(LettaApiClientTest)

#include "moc_LettaApiClientTest.cpp"
