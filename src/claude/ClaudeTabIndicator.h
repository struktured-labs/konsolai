/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDETABINDICATOR_H
#define CLAUDETABINDICATOR_H

#include "ClaudeProcess.h"

#include "konsoleprivate_export.h"
#include <QPainter>
#include <QTimer>
#include <QWidget>

namespace Konsolai
{

class ClaudeSession;

/**
 * ClaudeTabIndicator displays a status indicator in the tab bar.
 *
 * Shows a colored dot indicating Claude state:
 * - Gray (○): Not Running
 * - Blue (●): Idle
 * - Spinning Blue (◉): Working
 * - Orange (●): Waiting for Input
 * - Red (●): Error
 *
 * Size: 12x12 pixels, appears before tab title
 */
class KONSOLEPRIVATE_EXPORT ClaudeTabIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit ClaudeTabIndicator(QWidget *parent = nullptr);
    ~ClaudeTabIndicator() override;

    /**
     * Set the Claude session to monitor
     */
    void setSession(ClaudeSession *session);

    /**
     * Get the current session
     */
    ClaudeSession* session() const { return m_session; }

    /**
     * Get the current state
     */
    ClaudeProcess::State state() const { return m_currentState; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private Q_SLOTS:
    void updateState(ClaudeProcess::State state);
    void updateAnimation();
    void onSessionDestroyed();

private:
    QColor stateColor(ClaudeProcess::State state) const;

    ClaudeSession *m_session = nullptr;
    ClaudeProcess::State m_currentState = ClaudeProcess::State::NotRunning;

    // Animation for Working state
    QTimer *m_animationTimer = nullptr;
    qreal m_animationPhase = 0.0;

    static constexpr int SIZE = 12;
    static constexpr int DOT_SIZE = 8;
};

} // namespace Konsolai

#endif // CLAUDETABINDICATOR_H
