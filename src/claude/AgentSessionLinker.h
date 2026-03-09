/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_AGENTSESSIONLINKER_H
#define KONSOLAI_AGENTSESSIONLINKER_H

#include "konsoleprivate_export.h"

#include <QObject>
#include <QString>

namespace Konsolai
{

class AgentManagerPanel;
class SessionManagerPanel;

/**
 * Lightweight coordinator linking agent panel and session panel.
 *
 * Derives session↔agent relationships from SessionMetadata::agentId
 * (set when a session is created via agent attach). No new stored state —
 * all queries scan existing panel data.
 */
class KONSOLEPRIVATE_EXPORT AgentSessionLinker : public QObject
{
    Q_OBJECT

public:
    AgentSessionLinker(SessionManagerPanel *sessions, AgentManagerPanel *agents, QObject *parent = nullptr);
    ~AgentSessionLinker() override;

    /** Whether the agent has an active (tabbed) session. */
    bool hasActiveTab(const QString &agentId) const;

    /** Whether the agent has a detached (no tab) session. */
    bool hasDetachedSession(const QString &agentId) const;

    /** Return the agentId associated with a session, or empty. */
    QString agentIdForSession(const QString &sessionId) const;

    /** Return the session name for a given agent (most recent active preferred). */
    QString sessionNameForAgent(const QString &agentId) const;

    /** Focus the tab for an agent's active session. */
    void focusAgentTab(const QString &agentId);

    /** Switch to Agents tab and select the agent item. */
    void selectAgent(const QString &agentId);

    /** Notify that the session/agent mapping may have changed. */
    void notifyChanged(const QString &agentId);

Q_SIGNALS:
    void agentTabPresenceChanged(const QString &agentId, bool hasTab);

private:
    SessionManagerPanel *m_sessions;
    AgentManagerPanel *m_agents;
};

} // namespace Konsolai

#endif // KONSOLAI_AGENTSESSIONLINKER_H
