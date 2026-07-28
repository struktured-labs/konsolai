/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TOKENUSAGETEST_H
#define TOKENUSAGETEST_H

#include <QObject>

namespace Konsolai
{

class TokenUsageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaultZero();
    void testTotalTokens();
    void testEstimatedCostUSD();
    void testEstimatedCostZero();
    void testFormatCompactSmall();
    void testFormatCompactThousands();
    void testFormatCompactMillions();
    void testContextWindowSizeOpus();
    void testContextWindowSizeOpus5();
    void testContextWindowSizeSonnet();
    void testContextWindowSizeSonnet5();
    void testContextWindowSizeHaiku();
    void testContextWindowSizeDefault();
    void testContextPercentBasic();
    void testContextPercentNoData();
    void testContextPercentHigh();
    void testContextPercentOpus5NeverExceeds100();
};

}

#endif // TOKENUSAGETEST_H
