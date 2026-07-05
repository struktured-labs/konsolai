/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MERGEPOLICYTEST_H
#define MERGEPOLICYTEST_H

#include <QObject>

namespace Konsolai
{

class MergePolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInheritsLongestDescription();
    void testInheritsLongestDescriptionEmptyAllowed();
    void testPinnedIsOrOfSources();
    void testYoloIsOrOfSources();
    void testLastAccessedTakesMax();
    void testResumeIdPicksLargestJsonl();
    void testResumeIdFallsBackToPrimaryOnTie();
    void testApprovalLogConcatenatedSorted();
    void testApprovalLogDedupesByTimestampAction();
    void testApprovalCountsSummed();
    void testPromptRoundTakesMax();
    void testBudgetUnchangedByDefault();
    void testBudgetMergesWhenOptedIn();
    void testIdentityFieldsNeverChange();
    void testEmptyOthersIsNoOp();
    void testJsonlSizesReturnsZeroForMissing();
};

}

#endif
