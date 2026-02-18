/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BUDGETCONTROLLERTEST_H
#define BUDGETCONTROLLERTEST_H

#include <QObject>

namespace Konsolai
{

class BudgetControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testBudgetDefaults();
    void testTimeBudgetWarning();
    void testCostBudgetExceeded();
    void testTokenBudgetExceeded();
    void testResourceGateDebounce();
    void testTokenVelocity();
    void testBudgetSerialization();
    void testShouldBlockYolo();
};

}

#endif // BUDGETCONTROLLERTEST_H
