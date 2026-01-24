/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEPROCESS_H
#define CLAUDEPROCESS_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QTimer>

namespace Konsolai
{

/**
 * ClaudeProcess manages the Claude CLI process lifecycle.
 *
 * This class handles spawning, monitoring, and controlling the claude CLI
 * process, tracking its state and emitting signals when state changes occur.
 */
class ClaudeProcess : public QObject
{
    Q_OBJECT

public:
    /**
     * Current state of the Claude process
     */
    enum class State {
        NotRunning,     // Process not started
        Starting,       // Process starting up
        Idle,           // Waiting for user input
        Working,        // Processing a request
        WaitingInput,   // Waiting for user to provide more info (e.g., permission prompt)
        Error           // Process encountered an error
    };
    Q_ENUM(State)

    /**
     * Claude model to use
     */
    enum class Model {
        Default,        // Use default model
        Opus,           // claude-opus-4-5
        Sonnet,         // claude-sonnet-4
        Haiku           // claude-haiku
    };
    Q_ENUM(Model)

    explicit ClaudeProcess(QObject *parent = nullptr);
    ~ClaudeProcess() override;

    /**
     * Get the command to run Claude CLI
     *
     * @param model Which Claude model to use
     * @param workingDir Working directory for the session
     * @param additionalArgs Extra arguments to pass to claude
     */
    static QString buildCommand(Model model = Model::Default,
                                const QString &workingDir = QString(),
                                const QStringList &additionalArgs = {});

    /**
     * Check if the Claude CLI is available on the system
     */
    static bool isAvailable();

    /**
     * Get the path to the Claude CLI executable
     */
    static QString executablePath();

    /**
     * Current state of the Claude process
     */
    State state() const { return m_state; }

    /**
     * Whether the process is currently running
     */
    bool isRunning() const { return m_state != State::NotRunning && m_state != State::Error; }

    /**
     * Get the model name string for a given model enum
     */
    static QString modelName(Model model);

    /**
     * Parse model from string
     */
    static Model parseModel(const QString &name);

    /**
     * Get current task description (if any)
     */
    QString currentTask() const { return m_currentTask; }

Q_SIGNALS:
    /**
     * Emitted when Claude state changes
     */
    void stateChanged(State newState);

    /**
     * Emitted when Claude starts processing a new task
     */
    void taskStarted(const QString &taskDescription);

    /**
     * Emitted when Claude finishes a task
     */
    void taskFinished();

    /**
     * Emitted when Claude is waiting for a permission response
     */
    void permissionRequested(const QString &action, const QString &description);

    /**
     * Emitted when Claude encounters an error
     */
    void errorOccurred(const QString &message);

    /**
     * Emitted when a notification should be shown
     */
    void notificationReceived(const QString &type, const QString &message);

public Q_SLOTS:
    /**
     * Update state based on hook event
     *
     * This is called when a Claude hook event is received via the hook handler.
     *
     * @param eventType Type of hook event (e.g., "Stop", "PreToolUse", "Notification")
     * @param eventData JSON data associated with the event
     */
    void handleHookEvent(const QString &eventType, const QString &eventData);

    /**
     * Set the current task description
     */
    void setCurrentTask(const QString &task);

    /**
     * Clear the current task
     */
    void clearTask();

private:
    void setState(State newState);

    State m_state = State::NotRunning;
    QString m_currentTask;
};

} // namespace Konsolai

#endif // CLAUDEPROCESS_H
