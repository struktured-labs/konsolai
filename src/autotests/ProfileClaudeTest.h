/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROFILECLAUDETEST_H
#define PROFILECLAUDETEST_H

#include <QObject>

namespace Konsole
{

class ProfileClaudeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Claude property tests
    void testClaudeEnabledDefault();
    void testClaudeEnabledSet();

    void testClaudeTmuxPersistenceDefault();
    void testClaudeTmuxPersistenceSet();

    void testClaudeModelDefault();
    void testClaudeModelSet();

    void testClaudeArgsDefault();
    void testClaudeArgsSet();

    void testClaudeNotificationChannelsDefault();
    void testClaudeNotificationChannelsSet();

    void testClaudeAutoApproveReadDefault();
    void testClaudeAutoApproveReadSet();

    void testClaudeHooksConfigPathDefault();
    void testClaudeHooksConfigPathSet();

    // Inheritance tests
    void testClaudePropertyInheritance();

    // Clone tests
    void testClaudePropertyClone();
};

}

#endif // PROFILECLAUDETEST_H
