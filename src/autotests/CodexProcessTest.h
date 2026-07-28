/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CODEXPROCESSTEST_H
#define CODEXPROCESSTEST_H

#include <QObject>

namespace Konsolai
{

class CodexProcessTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBuildCommandFresh();
    void testBuildCommandWithWorkingDir();
    void testBuildCommandResume();
    void testBuildCommandResumeIsSubcommandBeforeOptions();
    void testBuildCommandWithModel();
    void testBuildCommandWithAdditionalArgs();
    void testSessionsRoot();
    void testParseSessionMeta();
    void testParseSessionMetaMissingFile();
    void testParseSessionMetaNotSessionMeta();
    void testParseSessionMetaMalformedJson();
    void testDiscoverConversationsFiltersByWorkingDir();
    void testDiscoverConversationsSortedNewestFirst();
};

}

#endif // CODEXPROCESSTEST_H
