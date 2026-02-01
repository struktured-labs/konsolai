/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionRegistry.h"
#include "ClaudeSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
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
    state.autoContinuePrompt = session->autoContinuePrompt();
    state.yoloMode = session->yoloMode();
    state.doubleYoloMode = session->doubleYoloMode();
    state.tripleYoloMode = session->tripleYoloMode();

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

    // Mark as detached but keep state (including per-session prompt)
    if (m_sessionStates.contains(name)) {
        m_sessionStates[name].isAttached = false;
        m_sessionStates[name].lastAccessed = QDateTime::currentDateTime();
        m_sessionStates[name].autoContinuePrompt = session->autoContinuePrompt();
        m_sessionStates[name].yoloMode = session->yoloMode();
        m_sessionStates[name].doubleYoloMode = session->doubleYoloMode();
        m_sessionStates[name].tripleYoloMode = session->tripleYoloMode();
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

QString ClaudeSessionRegistry::lastAutoContinuePrompt(const QString &workingDirectory) const
{
    // Find the most recently accessed session with a matching working directory
    // that has a custom prompt set
    QDateTime newest;
    QString result;

    for (const ClaudeSessionState &state : m_sessionStates) {
        if (state.workingDirectory == workingDirectory && !state.autoContinuePrompt.isEmpty()) {
            if (!newest.isValid() || state.lastAccessed > newest) {
                newest = state.lastAccessed;
                result = state.autoContinuePrompt;
            }
        }
    }

    return result;
}

void ClaudeSessionRegistry::updateSessionPrompt(const QString &sessionName, const QString &prompt)
{
    if (m_sessionStates.contains(sessionName)) {
        m_sessionStates[sessionName].autoContinuePrompt = prompt;
        saveState();
    }
}

const ClaudeSessionState *ClaudeSessionRegistry::lastSessionState(const QString &workingDirectory) const
{
    QDateTime newest;
    const ClaudeSessionState *result = nullptr;

    for (const ClaudeSessionState &state : m_sessionStates) {
        if (state.workingDirectory == workingDirectory) {
            if (!newest.isValid() || state.lastAccessed > newest) {
                newest = state.lastAccessed;
                result = &state;
            }
        }
    }

    return result;
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

QList<ClaudeConversation> ClaudeSessionRegistry::readClaudeConversations(const QString &projectPath)
{
    QList<ClaudeConversation> conversations;

    if (projectPath.isEmpty()) {
        return conversations;
    }

    // Convert project path to hashed directory name: replace '/' with '-'
    // e.g. /home/user/projects/foo → -home-user-projects-foo
    QString hashedName = projectPath;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));

    QString indexPath = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName + QStringLiteral("/sessions-index.json");

    QFile file(indexPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return conversations;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        return conversations;
    }

    // Handle both formats: bare array or { "version": N, "entries": [...] }
    QJsonArray entries;
    if (doc.isArray()) {
        entries = doc.array();
    } else if (doc.isObject()) {
        entries = doc.object().value(QStringLiteral("entries")).toArray();
    } else {
        return conversations;
    }
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject obj = value.toObject();
        ClaudeConversation conv;
        conv.sessionId = obj.value(QStringLiteral("sessionId")).toString();
        conv.summary = obj.value(QStringLiteral("summary")).toString();
        conv.firstPrompt = obj.value(QStringLiteral("firstPrompt")).toString();
        conv.messageCount = obj.value(QStringLiteral("messageCount")).toInt();
        conv.created = QDateTime::fromString(obj.value(QStringLiteral("created")).toString(), Qt::ISODate);
        conv.modified = QDateTime::fromString(obj.value(QStringLiteral("modified")).toString(), Qt::ISODate);

        if (!conv.sessionId.isEmpty()) {
            conversations.append(conv);
        }
    }

    // Sort by modified date descending (most recent first)
    std::sort(conversations.begin(), conversations.end(), [](const ClaudeConversation &a, const ClaudeConversation &b) {
        return a.modified > b.modified;
    });

    return conversations;
}

QList<ClaudeSessionState> ClaudeSessionRegistry::discoverSessions(const QString &searchRoot) const
{
    QList<ClaudeSessionState> discovered;

    if (searchRoot.isEmpty() || !QDir(searchRoot).exists()) {
        return discovered;
    }

    QDir rootDir(searchRoot);
    const auto entries = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString &dirName : entries) {
        QString projectPath = rootDir.filePath(dirName);

        // Check for .claude directory (Claude Code footprint)
        QString claudeDir = QDir(projectPath).filePath(QStringLiteral(".claude"));
        if (!QDir(claudeDir).exists()) {
            continue;
        }

        // Skip if we already know about this project
        bool alreadyKnown = false;
        for (auto it = m_sessionStates.constBegin(); it != m_sessionStates.constEnd(); ++it) {
            if (it.value().workingDirectory == projectPath) {
                alreadyKnown = true;
                break;
            }
        }
        if (alreadyKnown) {
            continue;
        }

        // Create a discoverable session state
        ClaudeSessionState state;
        state.sessionName = QStringLiteral("discovered-%1").arg(dirName);
        state.sessionId = dirName.left(8);
        state.workingDirectory = projectPath;
        state.isAttached = false;

        // Try to get more info from settings.local.json
        QString settingsPath = QDir(claudeDir).filePath(QStringLiteral("settings.local.json"));
        if (QFile::exists(settingsPath)) {
            QFile f(settingsPath);
            if (f.open(QIODevice::ReadOnly)) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    // If it has konsolai hooks, it was a konsolai session
                    QString content = QString::fromUtf8(doc.toJson());
                    if (content.contains(QStringLiteral("konsolai"))) {
                        state.profileName = QStringLiteral("Claude");
                    } else {
                        state.profileName = QStringLiteral("External");
                    }
                }
                f.close();
            }
        } else {
            state.profileName = QStringLiteral("External");
        }

        // Check file modification time for approximate date
        QFileInfo claudeDirInfo(claudeDir);
        state.created = claudeDirInfo.birthTime().isValid() ? claudeDirInfo.birthTime() : claudeDirInfo.lastModified();
        state.lastAccessed = claudeDirInfo.lastModified();

        discovered.append(state);
    }

    return discovered;
}

void ClaudeSessionRegistry::onPeriodicRefresh()
{
    refreshOrphanedSessions();
}

} // namespace Konsolai

#include "moc_ClaudeSessionRegistry.cpp"
