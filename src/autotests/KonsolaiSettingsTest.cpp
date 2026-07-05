/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "KonsolaiSettingsTest.h"

// Qt
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/KonsolaiSettings.h"

using namespace Konsolai;

void KonsolaiSettingsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void KonsolaiSettingsTest::cleanupTestCase()
{
}

void KonsolaiSettingsTest::cleanup()
{
    // Remove test config between tests so each test starts fresh
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/konsolairc");
    QFile::remove(configPath);
}

// ========== Default Value Tests ==========

void KonsolaiSettingsTest::testDefaultProjectRoot()
{
    KonsolaiSettings settings;
    const QString expected = QDir::homePath() + QStringLiteral("/projects");
    QCOMPARE(settings.projectRoot(), expected);
}

void KonsolaiSettingsTest::testDefaultGitRemoteRoot()
{
    KonsolaiSettings settings;
    const QString expected = QStringLiteral("git@github.com:%1/").arg(qEnvironmentVariable("USER"));
    QCOMPARE(settings.gitRemoteRoot(), expected);
}

void KonsolaiSettingsTest::testDefaultGithubApiToken()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.githubApiToken(), QString());
}

void KonsolaiSettingsTest::testDefaultModel()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.defaultModel(), QStringLiteral("claude-sonnet-4"));
}

void KonsolaiSettingsTest::testDefaultExtraClaudeArgs()
{
    KonsolaiSettings settings;
    // Defaults to the plugin's channel namespace so async DMs from
    // session-intercom actually bind in konsolai-launched sessions.
    // "server:session-intercom" was wrong — only matches top-level MCP
    // entries, not plugin-provided MCP servers.
    QCOMPARE(settings.extraClaudeArgs(), QStringLiteral("--dangerously-load-development-channels plugin:session-intercom@struktured-labs"));
}

void KonsolaiSettingsTest::testExtraClaudeArgsRoundTrip()
{
    KonsolaiSettings settings;
    settings.setExtraClaudeArgs(QStringLiteral("--debug --foo bar"));
    QCOMPARE(settings.extraClaudeArgs(), QStringLiteral("--debug --foo bar"));

    settings.setExtraClaudeArgs(QString());
    QCOMPARE(settings.extraClaudeArgs(), QString());
}

void KonsolaiSettingsTest::testDefaultGitMode()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.gitMode(), 0);
}

void KonsolaiSettingsTest::testDefaultWorktreeSourceRepo()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.worktreeSourceRepo(), QString());
}

void KonsolaiSettingsTest::testDefaultYoloMode()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.yoloMode(), false);
}

void KonsolaiSettingsTest::testDefaultDoubleYoloMode()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.doubleYoloMode(), false);
}

void KonsolaiSettingsTest::testDefaultTripleYoloMode()
{
    // Triple yolo mode has been removed. This test now verifies
    // that trySuggestionsFirst has a default value.
    KonsolaiSettings settings;
    QCOMPARE(settings.trySuggestionsFirst(), true);
}

void KonsolaiSettingsTest::testDefaultAutoContinuePrompt()
{
    // autoContinuePrompt was removed with triple yolo.
    // Verify settings instance is valid.
    KonsolaiSettings settings;
    QVERIFY(KonsolaiSettings::instance() != nullptr);
}

void KonsolaiSettingsTest::testDefaultTrySuggestionsFirst()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.trySuggestionsFirst(), true);
}

void KonsolaiSettingsTest::testDefaultTimeLimitMinutes()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.defaultTimeLimitMinutes(), 0);
}

void KonsolaiSettingsTest::testDefaultCostCeilingUSD()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.defaultCostCeilingUSD(), 0.0);
}

void KonsolaiSettingsTest::testDefaultBudgetPolicy()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.defaultBudgetPolicy(), 0);
}

void KonsolaiSettingsTest::testDefaultTokenCeiling()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.defaultTokenCeiling(), quint64(0));
}

void KonsolaiSettingsTest::testDefaultBudgetWarningThreshold()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.budgetWarningThresholdPercent(), 80.0);
}

void KonsolaiSettingsTest::testDefaultWeeklyBudgetUSD()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.weeklyBudgetUSD(), 0.0);
}

void KonsolaiSettingsTest::testDefaultMonthlyBudgetUSD()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.monthlyBudgetUSD(), 0.0);
}

void KonsolaiSettingsTest::testDefaultNotificationAudioEnabled()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.notificationAudioEnabled(), true);
}

void KonsolaiSettingsTest::testDefaultNotificationDesktopEnabled()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.notificationDesktopEnabled(), true);
}

void KonsolaiSettingsTest::testDefaultNotificationSystemTrayEnabled()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.notificationSystemTrayEnabled(), true);
}

void KonsolaiSettingsTest::testDefaultNotificationInTerminalEnabled()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.notificationInTerminalEnabled(), true);
}

void KonsolaiSettingsTest::testDefaultNotificationAudioVolume()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.notificationAudioVolume(), 0.7);
}

void KonsolaiSettingsTest::testDefaultNotificationYoloEnabled()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.notificationYoloEnabled(), false);
}

void KonsolaiSettingsTest::testDefaultLastSshHost()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.lastSshHost(), QString());
}

void KonsolaiSettingsTest::testDefaultLastSshUsername()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.lastSshUsername(), QString());
}

void KonsolaiSettingsTest::testDefaultLastSshPort()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.lastSshPort(), 22);
}

// ========== Round-Trip Tests ==========

void KonsolaiSettingsTest::testRoundTripStringSettings()
{
    KonsolaiSettings settings;

    settings.setProjectRoot(QStringLiteral("/tmp/test-project"));
    QCOMPARE(settings.projectRoot(), QStringLiteral("/tmp/test-project"));

    settings.setGitRemoteRoot(QStringLiteral("git@gitlab.com:testuser/"));
    QCOMPARE(settings.gitRemoteRoot(), QStringLiteral("git@gitlab.com:testuser/"));

    settings.setGithubApiToken(QStringLiteral("ghp_test123token"));
    QCOMPARE(settings.githubApiToken(), QStringLiteral("ghp_test123token"));

    settings.setDefaultModel(QStringLiteral("claude-opus-4"));
    QCOMPARE(settings.defaultModel(), QStringLiteral("claude-opus-4"));

    settings.setWorktreeSourceRepo(QStringLiteral("/home/user/repos/main"));
    QCOMPARE(settings.worktreeSourceRepo(), QStringLiteral("/home/user/repos/main"));

    settings.setLastSshHost(QStringLiteral("example.com"));
    QCOMPARE(settings.lastSshHost(), QStringLiteral("example.com"));

    settings.setLastSshUsername(QStringLiteral("deploy"));
    QCOMPARE(settings.lastSshUsername(), QStringLiteral("deploy"));
}

void KonsolaiSettingsTest::testRoundTripBoolSettings()
{
    KonsolaiSettings settings;

    settings.setYoloMode(true);
    QCOMPARE(settings.yoloMode(), true);
    settings.setYoloMode(false);
    QCOMPARE(settings.yoloMode(), false);

    settings.setDoubleYoloMode(true);
    QCOMPARE(settings.doubleYoloMode(), true);

    settings.setTrySuggestionsFirst(false);
    QCOMPARE(settings.trySuggestionsFirst(), false);

    settings.setNotificationAudioEnabled(false);
    QCOMPARE(settings.notificationAudioEnabled(), false);

    settings.setNotificationDesktopEnabled(false);
    QCOMPARE(settings.notificationDesktopEnabled(), false);

    settings.setNotificationSystemTrayEnabled(false);
    QCOMPARE(settings.notificationSystemTrayEnabled(), false);

    settings.setNotificationInTerminalEnabled(false);
    QCOMPARE(settings.notificationInTerminalEnabled(), false);

    settings.setNotificationYoloEnabled(true);
    QCOMPARE(settings.notificationYoloEnabled(), true);
}

void KonsolaiSettingsTest::testRoundTripIntSettings()
{
    KonsolaiSettings settings;

    settings.setGitMode(2);
    QCOMPARE(settings.gitMode(), 2);

    settings.setDefaultTimeLimitMinutes(30);
    QCOMPARE(settings.defaultTimeLimitMinutes(), 30);

    settings.setDefaultBudgetPolicy(1);
    QCOMPARE(settings.defaultBudgetPolicy(), 1);

    settings.setLastSshPort(2222);
    QCOMPARE(settings.lastSshPort(), 2222);
}

void KonsolaiSettingsTest::testRoundTripDoubleSettings()
{
    KonsolaiSettings settings;

    settings.setDefaultCostCeilingUSD(5.50);
    QCOMPARE(settings.defaultCostCeilingUSD(), 5.50);

    settings.setBudgetWarningThresholdPercent(90.0);
    QCOMPARE(settings.budgetWarningThresholdPercent(), 90.0);

    settings.setNotificationAudioVolume(0.3);
    QCOMPARE(settings.notificationAudioVolume(), 0.3);

    settings.setWeeklyBudgetUSD(25.0);
    QCOMPARE(settings.weeklyBudgetUSD(), 25.0);

    settings.setMonthlyBudgetUSD(100.0);
    QCOMPARE(settings.monthlyBudgetUSD(), 100.0);
}

void KonsolaiSettingsTest::testRoundTripQuint64Settings()
{
    KonsolaiSettings settings;

    settings.setDefaultTokenCeiling(1000000);
    QCOMPARE(settings.defaultTokenCeiling(), quint64(1000000));

    // Test with a large value
    settings.setDefaultTokenCeiling(500000000);
    QCOMPARE(settings.defaultTokenCeiling(), quint64(500000000));
}

// ========== Persistence Test ==========

void KonsolaiSettingsTest::testSavePersistsToDisk()
{
    // Write settings with first instance
    {
        KonsolaiSettings settings;
        settings.setProjectRoot(QStringLiteral("/persist/test"));
        settings.setYoloMode(true);
        settings.setDefaultCostCeilingUSD(42.5);
        settings.setDefaultTokenCeiling(999999);
        settings.setLastSshPort(8022);
        settings.save();
    }

    // Read back with a fresh instance
    {
        KonsolaiSettings settings;
        QCOMPARE(settings.projectRoot(), QStringLiteral("/persist/test"));
        QCOMPARE(settings.yoloMode(), true);
        QCOMPARE(settings.defaultCostCeilingUSD(), 42.5);
        QCOMPARE(settings.defaultTokenCeiling(), quint64(999999));
        QCOMPARE(settings.lastSshPort(), 8022);
    }
}

// ========== Signal Test ==========

void KonsolaiSettingsTest::testSettingsChangedSignal()
{
    KonsolaiSettings settings;
    QSignalSpy spy(&settings, &KonsolaiSettings::settingsChanged);
    QVERIFY(spy.isValid());

    settings.setProjectRoot(QStringLiteral("/signal/test"));
    QCOMPARE(spy.count(), 1);

    settings.setYoloMode(true);
    QCOMPARE(spy.count(), 2);

    settings.setDefaultCostCeilingUSD(10.0);
    QCOMPARE(spy.count(), 3);

    settings.setDefaultModel(QStringLiteral("claude-opus-4"));
    QCOMPARE(spy.count(), 4);

    settings.setNotificationAudioVolume(0.5);
    QCOMPARE(spy.count(), 5);

    // save() should NOT emit settingsChanged
    settings.save();
    QCOMPARE(spy.count(), 5);

    // SSH setters don't emit settingsChanged (by design — see source)
    settings.setLastSshHost(QStringLiteral("host"));
    QCOMPARE(spy.count(), 5);

    settings.setLastSshUsername(QStringLiteral("user"));
    QCOMPARE(spy.count(), 5);

    settings.setLastSshPort(2222);
    QCOMPARE(spy.count(), 5);
}

// ========== Session-tree alias / override / suppression ==========

void KonsolaiSettingsTest::testDefaultCategoryAliasesEmpty()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.categoryAliases().size(), 0);
}

void KonsolaiSettingsTest::testCategoryAliasesRoundTrip()
{
    KonsolaiSettings settings;

    QHash<QString, QString> aliases;
    aliases.insert(QStringLiteral("cowardly-irregular"), QStringLiteral("cowir"));
    aliases.insert(QStringLiteral("penta-dragon"), QStringLiteral("pdrag"));
    settings.setCategoryAliases(aliases);

    const auto read = settings.categoryAliases();
    QCOMPARE(read.size(), 2);
    QCOMPARE(read.value(QStringLiteral("cowardly-irregular")), QStringLiteral("cowir"));
    QCOMPARE(read.value(QStringLiteral("penta-dragon")), QStringLiteral("pdrag"));

    // Empty map round-trip.
    settings.setCategoryAliases({});
    QCOMPARE(settings.categoryAliases().size(), 0);
}

void KonsolaiSettingsTest::testDefaultWorkdirCategoryOverridesEmpty()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.workdirCategoryOverrides().size(), 0);
}

void KonsolaiSettingsTest::testWorkdirCategoryOverridesRoundTrip()
{
    KonsolaiSettings settings;

    QHash<QString, QString> overrides;
    overrides.insert(QStringLiteral("/home/u/wild-orphan"), QStringLiteral("cowir"));
    overrides.insert(QStringLiteral("/home/u/other-mid"), QStringLiteral("misc"));
    settings.setWorkdirCategoryOverrides(overrides);

    const auto read = settings.workdirCategoryOverrides();
    QCOMPARE(read.size(), 2);
    QCOMPARE(read.value(QStringLiteral("/home/u/wild-orphan")), QStringLiteral("cowir"));
    QCOMPARE(read.value(QStringLiteral("/home/u/other-mid")), QStringLiteral("misc"));

    settings.setWorkdirCategoryOverrides({});
    QCOMPARE(settings.workdirCategoryOverrides().size(), 0);
}

void KonsolaiSettingsTest::testDefaultSuppressedCategoriesEmpty()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.suppressedCategories().size(), 0);
}

void KonsolaiSettingsTest::testSuppressedCategoriesRoundTrip()
{
    KonsolaiSettings settings;

    QStringList names = {QStringLiteral("cowir"), QStringLiteral("penta")};
    settings.setSuppressedCategories(names);
    const QStringList read = settings.suppressedCategories();
    QCOMPARE(read.size(), 2);
    QVERIFY(read.contains(QStringLiteral("cowir")));
    QVERIFY(read.contains(QStringLiteral("penta")));

    // Empty round-trip.
    settings.setSuppressedCategories({});
    QCOMPARE(settings.suppressedCategories().size(), 0);

    // Duplicates get deduplicated on write.
    settings.setSuppressedCategories({QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("y")});
    QCOMPARE(settings.suppressedCategories().size(), 2);
}

void KonsolaiSettingsTest::testAddRemoveCategoryAlias()
{
    KonsolaiSettings settings;

    settings.addCategoryAlias(QStringLiteral("foo"), QStringLiteral("bar"));
    QCOMPARE(settings.categoryAliases().value(QStringLiteral("foo")), QStringLiteral("bar"));

    // add a second — first is untouched.
    settings.addCategoryAlias(QStringLiteral("baz"), QStringLiteral("qux"));
    QCOMPARE(settings.categoryAliases().size(), 2);

    // add overwrites existing entry.
    settings.addCategoryAlias(QStringLiteral("foo"), QStringLiteral("baz"));
    QCOMPARE(settings.categoryAliases().value(QStringLiteral("foo")), QStringLiteral("baz"));

    // remove one, other stays.
    settings.removeCategoryAlias(QStringLiteral("foo"));
    QCOMPARE(settings.categoryAliases().size(), 1);
    QVERIFY(!settings.categoryAliases().contains(QStringLiteral("foo")));

    // removing a non-existent key is a no-op.
    settings.removeCategoryAlias(QStringLiteral("does-not-exist"));
    QCOMPARE(settings.categoryAliases().size(), 1);

    // Empty source/target are rejected silently.
    settings.addCategoryAlias(QString(), QStringLiteral("nope"));
    settings.addCategoryAlias(QStringLiteral("nope"), QString());
    QCOMPARE(settings.categoryAliases().size(), 1);

    // Self-alias is rejected.
    settings.addCategoryAlias(QStringLiteral("loop"), QStringLiteral("loop"));
    QVERIFY(!settings.categoryAliases().contains(QStringLiteral("loop")));
}

void KonsolaiSettingsTest::testAddRemoveWorkdirOverride()
{
    KonsolaiSettings settings;

    settings.addWorkdirCategoryOverride(QStringLiteral("/p/one"), QStringLiteral("cat1"));
    settings.addWorkdirCategoryOverride(QStringLiteral("/p/two"), QStringLiteral("cat2"));
    QCOMPARE(settings.workdirCategoryOverrides().size(), 2);

    // Overwrite.
    settings.addWorkdirCategoryOverride(QStringLiteral("/p/one"), QStringLiteral("cat1b"));
    QCOMPARE(settings.workdirCategoryOverrides().value(QStringLiteral("/p/one")), QStringLiteral("cat1b"));

    // Remove.
    settings.removeWorkdirCategoryOverride(QStringLiteral("/p/one"));
    QCOMPARE(settings.workdirCategoryOverrides().size(), 1);
    QVERIFY(!settings.workdirCategoryOverrides().contains(QStringLiteral("/p/one")));

    // Empty inputs are rejected silently.
    settings.addWorkdirCategoryOverride(QString(), QStringLiteral("x"));
    settings.addWorkdirCategoryOverride(QStringLiteral("/p/three"), QString());
    QCOMPARE(settings.workdirCategoryOverrides().size(), 1);
}

void KonsolaiSettingsTest::testAddRemoveSuppressedCategory()
{
    KonsolaiSettings settings;

    settings.addSuppressedCategory(QStringLiteral("cowir"));
    QVERIFY(settings.suppressedCategories().contains(QStringLiteral("cowir")));

    // Adding again is idempotent.
    settings.addSuppressedCategory(QStringLiteral("cowir"));
    QCOMPARE(settings.suppressedCategories().size(), 1);

    settings.addSuppressedCategory(QStringLiteral("penta"));
    QCOMPARE(settings.suppressedCategories().size(), 2);

    settings.removeSuppressedCategory(QStringLiteral("cowir"));
    QCOMPARE(settings.suppressedCategories().size(), 1);
    QVERIFY(!settings.suppressedCategories().contains(QStringLiteral("cowir")));

    // Empty add is rejected silently.
    settings.addSuppressedCategory(QString());
    QCOMPARE(settings.suppressedCategories().size(), 1);

    // Remove no-op on missing key.
    settings.removeSuppressedCategory(QStringLiteral("does-not-exist"));
    QCOMPARE(settings.suppressedCategories().size(), 1);
}

// ========== User-defined empty categories ==========

void KonsolaiSettingsTest::testDefaultUserCategoriesEmpty()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.userCategories().size(), 0);
}

void KonsolaiSettingsTest::testUserCategoriesRoundTrip()
{
    KonsolaiSettings settings;

    QStringList names = {QStringLiteral("my-stuff"), QStringLiteral("scratch")};
    settings.setUserCategories(names);
    const QStringList read = settings.userCategories();
    QCOMPARE(read.size(), 2);
    QVERIFY(read.contains(QStringLiteral("my-stuff")));
    QVERIFY(read.contains(QStringLiteral("scratch")));

    // Empty round-trip.
    settings.setUserCategories({});
    QCOMPARE(settings.userCategories().size(), 0);

    // Duplicates deduplicated on write.
    settings.setUserCategories({QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("y")});
    QCOMPARE(settings.userCategories().size(), 2);
}

void KonsolaiSettingsTest::testAddRemoveUserCategory()
{
    KonsolaiSettings settings;

    settings.addUserCategory(QStringLiteral("alpha"));
    QVERIFY(settings.userCategories().contains(QStringLiteral("alpha")));

    // Adding again is idempotent.
    settings.addUserCategory(QStringLiteral("alpha"));
    QCOMPARE(settings.userCategories().size(), 1);

    settings.addUserCategory(QStringLiteral("beta"));
    QCOMPARE(settings.userCategories().size(), 2);

    settings.removeUserCategory(QStringLiteral("alpha"));
    QCOMPARE(settings.userCategories().size(), 1);
    QVERIFY(!settings.userCategories().contains(QStringLiteral("alpha")));

    // Empty add is rejected silently.
    settings.addUserCategory(QString());
    QCOMPARE(settings.userCategories().size(), 1);

    // Remove no-op on missing key.
    settings.removeUserCategory(QStringLiteral("does-not-exist"));
    QCOMPARE(settings.userCategories().size(), 1);
}

void KonsolaiSettingsTest::testDefaultVisibleStatesDoesNotIncludeSubagent()
{
    // Subagent sessions are hidden by default — they're worker-spawned
    // sessions the user doesn't drive directly.  Toggle on from the filter
    // chip overflow menu to inspect them.
    KonsolaiSettings settings;
    const QStringList defaults = settings.visibleSessionStates();
    QVERIFY2(!defaults.contains(QStringLiteral("subagent")), "Default visibleSessionStates must NOT include 'subagent'");
    // Sanity: the expected defaults are still present.
    QVERIFY(defaults.contains(QStringLiteral("active")));
    QVERIFY(defaults.contains(QStringLiteral("detached")));
    QVERIFY(defaults.contains(QStringLiteral("pinned")));
    QVERIFY(defaults.contains(QStringLiteral("closed")));
}

QTEST_GUILESS_MAIN(Konsolai::KonsolaiSettingsTest)

#include "KonsolaiSettingsTest.moc"
