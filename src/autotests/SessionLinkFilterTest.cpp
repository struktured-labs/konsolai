/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionLinkFilterTest.h"

#include "filterHotSpots/SessionLinkFilter.h"
#include "filterHotSpots/SessionLinkHotSpot.h"

#include "claude/ClaudeSession.h"
#include "claude/ClaudeSessionRegistry.h"

#include <QRegularExpression>
#include <QTest>

using namespace Konsole;

QTEST_GUILESS_MAIN(SessionLinkFilterTest)

// ========== Regex tests ==========

void SessionLinkFilterTest::testRegexMatches_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expectedCapture");

    QTest::newRow("simple_name") << " @konsolai" << "konsolai";
    QTest::newRow("with_dash") << " @my-project" << "my-project";
    QTest::newRow("with_dot") << " @project.v2" << "project.v2";
    QTest::newRow("with_underscore") << " @my_project" << "my_project";
    QTest::newRow("session_name") << " @konsolai-default-a1b2c3d4" << "konsolai-default-a1b2c3d4";
    QTest::newRow("start_of_line") << "@project-name" << "project-name";
    QTest::newRow("after_tab") << "\t@project-name" << "project-name";
    QTest::newRow("after_newline") << "\n@project-name" << "project-name";
    QTest::newRow("mid_sentence") << "see @other-session for details" << "other-session";
}

void SessionLinkFilterTest::testRegexMatches()
{
    QFETCH(QString, input);
    QFETCH(QString, expectedCapture);

    QRegularExpressionMatchIterator it = SessionLinkFilter::SessionLinkRegExp.globalMatch(input);
    QVERIFY2(it.hasNext(), qPrintable(QStringLiteral("Regex should match in: ") + input));

    QRegularExpressionMatch match = it.next();
    QCOMPARE(match.captured(1), expectedCapture);
}

void SessionLinkFilterTest::testRegexDoesNotMatchEmails_data()
{
    QTest::addColumn<QString>("input");

    QTest::newRow("email_simple") << "user@example.com";
    QTest::newRow("email_with_dots") << "first.last@domain.org";
    QTest::newRow("email_with_plus") << "user+tag@gmail.com";
}

void SessionLinkFilterTest::testRegexDoesNotMatchEmails()
{
    QFETCH(QString, input);

    QRegularExpressionMatchIterator it = SessionLinkFilter::SessionLinkRegExp.globalMatch(input);
    // The regex uses a lookbehind for whitespace or start-of-line before @
    // So "user@example.com" should NOT produce a match starting at the @
    // because "r" is not whitespace
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        // If it somehow matches, the full match should start with @
        // preceded by a word char — which our lookbehind should prevent
        int matchStart = match.capturedStart(0);
        if (matchStart > 0) {
            QChar before = input[matchStart - 1];
            QVERIFY2(before.isSpace() || matchStart == 0, qPrintable(QStringLiteral("Should not match after non-whitespace char: ") + before));
        }
    }
}

void SessionLinkFilterTest::testRegexDoesNotMatchShortNames()
{
    // Single char after @ should not match (minimum is 2 chars)
    const QString input = QStringLiteral(" @x ");
    QRegularExpressionMatchIterator it = SessionLinkFilter::SessionLinkRegExp.globalMatch(input);
    QVERIFY2(!it.hasNext(), "Single character after @ should not match");
}

// ========== Hotspot creation tests (require registry) ==========

void SessionLinkFilterTest::testHotSpotCreatedForMatchingSession()
{
    // Create a session and register it
    Konsolai::ClaudeSessionRegistry registry;
    auto *session = new Konsolai::ClaudeSession(QStringLiteral("default"), QStringLiteral("/home/user/projects/myproject"), &registry);
    registry.registerSession(session);

    SessionLinkFilter filter;
    // The captured texts simulate what RegExpFilter::process() produces
    QStringList capturedTexts;
    capturedTexts << QStringLiteral("@myproject") << QStringLiteral("myproject");

    auto hotspot = filter.newHotSpot(0, 0, 0, 10, capturedTexts);
    QVERIFY2(!hotspot.isNull(), "Hotspot should be created for matching session");

    registry.unregisterSession(session);
    delete session;
}

void SessionLinkFilterTest::testNoHotSpotForUnknownSession()
{
    // Create a registry with no sessions
    Konsolai::ClaudeSessionRegistry registry;

    SessionLinkFilter filter;
    QStringList capturedTexts;
    capturedTexts << QStringLiteral("@nonexistent") << QStringLiteral("nonexistent");

    auto hotspot = filter.newHotSpot(0, 0, 0, 12, capturedTexts);
    QVERIFY2(hotspot.isNull(), "No hotspot should be created for unknown session name");
}

void SessionLinkFilterTest::testHotSpotType()
{
    Konsolai::ClaudeSessionRegistry registry;
    auto *session = new Konsolai::ClaudeSession(QStringLiteral("default"), QStringLiteral("/home/user/projects/testproj"), &registry);
    registry.registerSession(session);

    SessionLinkFilter filter;
    QStringList capturedTexts;
    capturedTexts << QStringLiteral("@testproj") << QStringLiteral("testproj");

    auto hotspot = filter.newHotSpot(0, 0, 0, 9, capturedTexts);
    QVERIFY(!hotspot.isNull());
    QCOMPARE(hotspot->type(), HotSpot::Link);

    registry.unregisterSession(session);
    delete session;
}

void SessionLinkFilterTest::testFuzzyMatchSessionId()
{
    Konsolai::ClaudeSessionRegistry registry;
    auto *session = new Konsolai::ClaudeSession(QStringLiteral("default"), QStringLiteral("/home/user/projects/foo"), &registry);
    registry.registerSession(session);

    const QString sessionId = session->sessionId();

    SessionLinkFilter filter;
    QStringList capturedTexts;
    capturedTexts << (QStringLiteral("@") + sessionId) << sessionId;

    auto hotspot = filter.newHotSpot(0, 0, 0, sessionId.length() + 1, capturedTexts);
    QVERIFY2(!hotspot.isNull(), qPrintable(QStringLiteral("Should match by sessionId: ") + sessionId));

    registry.unregisterSession(session);
    delete session;
}

void SessionLinkFilterTest::testFuzzyMatchSessionName()
{
    Konsolai::ClaudeSessionRegistry registry;
    auto *session = new Konsolai::ClaudeSession(QStringLiteral("default"), QStringLiteral("/home/user/projects/bar"), &registry);
    registry.registerSession(session);

    const QString sessionName = session->sessionName();

    SessionLinkFilter filter;
    QStringList capturedTexts;
    capturedTexts << (QStringLiteral("@") + sessionName) << sessionName;

    auto hotspot = filter.newHotSpot(0, 0, 0, sessionName.length() + 1, capturedTexts);
    QVERIFY2(!hotspot.isNull(), qPrintable(QStringLiteral("Should match by sessionName: ") + sessionName));

    registry.unregisterSession(session);
    delete session;
}

void SessionLinkFilterTest::testFuzzyMatchTaskDescription()
{
    Konsolai::ClaudeSessionRegistry registry;
    auto *session = new Konsolai::ClaudeSession(QStringLiteral("default"), QStringLiteral("/home/user/projects/baz"), &registry);
    session->setTaskDescription(QStringLiteral("implement-session-links"));
    registry.registerSession(session);

    SessionLinkFilter filter;
    QStringList capturedTexts;
    capturedTexts << QStringLiteral("@session-links") << QStringLiteral("session-links");

    auto hotspot = filter.newHotSpot(0, 0, 0, 14, capturedTexts);
    QVERIFY2(!hotspot.isNull(), "Should match by substring of taskDescription");

    registry.unregisterSession(session);
    delete session;
}

void SessionLinkFilterTest::testFuzzyMatchPrefixOnDirname()
{
    Konsolai::ClaudeSessionRegistry registry;
    auto *session = new Konsolai::ClaudeSession(QStringLiteral("default"), QStringLiteral("/home/user/projects/konsolai-main"), &registry);
    registry.registerSession(session);

    SessionLinkFilter filter;
    QStringList capturedTexts;
    // "konsolai" is a prefix of "konsolai-main"
    capturedTexts << QStringLiteral("@konsolai") << QStringLiteral("konsolai");

    auto hotspot = filter.newHotSpot(0, 0, 0, 9, capturedTexts);
    QVERIFY2(!hotspot.isNull(), "Should match by prefix of directory basename");

    registry.unregisterSession(session);
    delete session;
}
