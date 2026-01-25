/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TMUXMANAGERTEST_H
#define TMUXMANAGERTEST_H

#include <QObject>

namespace Konsolai
{

class TmuxManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Static utility tests
    void testGenerateSessionId();
    void testBuildSessionName();
    void testBuildSessionNameWithTemplate();
    void testBuildSessionNameCustomTemplate();

    // Command building tests
    void testBuildNewSessionCommand();
    void testBuildNewSessionCommandWithWorkingDir();
    void testBuildAttachCommand();
    void testBuildKillCommand();
    void testBuildDetachCommand();

    // Availability tests
    void testIsAvailable();
    void testVersion();
};

}

#endif // TMUXMANAGERTEST_H
