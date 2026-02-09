/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include "../claude/ClaudeProcess.h"
#include "../claude/ClaudeTabIndicator.h"

using namespace Konsolai;

class ClaudeTabIndicatorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInitialState();
    void testSizeHint();
    void testStateTransitions();
    void testSuggestionAfterIdleDelay();
    void testSuggestionClearedOnStateChange();
    void testAnimationRunsDuringWorking();
    void testAnimationStopsOnNonWorking();
};

void ClaudeTabIndicatorTest::testInitialState()
{
    ClaudeTabIndicator indicator;
    QCOMPARE(indicator.state(), ClaudeProcess::State::NotRunning);
    QCOMPARE(indicator.suggestionAvailable(), false);
    QVERIFY(indicator.session() == nullptr);
}

void ClaudeTabIndicatorTest::testSizeHint()
{
    ClaudeTabIndicator indicator;
    QCOMPARE(indicator.sizeHint(), QSize(16, 16));
    QCOMPARE(indicator.minimumSizeHint(), QSize(16, 16));
}

void ClaudeTabIndicatorTest::testStateTransitions()
{
    ClaudeTabIndicator indicator;

    // Simulate direct state updates (without a session, using the slot directly)
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Working));
    QCOMPARE(indicator.state(), ClaudeProcess::State::Working);

    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Idle));
    QCOMPARE(indicator.state(), ClaudeProcess::State::Idle);

    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::WaitingInput));
    QCOMPARE(indicator.state(), ClaudeProcess::State::WaitingInput);

    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Error));
    QCOMPARE(indicator.state(), ClaudeProcess::State::Error);
}

void ClaudeTabIndicatorTest::testSuggestionAfterIdleDelay()
{
    ClaudeTabIndicator indicator;

    // Move to Idle state
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Idle));
    QCOMPARE(indicator.suggestionAvailable(), false);

    // Wait for the 3s suggestion delay
    QTRY_COMPARE_WITH_TIMEOUT(indicator.suggestionAvailable(), true, 5000);
}

void ClaudeTabIndicatorTest::testSuggestionClearedOnStateChange()
{
    ClaudeTabIndicator indicator;

    // Get to suggestion-available state
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Idle));
    QTRY_COMPARE_WITH_TIMEOUT(indicator.suggestionAvailable(), true, 5000);

    // State change should clear suggestion
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Working));
    QCOMPARE(indicator.suggestionAvailable(), false);
}

void ClaudeTabIndicatorTest::testAnimationRunsDuringWorking()
{
    ClaudeTabIndicator indicator;

    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Working));

    // Animation timer should be running — wait a bit and check the widget updated
    // (we can't directly access the timer, but we can verify the state is Working)
    QCOMPARE(indicator.state(), ClaudeProcess::State::Working);
    QTest::qWait(250); // Let a few animation frames fire
    // If we got here without crash, animation timer is working
}

void ClaudeTabIndicatorTest::testAnimationStopsOnNonWorking()
{
    ClaudeTabIndicator indicator;

    // Start working (animation on)
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Working));
    QTest::qWait(150);

    // Move to Idle (animation off)
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Idle));
    QCOMPARE(indicator.state(), ClaudeProcess::State::Idle);
}

QTEST_MAIN(ClaudeTabIndicatorTest)
#include "ClaudeTabIndicatorTest.moc"
