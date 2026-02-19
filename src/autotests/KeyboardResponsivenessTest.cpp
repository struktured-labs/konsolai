/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
 * KeyboardResponsivenessTest
 *
 * Regression test for terminal typing responsiveness.  Ensures that the
 * periodic timers used by ClaudeTabIndicator, ClaudeStatusWidget, and
 * ClaudeSession polling do not starve the Qt event loop and cause
 * perceptible keystroke lag.
 *
 * Strategy:
 *  1. Spin up several indicator + status widgets in their heaviest timer
 *     modes (Working state → animation timers running).
 *  2. Post synthetic key-press events and measure the wall-clock time
 *     between posting and delivery.
 *  3. Assert that the 95th-percentile latency stays below a threshold
 *     (currently 50 ms — well above typical <5 ms but catches regressions
 *     like the 80 ms / 100 ms timer floods fixed in 80e5b95).
 */

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTest>

#include "../claude/ClaudeProcess.h"
#include "../claude/ClaudeStatusWidget.h"
#include "../claude/ClaudeTabIndicator.h"

using namespace Konsolai;

// Number of concurrent "session" widget sets to simulate
static constexpr int SESSION_COUNT = 6;

// Number of synthetic keystrokes to send
static constexpr int KEYSTROKE_COUNT = 100;

// Maximum acceptable 95th-percentile latency (ms)
static constexpr qint64 MAX_P95_LATENCY_MS = 50;

// Maximum acceptable worst-case latency (ms)
static constexpr qint64 MAX_WORST_LATENCY_MS = 100;

/**
 * A thin QPlainTextEdit subclass that records the wall-clock time at which
 * each keyPressEvent is actually delivered by the event loop.
 */
class LatencyRecorder : public QPlainTextEdit
{
public:
    QElapsedTimer clock;
    QVector<qint64> deliveryTimestamps;

    explicit LatencyRecorder(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        deliveryTimestamps.append(clock.elapsed());
        QPlainTextEdit::keyPressEvent(event);
    }
};

class KeyboardResponsivenessTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEventLoopLatencyUnderTimerLoad();
    void testTimerIntervalsAreReasonable();
};

/**
 * Core latency test: spin up many indicator/status timers in Working state,
 * then measure how quickly keystrokes are delivered through the event loop.
 */
void KeyboardResponsivenessTest::testEventLoopLatencyUnderTimerLoad()
{
    // --- Set up widgets that create timer load ---
    QVector<ClaudeTabIndicator *> indicators;
    QVector<ClaudeStatusWidget *> statusWidgets;

    for (int i = 0; i < SESSION_COUNT; ++i) {
        auto *indicator = new ClaudeTabIndicator;
        QMetaObject::invokeMethod(indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Working));
        indicators.append(indicator);

        auto *status = new ClaudeStatusWidget;
        status->updateState(ClaudeProcess::State::Working);
        statusWidgets.append(status);
    }

    // Let timers settle for a few frames
    QTest::qWait(50);

    // --- Post keystrokes and measure delivery latency ---
    LatencyRecorder recorder;
    recorder.show();
    QVERIFY(QTest::qWaitForWindowExposed(&recorder, 1000));
    recorder.setFocus();
    QApplication::processEvents();

    recorder.clock.start();

    QVector<qint64> postTimestamps;
    postTimestamps.reserve(KEYSTROKE_COUNT);

    for (int i = 0; i < KEYSTROKE_COUNT; ++i) {
        postTimestamps.append(recorder.clock.elapsed());
        // Post an 'a' key-press through the event system
        auto *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QApplication::postEvent(&recorder, event);

        // Interleave processEvents so delivery happens between posts,
        // simulating realistic typing cadence (~10 ms between keys ≈ 100 wpm)
        if (i % 5 == 0) {
            QApplication::processEvents();
        }
    }

    // Drain remaining events
    QApplication::processEvents();
    QTest::qWait(20);
    QApplication::processEvents();

    // --- Analyze latencies ---
    QCOMPARE(recorder.deliveryTimestamps.size(), KEYSTROKE_COUNT);

    QVector<qint64> latencies;
    latencies.reserve(KEYSTROKE_COUNT);
    for (int i = 0; i < KEYSTROKE_COUNT; ++i) {
        qint64 latency = recorder.deliveryTimestamps[i] - postTimestamps[i];
        latencies.append(latency);
    }

    std::sort(latencies.begin(), latencies.end());

    qint64 median = latencies[KEYSTROKE_COUNT / 2];
    qint64 p95 = latencies[static_cast<int>(KEYSTROKE_COUNT * 0.95)];
    qint64 worst = latencies.last();

    qDebug() << "Keystroke latency (ms): median=" << median << "p95=" << p95 << "worst=" << worst;

    QVERIFY2(p95 <= MAX_P95_LATENCY_MS, qPrintable(QStringLiteral("p95 latency %1 ms exceeds %2 ms threshold").arg(p95).arg(MAX_P95_LATENCY_MS)));
    QVERIFY2(worst <= MAX_WORST_LATENCY_MS, qPrintable(QStringLiteral("worst latency %1 ms exceeds %2 ms threshold").arg(worst).arg(MAX_WORST_LATENCY_MS)));

    // --- Cleanup ---
    qDeleteAll(indicators);
    qDeleteAll(statusWidgets);
}

/**
 * Static check: verify that timer intervals haven't been accidentally
 * lowered back to problematic values.
 */
void KeyboardResponsivenessTest::testTimerIntervalsAreReasonable()
{
    // Tab indicator animation timer
    ClaudeTabIndicator indicator;
    QMetaObject::invokeMethod(&indicator, "updateState", Q_ARG(ClaudeProcess::State, ClaudeProcess::State::Working));
    // Access the timer interval through the widget — the animation timer
    // is started when state is Working.
    auto *animTimer = indicator.findChild<QTimer *>();
    QVERIFY(animTimer);
    QVERIFY2(animTimer->interval() >= 150,
             qPrintable(QStringLiteral("Tab indicator animation interval %1 ms is too fast (min 150 ms)").arg(animTimer->interval())));

    // Status widget spinner timer
    ClaudeStatusWidget status;
    status.updateState(ClaudeProcess::State::Working);
    auto *spinnerTimer = status.findChild<QTimer *>();
    QVERIFY(spinnerTimer);
    QVERIFY2(spinnerTimer->interval() >= 100,
             qPrintable(QStringLiteral("Status spinner interval %1 ms is too fast (min 100 ms)").arg(spinnerTimer->interval())));
}

QTEST_MAIN(KeyboardResponsivenessTest)
#include "KeyboardResponsivenessTest.moc"
