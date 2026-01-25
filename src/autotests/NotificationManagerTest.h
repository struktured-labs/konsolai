/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef NOTIFICATIONMANAGERTEST_H
#define NOTIFICATIONMANAGERTEST_H

#include <QObject>

namespace Konsolai
{

class NotificationManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Singleton tests
    void testInstance();
    void testInstanceConsistency();

    // Channel tests
    void testDefaultChannels();
    void testEnableChannel();
    void testDisableChannel();
    void testSetEnabledChannels();
    void testIsChannelEnabled();
    void testChannelFlags();

    // Volume tests
    void testDefaultVolume();
    void testSetVolume();
    void testVolumeClampingMin();
    void testVolumeClampingMax();

    // Static utility tests
    void testIconName();
    void testSoundPath();

    // Signal tests
    void testShowInTerminalNotificationSignal();
    void testSystemTrayStatusChangedSignal();
};

}

#endif // NOTIFICATIONMANAGERTEST_H
