/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONRESTARTTEST_H
#define CLAUDESESSIONRESTARTTEST_H

#include <QObject>
#include <QTemporaryDir>

namespace Konsolai
{

class ClaudeSessionRestartTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testRestartCommandCodexStaysCodex();
    void testRestartCommandHonoursModelOverride();
    void testRestartCommandResumesHookObservedConversation();
    void testRestartCommandFollowsClearToNewConversation();
    void testRestartCommandIsNotWrappedInTmux();
    void testRestartCommandPrefersLiveCliRegistry();

private:
    // Fake HOME: the resume lookups read ~/.claude/sessions and
    // ~/.claude/projects, and a test must not consult the real ones.
    QTemporaryDir m_home;
    QByteArray m_realHome;
};

}

#endif // CLAUDESESSIONRESTARTTEST_H
