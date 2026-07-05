/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEASSISTANTPROMPTBUILDERTEST_H
#define CLAUDEASSISTANTPROMPTBUILDERTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeAssistantPromptBuilderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Reorganize prompt.
    void testBuildReorganizePrompt_ListsAllProjects();
    void testBuildReorganizePrompt_ListsExistingAliases();
    void testBuildReorganizePrompt_IncludesUserIntentText();
    void testBuildReorganizePrompt_RequestsStrictJson();

    // Reorganize response parsing.
    void testParseReorganizeResponse_MinimalValid();
    void testParseReorganizeResponse_StripsMarkdownFences();
    void testParseReorganizeResponse_MissingFieldsBecomeEmpty();
    void testParseReorganizeResponse_MalformedJsonReportsError();
    void testParseReorganizeResponse_ExtraFieldsIgnored();

    // Suggest name prompt + response.
    void testBuildSuggestNamePrompt_IncludesAllProjects();
    void testParseSuggestNameResponse_TrimsWhitespaceAndPunctuation();
    void testParseSuggestNameResponse_LowercasesAndHyphenatesSpaces();
    void testParseSuggestNameResponse_StripsQuotesAndBackticks();
};

} // namespace Konsolai

#endif // CLAUDEASSISTANTPROMPTBUILDERTEST_H
