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
};

}

#endif // TOKENUSAGETEST_H
