/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AGENTSESSIONLINKERTEST_H
#define AGENTSESSIONLINKERTEST_H

#include <QObject>
#include <QTemporaryDir>

namespace Konsolai
{

class AgentSessionLinkerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    // Basic construction
    void testCreateLinker();

    // agentIdForSession
    void testAgentIdForSession_Empty();
    void testAgentIdForSession_Found();
    void testAgentIdForSession_NotFound();

    // hasDetachedSession
    void testHasDetachedSession_NoSessions();
    void testHasDetachedSession_WithSession();

    // sessionNameForAgent
    void testSessionNameForAgent_NotFound();

    // Signal emission
    void testNotifyChangedSignal();

    // agentId metadata persistence
    void testAgentIdPersistence();

private:
    QTemporaryDir *m_tempDir = nullptr;
    QString m_fleetPath;
};

} // namespace Konsolai

#endif // AGENTSESSIONLINKERTEST_H
