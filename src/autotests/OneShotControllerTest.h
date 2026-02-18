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
    void testInitialPhase();
    void testConfigDefaults();
    void testResultDefaults();
    void testIsRunning();
    void testPhaseTransitions();
    void testFormatBudgetProgress();
    void testElapsedMinutes();
    void testYoloLevelMapping();
    void testBudgetPolicySoft();
    void testBudgetPolicyHard();
    void testGsdPromptPrefix();
    void testOneShotConfig();
};

} // namespace Konsolai

#endif // ONESHOTCONTROLLERTEST_H
