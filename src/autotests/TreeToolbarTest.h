/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TREETOOLBARTEST_H
#define TREETOOLBARTEST_H

#include <QObject>

namespace Konsolai
{

class TreeToolbarTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testButtonsHaveObjectNames();
    void testExpandAll_RevealsNestedItems();
    void testCollapseAll_HidesNestedItems();
    void testButtonsAreNoOps_AfterTreeDeleted();
};

}

#endif // TREETOOLBARTEST_H
