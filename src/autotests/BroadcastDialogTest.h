/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BROADCASTDIALOGTEST_H
#define BROADCASTDIALOGTEST_H

#include <QObject>

namespace Konsolai
{

class BroadcastDialogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testPreviewUpdatesOnMessageEdit();
    void testPreviewUsesFirstCheckedRecipient();
    void testBroadcastDisabledWhenNoneChecked();
    void testHelpButtonInsertsVarAtCursor();
    void testSelectedSessionIdsExcludesUnchecked();
    void testReturnsTemplateUnsubstituted();
};

}

#endif
