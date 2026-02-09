/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEREMOTEDISCOVERYTEST_H
#define CLAUDEREMOTEDISCOVERYTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeRemoteDiscoveryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // KonsolaiSettings SSH hosts
    void testSshDiscoveryHostsPersistence();

    // Remote discovery output parsing
    void testParseRemoteDiscoveryOutputBasic();
    void testParseRemoteDiscoveryOutputEmpty();
    void testParseRemoteDiscoveryOutputMultiple();
    void testParseRemoteDiscoveryOutputExtraWhitespace();

    // ClaudeSessionState remote fields
    void testSessionStateRemoteFieldsSerialization();
};

}

#endif // CLAUDEREMOTEDISCOVERYTEST_H
