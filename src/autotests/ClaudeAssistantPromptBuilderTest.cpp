/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeAssistantPromptBuilderTest.h"

#include <QTest>

#include "../claude/ClaudeAssistantPromptBuilder.h"

using namespace Konsolai;

namespace
{

TreeInventory sampleInventory()
{
    TreeInventory inv;
    TreeInventory::Project p1;
    p1.workingDirectory = QStringLiteral("/home/u/projects/konsolai");
    p1.basename = QStringLiteral("konsolai");
    p1.description = QStringLiteral("terminal manager");
    p1.sessionCount = 3;
    inv.projects << p1;

    TreeInventory::Project p2;
    p2.workingDirectory = QStringLiteral("/home/u/projects/konsolai-handbook");
    p2.basename = QStringLiteral("konsolai-handbook");
    p2.description = QString();
    p2.sessionCount = 1;
    inv.projects << p2;

    TreeInventory::Project p3;
    p3.workingDirectory = QStringLiteral("/home/u/projects/cowir-frontend");
    p3.basename = QStringLiteral("cowir-frontend");
    p3.sessionCount = 2;
    inv.projects << p3;

    TreeInventory::Category konsolaiCat;
    konsolaiCat.key = QStringLiteral("konsolai");
    konsolaiCat.projectWorkdirs << QStringLiteral("/home/u/projects/konsolai") << QStringLiteral("/home/u/projects/konsolai-handbook");
    inv.categories << konsolaiCat;

    inv.userCategories << QStringLiteral("my-stuff");
    inv.existingAliases.insert(QStringLiteral("cowardly-irregular"), QStringLiteral("cowir"));
    inv.existingWorkdirOverrides.insert(QStringLiteral("/home/u/projects/foo"), QStringLiteral("misc"));
    inv.existingSuppressedCategories << QStringLiteral("legacy");

    return inv;
}

} // namespace

void ClaudeAssistantPromptBuilderTest::testBuildReorganizePrompt_ListsAllProjects()
{
    const TreeInventory inv = sampleInventory();
    const QString prompt = buildReorganizePrompt(inv, QStringLiteral("group konsolai things"));
    QVERIFY(prompt.contains(QStringLiteral("/home/u/projects/konsolai")));
    QVERIFY(prompt.contains(QStringLiteral("/home/u/projects/konsolai-handbook")));
    QVERIFY(prompt.contains(QStringLiteral("/home/u/projects/cowir-frontend")));
    // Session counts appear.
    QVERIFY(prompt.contains(QStringLiteral("3 sessions")));
    QVERIFY(prompt.contains(QStringLiteral("1 session")));
    // Description on p1 flows through.
    QVERIFY(prompt.contains(QStringLiteral("terminal manager")));
}

void ClaudeAssistantPromptBuilderTest::testBuildReorganizePrompt_ListsExistingAliases()
{
    const TreeInventory inv = sampleInventory();
    const QString prompt = buildReorganizePrompt(inv, QStringLiteral("hi"));
    QVERIFY(prompt.contains(QStringLiteral("cowardly-irregular")));
    QVERIFY(prompt.contains(QStringLiteral("cowir")));
    QVERIFY(prompt.contains(QStringLiteral("misc")));
    QVERIFY(prompt.contains(QStringLiteral("legacy")));
    // The user-created category is surfaced.
    QVERIFY(prompt.contains(QStringLiteral("my-stuff")));
}

void ClaudeAssistantPromptBuilderTest::testBuildReorganizePrompt_IncludesUserIntentText()
{
    const TreeInventory inv = sampleInventory();
    const QString prompt = buildReorganizePrompt(inv, QStringLiteral("Please rename cowir to Corridor Games"));
    QVERIFY(prompt.contains(QStringLiteral("Please rename cowir to Corridor Games")));
}

void ClaudeAssistantPromptBuilderTest::testBuildReorganizePrompt_RequestsStrictJson()
{
    const TreeInventory inv = sampleInventory();
    const QString prompt = buildReorganizePrompt(inv, QStringLiteral("hi"));
    QVERIFY(prompt.contains(QStringLiteral("categoryAliases")));
    QVERIFY(prompt.contains(QStringLiteral("workdirOverrides")));
    QVERIFY(prompt.contains(QStringLiteral("suppressedCategories")));
    QVERIFY(prompt.contains(QStringLiteral("userCategories")));
    QVERIFY(prompt.contains(QStringLiteral("rationale")));
    // Explicit instruction to avoid markdown.
    QVERIFY(prompt.contains(QStringLiteral("no markdown")));
}

void ClaudeAssistantPromptBuilderTest::testParseReorganizeResponse_MinimalValid()
{
    QString err;
    const QString body = QStringLiteral("{\"categoryAliases\":{\"a\":\"b\"},\"rationale\":\"x\"}");
    const ReorganizeProposal p = parseReorganizeResponse(body, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(p.categoryAliases.value(QStringLiteral("a")), QStringLiteral("b"));
    QCOMPARE(p.rationale, QStringLiteral("x"));
}

void ClaudeAssistantPromptBuilderTest::testParseReorganizeResponse_StripsMarkdownFences()
{
    QString err;
    const QString body = QStringLiteral("```json\n{\"categoryAliases\":{\"a\":\"b\"},\"rationale\":\"ok\"}\n```");
    const ReorganizeProposal p = parseReorganizeResponse(body, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(p.categoryAliases.value(QStringLiteral("a")), QStringLiteral("b"));
    QCOMPARE(p.rationale, QStringLiteral("ok"));
}

void ClaudeAssistantPromptBuilderTest::testParseReorganizeResponse_MissingFieldsBecomeEmpty()
{
    QString err;
    const QString body = QStringLiteral("{\"categoryAliases\":{\"a\":\"b\"}}");
    const ReorganizeProposal p = parseReorganizeResponse(body, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(p.categoryAliases.value(QStringLiteral("a")), QStringLiteral("b"));
    QVERIFY(p.workdirOverrides.isEmpty());
    QVERIFY(p.suppressedCategories.isEmpty());
    QVERIFY(p.userCategories.isEmpty());
    QVERIFY(p.rationale.isEmpty());
}

void ClaudeAssistantPromptBuilderTest::testParseReorganizeResponse_MalformedJsonReportsError()
{
    QString err;
    // Missing closing brace and no isolated {...} — clearly bad.
    const QString body = QStringLiteral("not json at all!");
    const ReorganizeProposal p = parseReorganizeResponse(body, &err);
    QVERIFY(!err.isEmpty());
    QVERIFY(p.isEmpty());
    QVERIFY(p.rationale.isEmpty());
}

void ClaudeAssistantPromptBuilderTest::testParseReorganizeResponse_ExtraFieldsIgnored()
{
    QString err;
    const QString body = QStringLiteral("{\"categoryAliases\":{\"a\":\"b\"},\"extra\":\"field\",\"nested\":{\"foo\":1},\"rationale\":\"r\"}");
    const ReorganizeProposal p = parseReorganizeResponse(body, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(p.categoryAliases.value(QStringLiteral("a")), QStringLiteral("b"));
    QCOMPARE(p.rationale, QStringLiteral("r"));
}

void ClaudeAssistantPromptBuilderTest::testBuildSuggestNamePrompt_IncludesAllProjects()
{
    const QStringList bases = {QStringLiteral("foo"), QStringLiteral("bar-baz"), QStringLiteral("qux")};
    const QStringList descs = {QStringLiteral("a widget"), QString(), QStringLiteral("a gadget")};
    const QString prompt = buildSuggestNamePrompt(bases, descs);
    QVERIFY(prompt.contains(QStringLiteral("foo")));
    QVERIFY(prompt.contains(QStringLiteral("bar-baz")));
    QVERIFY(prompt.contains(QStringLiteral("qux")));
    QVERIFY(prompt.contains(QStringLiteral("a widget")));
    QVERIFY(prompt.contains(QStringLiteral("a gadget")));
    // Constraint reminders.
    QVERIFY(prompt.contains(QStringLiteral("kebab-case")));
    QVERIFY(prompt.contains(QStringLiteral("1 to 3 words")));
}

void ClaudeAssistantPromptBuilderTest::testParseSuggestNameResponse_TrimsWhitespaceAndPunctuation()
{
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("corridor-games.\n")), QStringLiteral("corridor-games"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("  spaced-name  ")), QStringLiteral("spaced-name"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("name!?;")), QStringLiteral("name"));
}

void ClaudeAssistantPromptBuilderTest::testParseSuggestNameResponse_LowercasesAndHyphenatesSpaces()
{
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("Corridor Games")), QStringLiteral("corridor-games"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("BIG NAME HERE")), QStringLiteral("big-name-here"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("mixed_Under_Scores")), QStringLiteral("mixed-under-scores"));
}

void ClaudeAssistantPromptBuilderTest::testParseSuggestNameResponse_StripsQuotesAndBackticks()
{
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("\"my-name\"")), QStringLiteral("my-name"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("'my-name'")), QStringLiteral("my-name"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("`my-name`")), QStringLiteral("my-name"));
    QCOMPARE(parseSuggestNameResponse(QStringLiteral("```json\nmy-name\n```")), QStringLiteral("my-name"));
}

QTEST_GUILESS_MAIN(Konsolai::ClaudeAssistantPromptBuilderTest)
#include "moc_ClaudeAssistantPromptBuilderTest.cpp"
