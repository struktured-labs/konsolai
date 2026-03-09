/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef REMOTESSHARGSTEST_H
#define REMOTESSHARGSTEST_H

#include <QObject>

namespace Konsolai
{

class RemoteSshArgsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // === buildRemoteSshArgs() tests ===

    // Basic SSH connection arguments
    void testBasicSshArgs_hostOnly();
    void testBasicSshArgs_userAndHost();
    void testBasicSshArgs_defaultPort();
    void testBasicSshArgs_customPort();
    void testBasicSshArgs_forceTty();

    // Working directory handling
    void testWorkingDir_specified();
    void testWorkingDir_empty_defaultsToTilde();

    // Tmux command generation
    void testTmuxNewSession_inRemoteCommand();
    void testTmuxAttach_existingRemoteSession();

    // Resume session
    void testResumeSessionId_inRemoteCommand();

    // SSH args ordering and format
    void testArgsOrdering_ttyFirst();
    void testArgsOrdering_targetBeforeCommand();
    void testRemoteCommand_isSingleElement();

    // Profile sourcing
    void testProfileSourcing_inRemoteCommand();

    // No-tunnel path (no hook handler)
    void testNoTunnel_simpleTmuxCommand();

    // Passthrough suppression
    void testPassthroughOff_inTmuxCommand();

    // === shellCommand() tests ===

    // Local session command generation
    void testShellCommand_newLocalSession();
    void testShellCommand_reattachSession();
    void testShellCommand_withResumeId();
    void testShellCommand_withWorkingDir();
};

} // namespace Konsolai

#endif // REMOTESSHARGSTEST_H
