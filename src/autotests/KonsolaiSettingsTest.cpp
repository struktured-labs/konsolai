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
    KonsolaiSettings settings;
    QCOMPARE(settings.tripleYoloMode(), false);
}

void KonsolaiSettingsTest::testDefaultAutoContinuePrompt()
{
    KonsolaiSettings settings;
    QCOMPARE(settings.autoContinuePrompt(), QStringLiteral("Continue improving, debugging, fixing, adding features, or introducing tests where applicable."));
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

    settings.setAutoContinuePrompt(QStringLiteral("Keep going!"));
    QCOMPARE(settings.autoContinuePrompt(), QStringLiteral("Keep going!"));

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

    settings.setTripleYoloMode(true);
    QCOMPARE(settings.tripleYoloMode(), true);

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

QTEST_GUILESS_MAIN(Konsolai::KonsolaiSettingsTest)

#include "KonsolaiSettingsTest.moc"
