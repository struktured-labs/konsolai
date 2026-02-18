/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ONESHOTCONTROLLER_H
#define ONESHOTCONTROLLER_H

#include "konsoleprivate_export.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace Konsolai
{

class ClaudeSession;
class BudgetController;
class SessionObserver;

/**
 * Configuration for a one-shot Claude session.
 */
struct KONSOLEPRIVATE_EXPORT OneShotConfig {
    QString prompt;
    QString workingDir;
    QString model;
    int timeLimitMinutes = 0;
    double costCeilingUSD = 0.0;
    quint64 tokenCeiling = 0;
    int yoloLevel = 3; // 1=L1, 2=L1+L2, 3=L1+L2+L3
    bool useGsd = false;
    int qualityScore = 0; // from Assessment
};

/**
 * Result of a completed one-shot session.
 */
struct KONSOLEPRIVATE_EXPORT OneShotResult {
    bool success = false;
    QString summary;
    double costUSD = 0.0;
    int durationSeconds = 0;
    quint64 totalTokens = 0;
    int filesModified = 0;
    int commits = 0;
    QStringList errors;
};

/**
 * OneShotController orchestrates a fire-and-forget Claude session.
 *
 * Attaches to a ClaudeSession, waits for the first Idle state to send
 * the configured prompt, monitors budget limits, and emits completed()
 * when the session finishes.
 *
 * The controller does NOT own the session -- it monitors and orchestrates.
 */
class KONSOLEPRIVATE_EXPORT OneShotController : public QObject
{
    Q_OBJECT

public:
    explicit OneShotController(QObject *parent = nullptr);
    ~OneShotController() override;

    void setConfig(const OneShotConfig &config);
    const OneShotConfig &config() const;

    /**
     * Attach to a session: connect signals, set up budget + observer.
     */
    void attachToSession(ClaudeSession *session);

    /**
     * Begin monitoring; sends prompt on first Idle.
     */
    void start();

    bool isRunning() const;
    const OneShotResult &result() const;

    /**
     * Format budget status: "3:24 / 15:00 | $0.14 / $0.50"
     */
    QString formatBudgetStatus() const;

    /**
     * Format state label: "Working ϟϟϟ [7]" or "Idle" etc.
     */
    QString formatStateLabel() const;

Q_SIGNALS:
    void promptSent();
    void completed(const OneShotResult &result);
    void budgetStatusChanged(const QString &status);

private:
    void onStateChanged(int state);

    OneShotConfig m_config;
    OneShotResult m_result;
    QPointer<ClaudeSession> m_session;
    bool m_running = false;
    bool m_promptSent = false;
    QDateTime m_startedAt;
};

} // namespace Konsolai

#endif // ONESHOTCONTROLLER_H
