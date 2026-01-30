/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionRegistry.h"
#include "ClaudeSession.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>

namespace Konsolai
{

static ClaudeSessionRegistry *s_registryInstance = nullptr;

ClaudeSessionRegistry::ClaudeSessionRegistry(QObject *parent)
    : QObject(parent)
    , m_tmuxManager(new TmuxManager(this))
    , m_refreshTimer(new QTimer(this))
{
    if (!s_registryInstance) {
        s_registryInstance = this;
    }

    // Load persisted state
    loadState();

    // Initial refresh of orphaned sessions
    refreshOrphanedSessions();

    // Setup periodic refresh
    connect(m_refreshTimer, &QTimer::timeout, this, &ClaudeSessionRegistry::onPeriodicRefresh);
    m_refreshTimer->start(REFRESH_INTERVAL_MS);
}

ClaudeSessionRegistry::~ClaudeSessionRegistry()
{
    // Save state before shutdown
    saveState();

    if (s_registryInstance == this) {
        s_registryInstance = nullptr;
    }
}

ClaudeSessionRegistry* ClaudeSessionRegistry::instance()
{
    return s_registryInstance;
}

void ClaudeSessionRegistry::registerSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString name = session->sessionName();
    m_activeSessions.insert(name, session);

    // Update or create state entry
    ClaudeSessionState state;
    state.sessionName = name;
    state.sessionId = session->sessionId();
    state.profileName = session->profileName();
    state.workingDirectory = session->workingDirectory();
    state.created = m_sessionStates.contains(name) ?
                    m_sessionStates[name].created : QDateTime::currentDateTime();
    state.lastAccessed = QDateTime::currentDateTime();
    state.isAttached = true;

    m_sessionStates.insert(name, state);

    Q_EMIT sessionRegistered(session);
    saveState();
}

void ClaudeSessionRegistry::unregisterSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString name = session->sessionName();
    m_activeSessions.remove(name);

    // Mark as detached but keep state
    if (m_sessionStates.contains(name)) {
        m_sessionStates[name].isAttached = false;
        m_sessionStates[name].lastAccessed = QDateTime::currentDateTime();
    }

    Q_EMIT sessionUnregistered(name);
    saveState();
}

void ClaudeSessionRegistry::markAttached(const QString &sessionName)
{
    if (m_sessionStates.contains(sessionName)) {
        m_sessionStates[sessionName].isAttached = true;
        m_sessionStates[sessionName].lastAccessed = QDateTime::currentDateTime();
        saveState();
    }
}

void ClaudeSessionRegistry::markDetached(const QString &sessionName)
{
    if (m_sessionStates.contains(sessionName)) {
        m_sessionStates[sessionName].isAttached = false;
        m_sessionStates[sessionName].lastAccessed = QDateTime::currentDateTime();
        saveState();
    }
}

QList<ClaudeSessionState> ClaudeSessionRegistry::orphanedSessions() const
{
    QList<ClaudeSessionState> orphans;

    for (const ClaudeSessionState &state : m_sessionStates) {
        // Orphaned = exists in tmux but not attached to Konsolai
        if (!state.isAttached && m_tmuxManager->sessionExists(state.sessionName)) {
            orphans.append(state);
        }
    }

    return orphans;
}

QList<ClaudeSessionState> ClaudeSessionRegistry::allSessionStates() const
{
    return m_sessionStates.values();
}

ClaudeSession* ClaudeSessionRegistry::findSession(const QString &sessionName) const
{
    return m_activeSessions.value(sessionName, nullptr);
}

bool ClaudeSessionRegistry::sessionExists(const QString &sessionName) const
{
    return m_tmuxManager->sessionExists(sessionName);
}

void ClaudeSessionRegistry::refreshOrphanedSessions()
{
    // Get current tmux sessions
    QList<TmuxManager::SessionInfo> tmuxSessions = m_tmuxManager->listKonsolaiSessions();

    bool changed = false;

    // Update state for each tmux session
    for (const TmuxManager::SessionInfo &info : tmuxSessions) {
        if (!m_sessionStates.contains(info.name)) {
            // New session discovered (created outside Konsolai or from previous run)
            ClaudeSessionState state;
            state.sessionName = info.name;

            // Try to parse session name: konsolai-{profile}-{id}
            QRegularExpression pattern(QStringLiteral("^konsolai-(.+)-([a-f0-9]{8})$"));
            QRegularExpressionMatch match = pattern.match(info.name);
            if (match.hasMatch()) {
                state.profileName = match.captured(1);
                state.sessionId = match.captured(2);
            }

            state.created = QDateTime::currentDateTime();  // Unknown, use now
            state.lastAccessed = QDateTime::currentDateTime();
            state.isAttached = m_activeSessions.contains(info.name);

            m_sessionStates.insert(info.name, state);
            changed = true;
        }
    }

    // Check for sessions that no longer exist in tmux
    QList<QString> toRemove;
    for (auto it = m_sessionStates.begin(); it != m_sessionStates.end(); ++it) {
        bool existsInTmux = false;
        for (const TmuxManager::SessionInfo &info : tmuxSessions) {
            if (info.name == it.key()) {
                existsInTmux = true;
                break;
            }
        }

        if (!existsInTmux && !m_activeSessions.contains(it.key())) {
            // Session no longer exists, remove from state
            toRemove.append(it.key());
            changed = true;
        }
    }

    for (const QString &name : toRemove) {
        m_sessionStates.remove(name);
    }

    if (changed) {
        Q_EMIT orphanedSessionsChanged();
        saveState();
    }
}

QString ClaudeSessionRegistry::sessionStateFilePath()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataDir + QStringLiteral("/konsolai/sessions.json");
}

void ClaudeSessionRegistry::loadState()
{
    QString filePath = sessionStateFilePath();
    QFile file(filePath);

    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        return;
    }

    if (!doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();

    for (const QJsonValue &value : sessions) {
        if (value.isObject()) {
            ClaudeSessionState state = ClaudeSessionState::fromJson(value.toObject());
            if (state.isValid()) {
                // Mark as detached since we're just loading from disk
                state.isAttached = false;
                m_sessionStates.insert(state.sessionName, state);
            }
        }
    }
}

void ClaudeSessionRegistry::saveState()
{
    QString filePath = sessionStateFilePath();

    // Ensure directory exists
    QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonArray sessions;
    for (const ClaudeSessionState &state : m_sessionStates) {
        sessions.append(state.toJson());
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("sessions")] = sessions;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

void ClaudeSessionRegistry::onPeriodicRefresh()
{
    refreshOrphanedSessions();
}

} // namespace Konsolai

#include "moc_ClaudeSessionRegistry.cpp"
