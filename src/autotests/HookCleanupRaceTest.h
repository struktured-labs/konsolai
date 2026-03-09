/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef HOOKCLEANUPRACETTEST_H
#define HOOKCLEANUPRACETTEST_H

#include <QObject>

namespace Konsolai
{

class HookCleanupRaceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Registry interaction tests
    void testRegistryFindSessionByName();
    void testRegistryActiveSessionsList();

    // Race condition regression tests
    void testReplacementSessionPreventsHookCleanup();
    void testNoReplacementAllowsHookCleanup();
    void testSettingsFileStateAfterCleanup();
    void testSettingsFileStateAfterSkippedCleanup();
    void testSameSessionIdDifferentNames();
};

} // namespace Konsolai

#endif // HOOKCLEANUPRACETTEST_H
