/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "NotificationManagerTest.h"

// Qt
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/NotificationManager.h"

using namespace Konsolai;

void NotificationManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);

    // The singleton requires an explicit construction before instance() works.
    m_manager = new NotificationManager(this);
}

void NotificationManagerTest::cleanupTestCase()
{
    delete m_manager;
    m_manager = nullptr;
}

void NotificationManagerTest::testInstance()
{
    NotificationManager *manager = NotificationManager::instance();
    QVERIFY(manager != nullptr);
}

void NotificationManagerTest::testInstanceConsistency()
{
    NotificationManager *manager1 = NotificationManager::instance();
    NotificationManager *manager2 = NotificationManager::instance();

    QCOMPARE(manager1, manager2);
}

void NotificationManagerTest::testDefaultChannels()
{
    NotificationManager *manager = NotificationManager::instance();

    // Default should be all channels enabled
    NotificationManager::Channels channels = manager->enabledChannels();
    QVERIFY(channels.testFlag(NotificationManager::Channel::SystemTray));
    QVERIFY(channels.testFlag(NotificationManager::Channel::Desktop));
    QVERIFY(channels.testFlag(NotificationManager::Channel::Audio));
    QVERIFY(channels.testFlag(NotificationManager::Channel::InTerminal));
}

void NotificationManagerTest::testEnableChannel()
{
    NotificationManager *manager = NotificationManager::instance();

    // Disable all first
    manager->setEnabledChannels(NotificationManager::Channel::None);

    // Enable just SystemTray
    manager->enableChannel(NotificationManager::Channel::SystemTray, true);

    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::SystemTray));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::Desktop));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::Audio));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::InTerminal));

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testDisableChannel()
{
    NotificationManager *manager = NotificationManager::instance();

    // Start with all enabled
    manager->setEnabledChannels(NotificationManager::Channel::All);

    // Disable Audio
    manager->enableChannel(NotificationManager::Channel::Audio, false);

    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::SystemTray));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::Desktop));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::Audio));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::InTerminal));

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testSetEnabledChannels()
{
    NotificationManager *manager = NotificationManager::instance();

    // Set specific combination
    NotificationManager::Channels combo =
        NotificationManager::Channel::Desktop | NotificationManager::Channel::Audio;
    manager->setEnabledChannels(combo);

    QCOMPARE(manager->enabledChannels(), combo);
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::SystemTray));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::Desktop));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::Audio));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::InTerminal));

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testIsChannelEnabled()
{
    NotificationManager *manager = NotificationManager::instance();

    // Test with all enabled
    manager->setEnabledChannels(NotificationManager::Channel::All);

    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::SystemTray));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::Desktop));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::Audio));
    QVERIFY(manager->isChannelEnabled(NotificationManager::Channel::InTerminal));

    // Test with none enabled
    manager->setEnabledChannels(NotificationManager::Channel::None);

    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::SystemTray));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::Desktop));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::Audio));
    QVERIFY(!manager->isChannelEnabled(NotificationManager::Channel::InTerminal));

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testChannelFlags()
{
    // Test flag combinations work correctly
    NotificationManager::Channels combo =
        NotificationManager::Channel::SystemTray | NotificationManager::Channel::Audio;

    QVERIFY(combo.testFlag(NotificationManager::Channel::SystemTray));
    QVERIFY(!combo.testFlag(NotificationManager::Channel::Desktop));
    QVERIFY(combo.testFlag(NotificationManager::Channel::Audio));
    QVERIFY(!combo.testFlag(NotificationManager::Channel::InTerminal));

    // Test None flag
    NotificationManager::Channels none = NotificationManager::Channel::None;
    QVERIFY(!none.testFlag(NotificationManager::Channel::SystemTray));
    QVERIFY(!none.testFlag(NotificationManager::Channel::Desktop));

    // Test All flag
    NotificationManager::Channels all = NotificationManager::Channel::All;
    QVERIFY(all.testFlag(NotificationManager::Channel::SystemTray));
    QVERIFY(all.testFlag(NotificationManager::Channel::Desktop));
    QVERIFY(all.testFlag(NotificationManager::Channel::Audio));
    QVERIFY(all.testFlag(NotificationManager::Channel::InTerminal));
}

void NotificationManagerTest::testDefaultVolume()
{
    NotificationManager *manager = NotificationManager::instance();

    qreal volume = manager->audioVolume();
    QVERIFY(volume >= 0.0 && volume <= 1.0);
    QCOMPARE(volume, 0.7);  // Default is 0.7
}

void NotificationManagerTest::testSetVolume()
{
    NotificationManager *manager = NotificationManager::instance();

    manager->setAudioVolume(0.5);
    QCOMPARE(manager->audioVolume(), 0.5);

    manager->setAudioVolume(1.0);
    QCOMPARE(manager->audioVolume(), 1.0);

    manager->setAudioVolume(0.0);
    QCOMPARE(manager->audioVolume(), 0.0);

    // Restore default
    manager->setAudioVolume(0.7);
}

void NotificationManagerTest::testVolumeClampingMin()
{
    NotificationManager *manager = NotificationManager::instance();

    // Negative values should be clamped to 0
    manager->setAudioVolume(-0.5);
    QVERIFY(manager->audioVolume() >= 0.0);

    // Restore default
    manager->setAudioVolume(0.7);
}

void NotificationManagerTest::testVolumeClampingMax()
{
    NotificationManager *manager = NotificationManager::instance();

    // Values > 1.0 should be clamped to 1.0
    manager->setAudioVolume(2.0);
    QVERIFY(manager->audioVolume() <= 1.0);

    // Restore default
    manager->setAudioVolume(0.7);
}

void NotificationManagerTest::testIconName()
{
    // Test icon names for different notification types
    QString infoIcon = NotificationManager::iconName(NotificationManager::NotificationType::Info);
    QVERIFY(!infoIcon.isEmpty());

    QString completeIcon = NotificationManager::iconName(NotificationManager::NotificationType::TaskComplete);
    QVERIFY(!completeIcon.isEmpty());

    QString waitingIcon = NotificationManager::iconName(NotificationManager::NotificationType::WaitingInput);
    QVERIFY(!waitingIcon.isEmpty());

    QString permissionIcon = NotificationManager::iconName(NotificationManager::NotificationType::Permission);
    QVERIFY(!permissionIcon.isEmpty());

    QString errorIcon = NotificationManager::iconName(NotificationManager::NotificationType::Error);
    QVERIFY(!errorIcon.isEmpty());

    // Different types should have different icons (or at least not crash)
    Q_UNUSED(infoIcon)
    Q_UNUSED(completeIcon)
    Q_UNUSED(waitingIcon)
    Q_UNUSED(permissionIcon)
    Q_UNUSED(errorIcon)
}

void NotificationManagerTest::testSoundPath()
{
    // Test sound paths for different notification types.
    // In a test environment, the .wav files are typically not installed,
    // so soundPath() will return empty strings.  We verify:
    // 1. The function does not crash for any type.
    // 2. Info type always returns empty (no sound by design).
    QString permissionSound = NotificationManager::soundPath(NotificationManager::NotificationType::Permission);
    QString errorSound = NotificationManager::soundPath(NotificationManager::NotificationType::Error);
    QString waitingSound = NotificationManager::soundPath(NotificationManager::NotificationType::WaitingInput);
    QString infoSound = NotificationManager::soundPath(NotificationManager::NotificationType::Info);
    QString completeSound = NotificationManager::soundPath(NotificationManager::NotificationType::TaskComplete);

    // Info should always return empty (no sound by design)
    QVERIFY(infoSound.isEmpty());

    // If any path is returned, it must end in .wav
    for (const QString &path : {permissionSound, errorSound, waitingSound, completeSound}) {
        if (!path.isEmpty()) {
            QVERIFY2(path.endsWith(QStringLiteral(".wav")), qPrintable(path));
        }
    }
}

void NotificationManagerTest::testShowInTerminalNotificationSignal()
{
    NotificationManager *manager = NotificationManager::instance();

    QSignalSpy spy(manager, &NotificationManager::showInTerminalNotification);

    // Signal should be connectable
    QVERIFY(spy.isValid());
}

void NotificationManagerTest::testSystemTrayStatusChangedSignal()
{
    NotificationManager *manager = NotificationManager::instance();

    QSignalSpy spy(manager, &NotificationManager::systemTrayStatusChanged);

    // Signal should be connectable
    QVERIFY(spy.isValid());
}

// ============================================================
// notify() integration tests
// ============================================================

void NotificationManagerTest::testNotifyDispatchesInTerminal()
{
    NotificationManager *manager = NotificationManager::instance();

    // Enable only InTerminal channel
    manager->setEnabledChannels(NotificationManager::Channel::InTerminal);

    QSignalSpy terminalSpy(manager, &NotificationManager::showInTerminalNotification);

    manager->notify(NotificationManager::NotificationType::TaskComplete, QStringLiteral("Done"), QStringLiteral("Task finished"), nullptr);

    QCOMPARE(terminalSpy.count(), 1);
    // Verify signal carries correct type and message
    auto args = terminalSpy.at(0);
    QCOMPARE(args.at(0).value<NotificationManager::NotificationType>(), NotificationManager::NotificationType::TaskComplete);
    QCOMPARE(args.at(1).toString(), QStringLiteral("Done"));
    QCOMPARE(args.at(2).toString(), QStringLiteral("Task finished"));

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testNotifyChannelFiltering()
{
    NotificationManager *manager = NotificationManager::instance();

    // Disable all channels
    manager->setEnabledChannels(NotificationManager::Channel::None);

    QSignalSpy terminalSpy(manager, &NotificationManager::showInTerminalNotification);
    QSignalSpy traySpy(manager, &NotificationManager::systemTrayStatusChanged);

    manager->notify(NotificationManager::NotificationType::Error, QStringLiteral("Error"), QStringLiteral("Something went wrong"), nullptr);

    // Nothing should fire with all channels disabled
    QCOMPARE(terminalSpy.count(), 0);
    QCOMPARE(traySpy.count(), 0);

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testNotifyAudioDisabledNoSound()
{
    NotificationManager *manager = NotificationManager::instance();

    // Enable only Desktop + InTerminal (no Audio)
    manager->setEnabledChannels(NotificationManager::Channel::Desktop | NotificationManager::Channel::InTerminal);

    QSignalSpy terminalSpy(manager, &NotificationManager::showInTerminalNotification);

    // This should dispatch to InTerminal but NOT Audio
    manager->notify(NotificationManager::NotificationType::Permission, QStringLiteral("Permission"), QStringLiteral("Need auth"), nullptr);

    QCOMPARE(terminalSpy.count(), 1);
    // Audio not easily verifiable without mock, but we verify the channel gating logic works

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testNotifySystemTrayEmitsSignal()
{
    NotificationManager *manager = NotificationManager::instance();

    // Enable only SystemTray
    manager->setEnabledChannels(NotificationManager::Channel::SystemTray);

    QSignalSpy traySpy(manager, &NotificationManager::systemTrayStatusChanged);

    manager->notify(NotificationManager::NotificationType::WaitingInput, QStringLiteral("Input"), QStringLiteral("Waiting for user"), nullptr);

    QCOMPARE(traySpy.count(), 1);
    auto args = traySpy.at(0);
    QCOMPARE(args.at(0).value<NotificationManager::NotificationType>(), NotificationManager::NotificationType::WaitingInput);
    QCOMPARE(args.at(1).toString(), QStringLiteral("Waiting for user"));

    // Restore defaults
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

void NotificationManagerTest::testYoloCooldown()
{
    NotificationManager *manager = NotificationManager::instance();

    // Enable yolo notifications and audio
    manager->setYoloNotificationsEnabled(true);
    manager->setEnabledChannels(NotificationManager::Channel::InTerminal);

    QSignalSpy terminalSpy(manager, &NotificationManager::showInTerminalNotification);

    // First yolo notification should go through
    manager->notify(NotificationManager::NotificationType::YoloApproval, QStringLiteral("Auto"), QStringLiteral("Approved Bash"), nullptr);
    QCOMPARE(terminalSpy.count(), 1);

    // Second immediate yolo notification should also dispatch to InTerminal
    // (cooldown only applies to playSound, not other channels)
    manager->notify(NotificationManager::NotificationType::YoloApproval, QStringLiteral("Auto"), QStringLiteral("Approved Write"), nullptr);
    QCOMPARE(terminalSpy.count(), 2);

    // Restore defaults
    manager->setYoloNotificationsEnabled(false);
    manager->setEnabledChannels(NotificationManager::Channel::All);
}

QTEST_MAIN(NotificationManagerTest)

#include "moc_NotificationManagerTest.cpp"
