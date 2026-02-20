/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXMANAGERTEST_H
#define TMUXMANAGERTEST_H

#include <QObject>

namespace Konsolai
{

class TmuxManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Static utility tests
    void testGenerateSessionId();
    void testBuildSessionName();
    void testBuildSessionNameWithTemplate();
    void testBuildSessionNameCustomTemplate();

    // Command building tests
    void testBuildNewSessionCommand();
    void testBuildNewSessionCommandWithWorkingDir();
    void testBuildAttachCommand();
    void testBuildKillCommand();
    void testBuildDetachCommand();

    // Availability tests
    void testIsAvailable();
    void testVersion();

    // Session name sanitization
    void testBuildSessionNameSanitizesChars();

    // Session ID uniqueness
    void testSessionIdUniqueness();

    // Command building edge cases
    void testBuildNewSessionCommandNoAttach();
    void testBuildNewSessionCommandPassthrough();

    // Execution tests (require tmux)
    void testSessionExistsNonexistent();
    void testListSessionsWhenAvailable();
    void testKillNonexistentSession();
    void testCapturePaneNonexistent();
    void testSendKeysNonexistent();
    void testSendKeySequenceNonexistent();
    void testGetPaneWorkingDirNonexistent();
    void testGetPanePidNonexistent();

    // Async execution tests (require tmux)
    void testSessionExistsAsyncNonexistent();
    void testCapturePaneAsyncNonexistent();
    void testListKonsolaiSessionsAsync();
    void testGetPanePidAsyncNonexistent();

    // Attach command passthrough
    void testBuildAttachCommandPassthrough();

    // Build new session command with all options
    void testBuildNewSessionCommandAllOptions();

    // Session name edge cases
    void testBuildSessionNameMultipleBadChars();
    void testBuildSessionNameTemplateNoPlaceholders();
};

}

#endif // TMUXMANAGERTEST_H
