/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONLINKFILTERTEST_H
#define SESSIONLINKFILTERTEST_H

#include <QObject>

class SessionLinkFilterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRegexMatches_data();
    void testRegexMatches();
    void testRegexDoesNotMatchEmails_data();
    void testRegexDoesNotMatchEmails();
    void testRegexDoesNotMatchShortNames();
    void testHotSpotCreatedForMatchingSession();
    void testNoHotSpotForUnknownSession();
    void testHotSpotType();
    void testFuzzyMatchSessionId();
    void testFuzzyMatchSessionName();
    void testFuzzyMatchTaskDescription();
    void testFuzzyMatchPrefixOnDirname();
};

#endif // SESSIONLINKFILTERTEST_H
