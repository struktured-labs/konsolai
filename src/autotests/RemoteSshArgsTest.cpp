/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "RemoteSshArgsTest.h"

// Qt
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

void RemoteSshArgsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void RemoteSshArgsTest::cleanupTestCase()
{
}

// ========================================================================
// buildRemoteSshArgs() — Basic SSH connection arguments
// ========================================================================

void RemoteSshArgsTest::testBasicSshArgs_hostOnly()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    // Target should be just the host (no user@ prefix)
    bool foundTarget = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("myserver.example.com")) {
            foundTarget = true;
            break;
        }
    }
    QVERIFY2(foundTarget, "SSH args should contain the host as target");
}

void RemoteSshArgsTest::testBasicSshArgs_userAndHost()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    session.setSshUsername(QStringLiteral("deploy"));

    QStringList args = session.buildRemoteSshArgs();

    // Target should be user@host
    bool foundTarget = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("deploy@myserver.example.com")) {
            foundTarget = true;
            break;
        }
    }
    QVERIFY2(foundTarget, "SSH args should contain user@host as target");
}

void RemoteSshArgsTest::testBasicSshArgs_defaultPort()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    // Default port is 22

    QStringList args = session.buildRemoteSshArgs();

    // Should NOT contain -p flag for default port
    QVERIFY2(!args.contains(QStringLiteral("-p")), "Default port 22 should not produce -p flag");
}

void RemoteSshArgsTest::testBasicSshArgs_customPort()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    session.setSshPort(2222);

    QStringList args = session.buildRemoteSshArgs();

    // Should contain -p 2222
    int pIdx = args.indexOf(QStringLiteral("-p"));
    QVERIFY2(pIdx >= 0, "Custom port should produce -p flag");
    QVERIFY2(pIdx + 1 < args.size(), "-p flag must have a value following it");
    QCOMPARE(args.at(pIdx + 1), QStringLiteral("2222"));
}

void RemoteSshArgsTest::testBasicSshArgs_forceTty()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    // First arg should be -t (force TTY)
    QVERIFY2(!args.isEmpty(), "Args should not be empty");
    QCOMPARE(args.first(), QStringLiteral("-t"));
}

// ========================================================================
// buildRemoteSshArgs() — Working directory handling
// ========================================================================

void RemoteSshArgsTest::testWorkingDir_specified()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/myproject"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    // The remote command (last element) should contain the working directory
    QVERIFY2(!args.isEmpty(), "Args should not be empty");
    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("/home/user/myproject")), "Remote command should contain the specified working directory");
}

void RemoteSshArgsTest::testWorkingDir_empty_defaultsToTilde()
{
    // Create session with empty working dir - constructor uses QDir::homePath() as fallback,
    // but we can test by explicitly setting an empty workingDir path scenario.
    // The buildRemoteSshArgs uses m_workingDir directly, falling back to ~.
    ClaudeSession session(QStringLiteral("test"), QString());
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    // When workingDir is the home path (constructor fallback), the remote command should use it.
    // The key behavior: an empty m_workingDir in buildRemoteSshArgs() falls back to "~"
    QVERIFY2(!args.isEmpty(), "Args should not be empty");
    const QString remoteCmd = args.last();
    // It should contain either ~ or the home path
    QVERIFY2(remoteCmd.contains(QStringLiteral("~")) || remoteCmd.contains(QDir::homePath()),
             "Remote command should use home dir or ~ as working directory when none specified");
}

// ========================================================================
// buildRemoteSshArgs() — Tmux command generation
// ========================================================================

void RemoteSshArgsTest::testTmuxNewSession_inRemoteCommand()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("tmux new-session")), "Remote command should contain 'tmux new-session' for new sessions");
    QVERIFY2(remoteCmd.contains(QStringLiteral("-A")), "Remote command should contain -A flag (attach-or-create)");
    QVERIFY2(remoteCmd.contains(QStringLiteral("-s ")), "Remote command should contain -s flag for session name");
    QVERIFY2(remoteCmd.contains(session.sessionName()), "Remote command should contain the session name");
}

void RemoteSshArgsTest::testTmuxAttach_existingRemoteSession()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    session.setExistingRemoteTmuxSession(QStringLiteral("existing-session-abc"));

    QStringList args = session.buildRemoteSshArgs();

    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("tmux attach-session")),
             "Remote command should use 'tmux attach-session' when existingRemoteTmuxSession is set");
    QVERIFY2(remoteCmd.contains(QStringLiteral("-t existing-session-abc")), "Remote command should reference the existing session name");
    QVERIFY2(!remoteCmd.contains(QStringLiteral("tmux new-session")), "Remote command should NOT contain 'tmux new-session' for attach");
}

// ========================================================================
// buildRemoteSshArgs() — Resume session
// ========================================================================

void RemoteSshArgsTest::testResumeSessionId_inRemoteCommand()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    session.setResumeSessionId(QStringLiteral("conv_abc123def456"));

    QStringList args = session.buildRemoteSshArgs();

    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("--resume")), "Remote command should contain --resume flag");
    QVERIFY2(remoteCmd.contains(QStringLiteral("conv_abc123def456")), "Remote command should contain the resume session ID");
}

// ========================================================================
// buildRemoteSshArgs() — Args ordering and format
// ========================================================================

void RemoteSshArgsTest::testArgsOrdering_ttyFirst()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    session.setSshPort(2222);

    QStringList args = session.buildRemoteSshArgs();

    // -t must be the first argument
    QCOMPARE(args.at(0), QStringLiteral("-t"));
}

void RemoteSshArgsTest::testArgsOrdering_targetBeforeCommand()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    session.setSshUsername(QStringLiteral("user"));

    QStringList args = session.buildRemoteSshArgs();

    // Find the target (user@host) — it should be second-to-last
    // Last element is the remote command
    QVERIFY2(args.size() >= 3, "Should have at least: -t, target, remote-cmd");

    int targetIdx = args.indexOf(QStringLiteral("user@myserver.example.com"));
    QVERIFY2(targetIdx >= 0, "Target user@host should be in args");
    QVERIFY2(targetIdx == args.size() - 2, "Target should be second-to-last (before remote command)");
}

void RemoteSshArgsTest::testRemoteCommand_isSingleElement()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    // The remote command should be the last single element containing tmux
    const QString lastArg = args.last();
    QVERIFY2(lastArg.contains(QStringLiteral("tmux")), "Last arg should be the remote command containing tmux");
    // It should be a compound command (contains shell operators)
    QVERIFY2(lastArg.contains(QStringLiteral("&&")) || lastArg.contains(QStringLiteral(";")),
             "Remote command should be a compound shell command (profile sourcing + tmux)");
}

// ========================================================================
// buildRemoteSshArgs() — Profile sourcing
// ========================================================================

void RemoteSshArgsTest::testProfileSourcing_inRemoteCommand()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    const QString remoteCmd = args.last();
    // Should source login profiles for PATH setup
    QVERIFY2(remoteCmd.contains(QStringLiteral(".profile")), "Remote command should source .profile");
    QVERIFY2(remoteCmd.contains(QStringLiteral(".bashrc")), "Remote command should source .bashrc");
    QVERIFY2(remoteCmd.contains(QStringLiteral(".bash_profile")), "Remote command should source .bash_profile");
}

// ========================================================================
// buildRemoteSshArgs() — No-tunnel path
// ========================================================================

void RemoteSshArgsTest::testNoTunnel_simpleTmuxCommand()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));
    // No hook handler with TCP mode → no tunnel

    QStringList args = session.buildRemoteSshArgs();

    // Should NOT contain -R (reverse tunnel) since hook handler uses Unix socket by default.
    // With default (Unix socket) mode on the session, there should be no -R.
    QVERIFY2(!args.contains(QStringLiteral("-R")), "No -R flag expected when hook handler uses Unix socket mode (no TCP tunnel)");
    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("tmux")), "Remote command should still contain tmux even without tunnel");
    QVERIFY2(remoteCmd.contains(QStringLiteral("claude")), "Remote command should contain claude CLI invocation");
}

// ========================================================================
// buildRemoteSshArgs() — Passthrough suppression
// ========================================================================

void RemoteSshArgsTest::testPassthroughOff_inTmuxCommand()
{
    ClaudeSession session(QStringLiteral("test"), QStringLiteral("/home/user/project"));
    session.setIsRemote(true);
    session.setSshHost(QStringLiteral("myserver.example.com"));

    QStringList args = session.buildRemoteSshArgs();

    const QString remoteCmd = args.last();
    QVERIFY2(remoteCmd.contains(QStringLiteral("allow-passthrough off")), "Remote tmux command should suppress DCS passthrough");
}

// ========================================================================
// shellCommand() — Local session tests
// ========================================================================

void RemoteSshArgsTest::testShellCommand_newLocalSession()
{
    ClaudeSession session(QStringLiteral("myprofile"), QStringLiteral("/home/user/project"));

    QString cmd = session.shellCommand();

    QVERIFY2(cmd.contains(QStringLiteral("tmux")), "Shell command should invoke tmux");
    QVERIFY2(cmd.contains(QStringLiteral("new-session")), "Shell command should create new session");
    QVERIFY2(cmd.contains(QStringLiteral("-A")), "Shell command should have -A (attach-or-create)");
    QVERIFY2(cmd.contains(session.sessionName()), "Shell command should reference session name");
    QVERIFY2(cmd.contains(QStringLiteral("claude")), "Shell command should invoke claude");
}

void RemoteSshArgsTest::testShellCommand_reattachSession()
{
    auto *session = ClaudeSession::createForReattach(QStringLiteral("konsolai-test-abcd1234"));

    QString cmd = session->shellCommand();

    QVERIFY2(cmd.contains(QStringLiteral("tmux")), "Reattach shell command should invoke tmux");
    QVERIFY2(cmd.contains(QStringLiteral("attach-session")), "Reattach shell command should use attach-session");
    QVERIFY2(cmd.contains(QStringLiteral("konsolai-test-abcd1234")), "Reattach shell command should reference the existing session name");
    QVERIFY2(!cmd.contains(QStringLiteral("new-session")), "Reattach shell command should NOT use new-session");

    delete session;
}

void RemoteSshArgsTest::testShellCommand_withResumeId()
{
    ClaudeSession session(QStringLiteral("myprofile"), QStringLiteral("/home/user/project"));
    session.setResumeSessionId(QStringLiteral("conv_resume123"));

    QString cmd = session.shellCommand();

    QVERIFY2(cmd.contains(QStringLiteral("--resume")), "Shell command should contain --resume flag");
    QVERIFY2(cmd.contains(QStringLiteral("conv_resume123")), "Shell command should contain the resume ID");
}

void RemoteSshArgsTest::testShellCommand_withWorkingDir()
{
    ClaudeSession session(QStringLiteral("myprofile"), QStringLiteral("/opt/myproject"));

    QString cmd = session.shellCommand();

    QVERIFY2(cmd.contains(QStringLiteral("-c")), "Shell command should contain -c flag for working directory");
    QVERIFY2(cmd.contains(QStringLiteral("/opt/myproject")), "Shell command should contain the specified working directory");
}

QTEST_GUILESS_MAIN(RemoteSshArgsTest)

#include "moc_RemoteSshArgsTest.cpp"
