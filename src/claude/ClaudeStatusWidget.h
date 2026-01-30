/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESTATUSWIDGET_H
#define CLAUDESTATUSWIDGET_H

#include "ClaudeProcess.h"

#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace Konsolai
{

class ClaudeSession;

/**
 * ClaudeStatusWidget displays Claude status in the status bar.
 *
 * Shows:
 * - Current state (Idle/Working/Waiting/Error)
 * - Current task description (if any)
 * - Animated spinner when working
 *
 * Format: "[Claude: Working] [Task: Writing code...]"
 */
class ClaudeStatusWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ClaudeStatusWidget(QWidget *parent = nullptr);
    ~ClaudeStatusWidget() override;

    /**
     * Set the Claude session to monitor
     */
    void setSession(ClaudeSession *session);

    /**
     * Clear the current session
     */
    void clearSession();

    /**
     * Get the current session
     */
    ClaudeSession* session() const { return m_session; }

public Q_SLOTS:
    /**
     * Update the displayed state
     */
    void updateState(ClaudeProcess::State state);

    /**
     * Update the displayed task
     */
    void updateTask(const QString &task);

private Q_SLOTS:
    void updateSpinner();
    void onSessionDestroyed();

private:
    void updateDisplay();
    QString stateText(ClaudeProcess::State state) const;
    QString stateIcon(ClaudeProcess::State state) const;

    ClaudeSession *m_session = nullptr;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_taskLabel = nullptr;

    ClaudeProcess::State m_currentState = ClaudeProcess::State::NotRunning;
    QString m_currentTask;

    // Spinner animation
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerIndex = 0;
    static constexpr const char* SPINNER_FRAMES[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    static constexpr int SPINNER_FRAME_COUNT = 10;
};

} // namespace Konsolai

#endif // CLAUDESTATUSWIDGET_H
