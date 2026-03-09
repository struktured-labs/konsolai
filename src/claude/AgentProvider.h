/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_AGENTPROVIDER_H
#define KONSOLAI_AGENTPROVIDER_H

#include "konsoleprivate_export.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Konsolai
{

struct AgentBudget {
    double perRunUSD = 0.0;
    double dailyUSD = 0.0;
    QString model;
};

struct AgentBrief {
    QString direction;
    QStringList steeringNotes;
    bool isDone = false;
};

struct AgentInfo {
    QString id;
    QString name;
    QString project;
    QString schedule;
    QString goal;
    QString provider;
    AgentBudget budget;
};

struct AgentStatus {
    enum State {
        Idle,
        Running,
        Budget,
        Error,
        Paused
    };
    State state = Idle;
    QString sessionId;
    QDateTime lastRun;
    QString lastSummary;
    int runCount = 0;
    double dailySpentUSD = 0.0;
    AgentBrief brief;
};

struct AgentReport {
    QString title;
    QString content;
    QDateTime timestamp;
    QString filePath;
};

struct AgentRunResult {
    enum Status {
        Ok,
        Error,
        Budget,
        Timeout
    };
    Status status = Ok;
    QString summary;
    QString fullOutput;
    double costUSD = 0.0;
    int exitCode = 0;
    QDateTime timestamp;
    QString sessionId;
    QString conversationPath;
};

struct AgentAttachInfo {
    bool canAttach = false;
    QString tmuxSessionName;
    QString workingDirectory;
    bool isRemote = false;
    QString sshHost;
    QString sshUsername;
    int sshPort = 22;
    QString resumeSessionId;
    QString conversationPath;
    QString agentName; // Human-readable name for tab title
    QString agentId; // Agent identifier for session linkage
};

struct AgentConfig {
    QString name;
    QString project;
    QString goal;
    QString schedule;
    AgentBudget budget;
    QString agentFile;
    QStringList allowedTools;
    QString permissionMode;
    int timeoutSeconds = 600;
    bool innovationEnabled = false;
    QString innovationPrompt;
    bool schedulePaused = false;
};

/**
 * Abstract interface for agent management backends.
 *
 * Providers implement this interface to expose their agents to Konsolai's
 * agent management panel. The interface is versioned: interfaceVersion()
 * returns 1 for the initial release. When new optional methods are added,
 * the panel checks provider->interfaceVersion() >= N before calling them.
 */
class KONSOLEPRIVATE_EXPORT AgentProvider : public QObject
{
    Q_OBJECT

public:
    explicit AgentProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~AgentProvider() override = default;

    /** Interface version. 1 for initial release. */
    virtual int interfaceVersion() const = 0;

    /** Provider name (e.g., "agent-fleet", "cowork"). */
    virtual QString name() const = 0;

    /** Whether this provider can be used on the current system. */
    virtual bool isAvailable() const = 0;

    /** List all defined agents. */
    virtual QList<AgentInfo> agents() const = 0;

    /** Current status of one agent. */
    virtual AgentStatus agentStatus(const QString &id) const = 0;

    /** Kick off an immediate run. */
    virtual bool triggerRun(const QString &id, const QString &task = QString()) = 0;

    /** Set creative brief for an agent. */
    virtual bool setBrief(const QString &id, const QString &direction) = 0;

    /** Add a steering note. */
    virtual bool addSteeringNote(const QString &id, const QString &note) = 0;

    /** Complete a brief. */
    virtual bool markBriefDone(const QString &id) = 0;

    /** Read agent reports. */
    virtual QList<AgentReport> recentReports(const QString &id, int count = 5) const = 0;

    /** Past run results. */
    virtual QList<AgentRunResult> recentResults(const QString &id, int count = 10) const = 0;

    /** Most recent run result. */
    virtual AgentRunResult lastResult(const QString &id) const = 0;

    /** How to attach interactively. */
    virtual AgentAttachInfo attachInfo(const QString &id) const = 0;

    /** Create new agent from config. */
    virtual bool createAgent(const AgentConfig &config) = 0;

    /** Edit agent configuration. */
    virtual bool updateAgent(const QString &id, const AgentConfig &config) = 0;

    /** Remove agent definition. */
    virtual bool deleteAgent(const QString &id) = 0;

    /** Temporarily disable scheduling. v2+ — check interfaceVersion() >= 2. */
    virtual bool pauseSchedule(const QString &id)
    {
        Q_UNUSED(id);
        return false;
    }

    /** Re-enable scheduling. v2+ — check interfaceVersion() >= 2. */
    virtual bool resumeSchedule(const QString &id)
    {
        Q_UNUSED(id);
        return false;
    }

    /** Clear session ID for fresh context. */
    virtual bool resetSession(const QString &id) = 0;

Q_SIGNALS:
    /** Emitted when a specific agent's status changes. */
    void agentChanged(const QString &id);

    /** Emitted when the agent list changes (agent added/removed). */
    void agentsReloaded();
};

} // namespace Konsolai

#endif // KONSOLAI_AGENTPROVIDER_H
