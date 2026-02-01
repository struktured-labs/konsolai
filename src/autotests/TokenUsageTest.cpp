/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "TokenUsageTest.h"

// Qt
#include <QTest>

// Konsolai
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

void TokenUsageTest::testDefaultZero()
{
    TokenUsage usage;

    QCOMPARE(usage.inputTokens, quint64(0));
    QCOMPARE(usage.outputTokens, quint64(0));
    QCOMPARE(usage.cacheReadTokens, quint64(0));
    QCOMPARE(usage.cacheCreationTokens, quint64(0));
    QCOMPARE(usage.totalTokens(), quint64(0));
    QCOMPARE(usage.estimatedCostUSD(), 0.0);
}

void TokenUsageTest::testTotalTokens()
{
    TokenUsage usage;
    usage.inputTokens = 1000;
    usage.outputTokens = 500;
    usage.cacheReadTokens = 200;
    usage.cacheCreationTokens = 100;

    QCOMPARE(usage.totalTokens(), quint64(1800));
}

void TokenUsageTest::testEstimatedCostUSD()
{
    TokenUsage usage;
    usage.inputTokens = 1000000; // 1M input tokens
    usage.outputTokens = 100000; // 100K output tokens
    usage.cacheReadTokens = 0;
    usage.cacheCreationTokens = 0;

    // 1M * 3.0 / 1M + 100K * 15.0 / 1M = 3.0 + 1.5 = 4.5
    QVERIFY(qAbs(usage.estimatedCostUSD() - 4.5) < 0.001);
}

void TokenUsageTest::testEstimatedCostZero()
{
    TokenUsage usage;
    QCOMPARE(usage.estimatedCostUSD(), 0.0);
}

void TokenUsageTest::testFormatCompactSmall()
{
    TokenUsage usage;
    usage.inputTokens = 500;
    usage.outputTokens = 200;

    QString formatted = usage.formatCompact();
    // Should show raw numbers (< 1K)
    QVERIFY(formatted.contains(QStringLiteral("500")));
    QVERIFY(formatted.contains(QStringLiteral("200")));
}

void TokenUsageTest::testFormatCompactThousands()
{
    TokenUsage usage;
    usage.inputTokens = 45000;
    usage.outputTokens = 12000;

    QString formatted = usage.formatCompact();
    // Input side: 45000 + 0 + 0 = 45K
    QVERIFY(formatted.contains(QStringLiteral("45.0K")));
    // Output side: 12K
    QVERIFY(formatted.contains(QStringLiteral("12.0K")));
}

void TokenUsageTest::testFormatCompactMillions()
{
    TokenUsage usage;
    usage.inputTokens = 2500000;
    usage.outputTokens = 1200000;

    QString formatted = usage.formatCompact();
    // Input: 2.5M
    QVERIFY(formatted.contains(QStringLiteral("2.5M")));
    // Output: 1.2M
    QVERIFY(formatted.contains(QStringLiteral("1.2M")));
}

QTEST_GUILESS_MAIN(TokenUsageTest)

#include "moc_TokenUsageTest.cpp"
