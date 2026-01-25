/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEHOOKHANDLERTEST_H
#define CLAUDEHOOKHANDLERTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeHookHandlerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Socket path tests
    void testSocketPath();
    void testSessionDataDir();

    // Server lifecycle tests
    void testStartStop();
    void testIsRunning();
    void testStartTwice();

    // Hook configuration tests
    void testGenerateHooksConfig();
    void testHookHandlerPath();

    // Client communication tests
    void testClientConnection();
    void testReceiveHookEvent();
    void testReceiveMultipleEvents();
    void testMalformedJson();

    // Signal tests
    void testHookEventReceivedSignal();
    void testClientConnectedSignal();
    void testClientDisconnectedSignal();
    void testErrorSignal();
};

}

#endif // CLAUDEHOOKHANDLERTEST_H
