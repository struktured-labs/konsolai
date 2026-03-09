/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AgentSessionLinker.h"

#include "AgentManagerPanel.h"
#include "SessionManagerPanel.h"

#include <QTabWidget>

namespace Konsolai
{

AgentSessionLinker::AgentSessionLinker(SessionManagerPanel *sessions, AgentManagerPanel *agents, QObject *parent)
    : QObject(parent)
    , m_sessions(sessions)
    , m_agents(agents)
{
}

AgentSessionLinker::~AgentSessionLinker() = default;

bool AgentSessionLinker::hasActiveTab(const QString &agentId) const
{
    if (agentId.isEmpty() || !m_sessions) {
        return false;
    }

    const auto allMeta = m_sessions->allSessions();
    for (const auto &meta : allMeta) {
        if (meta.agentId == agentId && m_sessions->isSessionActive(meta.sessionId)) {
            return true;
        }
    }
    return false;
}

bool AgentSessionLinker::hasDetachedSession(const QString &agentId) const
{
    if (agentId.isEmpty() || !m_sessions) {
        return false;
    }

    const auto allMeta = m_sessions->allSessions();
    for (const auto &meta : allMeta) {
        if (meta.agentId == agentId && !meta.isArchived && !meta.isDismissed && !meta.isExpired) {
            // Only "detached" if NOT active (has metadata but no open tab)
            if (!m_sessions->isSessionActive(meta.sessionId)) {
                return true;
            }
        }
    }
    return false;
}

QString AgentSessionLinker::agentIdForSession(const QString &sessionId) const
{
    if (sessionId.isEmpty() || !m_sessions) {
        return {};
    }

    const auto *meta = m_sessions->sessionMetadata(sessionId);
    if (meta) {
        return meta->agentId;
    }
    return {};
}

QString AgentSessionLinker::sessionNameForAgent(const QString &agentId) const
{
    if (agentId.isEmpty() || !m_sessions) {
        return {};
    }

    // Prefer active session, then most recently accessed
    const auto allMeta = m_sessions->allSessions();
    const SessionMetadata *bestActive = nullptr;
    const SessionMetadata *bestAny = nullptr;
    for (const auto &meta : allMeta) {
        if (meta.agentId == agentId && !meta.isDismissed) {
            if (m_sessions->isSessionActive(meta.sessionId)) {
                if (!bestActive || meta.lastAccessed > bestActive->lastAccessed) {
                    bestActive = &meta;
                }
            }
            if (!bestAny || meta.lastAccessed > bestAny->lastAccessed) {
                bestAny = &meta;
            }
        }
    }
    const auto *best = bestActive ? bestActive : bestAny;
    return best ? best->sessionName : QString();
}

void AgentSessionLinker::focusAgentTab(const QString &agentId)
{
    if (agentId.isEmpty() || !m_sessions) {
        return;
    }

    // Find the session for this agent and request attach/focus
    QString sessionName = sessionNameForAgent(agentId);
    if (!sessionName.isEmpty()) {
        Q_EMIT m_sessions->attachRequested(sessionName);
    }
}

void AgentSessionLinker::selectAgent(const QString &agentId)
{
    if (agentId.isEmpty() || !m_agents) {
        return;
    }

    // Switch to agent tab in the sidebar
    auto *parentWidget = m_agents->parentWidget();
    while (parentWidget) {
        auto *tabWidget = qobject_cast<QTabWidget *>(parentWidget);
        if (tabWidget) {
            for (int i = 0; i < tabWidget->count(); ++i) {
                if (tabWidget->widget(i) == m_agents) {
                    tabWidget->setCurrentIndex(i);
                    break;
                }
            }
            break;
        }
        parentWidget = parentWidget->parentWidget();
    }

    // Select the agent in the tree
    m_agents->selectAgentById(agentId);
}

void AgentSessionLinker::notifyChanged(const QString &agentId)
{
    if (!agentId.isEmpty()) {
        bool hasTab = hasActiveTab(agentId);
        Q_EMIT agentTabPresenceChanged(agentId, hasTab);
    }
}

} // namespace Konsolai

#include "moc_AgentSessionLinker.cpp"
