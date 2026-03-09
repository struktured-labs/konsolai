/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AgentSessionLinkerTest.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "../claude/AgentFleetProvider.h"
#include "../claude/AgentManagerPanel.h"
#include "../claude/AgentSessionLinker.h"
#include "../claude/SessionManagerPanel.h"

using namespace Konsolai;

// Helper to inject metadata directly into the session panel's persisted store.
// Writes a sessions.json file that loadMetadata() will pick up on construction.
static void writeSessionMetadata(const QString &sessionId, const QString &sessionName, const QString &agentId)
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    QString filePath = dataPath + QStringLiteral("/sessions.json");

    // Read existing
    QJsonArray array;
    QFile readFile(filePath);
    if (readFile.open(QIODevice::ReadOnly)) {
        array = QJsonDocument::fromJson(readFile.readAll()).array();
        readFile.close();
    }

    QJsonObject obj;
    obj[QStringLiteral("sessionId")] = sessionId;
    obj[QStringLiteral("sessionName")] = sessionName;
    obj[QStringLiteral("workingDirectory")] = QDir::tempPath();
    obj[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    obj[QStringLiteral("createdAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (!agentId.isEmpty()) {
        obj[QStringLiteral("agentId")] = agentId;
    }
    array.append(obj);

    QFile writeFile(filePath);
    QVERIFY(writeFile.open(QIODevice::WriteOnly));
    writeFile.write(QJsonDocument(array).toJson());
    writeFile.close();
}

static void clearSessionMetadata()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile::remove(dataPath + QStringLiteral("/sessions.json"));
}

void AgentSessionLinkerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AgentSessionLinkerTest::cleanup()
{
    clearSessionMetadata();

    delete m_tempDir;
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    m_fleetPath = m_tempDir->path() + QStringLiteral("/fleet");
    QDir().mkpath(m_fleetPath + QStringLiteral("/goals"));
}

void AgentSessionLinkerTest::cleanupTestCase()
{
    clearSessionMetadata();
    delete m_tempDir;
    m_tempDir = nullptr;
}

void AgentSessionLinkerTest::testCreateLinker()
{
    SessionManagerPanel sessions;
    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);
    QVERIFY(!linker.hasActiveTab(QStringLiteral("test")));
    QVERIFY(!linker.hasDetachedSession(QStringLiteral("test")));
    QVERIFY(linker.agentIdForSession(QStringLiteral("test")).isEmpty());
}

void AgentSessionLinkerTest::testAgentIdForSession_Empty()
{
    SessionManagerPanel sessions;
    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);
    QVERIFY(linker.agentIdForSession(QString()).isEmpty());
    QVERIFY(linker.agentIdForSession(QStringLiteral("nonexistent")).isEmpty());
}

void AgentSessionLinkerTest::testAgentIdForSession_Found()
{
    // Write metadata with agentId, then construct panel (which loads it)
    writeSessionMetadata(QStringLiteral("sess-abc"), QStringLiteral("af-fleet-ops"), QStringLiteral("fleet-ops"));

    SessionManagerPanel sessions;
    QTest::qWait(100); // Allow deferred init

    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);

    QCOMPARE(linker.agentIdForSession(QStringLiteral("sess-abc")), QStringLiteral("fleet-ops"));
}

void AgentSessionLinkerTest::testAgentIdForSession_NotFound()
{
    writeSessionMetadata(QStringLiteral("sess-xyz"), QStringLiteral("konsolai-default-test"), QString());

    SessionManagerPanel sessions;
    QTest::qWait(100);

    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);

    // Session exists but has no agentId
    QVERIFY(linker.agentIdForSession(QStringLiteral("sess-xyz")).isEmpty());
}

void AgentSessionLinkerTest::testHasDetachedSession_NoSessions()
{
    SessionManagerPanel sessions;
    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);
    QVERIFY(!linker.hasDetachedSession(QStringLiteral("fleet-ops")));
}

void AgentSessionLinkerTest::testHasDetachedSession_WithSession()
{
    // A session with agentId but NOT in activeSessions → detached
    writeSessionMetadata(QStringLiteral("sess-det"), QStringLiteral("af-fleet-ops"), QStringLiteral("fleet-ops"));

    SessionManagerPanel sessions;
    QTest::qWait(100);

    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);

    // Session metadata exists with agentId, but no active session → detached
    QVERIFY(linker.hasDetachedSession(QStringLiteral("fleet-ops")));
    // Not active (no tab open)
    QVERIFY(!linker.hasActiveTab(QStringLiteral("fleet-ops")));
}

void AgentSessionLinkerTest::testSessionNameForAgent_NotFound()
{
    SessionManagerPanel sessions;
    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);
    QVERIFY(linker.sessionNameForAgent(QStringLiteral("nonexistent")).isEmpty());
}

void AgentSessionLinkerTest::testNotifyChangedSignal()
{
    SessionManagerPanel sessions;
    AgentManagerPanel agents;
    AgentSessionLinker linker(&sessions, &agents);
    QSignalSpy spy(&linker, &AgentSessionLinker::agentTabPresenceChanged);

    linker.notifyChanged(QStringLiteral("test-agent"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("test-agent"));
    QCOMPARE(spy.at(0).at(1).toBool(), false); // No active tab
}

void AgentSessionLinkerTest::testAgentIdPersistence()
{
    // Write metadata with agentId, read it back
    writeSessionMetadata(QStringLiteral("sess-per"), QStringLiteral("af-persist"), QStringLiteral("persist-agent"));

    SessionManagerPanel sessions;
    QTest::qWait(100);

    const auto *meta = sessions.sessionMetadata(QStringLiteral("sess-per"));
    QVERIFY(meta);
    QCOMPARE(meta->agentId, QStringLiteral("persist-agent"));

    // Verify it round-trips through metadata correctly
    SessionMetadata newMeta;
    QVERIFY(newMeta.agentId.isEmpty());
    newMeta.agentId = QStringLiteral("test-id");
    QCOMPARE(newMeta.agentId, QStringLiteral("test-id"));
}

QTEST_MAIN(AgentSessionLinkerTest)

#include "moc_AgentSessionLinkerTest.cpp"
