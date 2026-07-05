/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSION_TREE_WIDGET_TEST_H
#define SESSION_TREE_WIDGET_TEST_H

#include <QObject>

namespace Konsolai
{

class SessionTreeWidgetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testMimeDataContainsCompositeKey();
    void testCanDropOnCategoryTarget();
    void testCantDropOnProjectGroup();
    void testCantDropOnSessionLeaf();
    void testCantDropOnSelf();
    void testDropEmitsSignalWithSourceAndTarget();
    void testMimeDataIsNullForSessionLeaves();
    void testCanDropRejectsInvalidSource();

    // Multi-select drag
    void testMimeDataEncodesMultipleSourcesNewlineSeparated();
    void testDropEmitsQStringListSignal();
    void testMimeDataFiltersUndraggableItemsFromSelection();

    // Vim-style hotkey navigation
    void testJKMapsToArrowNavigation();
    void testHLCollapsesAndExpands();
    void testGGGoesToFirstItem_MultiKeySequence();
    void testGGSingleGTimesOut();
    void testGCapitalGoesToLast();
    void testSlashEmitsFocusFilterRequested();
    void testEscapeEmitsEscapePressed();
    void testDDDismissesCurrentSession_MultiKeySequence();
    void testActionKeys_SingleShotEmissions();
    void testEnterOnLeafEmitsAttach();
    void testEnterOnCategoryTogglesExpanded();
    void testCtrlModifiedKeyFallsThroughToBase();
};

} // namespace Konsolai

#endif // SESSION_TREE_WIDGET_TEST_H
