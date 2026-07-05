/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BroadcastPolicyTest.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include "../claude/BroadcastPolicy.h"
#include "../claude/SessionManagerPanel.h"

using namespace Konsolai;

namespace
{

BroadcastVars makeVars(const QString &sessionName = QStringLiteral("foo"), const QString &project = QStringLiteral("cowir"), int index = 1, int count = 1)
{
    BroadcastVars v;
    v.sessionId = QStringLiteral("session-id-123");
    v.sessionName = sessionName;
    v.project = project;
    v.workingDirectory = QStringLiteral("/home/u/cowir-pcc-base");
    v.tmuxSession = QStringLiteral("konsolai-test-foo");
    v.index = index;
    v.count = count;
    return v;
}

} // namespace

void BroadcastPolicyTest::testSubstitutesKnownVar()
{
    const BroadcastVars vars = makeVars(QStringLiteral("foo"));
    QCOMPARE(substituteTemplate(QStringLiteral("{session_name}"), vars), QStringLiteral("foo"));
    QCOMPARE(substituteTemplate(QStringLiteral("hi {session_name}!"), vars), QStringLiteral("hi foo!"));
}

void BroadcastPolicyTest::testLeavesUnknownVarLiteral()
{
    const BroadcastVars vars = makeVars();
    // Unknown variable stays literal — no error, no replacement, no consumed braces.
    QCOMPARE(substituteTemplate(QStringLiteral("{not_a_var}"), vars), QStringLiteral("{not_a_var}"));
    QCOMPARE(substituteTemplate(QStringLiteral("before {not_a_var} after"), vars), QStringLiteral("before {not_a_var} after"));
}

void BroadcastPolicyTest::testHandlesUnclosedBrace()
{
    const BroadcastVars vars = makeVars(QStringLiteral("foo"));
    // Unclosed `{` — the spec says: leave everything from `{` on literal.
    QCOMPARE(substituteTemplate(QStringLiteral("hello {session_name"), vars), QStringLiteral("hello {session_name"));
    // Even when the unclosed brace occurs after other substitutions, the prefix
    // resolves and the unclosed tail stays literal.
    QCOMPARE(substituteTemplate(QStringLiteral("{session_name} and {oops"), vars), QStringLiteral("foo and {oops"));
}

void BroadcastPolicyTest::testHandlesEmptyBraces()
{
    const BroadcastVars vars = makeVars();
    // `{}` is not a known key, so it stays literal.
    QCOMPARE(substituteTemplate(QStringLiteral("a {} b"), vars), QStringLiteral("a {} b"));
}

void BroadcastPolicyTest::testMultipleVarsInOneString()
{
    const BroadcastVars vars = makeVars(QStringLiteral("foo"), QStringLiteral("cowir"), 2, 5);
    QCOMPARE(substituteTemplate(QStringLiteral("[{index}/{count}] {project}: {session_name}"), vars), QStringLiteral("[2/5] cowir: foo"));
}

void BroadcastPolicyTest::testRepeatedVar()
{
    const BroadcastVars vars = makeVars(QStringLiteral("foo"));
    QCOMPARE(substituteTemplate(QStringLiteral("{session_name} and {session_name}"), vars), QStringLiteral("foo and foo"));
}

void BroadcastPolicyTest::testIndexAndCount()
{
    const BroadcastVars vars = makeVars(QStringLiteral("foo"), QStringLiteral("cowir"), 3, 7);
    QCOMPARE(substituteTemplate(QStringLiteral("{index}"), vars), QStringLiteral("3"));
    QCOMPARE(substituteTemplate(QStringLiteral("{count}"), vars), QStringLiteral("7"));
    // Sanity: both rendered as integers, not as e.g. "3.0".
    QCOMPARE(substituteTemplate(QStringLiteral("{index}/{count}"), vars), QStringLiteral("3/7"));
}

void BroadcastPolicyTest::testNoVarsIsIdentity()
{
    const BroadcastVars vars = makeVars();
    const QString msg = QStringLiteral("hello world, this has no template vars at all.");
    QCOMPARE(substituteTemplate(msg, vars), msg);
}

void BroadcastPolicyTest::testEmptyTemplate()
{
    const BroadcastVars vars = makeVars();
    QCOMPARE(substituteTemplate(QString(), vars), QString());
    QCOMPARE(substituteTemplate(QStringLiteral(""), vars), QStringLiteral(""));
}

void BroadcastPolicyTest::testTemplateVariableNamesIsNonEmptyAndUnique()
{
    const QStringList names = templateVariableNames();
    QVERIFY(!names.isEmpty());
    const QSet<QString> unique(names.begin(), names.end());
    QCOMPARE(unique.size(), names.size());
    // Every advertised name MUST actually substitute when used in a template.
    BroadcastVars vars = makeVars();
    for (const QString &name : names) {
        const QString tmpl = QStringLiteral("{%1}").arg(name);
        const QString rendered = substituteTemplate(tmpl, vars);
        QVERIFY2(rendered != tmpl, qPrintable(QStringLiteral("Advertised variable %1 did not substitute").arg(name)));
    }
}

void BroadcastPolicyTest::testBuildVarsFromMetadata()
{
    SessionMetadata meta;
    meta.sessionId = QStringLiteral("the-id");
    meta.sessionName = QStringLiteral("konsolai-test-xyz");
    meta.workingDirectory = QStringLiteral("/home/u/cowir-pcc-base");

    // With explicit displayName provided, that wins (used by the dialog row).
    {
        const BroadcastVars v = buildVars(meta, QStringLiteral("Explicit Title"), QStringLiteral("cowir"), 1, 4);
        QCOMPARE(v.sessionName, QStringLiteral("Explicit Title"));
        QCOMPARE(v.project, QStringLiteral("cowir"));
        QCOMPARE(v.sessionId, QStringLiteral("the-id"));
        QCOMPARE(v.workingDirectory, QStringLiteral("/home/u/cowir-pcc-base"));
        QCOMPARE(v.tmuxSession, QStringLiteral("konsolai-test-xyz"));
        QCOMPARE(v.index, 1);
        QCOMPARE(v.count, 4);
    }

    // With empty displayName + description set, description wins.
    {
        meta.description = QStringLiteral("  My description  ");
        const BroadcastVars v = buildVars(meta, QString(), QStringLiteral("cowir"), 2, 3);
        QCOMPARE(v.sessionName, QStringLiteral("My description"));
    }

    // With empty displayName + empty description, basename(workingDirectory) wins.
    {
        meta.description.clear();
        const BroadcastVars v = buildVars(meta, QString(), QStringLiteral("cowir"), 1, 1);
        QCOMPARE(v.sessionName, QStringLiteral("cowir-pcc-base"));
    }

    // With empty displayName + empty description + empty workingDirectory, falls back
    // to meta.sessionName so the recipient is never anonymous.
    {
        meta.workingDirectory.clear();
        const BroadcastVars v = buildVars(meta, QString(), QStringLiteral("cowir"), 1, 1);
        QCOMPARE(v.sessionName, QStringLiteral("konsolai-test-xyz"));
    }
}

QTEST_GUILESS_MAIN(BroadcastPolicyTest)
#include "moc_BroadcastPolicyTest.cpp"
