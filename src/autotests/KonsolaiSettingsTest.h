/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAISETTINGSTEST_H
#define KONSOLAISETTINGSTEST_H

#include <QObject>

namespace Konsolai
{

class KonsolaiSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Default value tests
    void testDefaultProjectRoot();
    void testDefaultGitRemoteRoot();
    void testDefaultGithubApiToken();
    void testDefaultModel();
    void testDefaultPermissionMode();
    void testDefaultExtraClaudeArgs();
    void testExtraClaudeArgsRoundTrip();
    void testDefaultGitMode();
    void testDefaultWorktreeSourceRepo();
    void testDefaultYoloMode();
    void testDefaultDoubleYoloMode();
    void testDefaultTripleYoloMode();
    void testDefaultAutoContinuePrompt();
    void testDefaultTrySuggestionsFirst();
    void testDefaultTimeLimitMinutes();
    void testDefaultCostCeilingUSD();
    void testDefaultBudgetPolicy();
    void testDefaultTokenCeiling();
    void testDefaultBudgetWarningThreshold();
    void testDefaultWeeklyBudgetUSD();
    void testDefaultMonthlyBudgetUSD();
    void testDefaultNotificationAudioEnabled();
    void testDefaultNotificationDesktopEnabled();
    void testDefaultNotificationSystemTrayEnabled();
    void testDefaultNotificationInTerminalEnabled();
    void testDefaultNotificationAudioVolume();
    void testDefaultNotificationYoloEnabled();
    void testDefaultLastSshHost();
    void testDefaultLastSshUsername();
    void testDefaultLastSshPort();

    // Round-trip tests (set + get)
    void testRoundTripStringSettings();
    void testRoundTripBoolSettings();
    void testRoundTripIntSettings();
    void testRoundTripDoubleSettings();
    void testRoundTripQuint64Settings();

    // Persistence test
    void testSavePersistsToDisk();

    // Signal test
    void testSettingsChangedSignal();

    // Session-tree alias / override / suppression persistence
    void testDefaultCategoryAliasesEmpty();
    void testCategoryAliasesRoundTrip();
    void testDefaultWorkdirCategoryOverridesEmpty();
    void testWorkdirCategoryOverridesRoundTrip();
    void testDefaultSuppressedCategoriesEmpty();
    void testSuppressedCategoriesRoundTrip();
    void testAddRemoveCategoryAlias();
    void testAddRemoveWorkdirOverride();
    void testAddRemoveSuppressedCategory();

    // User-defined empty categories
    void testDefaultUserCategoriesEmpty();
    void testUserCategoriesRoundTrip();
    void testAddRemoveUserCategory();

    // Session-tree default visibility
    void testDefaultVisibleStatesDoesNotIncludeSubagent();
};

}

#endif // KONSOLAISETTINGSTEST_H
