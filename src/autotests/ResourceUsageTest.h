/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef RESOURCEUSAGETEST_H
#define RESOURCEUSAGETEST_H

#include <QObject>

namespace Konsolai
{

class ResourceUsageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ResourceUsage::formatCompact tests
    void testFormatZero();
    void testFormatBytes();
    void testFormatKilobytes();
    void testFormatMegabytes();
    void testFormatGigabytes();
    void testFormatCpuLow();
    void testFormatCpuHigh();

    // TokenUsage edge cases
    void testCacheTokensCostEstimate();
    void testFormatCompactWithCacheTokens();
    void testEstimatedCostMixed();

    // ApprovalLogEntry
    void testApprovalLogEntryDefaults();
};

}

#endif // RESOURCEUSAGETEST_H
