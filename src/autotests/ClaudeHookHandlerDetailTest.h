/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEHOOKHANDLERDETAILTEST_H
#define CLAUDEHOOKHANDLERDETAILTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeHookHandlerDetailTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // generateHooksConfig() tests
    void testGenerateHooksConfigIsValidJson();
    void testGenerateHooksConfigContainsAllEventTypes();
    void testGenerateHooksConfigReferencesSocketPath();
    void testGenerateHooksConfigReferencesHandlerBinary();
    void testGenerateHooksConfigHasCorrectStructure();

    // Hook event parsing tests
    void testParsePreToolUseEvent();
    void testParsePostToolUseEvent();
    void testParseStopEvent();
    void testParseNotificationEvent();
    void testParsePermissionRequestEvent();
    void testParseSubagentStartEvent();
    void testMalformedJsonEmitsError();
    void testNonObjectJsonEmitsError();
    void testEmptyEventTypeEmitsError();
    void testEventDataPreserved();

    // Socket path generation tests
    void testSocketPathContainsSessionId();
    void testSocketPathEndsSock();
    void testSocketPathUnique();
    void testSocketPathInSessionsDir();

    // generateRemoteHookScript() tests
    void testRemoteHookScriptContainsTunnelPort();
    void testRemoteHookScriptIsValidShell();
    void testRemoteHookScriptAlwaysExitsZero();
    void testRemoteHookScriptContainsEventHandling();

    // ClaudeHookClient round-trip tests
    void testClientServerRoundTripPreToolUse();
    void testClientServerRoundTripWithData();
};

} // namespace Konsolai

#endif // CLAUDEHOOKHANDLERDETAILTEST_H
