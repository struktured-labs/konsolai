/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BUDGETCONTROLLER_H
#define BUDGETCONTROLLER_H

#include "konsoleprivate_export.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace Konsolai
{

struct TokenUsage;
struct ResourceUsage;

/**
 * Budget limits for a one-shot session.
 * Zero values mean "unlimited" for that dimension.
 */
struct KONSOLEPRIVATE_EXPORT SessionBudget {
    int timeLimitMinutes = 0;
    double costCeilingUSD = 0.0;
    quint64 tokenCeiling = 0;

    enum Policy {
        Soft,
        Hard
    };
    Policy timePolicy = Soft;
    Policy costPolicy = Soft;
    Policy tokenPolicy = Soft;

    double warningThresholdPercent = 80.0;

    bool timeExceeded = false;
    bool costExceeded = false;
    bool tokenExceeded = false;

    QDateTime startedAt;

    bool hasAnyLimit() const
    {
        return timeLimitMinutes > 0 || costCeilingUSD > 0.0 || tokenCeiling > 0;
    }

    int elapsedMinutes() const
    {
        if (!startedAt.isValid()) {
            return 0;
        }
        return static_cast<int>(startedAt.secsTo(QDateTime::currentDateTime()) / 60);
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj[QStringLiteral("timeLimitMinutes")] = timeLimitMinutes;
        obj[QStringLiteral("costCeilingUSD")] = costCeilingUSD;
        obj[QStringLiteral("tokenCeiling")] = static_cast<qint64>(tokenCeiling);
        obj[QStringLiteral("timePolicy")] = static_cast<int>(timePolicy);
        obj[QStringLiteral("costPolicy")] = static_cast<int>(costPolicy);
        obj[QStringLiteral("tokenPolicy")] = static_cast<int>(tokenPolicy);
        obj[QStringLiteral("warningThresholdPercent")] = warningThresholdPercent;
        obj[QStringLiteral("timeExceeded")] = timeExceeded;
        obj[QStringLiteral("costExceeded")] = costExceeded;
        obj[QStringLiteral("tokenExceeded")] = tokenExceeded;
        if (startedAt.isValid()) {
            obj[QStringLiteral("startedAt")] = startedAt.toString(Qt::ISODate);
        }
        return obj;
    }

    static SessionBudget fromJson(const QJsonObject &obj)
    {
        SessionBudget b;
        b.timeLimitMinutes = obj[QStringLiteral("timeLimitMinutes")].toInt(0);
        b.costCeilingUSD = obj[QStringLiteral("costCeilingUSD")].toDouble(0.0);
        b.tokenCeiling = static_cast<quint64>(obj[QStringLiteral("tokenCeiling")].toInteger(0));
        b.timePolicy = static_cast<Policy>(obj[QStringLiteral("timePolicy")].toInt(0));
        b.costPolicy = static_cast<Policy>(obj[QStringLiteral("costPolicy")].toInt(0));
        b.tokenPolicy = static_cast<Policy>(obj[QStringLiteral("tokenPolicy")].toInt(0));
        b.warningThresholdPercent = obj[QStringLiteral("warningThresholdPercent")].toDouble(80.0);
        b.timeExceeded = obj[QStringLiteral("timeExceeded")].toBool(false);
        b.costExceeded = obj[QStringLiteral("costExceeded")].toBool(false);
        b.tokenExceeded = obj[QStringLiteral("tokenExceeded")].toBool(false);
        const QString ts = obj[QStringLiteral("startedAt")].toString();
        if (!ts.isEmpty()) {
            b.startedAt = QDateTime::fromString(ts, Qt::ISODate);
        }
        return b;
    }
};

/**
 * Ring-buffer velocity tracker: one sample per minute, 2-hour history.
 */
struct KONSOLEPRIVATE_EXPORT TokenVelocity {
    struct VelocitySample {
        QDateTime timestamp;
        quint64 totalTokens = 0;
        double costUSD = 0.0;
    };

    static constexpr int kMaxSamples = 120;

    QVector<VelocitySample> samples;
    int head = 0;
    int count = 0;

    TokenVelocity()
    {
        samples.resize(kMaxSamples);
    }

    void addSample(quint64 totalTokens, double costUSD)
    {
        VelocitySample s;
        s.timestamp = QDateTime::currentDateTime();
        s.totalTokens = totalTokens;
        s.costUSD = costUSD;
        samples[head] = s;
        head = (head + 1) % kMaxSamples;
        if (count < kMaxSamples) {
            ++count;
        }
    }

    /**
     * 5-minute rolling average of tokens per minute.
     */
    double tokensPerMinute() const
    {
        if (count < 2) {
            return 0.0;
        }
        // Look back up to 5 samples (5 minutes)
        int lookback = qMin(count, 5);
        int newest = (head - 1 + kMaxSamples) % kMaxSamples;
        int oldest = (head - lookback + kMaxSamples) % kMaxSamples;
        const auto &s0 = samples[oldest];
        const auto &s1 = samples[newest];
        double minutes = s0.timestamp.secsTo(s1.timestamp) / 60.0;
        if (minutes <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(s1.totalTokens - s0.totalTokens) / minutes;
    }

    /**
     * 5-minute rolling average of cost per minute.
     */
    double costPerMinute() const
    {
        if (count < 2) {
            return 0.0;
        }
        int lookback = qMin(count, 5);
        int newest = (head - 1 + kMaxSamples) % kMaxSamples;
        int oldest = (head - lookback + kMaxSamples) % kMaxSamples;
        const auto &s0 = samples[oldest];
        const auto &s1 = samples[newest];
        double minutes = s0.timestamp.secsTo(s1.timestamp) / 60.0;
        if (minutes <= 0.0) {
            return 0.0;
        }
        return (s1.costUSD - s0.costUSD) / minutes;
    }

    /**
     * Estimated minutes remaining to hit ceiling, based on current velocity.
     */
    double estimatedMinutesRemaining(quint64 ceiling, quint64 current) const
    {
        if (ceiling == 0 || current >= ceiling) {
            return 0.0;
        }
        double velocity = tokensPerMinute();
        if (velocity <= 0.0) {
            return -1.0; // unknown
        }
        return static_cast<double>(ceiling - current) / velocity;
    }

    /**
     * Format as "2.3K/m $0.04/m"
     */
    QString formatVelocity() const
    {
        double tpm = tokensPerMinute();
        double cpm = costPerMinute();
        QString tokenStr;
        if (tpm >= 1000.0) {
            tokenStr = QStringLiteral("%1K/m").arg(tpm / 1000.0, 0, 'f', 1);
        } else {
            tokenStr = QStringLiteral("%1/m").arg(tpm, 0, 'f', 0);
        }
        return QStringLiteral("%1 $%2/m").arg(tokenStr).arg(cpm, 0, 'f', 2);
    }
};

/**
 * Resource-based gate: throttle yolo when CPU/RAM are critically high.
 */
struct KONSOLEPRIVATE_EXPORT ResourceGate {
    double cpuThresholdPercent = 95.0;
    int cpuDebounceCount = 6; // 30s sustained at 5s polling
    quint64 rssThresholdBytes = 0; // 0 = auto: 80% of system RAM

    enum Action {
        PauseYolo,
        ReduceYolo,
        NotifyOnly
    };
    Action action = PauseYolo;

    int currentCpuExceedCount = 0;
    bool gateTriggered = false;

    /**
     * Returns rssThresholdBytes if set, else 80% of system RAM.
     */
    quint64 autoRssThreshold() const
    {
        if (rssThresholdBytes > 0) {
            return rssThresholdBytes;
        }
#ifdef Q_OS_LINUX
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0) {
            return static_cast<quint64>(pages) * static_cast<quint64>(pageSize) * 80 / 100;
        }
#endif
        // Fallback: 8 GB * 80% = 6.4 GB
        return static_cast<quint64>(6400) * 1024 * 1024;
    }
};

/**
 * BudgetController monitors session cost, token usage, time, and resources.
 *
 * Emits warnings at configurable thresholds and exceeded signals when limits
 * are hit. The shouldBlockYolo() method returns true whenever any budget
 * dimension has been exceeded or the resource gate has triggered.
 */
class KONSOLEPRIVATE_EXPORT BudgetController : public QObject
{
    Q_OBJECT

public:
    explicit BudgetController(QObject *parent = nullptr);
    ~BudgetController() override;

    SessionBudget &budget()
    {
        return m_budget;
    }
    const SessionBudget &budget() const
    {
        return m_budget;
    }
    void setBudget(const SessionBudget &b);

    const TokenVelocity &velocity() const
    {
        return m_velocity;
    }

    const ResourceGate &resourceGate() const
    {
        return m_gate;
    }
    ResourceGate &resourceGate()
    {
        return m_gate;
    }

    void onTokenUsageChanged(const TokenUsage &usage);
    void onResourceUsageChanged(const ResourceUsage &usage);
    void checkTimeBudget();

    bool shouldBlockYolo() const;

Q_SIGNALS:
    void budgetWarning(const QString &type, double percent);
    void budgetExceeded(const QString &type);
    void resourceGateTriggered(const QString &reason);
    void resourceGateCleared();
    void velocityUpdated();

private:
    QTimer *m_checkTimer = nullptr;
    SessionBudget m_budget;
    TokenVelocity m_velocity;
    ResourceGate m_gate;
    bool m_yoloPausedByGate = false;

    // Deduplication: track whether we already emitted warning/exceeded for each type
    bool m_timeWarningEmitted = false;
    bool m_costWarningEmitted = false;
    bool m_tokenWarningEmitted = false;
    bool m_timeExceededEmitted = false;
    bool m_costExceededEmitted = false;
    bool m_tokenExceededEmitted = false;
};

} // namespace Konsolai

#endif // BUDGETCONTROLLER_H
