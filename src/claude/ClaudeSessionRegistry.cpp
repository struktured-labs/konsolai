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
#include <QPointer>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>

#include <unistd.h>

namespace Konsolai
{

/**
 * Ensure SSH_AUTH_SOCK is set in the given process environment.
 * Desktop-launched apps may not inherit the SSH agent socket,
 * so we probe common locations if the variable is missing.
 */
void ClaudeSessionRegistry::ensureSshAuthSock(QProcess *process)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.value(QStringLiteral("SSH_AUTH_SOCK")).isEmpty()) {
        return; // already set
    }

    // Common socket locations (in priority order)
    const uid_t uid = getuid();
    const QString runtimeDir = QStringLiteral("/run/user/%1").arg(uid);
    const QStringList candidates = {
        runtimeDir + QStringLiteral("/openssh_agent"),
        runtimeDir + QStringLiteral("/ssh-agent"),
        runtimeDir + QStringLiteral("/gcr/ssh"),
        runtimeDir + QStringLiteral("/keyring/ssh"),
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            env.insert(QStringLiteral("SSH_AUTH_SOCK"), path);
            process->setProcessEnvironment(env);
            qDebug() << "ClaudeSessionRegistry: Set SSH_AUTH_SOCK to" << path;
            return;
        }
    }

    // Try /tmp/ssh-*/agent.* as last resort
    QDir tmpDir(QStringLiteral("/tmp"));
    const auto sshDirs = tmpDir.entryList({QStringLiteral("ssh-*")}, QDir::Dirs);
    for (const QString &dirName : sshDirs) {
        QDir sshDir(tmpDir.filePath(dirName));
        const auto agents = sshDir.entryList({QStringLiteral("agent.*")}, QDir::Files);
        if (!agents.isEmpty()) {
            QString agentPath = sshDir.filePath(agents.first());
            env.insert(QStringLiteral("SSH_AUTH_SOCK"), agentPath);
            process->setProcessEnvironment(env);
            qDebug() << "ClaudeSessionRegistry: Set SSH_AUTH_SOCK to" << agentPath;
            return;
        }
    }

    qWarning() << "ClaudeSessionRegistry: Could not find SSH_AUTH_SOCK - SSH connections may fail";
}

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

    // Initial refresh of orphaned sessions (async to avoid blocking GUI)
    refreshOrphanedSessionsAsync();

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
    state.taskDescription = session->taskDescription();
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

    // Mark as detached but keep state (including per-session prompt and task description)
    if (m_sessionStates.contains(name)) {
        m_sessionStates[name].isAttached = false;
        m_sessionStates[name].lastAccessed = QDateTime::currentDateTime();
        m_sessionStates[name].taskDescription = session->taskDescription();
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
    // Return orphaned sessions based on cached state from the last
    // refreshOrphanedSessions() call, avoiding synchronous tmux queries
    // on the GUI thread.
    QList<ClaudeSessionState> orphans;
    for (const ClaudeSessionState &state : m_sessionStates) {
        // Orphaned = not attached to Konsolai and still known to exist
        // (liveness is determined during refreshOrphanedSessions)
        if (!state.isAttached && !m_activeSessions.contains(state.sessionName)) {
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

const ClaudeSessionState *ClaudeSessionRegistry::sessionState(const QString &sessionName) const
{
    auto it = m_sessionStates.constFind(sessionName);
    if (it != m_sessionStates.constEnd()) {
        return &it.value();
    }
    return nullptr;
}

bool ClaudeSessionRegistry::sessionExists(const QString &sessionName) const
{
    return m_tmuxManager->sessionExists(sessionName);
}

void ClaudeSessionRegistry::refreshOrphanedSessions()
{
    // Synchronous variant — blocks the GUI thread
    QList<TmuxManager::SessionInfo> tmuxSessions = m_tmuxManager->listKonsolaiSessions();
    refreshOrphanedSessions(tmuxSessions);
}

void ClaudeSessionRegistry::refreshOrphanedSessions(const QList<TmuxManager::SessionInfo> &tmuxSessions)
{
    bool changed = false;

    // Update state for each tmux session
    for (const TmuxManager::SessionInfo &info : tmuxSessions) {
        if (!m_sessionStates.contains(info.name)) {
            // New session discovered (created outside Konsolai or from previous run)
            ClaudeSessionState state;
            state.sessionName = info.name;

            // Try to parse session name: konsolai-{profile}-{id}
            static const QRegularExpression pattern(QStringLiteral("^konsolai-(.+)-([a-f0-9]{8})$"));
            QRegularExpressionMatch match = pattern.match(info.name);
            if (match.hasMatch()) {
                state.profileName = match.captured(1);
                state.sessionId = match.captured(2);
            }

            // Use the tmux session creation time if available, otherwise fall back to now
            bool ok = false;
            qint64 epoch = info.created.toLongLong(&ok);
            if (ok && epoch > 0) {
                state.created = QDateTime::fromSecsSinceEpoch(epoch);
            } else {
                state.created = QDateTime::currentDateTime();
            }
            state.lastAccessed = state.created;
            state.isAttached = m_activeSessions.contains(info.name);

            m_sessionStates.insert(info.name, state);
            changed = true;
        }
    }

    // Mark sessions that no longer exist in tmux as detached, but keep them.
    // Per-project settings (yolo modes, prompts) must survive tmux session death.
    for (auto it = m_sessionStates.begin(); it != m_sessionStates.end(); ++it) {
        bool existsInTmux = false;
        for (const TmuxManager::SessionInfo &info : tmuxSessions) {
            if (info.name == it.key()) {
                existsInTmux = true;
                break;
            }
        }

        if (!existsInTmux && !m_activeSessions.contains(it.key())) {
            if (it->isAttached) {
                it->isAttached = false;
                changed = true;
            }
        }
    }

    if (changed) {
        Q_EMIT orphanedSessionsChanged();
        saveState();
    }
}

QString ClaudeSessionRegistry::sessionStateFilePath()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataDir + QStringLiteral("/konsolai/session-registry.json");
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
        qWarning() << "ClaudeSessionRegistry::saveState: Failed to write session state to" << filePath;
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

void ClaudeSessionRegistry::removeSessionState(const QString &sessionName)
{
    if (m_sessionStates.remove(sessionName) > 0) {
        saveState();
        qDebug() << "ClaudeSessionRegistry: Removed session state for:" << sessionName;
    }
}

QList<ClaudeConversation> ClaudeSessionRegistry::readClaudeConversations(const QString &projectPath)
{
    QList<ClaudeConversation> conversations;

    if (projectPath.isEmpty()) {
        return conversations;
    }

    QString hashedName = hashedProjectPath(projectPath);

    QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;
    QString indexPath = projectDir + QStringLiteral("/sessions-index.json");

    QSet<QString> indexedIds;

    // Phase 1: Read sessions-index.json (Claude's official index)
    QFile file(indexPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        file.close();

        if (error.error == QJsonParseError::NoError) {
            // Handle both formats: bare array or { "version": N, "entries": [...] }
            QJsonArray entries;
            if (doc.isArray()) {
                entries = doc.array();
            } else if (doc.isObject()) {
                entries = doc.object().value(QStringLiteral("entries")).toArray();
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
                    // Count files modified from the .jsonl
                    QString jsonlPath = projectDir + QStringLiteral("/") + conv.sessionId + QStringLiteral(".jsonl");
                    conv.filesModifiedCount = countFilesModified(jsonlPath);
                    conversations.append(conv);
                    indexedIds.insert(conv.sessionId);
                }
            }
        }
    }

    // Phase 2: Scan .jsonl files not in the index.
    // Claude Code doesn't always index every session (e.g., subagent/teammate sessions).
    // We read the first line of each .jsonl to check if it's a conversation (type=user)
    // vs a file-history-snapshot (not a conversation).
    QDir dir(projectDir);
    if (dir.exists()) {
        const auto jsonlFiles = dir.entryInfoList({QStringLiteral("*.jsonl")}, QDir::Files);
        for (const QFileInfo &fi : jsonlFiles) {
            QString sessionId = fi.completeBaseName();
            if (indexedIds.contains(sessionId)) {
                continue;
            }

            QFile jsonlFile(fi.absoluteFilePath());
            if (!jsonlFile.open(QIODevice::ReadOnly)) {
                continue;
            }

            // Scan first lines for the first meaningful "user" message.
            // Claude Code prepends file-history-snapshot lines before user messages.
            // Skip prompts that are just "[Request interrupted by user for tool use]".
            QString firstPrompt;
            bool foundUser = false;
            int messageCount = 0;
            constexpr int kMaxProbeLines = 10;

            for (int lineNum = 0; lineNum < kMaxProbeLines && !jsonlFile.atEnd(); ++lineNum) {
                QByteArray line = jsonlFile.readLine(8192).trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                messageCount++;

                QJsonParseError lineError;
                QJsonDocument lineDoc = QJsonDocument::fromJson(line, &lineError);
                if (lineError.error != QJsonParseError::NoError || !lineDoc.isObject()) {
                    continue;
                }

                QString lineType = lineDoc.object().value(QStringLiteral("type")).toString();
                if (lineType == QStringLiteral("user")) {
                    foundUser = true;
                    QString prompt;
                    QJsonObject msgObj = lineDoc.object().value(QStringLiteral("message")).toObject();
                    QJsonValue content = msgObj.value(QStringLiteral("content"));
                    if (content.isString()) {
                        prompt = content.toString().left(200);
                    } else if (content.isArray()) {
                        for (const QJsonValue &part : content.toArray()) {
                            if (part.isObject() && part.toObject().value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                                prompt = part.toObject().value(QStringLiteral("text")).toString().left(200);
                                break;
                            }
                        }
                    }
                    // Skip synthetic/interrupted prompts — keep scanning for a real one
                    if (!prompt.isEmpty() && !prompt.startsWith(QStringLiteral("[Request interrupted"))) {
                        firstPrompt = prompt;
                        break;
                    }
                }
            }

            if (!foundUser) {
                jsonlFile.close();
                continue;
            }

            // Count remaining messages
            while (!jsonlFile.atEnd()) {
                jsonlFile.readLine();
                messageCount++;
            }
            jsonlFile.close();

            ClaudeConversation conv;
            conv.sessionId = sessionId;
            conv.firstPrompt = firstPrompt;
            conv.messageCount = messageCount;
            conv.filesModifiedCount = countFilesModified(fi.absoluteFilePath());
            conv.modified = fi.lastModified();
            conv.created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();

            conversations.append(conv);
        }
    }

    // Sort by modified date descending (most recent first)
    std::sort(conversations.begin(), conversations.end(), [](const ClaudeConversation &a, const ClaudeConversation &b) {
        return a.modified > b.modified;
    });

    return conversations;
}

int ClaudeSessionRegistry::countFilesModified(const QString &jsonlPath)
{
    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    QSet<QString> files;
    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        // Quick heuristic: skip lines that can't contain Write/Edit tool_use
        if (!line.contains("tool_use")) {
            continue;
        }
        QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
        if (!doc.isObject()) {
            continue;
        }
        QJsonArray content = doc.object()[QStringLiteral("message")].toObject()[QStringLiteral("content")].toArray();
        for (const auto &block : content) {
            QJsonObject b = block.toObject();
            if (b[QStringLiteral("type")].toString() != QStringLiteral("tool_use")) {
                continue;
            }
            QString toolName = b[QStringLiteral("name")].toString();
            if (toolName == QStringLiteral("Write") || toolName == QStringLiteral("Edit")) {
                QString fp = b[QStringLiteral("input")].toObject()[QStringLiteral("file_path")].toString();
                if (!fp.isEmpty()) {
                    files.insert(fp);
                }
            }
        }
    }
    file.close();
    return files.size();
}

QString ClaudeSessionRegistry::hashedProjectPath(const QString &projectPath)
{
    QString hashed = projectPath;
    hashed.replace(QLatin1Char('/'), QLatin1Char('-'));
    return hashed;
}

void ClaudeSessionRegistry::readRemoteConversationsAsync(
    const QString &sshTarget, int sshPort,
    const QString &projectPath,
    std::function<void(const QList<ClaudeConversation> &)> callback)
{
    if (sshTarget.isEmpty() || projectPath.isEmpty()) {
        if (callback) {
            callback({});
        }
        return;
    }

    QString hashedName = hashedProjectPath(projectPath);

    // Read conversations from the remote host by scanning .jsonl files
    // for the first real user prompt. Merges summaries from sessions-index.json
    // when available (Claude CLI's index often has empty summary/firstPrompt).
    // The Python script is piped via stdin to avoid shell quoting issues.
    QString remoteCmd = QStringLiteral("python3 - %1").arg(hashedName);

    QStringList args;
    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
         << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=5");
    if (sshPort != 22 && sshPort > 0) {
        args << QStringLiteral("-p") << QString::number(sshPort);
    }
    args << sshTarget << remoteCmd;

    // Python script to scan conversations — sent via stdin to avoid quoting
    static const QByteArray pyScript = QByteArrayLiteral(
        "import json, os, glob, sys\n"
        "d = os.path.join(os.path.expanduser('~'), '.claude/projects/' + sys.argv[1])\n"
        "if not os.path.isdir(d): print('[]'); exit()\n"
        "idx = {}\n"
        "ix = os.path.join(d, 'sessions-index.json')\n"
        "if os.path.exists(ix):\n"
        "    try:\n"
        "        with open(ix) as xf: raw = json.load(xf)\n"
        "        entries = raw if isinstance(raw, list) else raw.get('entries', [])\n"
        "        for e in entries:\n"
        "            sid = e.get('sessionId', '')\n"
        "            if sid: idx[sid] = e\n"
        "    except: pass\n"
        "results = []\n"
        "for f in sorted(glob.glob(os.path.join(d, '*.jsonl'))):\n"
        "    bn = os.path.splitext(os.path.basename(f))[0]\n"
        "    if bn.startswith('agent-'): continue\n"
        "    prompt = ''\n"
        "    try:\n"
        "        with open(f) as fh:\n"
        "            for i, line in enumerate(fh):\n"
        "                if i >= 10: break\n"
        "                obj = json.loads(line)\n"
        "                if obj.get('type') != 'user': continue\n"
        "                m = obj.get('message', {})\n"
        "                c = m.get('content', '')\n"
        "                if isinstance(c, list):\n"
        "                    c = next((p['text'] for p in c if p.get('type')=='text'), '')\n"
        "                if c and not c.startswith('[Request interrupted'):\n"
        "                    prompt = c[:200]; break\n"
        "    except: continue\n"
        "    if not prompt: continue\n"
        "    st = os.stat(f)\n"
        "    with open(f) as cf: mc = sum(1 for _ in cf)\n"
        "    from datetime import datetime, timezone\n"
        "    mod = datetime.fromtimestamp(st.st_mtime, tz=timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')\n"
        "    ie = idx.get(bn, {})\n"
        "    summary = ie.get('summary', '')\n"
        "    results.append({'sessionId': bn, 'summary': summary, 'firstPrompt': prompt,\n"
        "                    'messageCount': ie.get('messageCount', mc), 'modified': mod,\n"
        "                    'created': ie.get('created', mod)})\n"
        "results.sort(key=lambda x: x['modified'], reverse=True)\n"
        "print(json.dumps(results))\n"
    );

    auto *process = new QProcess(this);
    ensureSshAuthSock(process);
    QPointer<ClaudeSessionRegistry> guard(this);

    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            qDebug() << "readRemoteConversationsAsync: timeout, killing process";
            process->kill();
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [process, callback, guard, timeout](int exitCode, QProcess::ExitStatus) {
        timeout->stop();
        QList<ClaudeConversation> conversations;
        if (guard) {
            QByteArray data = process->readAllStandardOutput();
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            if (error.error == QJsonParseError::NoError) {
                QJsonArray entries;
                if (doc.isArray()) {
                    entries = doc.array();
                } else if (doc.isObject()) {
                    entries = doc.object().value(QStringLiteral("entries")).toArray();
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
                    conv.created = QDateTime::fromString(
                        obj.value(QStringLiteral("created")).toString(), Qt::ISODate);
                    conv.modified = QDateTime::fromString(
                        obj.value(QStringLiteral("modified")).toString(), Qt::ISODate);
                    if (!conv.sessionId.isEmpty()) {
                        conversations.append(conv);
                    }
                }
                std::sort(conversations.begin(), conversations.end(),
                    [](const ClaudeConversation &a, const ClaudeConversation &b) {
                        return a.modified > b.modified;
                    });
            } else {
                qDebug() << "readRemoteConversationsAsync: JSON parse error:" << error.errorString()
                         << "exitCode:" << exitCode << "output:" << data.left(200);
            }
        }
        if (callback) {
            callback(conversations);
        }
        process->deleteLater();
    });

    process->start(QStringLiteral("ssh"), args);
    if (process->waitForStarted(5000)) {
        process->write(pyScript);
        process->closeWriteChannel();
        timeout->start(15000);
    } else {
        qDebug() << "readRemoteConversationsAsync: SSH process failed to start";
        if (callback) {
            callback({});
        }
        process->deleteLater();
    }
}

void ClaudeSessionRegistry::discoverAllRemoteConversationsAsync(
    const QString &sshTarget, int sshPort,
    std::function<void(const QList<ClaudeConversation> &)> callback)
{
    if (sshTarget.isEmpty()) {
        if (callback) {
            callback({});
        }
        return;
    }

    // Python script scans ALL projects under ~/.claude/projects/
    // Returns conversations with projectDir (hashed name) and reconstructed projectPath
    QString remoteCmd = QStringLiteral("python3 -");

    QStringList args;
    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
         << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=10");
    if (sshPort != 22 && sshPort > 0) {
        args << QStringLiteral("-p") << QString::number(sshPort);
    }
    args << sshTarget << remoteCmd;

    static const QByteArray pyScript = QByteArrayLiteral(
        "import json, os, glob, sys\n"
        "from datetime import datetime, timezone\n"
        "def rpath(h):\n"
        "    if not h.startswith('-'): return h\n"
        "    segs = h[1:].split('-')\n"
        "    p = '/'\n"
        "    i = 0\n"
        "    while i < len(segs):\n"
        "        ok = False\n"
        "        for j in range(len(segs), i, -1):\n"
        "            c = os.path.join(p, '-'.join(segs[i:j]))\n"
        "            if os.path.isdir(c):\n"
        "                p = c; i = j; ok = True; break\n"
        "        if not ok:\n"
        "            p = os.path.join(p, '-'.join(segs[i:]))\n"
        "            break\n"
        "    return p\n"
        "base = os.path.join(os.path.expanduser('~'), '.claude/projects')\n"
        "if not os.path.isdir(base): print('[]'); exit()\n"
        "results = []\n"
        "for proj in os.listdir(base):\n"
        "    d = os.path.join(base, proj)\n"
        "    if not os.path.isdir(d): continue\n"
        "    orig = rpath(proj)\n"
        "    idx = {}\n"
        "    ix = os.path.join(d, 'sessions-index.json')\n"
        "    if os.path.exists(ix):\n"
        "        try:\n"
        "            with open(ix) as xf: raw = json.load(xf)\n"
        "            entries = raw if isinstance(raw, list) else raw.get('entries', [])\n"
        "            for e in entries:\n"
        "                sid = e.get('sessionId', '')\n"
        "                if sid: idx[sid] = e\n"
        "        except: pass\n"
        "    for f in sorted(glob.glob(os.path.join(d, '*.jsonl'))):\n"
        "        bn = os.path.splitext(os.path.basename(f))[0]\n"
        "        if bn.startswith('agent-'): continue\n"
        "        prompt = ''\n"
        "        try:\n"
        "            with open(f) as fh:\n"
        "                for i, line in enumerate(fh):\n"
        "                    if i >= 10: break\n"
        "                    obj = json.loads(line)\n"
        "                    if obj.get('type') != 'user': continue\n"
        "                    m = obj.get('message', {})\n"
        "                    c = m.get('content', '')\n"
        "                    if isinstance(c, list):\n"
        "                        c = next((p['text'] for p in c if p.get('type')=='text'), '')\n"
        "                    if c and not c.startswith('[Request interrupted'):\n"
        "                        prompt = c[:200]; break\n"
        "        except: continue\n"
        "        if not prompt: continue\n"
        "        try:\n"
        "            st = os.stat(f)\n"
        "            with open(f) as cf: mc = sum(1 for _ in cf)\n"
        "        except: continue\n"
        "        mod = datetime.fromtimestamp(st.st_mtime, tz=timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')\n"
        "        ie = idx.get(bn, {})\n"
        "        summary = ie.get('summary', '')\n"
        "        results.append({'sessionId': bn, 'summary': summary, 'firstPrompt': prompt,\n"
        "                        'messageCount': ie.get('messageCount', mc), 'modified': mod,\n"
        "                        'created': ie.get('created', mod),\n"
        "                        'projectDir': proj, 'projectPath': orig})\n"
        "results.sort(key=lambda x: x['modified'], reverse=True)\n"
        "print(json.dumps(results))\n"
    );

    auto *process = new QProcess(this);
    ensureSshAuthSock(process);
    QPointer<ClaudeSessionRegistry> guard(this);

    // Kill process after 30 seconds to avoid infinite hangs
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            qDebug() << "discoverAllRemoteConversationsAsync: timeout, killing process";
            process->kill();
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [process, callback, guard, timeout](int exitCode, QProcess::ExitStatus) {
        timeout->stop();
        QList<ClaudeConversation> conversations;
        if (guard) {
            QByteArray data = process->readAllStandardOutput();
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            if (error.error == QJsonParseError::NoError && doc.isArray()) {
                for (const QJsonValue &value : doc.array()) {
                    if (!value.isObject()) {
                        continue;
                    }
                    QJsonObject obj = value.toObject();
                    ClaudeConversation conv;
                    conv.sessionId = obj.value(QStringLiteral("sessionId")).toString();
                    conv.summary = obj.value(QStringLiteral("summary")).toString();
                    conv.firstPrompt = obj.value(QStringLiteral("firstPrompt")).toString();
                    conv.messageCount = obj.value(QStringLiteral("messageCount")).toInt();
                    conv.created = QDateTime::fromString(
                        obj.value(QStringLiteral("created")).toString(), Qt::ISODate);
                    conv.modified = QDateTime::fromString(
                        obj.value(QStringLiteral("modified")).toString(), Qt::ISODate);
                    conv.projectPath = obj.value(QStringLiteral("projectPath")).toString();
                    if (!conv.sessionId.isEmpty()) {
                        conversations.append(conv);
                    }
                }
            } else {
                qDebug() << "discoverAllRemoteConversationsAsync: JSON parse error:"
                         << error.errorString() << "exitCode:" << exitCode
                         << "stderr:" << process->readAllStandardError().left(200);
            }
        }
        if (callback) {
            callback(conversations);
        }
        process->deleteLater();
    });

    process->start(QStringLiteral("ssh"), args);
    if (process->waitForStarted(5000)) {
        process->write(pyScript);
        process->closeWriteChannel();
        timeout->start(30000);
    } else {
        qDebug() << "discoverAllRemoteConversationsAsync: SSH process failed to start";
        if (callback) {
            callback({});
        }
        process->deleteLater();
    }
}

void ClaudeSessionRegistry::discoverRemoteTmuxSessionsAsync(
    const QString &sshTarget, int sshPort, bool konsolaiOnly,
    std::function<void(const QList<TmuxManager::SessionInfo> &)> callback)
{
    if (sshTarget.isEmpty()) {
        if (callback) {
            callback({});
        }
        return;
    }

    QStringList args;
    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
         << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=5");
    if (sshPort != 22 && sshPort > 0) {
        args << QStringLiteral("-p") << QString::number(sshPort);
    }
    args << sshTarget
         << QStringLiteral("tmux list-sessions -F '#{session_name}:#{session_id}:#{session_attached}:#{session_windows}:#{session_created}:#{pane_current_path}' 2>/dev/null");

    auto *process = new QProcess(this);
    ensureSshAuthSock(process);
    QPointer<ClaudeSessionRegistry> guard(this);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [process, callback, konsolaiOnly, guard](int exitCode, QProcess::ExitStatus) {
        QList<TmuxManager::SessionInfo> sessions;
        if (guard && (exitCode == 0 || exitCode == 1)) {
            // exitCode 1 can mean "no sessions" which is valid
            QString output = QString::fromUtf8(process->readAllStandardOutput());
            const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                QStringList parts = line.split(QLatin1Char(':'));
                if (parts.size() < 3) {
                    continue;
                }

                TmuxManager::SessionInfo info;
                info.name = parts[0].trimmed();
                info.id = parts.size() > 1 ? parts[1].trimmed() : QString();
                info.attached = parts.size() > 2 && parts[2].trimmed() != QStringLiteral("0");
                info.windows = parts.size() > 3 ? parts[3].trimmed().toInt() : 1;
                info.created = parts.size() > 4 ? parts[4].trimmed() : QString();
                // paneCurrentPath is the last field and may contain colons — rejoin
                if (parts.size() > 5) {
                    QStringList pathParts = parts.mid(5);
                    info.paneCurrentPath = pathParts.join(QLatin1Char(':')).trimmed();
                }

                if (konsolaiOnly && !info.name.startsWith(QStringLiteral("konsolai-"))) {
                    continue;
                }
                sessions.append(info);
            }
        }
        if (callback) {
            callback(sessions);
        }
        process->deleteLater();
    });

    process->start(QStringLiteral("ssh"), args);
    if (!process->waitForStarted(5000)) {
        qDebug() << "discoverRemoteTmuxSessionsAsync: SSH process failed to start";
        if (callback) {
            callback({});
        }
        process->deleteLater();
    }
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
    refreshOrphanedSessionsAsync();
}

void ClaudeSessionRegistry::refreshOrphanedSessionsAsync()
{
    auto *tmux = new TmuxManager(nullptr);
    QPointer<ClaudeSessionRegistry> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();
        if (guard) {
            guard->refreshOrphanedSessions(liveSessions);
        }
    });
}

} // namespace Konsolai

#include "moc_ClaudeSessionRegistry.cpp"
