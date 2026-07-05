/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BROADCASTPOLICYTEST_H
#define BROADCASTPOLICYTEST_H

#include <QObject>

namespace Konsolai
{

class BroadcastPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSubstitutesKnownVar();
    void testLeavesUnknownVarLiteral();
    void testHandlesUnclosedBrace();
    void testHandlesEmptyBraces();
    void testMultipleVarsInOneString();
    void testRepeatedVar();
    void testIndexAndCount();
    void testNoVarsIsIdentity();
    void testEmptyTemplate();
    void testTemplateVariableNamesIsNonEmptyAndUnique();
    void testBuildVarsFromMetadata();
};

}

#endif
