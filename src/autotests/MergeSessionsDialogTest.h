/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MERGESESSIONSDIALOGTEST_H
#define MERGESESSIONSDIALOGTEST_H

#include <QObject>

namespace Konsolai
{

class MergeSessionsDialogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testPrimaryDefaultsToMostRecent();
    void testSummaryUpdatesOnPrimaryChange();
    void testSummaryUpdatesOnCheckboxToggle();
    void testReturnsChoicesAfterAccept();
};

}

#endif
