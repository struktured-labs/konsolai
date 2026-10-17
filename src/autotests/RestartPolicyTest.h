/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef RESTARTPOLICYTEST_H
#define RESTARTPOLICYTEST_H

#include <QObject>

namespace Konsolai
{

class RestartPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testTabbedSessionsRestartInPlace();
    void testTablessSessionsAreReopenedFirst();
    void testRemoteSessionsAreSkipped();
    void testDuplicateIdsCountedOnce();
    void testEmptyInputIsEmptyPlan();
    void testOrderIsPreserved();
};

}

#endif // RESTARTPOLICYTEST_H
