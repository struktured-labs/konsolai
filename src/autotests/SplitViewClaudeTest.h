/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SPLITVIEWCLAUDETEST_H
#define SPLITVIEWCLAUDETEST_H

#include <QObject>

namespace Konsolai
{

class SplitViewClaudeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Split view safety tests
    void testClaudeSessionReusedOnSplit();
    void testRemoveOneViewDoesNotKillSession();
    void testSessionMapTracksMultipleViews();
    void testCreateForReattachProducesIndependentSession();
    void testReattachSessionHasSameNameButDifferentObject();
    void testRemoveHooksForWorkDirClearsAllKonsolaiHooks();
    void testRemoveHooksForWorkDirPreservesNonKonsolaiHooks();
    void testRemoveHooksForWorkDirNoFileNoCrash();
};

} // namespace Konsolai

#endif // SPLITVIEWCLAUDETEST_H
