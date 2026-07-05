/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef REORGANIZETREEDIALOGTEST_H
#define REORGANIZETREEDIALOGTEST_H

#include <QObject>

namespace Konsolai
{

class ReorganizeTreeDialogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testShowsAllProjectsInInventory();
    void testUncheckingProjectExcludesFromPrompt();
    void testAskButtonDisabledWhenIntentEmpty();
    void testAskButtonRunsWithNonEmptyIntent();
    void testApplyButtonStoresProposal();
    void testEmptyProposalDisablesApply();
    void testFailureShowsRetryButton();
};

} // namespace Konsolai

#endif // REORGANIZETREEDIALOGTEST_H
