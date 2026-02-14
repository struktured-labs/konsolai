/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QSignalSpy>
#include <QTest>

#include "../claude/ClaudeSession.h"
#include "../claude/ClaudeStatusWidget.h"

using namespace Konsolai;

class ClaudeStatusWidgetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSetAndClearSession();
    void testSessionDestroyedWhileActive();
    void testSetSessionToNullptr();
    void testUpdateDisplayAfterSessionDestroyed();
    void testMultipleSessionSwitches();
};

void ClaudeStatusWidgetTest::testSetAndClearSession()
{
    ClaudeStatusWidget widget;

    auto *session = ClaudeSession::createForReattach(QStringLiteral("test-session"), nullptr);
    widget.setSession(session);

    QCOMPARE(widget.session(), session);

    widget.clearSession();
    QVERIFY(widget.session() == nullptr);

    delete session;
}

void ClaudeStatusWidgetTest::testSessionDestroyedWhileActive()
{
    // Regression test: crash when tab closes and session is destroyed
    // while ClaudeStatusWidget still holds a reference.
    ClaudeStatusWidget widget;

    auto *session = ClaudeSession::createForReattach(QStringLiteral("test-session"), nullptr);
    widget.setSession(session);
    QCOMPARE(widget.session(), session);

    // Destroy the session — widget should handle this gracefully
    delete session;

    // QPointer should auto-null
    QVERIFY(widget.session() == nullptr);

    // Widget should not crash when updating display after session death
    // (this exercises the code path that previously could dereference a dangling pointer)
    widget.updateState(ClaudeProcess::State::Idle);
    widget.updateTask(QStringLiteral("some task"));
}

void ClaudeStatusWidgetTest::testSetSessionToNullptr()
{
    ClaudeStatusWidget widget;

    auto *session = ClaudeSession::createForReattach(QStringLiteral("test-session"), nullptr);
    widget.setSession(session);
    QCOMPARE(widget.session(), session);

    // Setting nullptr session should clearSession internally
    widget.setSession(nullptr);
    QVERIFY(widget.session() == nullptr);

    delete session;
}

void ClaudeStatusWidgetTest::testUpdateDisplayAfterSessionDestroyed()
{
    // Simulate the exact crash scenario: session is destroyed, then
    // activeViewChanged triggers updateState/updateDisplay
    ClaudeStatusWidget widget;

    auto *session = ClaudeSession::createForReattach(QStringLiteral("test-obs-studio"), nullptr);
    widget.setSession(session);

    // Simulate state changes while session is alive
    widget.updateState(ClaudeProcess::State::Working);
    widget.updateState(ClaudeProcess::State::Idle);

    // Destroy session (simulates tab close → Session::finished → deleteLater)
    delete session;

    // Now simulate what happens when a different tab's activeViewChanged
    // fires and the widget tries to display — must not crash
    widget.updateState(ClaudeProcess::State::NotRunning);
}

void ClaudeStatusWidgetTest::testMultipleSessionSwitches()
{
    // Test rapid session switching (simulates clicking between Claude and non-Claude tabs)
    ClaudeStatusWidget widget;

    auto *session1 = ClaudeSession::createForReattach(QStringLiteral("session-1"), nullptr);
    auto *session2 = ClaudeSession::createForReattach(QStringLiteral("session-2"), nullptr);

    widget.setSession(session1);
    QCOMPARE(widget.session(), session1);

    // Switch to a non-Claude tab (clearSession)
    widget.clearSession();
    QVERIFY(widget.session() == nullptr);

    // Switch to session2
    widget.setSession(session2);
    QCOMPARE(widget.session(), session2);

    // Destroy session1 while session2 is active — should not affect widget
    delete session1;
    QCOMPARE(widget.session(), session2);

    // Destroy session2 while it's the active session
    delete session2;
    QVERIFY(widget.session() == nullptr);

    // Widget should still be usable
    widget.updateState(ClaudeProcess::State::NotRunning);
}

QTEST_MAIN(ClaudeStatusWidgetTest)
#include "ClaudeStatusWidgetTest.moc"
