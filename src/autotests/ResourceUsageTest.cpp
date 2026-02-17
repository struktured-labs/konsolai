/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ResourceUsageTest.h"

// Qt
#include <QTest>

// Konsolai
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

// ============================================================
// ResourceUsage::formatCompact tests
// ============================================================

void ResourceUsageTest::testFormatZero()
{
    ResourceUsage usage;
    QString formatted = usage.formatCompact();
    // CPU should be "0.0%", RSS should be "0B"
    QVERIFY(formatted.contains(QStringLiteral("0.0%")));
    QVERIFY(formatted.contains(QStringLiteral("0B")));
}

void ResourceUsageTest::testFormatBytes()
{
    ResourceUsage usage;
    usage.cpuPercent = 1.5;
    usage.rssBytes = 512;

    QString formatted = usage.formatCompact();
    QVERIFY(formatted.contains(QStringLiteral("1.5%")));
    QVERIFY(formatted.contains(QStringLiteral("512B")));
}

void ResourceUsageTest::testFormatKilobytes()
{
    ResourceUsage usage;
    usage.cpuPercent = 5.0;
    usage.rssBytes = 512 * 1024; // 512 KB

    QString formatted = usage.formatCompact();
    QVERIFY(formatted.contains(QStringLiteral("5.0%")));
    QVERIFY(formatted.contains(QStringLiteral("512K")));
}

void ResourceUsageTest::testFormatMegabytes()
{
    ResourceUsage usage;
    usage.cpuPercent = 25.0;
    usage.rssBytes = 256 * 1048576ULL; // 256 MB

    QString formatted = usage.formatCompact();
    QVERIFY(formatted.contains(QStringLiteral("25%")));
    QVERIFY(formatted.contains(QStringLiteral("256M")));
}

void ResourceUsageTest::testFormatGigabytes()
{
    ResourceUsage usage;
    usage.cpuPercent = 100.0;
    usage.rssBytes = 2ULL * 1073741824ULL; // 2 GB

    QString formatted = usage.formatCompact();
    QVERIFY(formatted.contains(QStringLiteral("100%")));
    QVERIFY(formatted.contains(QStringLiteral("2.0G")));
}

void ResourceUsageTest::testFormatCpuLow()
{
    // CPU < 10% should show one decimal place
    ResourceUsage usage;
    usage.cpuPercent = 3.7;
    usage.rssBytes = 0;

    QString formatted = usage.formatCompact();
    QVERIFY(formatted.contains(QStringLiteral("3.7%")));
}

void ResourceUsageTest::testFormatCpuHigh()
{
    // CPU >= 10% should show integer
    ResourceUsage usage;
    usage.cpuPercent = 67.3;
    usage.rssBytes = 0;

    QString formatted = usage.formatCompact();
    QVERIFY(formatted.contains(QStringLiteral("67%")));
    // Should NOT have a decimal
    QVERIFY(!formatted.contains(QStringLiteral("67.3%")));
}

// ============================================================
// TokenUsage edge cases (not covered by TokenUsageTest)
// ============================================================

void ResourceUsageTest::testCacheTokensCostEstimate()
{
    TokenUsage usage;
    usage.inputTokens = 0;
    usage.outputTokens = 0;
    usage.cacheReadTokens = 1000000; // 1M cache read tokens
    usage.cacheCreationTokens = 500000; // 500K cache creation tokens

    // Cost: (0 * 3.0 + 0 * 15.0 + 500000 * 0.30 + 1000000 * 0.30) / 1000000
    // = (0 + 0 + 150000 + 300000) / 1000000 = 0.45
    QVERIFY(qAbs(usage.estimatedCostUSD() - 0.45) < 0.001);
}

void ResourceUsageTest::testFormatCompactWithCacheTokens()
{
    TokenUsage usage;
    usage.inputTokens = 10000;
    usage.outputTokens = 5000;
    usage.cacheReadTokens = 20000;
    usage.cacheCreationTokens = 3000;

    QString formatted = usage.formatCompact();
    // Input side: 10000 + 20000 + 3000 = 33000 → "33.0K↑"
    QVERIFY(formatted.contains(QStringLiteral("33.0K")));
    // Output side: 5000 → "5.0K↓"
    QVERIFY(formatted.contains(QStringLiteral("5.0K")));
}

void ResourceUsageTest::testEstimatedCostMixed()
{
    TokenUsage usage;
    usage.inputTokens = 500000; // 500K
    usage.outputTokens = 50000; // 50K
    usage.cacheReadTokens = 100000; // 100K
    usage.cacheCreationTokens = 10000; // 10K

    // Cost: (500000*3.0 + 50000*15.0 + 10000*0.30 + 100000*0.30) / 1000000
    // = (1500000 + 750000 + 3000 + 30000) / 1000000 = 2.283
    QVERIFY(qAbs(usage.estimatedCostUSD() - 2.283) < 0.001);
}

// ============================================================
// ApprovalLogEntry
// ============================================================

void ResourceUsageTest::testApprovalLogEntryDefaults()
{
    ApprovalLogEntry entry;
    QVERIFY(entry.toolName.isEmpty());
    QVERIFY(entry.action.isEmpty());
    QCOMPARE(entry.yoloLevel, 0);
    QCOMPARE(entry.totalTokens, quint64(0));
    QCOMPARE(entry.estimatedCostUSD, 0.0);
    // timestamp should be null/invalid by default
    QVERIFY(!entry.timestamp.isValid());
}

QTEST_GUILESS_MAIN(ResourceUsageTest)

#include "moc_ResourceUsageTest.cpp"
