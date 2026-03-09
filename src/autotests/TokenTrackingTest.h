/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TOKENTRACKINGTEST_H
#define TOKENTRACKINGTEST_H

#include <QObject>

namespace Konsolai
{

class TokenTrackingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // parseConversationTokens tests
    void testParseValidJsonl();
    void testParseMultipleAssistantMessages();
    void testIncrementalParsing();
    void testFileTruncationDetection();
    void testMalformedJsonLinesSkipped();
    void testNonAssistantMessagesSkipped();
    void testModelDetection();
    void testModelDetectionUsesLast();
    void testContextWindowTracking();
    void testEmptyFile();
    void testEmptyUsageObject();

    // refreshTokenUsage tests
    void testRefreshFindsNewestJsonl();
    void testRefreshHandlesMissingProjectDir();
    void testRefreshHandlesEmptyWorkingDir();
    void testRefreshEmitsSignalOnChange();
    void testRefreshNoSignalWhenUnchanged();
    void testRefreshResetsOnFileChange();

    // File watcher / debounce tests
    void testTokenRefreshTimerStarted();
};

}

#endif // TOKENTRACKINGTEST_H
