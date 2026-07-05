/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_CLAUDEASSISTANT_H
#define KONSOLAI_CLAUDEASSISTANT_H

#include "konsoleprivate_export.h"

#include <QObject>
#include <QProcess>
#include <QString>

class QTimer;

namespace Konsolai
{

/**
 * Fire-and-forget wrapper around `claude -p <prompt>` — the CLI's one-shot
 * non-interactive mode. Signals arrive when the process finishes: `finished()`
 * with stdout+exitCode, or `failed()` with an error string.
 *
 * NOT a singleton — construct one per request so cancelling / timeout is
 * clean. The process is killed on destruction if still running.
 *
 * The class knows nothing about tree topology or JSON schemas — that lives
 * in the callers (ReorganizeTreeDialog, ClaudeAssistantPromptBuilder).
 */
class KONSOLEPRIVATE_EXPORT ClaudeAssistant : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeAssistant(QObject *parent = nullptr);
    ~ClaudeAssistant() override;

    /**
     * Ask the CLI to answer `prompt`. Emits either `finished(output, exitCode)`
     * or `failed(errorString)`. If `jsonOutput` is true, adds
     * `--output-format json` so the caller can parse structured output.
     *
     * Uses `KonsolaiSettings::defaultModel()` for the model flag when
     * non-empty. Adds `--allowedTools ""` to suppress any tool invocation —
     * we want a pure text answer.
     *
     * If the `claude` binary is not on PATH, emits `failed("claude CLI not found on PATH")`.
     * Default timeout: 60 seconds. Adjustable via `setTimeoutMs`.
     */
    virtual void ask(const QString &prompt, bool jsonOutput = false);

    /**
     * Cancel any in-flight request. Terminates the child process. Emits
     * `failed("cancelled")` if a request was running.
     */
    void cancel();

    void setTimeoutMs(int ms)
    {
        m_timeoutMs = ms;
    }
    int timeoutMs() const
    {
        return m_timeoutMs;
    }

    /**
     * Returns the resolved path to the `claude` executable, or empty if not found.
     * Static — safe to call at startup for a quick availability check.
     */
    static QString claudeExecutablePath();

Q_SIGNALS:
    /** stdout on success — full output, not streamed line-by-line for MVP. */
    void finished(const QString &stdoutOutput, int exitCode);
    /** anything that isn't a clean exit — timeout, missing binary, non-zero exit. */
    void failed(const QString &errorString);

private Q_SLOTS:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onReadyReadStdout();
    void onTimeout();

private:
    void emitDeferredFailure(const QString &message);
    void teardownProcess();

    QProcess *m_process = nullptr;
    QString m_stdoutBuffer;
    QString m_stderrBuffer;
    int m_timeoutMs = 60000;
    QTimer *m_timeoutTimer = nullptr;
    bool m_cancelled = false;
    bool m_finishedEmitted = false;
};

} // namespace Konsolai

#endif // KONSOLAI_CLAUDEASSISTANT_H
