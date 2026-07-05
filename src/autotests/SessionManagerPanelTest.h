/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONMANAGERPANELTEST_H
#define SESSIONMANAGERPANELTEST_H

#include <QObject>

namespace Konsolai
{

class SessionManagerPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Metadata filtering
    void testAllSessionsEmpty();
    void testAllSessionsLoaded();
    void testPinnedSessionsFilter();
    void testArchivedSessionsFilter();
    void testPinnedExcludesArchived();

    // Pin/Unpin
    void testPinSession();
    void testUnpinSession();
    void testPinNonexistentSession();
    void testPinSession_ImmediateTreeUpdate();

    // Archive
    void testArchiveSession();
    void testArchiveNonexistentSession();

    // Close (new feature)
    void testCloseSessionNotArchived();
    void testCloseNonexistentSession();

    // Mark expired
    void testMarkExpired();
    void testMarkExpiredUnknownSession();

    // Collapsed state
    void testCollapsedToggle();
    void testCollapsedSignal();
    void testCollapsedIdempotent();

    // Dismiss / Restore / Purge lifecycle
    void testDismissSession();
    void testDismissNonexistentSession();
    void testRestoreSession();
    void testRestoreNonexistentSession();
    void testPurgeSession();
    void testPurgeNonexistentSession();
    void testPurgeDismissed();
    void testDismissRestorePurgeRoundTrip();

    // Metadata persistence round-trip
    void testMetadataPersistence();
    void testMetadataYoloPersistence();
    void testMetadataSshFields();
    void testMetadataBudgetPersistence();
    void testMetadataCorruptedJson();
    void testMetadataMissingFields();
    void testMetadataApprovalCountPersistence();

    // Full round-trip with ALL fields
    void testMetadataAllFieldsRoundTrip();
    void testMetadataApprovalLogRoundTrip();
    void testMetadataMultipleSessionsRoundTrip();
    void testMetadataSaveLoadIdempotent();

    // Subagent/subprocess metadata persistence
    void testMetadataSubagentPersistence();
    void testMetadataSubprocessPersistence();
    void testMetadataPromptLabelsPersistence();
    void testMetadataSubagentEmptyNotSerialized();
    void testMetadataSubagentRoundTrip();

    // Remote session registration and restoration
    void testRegisterSessionCapturesRemoteFields();
    void testUnarchiveEmitsRemoteFields();
    void testUnarchiveLocalSessionEmitsNoRemoteFields();
    void testRegisterRemoteSessionRoundTrip();

    // Bulk operations
    void testBulkArchiveMultipleSessions();
    void testBulkDismissMultipleSessions();
    void testBulkDismissOlderThan();
    void testBulkCloseMultipleSessions();

    // Note: inline subagent/subprocess tree rendering tests were
    // removed when the tree was simplified — that detail moved to
    // the "Show Session Structure..." popup dialog.

    // Timer pause/resume (window activation)
    void testPauseResumeIdempotent();
    void testPauseSuppressesTreeUpdates();
    void testPauseSuppressesMetadataSaves();
    void testResumeFlushesDeferred();

    // Register fast-path (tab switch)
    void testRegisterSessionFastPath();

    // Auto-archive old closed sessions
    void testAutoArchiveClosedSessions();
    void testAutoArchiveSkipsPinned();
    void testAutoArchiveSkipsRecent();

    // Merge sessions
    void testMergeSessions_DismissesOthersAndSetsMergedInto();
    void testMergeSessions_RejectsCrossProjectSelection();
    void testMergeSessions_RejectsSingleSession();
    void testUnmergeSession_RestoresDismissedAndClearsMergedInto();
    void testMergePersistsToMetadataFile();
    void testContextMenu_MergeActionShownForMultiSameProject();
    void testContextMenu_MergeActionHiddenForCrossProject();
    void testContextMenu_UnmergeActionShownForMergedAway();

    // Broadcast message
    void testContextMenu_BroadcastShownForGroupWithActiveSessions();
    void testContextMenu_BroadcastHiddenWhenNoActiveSession();
    void testBroadcastMessage_CallsSendTextOnEachActive();
    void testBroadcastMessage_SkipsMissingSessions();

    // Category-map (longest common prefix) tests
    void testCategoryMap_GroupsCommonPrefix();
    void testCategoryMap_GroupsLongestPrefix();
    void testCategoryMap_StandaloneNoRelatives();
    void testCategoryMap_NormalizesUnderscoreToHyphen();
    void testCategoryMap_SingleTokenProjectWithMultiTokenRelatives();
    void testCategoryMap_EmptyAndSpecialInputs();

    // Alias / override / suppression routing in the live tree
    void testAliasReroutesCategoryInTree();
    void testWorkdirOverrideBeatsAlias();
    void testSuppressedCategoryUngroupsToStandalone();
    void testUngroupCategoryClearsAliasIfPresent();
    void testUngroupCategoryAddsSuppressForLcpCategory();

    // Consolidate duplicates predicate
    void testConsolidateDialogOpensWithAllProjectSessions();
    void testCanOfferConsolidate_HiddenWhenSingleSession();

    // User-defined empty categories
    void testCreateUserCategory_ShowsAsEmptyTopLevel();
    void testCreateUserCategory_LosesEmptySuffixWhenProjectDrops();

    // Rename category
    void testRenameCategory_AddsAliasEntry();
    void testRenameCategory_UsesNewLabelInTree();

    // Multi-select drag
    void testMultiDrop_AppliesAllSourceRoutingsInOneCall();

    // LLM-assisted reorganize
    void testBuildTreeInventory_PopulatesProjectsAndCounts();
    void testBuildTreeInventory_IncludesAliasesAndOverrides();
    void testReorganizeApplyAtomicallyPersistsAllChanges();
    void testReorganizeApplyOnEmptyProposalIsNoOp();

    // Vim-style hotkey dispatch (handleTreeAction)
    void testHandleTreeAction_ArchiveArchivesCurrentSession();
    void testHandleTreeAction_PinTogglesPin();
    void testHandleTreeAction_RenameOnCategoryOpensRenameDialog();
    void testHandleTreeAction_RenameOnSessionUpdatesDescription();

    // Subagent detection + state token routing
    void testIsSubagentSession_SessionNameHeuristic();
    void testIsSubagentSession_MetadataFlag();
    void testIsSubagentSession_NotASubagent();
    void testStateTokenFor_SubagentFlagReturnsSubagentToken();
    void testDefaultVisibleStatesExcludesSubagent();
    void testIsSubagentPersistsToMetadataFile();
};

}

#endif // SESSIONMANAGERPANELTEST_H
