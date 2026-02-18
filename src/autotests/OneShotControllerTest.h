/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ONESHOTCONTROLLERTEST_H
#define ONESHOTCONTROLLERTEST_H

#include <QObject>

namespace Konsolai
{

class OneShotControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testDefaultConfig();
    void testDefaultResult();
    void testSetConfig();
    void testIsRunning();
    void testFormatBudgetStatus();
    void testFormatStateLabel();
};

}

#endif // ONESHOTCONTROLLERTEST_H
