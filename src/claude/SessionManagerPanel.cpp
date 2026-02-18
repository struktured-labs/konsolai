/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionManagerPanel.h"
#include "ClaudeSession.h"
#include "ClaudeSessionRegistry.h"
#include "KonsolaiSettings.h"
#include "TmuxManager.h"

#include <KLocalizedString>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace Konsolai
{

static const QString SETTINGS_GROUP = QStringLiteral("SessionManager");

static QString formatElapsed(const QDateTime &start)
{
    if (!start.isValid()) {
        return {};
    }
    qint64 secs = start.secsTo(QDateTime::currentDateTime());
    if (secs < 0) {
        secs = 0;
    }
    if (secs < 60) {
        return QStringLiteral("%1s").arg(secs);
    }
    qint64 mins = secs / 60;
    qint64 remSecs = secs % 60;
    if (mins < 60) {
        return QStringLiteral("%1m %2s").arg(mins).arg(remSecs);
    }
    qint64 hours = mins / 60;
    qint64 remMins = mins % 60;
    return QStringLiteral("%1h %2m").arg(hours).arg(remMins);
}

SessionManagerPanel::SessionManagerPanel(QWidget *parent)
    : QWidget(parent)
    , m_registry(ClaudeSessionRegistry::instance())
{
    setupUi();
    loadMetadata();
    cleanupStaleSockets(); // async — returns immediately
    refresh(); // async — returns immediately
}

SessionManagerPanel::~SessionManagerPanel()
{
    saveMetadata();
}

void SessionManagerPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Header with collapse button and new session button
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(4, 4, 4, 4);

    m_collapseButton = new QPushButton(this);
    m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-collapse-left")));
    m_collapseButton->setFlat(true);
    m_collapseButton->setFixedSize(24, 24);
    m_collapseButton->setToolTip(i18n("Toggle Session Panel"));
    connect(m_collapseButton, &QPushButton::clicked, this, [this]() {
        setCollapsed(!m_collapsed);
    });
    headerLayout->addWidget(m_collapseButton);

    // Note: Title "Sessions" is shown in dock widget title bar, no need for duplicate label here

    headerLayout->addStretch();

    m_newSessionButton = new QPushButton(this);
    m_newSessionButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_newSessionButton->setFlat(true);
    m_newSessionButton->setFixedSize(24, 24);
    m_newSessionButton->setToolTip(i18n("New Claude Session"));
    connect(m_newSessionButton, &QPushButton::clicked, this, &SessionManagerPanel::onNewSessionClicked);
    headerLayout->addWidget(m_newSessionButton);

    layout->addLayout(headerLayout);

    // Tree widget for sessions
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setColumnCount(2);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setIndentation(12);
    // Column 0: session name (stretches), Column 1: indicators (fixed width, right-aligned)
    m_treeWidget->header()->setStretchLastSection(false);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SessionManagerPanel::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &SessionManagerPanel::onContextMenu);

    layout->addWidget(m_treeWidget);

    // Create category items
    m_pinnedCategory = new QTreeWidgetItem(m_treeWidget);
    m_pinnedCategory->setText(0, i18n("Pinned"));
    m_pinnedCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("pin")));
    m_pinnedCategory->setFlags(Qt::ItemIsEnabled);
    m_pinnedCategory->setExpanded(true);

    m_activeCategory = new QTreeWidgetItem(m_treeWidget);
    m_activeCategory->setText(0, i18n("Active"));
    m_activeCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("media-playback-start")));
    m_activeCategory->setFlags(Qt::ItemIsEnabled);
    m_activeCategory->setExpanded(true);

    m_detachedCategory = new QTreeWidgetItem(m_treeWidget);
    m_detachedCategory->setText(0, i18n("Detached"));
    m_detachedCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("media-playback-pause")));
    m_detachedCategory->setFlags(Qt::ItemIsEnabled);
    m_detachedCategory->setExpanded(true);

    m_closedCategory = new QTreeWidgetItem(m_treeWidget);
    m_closedCategory->setText(0, i18n("Closed"));
    m_closedCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("window-close")));
    m_closedCategory->setFlags(Qt::ItemIsEnabled);
    m_closedCategory->setExpanded(true);

    m_archivedCategory = new QTreeWidgetItem(m_treeWidget);
    m_archivedCategory->setText(0, i18n("Archived"));
    m_archivedCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("archive-remove")));
    m_archivedCategory->setFlags(Qt::ItemIsEnabled);
    m_archivedCategory->setExpanded(false);

    m_dismissedCategory = new QTreeWidgetItem(m_treeWidget);
    m_dismissedCategory->setText(0, i18n("Dismissed"));
    m_dismissedCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("edit-clear-history")));
    m_dismissedCategory->setFlags(Qt::ItemIsEnabled);
    m_dismissedCategory->setExpanded(false);

    m_discoveredCategory = new QTreeWidgetItem(m_treeWidget);
    m_discoveredCategory->setText(0, i18n("Discovered"));
    m_discoveredCategory->setIcon(0, QIcon::fromTheme(QStringLiteral("edit-find")));
    m_discoveredCategory->setFlags(Qt::ItemIsEnabled);
    m_discoveredCategory->setExpanded(false);

    setMinimumWidth(200);
    setMaximumWidth(350);
}

void SessionManagerPanel::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) {
        return;
    }

    m_collapsed = collapsed;

    if (collapsed) {
        m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-expand-left")));
        setMaximumWidth(32);
        m_treeWidget->hide();
        m_newSessionButton->hide();
    } else {
        m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-collapse-left")));
        setMaximumWidth(350);
        m_treeWidget->show();
        m_newSessionButton->show();
    }

    Q_EMIT collapsedChanged(collapsed);
}

void SessionManagerPanel::ensureHooksConfigured(ClaudeSession *session)
{
    if (!session || session->isRemote()) {
        return; // Skip for null or remote sessions
    }

    QString workDir = session->workingDirectory();
    if (workDir.isEmpty()) {
        return;
    }

    QString settingsPath = workDir + QStringLiteral("/.claude/settings.local.json");
    QString sessionId = session->sessionId();
    QString socketPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
    QString handlerPath = ClaudeHookHandler::hookHandlerPath();

    if (handlerPath.isEmpty()) {
        return; // No hook handler available
    }

    // Check if hooks are already configured for this session
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (doc.isObject()) {
            QJsonObject settings = doc.object();
            if (settings.contains(QStringLiteral("hooks"))) {
                QJsonObject hooks = settings[QStringLiteral("hooks")].toObject();
                // Check if any hook points to our socket
                QByteArray hooksBytes = QJsonDocument(hooks).toJson();
                if (hooksBytes.contains(socketPath.toUtf8())) {
                    return; // Hooks already configured correctly
                }
            }
        }
    }

    // Hooks missing or pointing to wrong socket - repair them
    qDebug() << "SessionManagerPanel: Repairing missing/stale hooks for session" << sessionId;

    // Read existing settings
    QJsonObject settings;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            settings = doc.object();
        }
        file.close();
    }

    // Generate hooks config
    auto makeHookEntry = [&](const QString &eventType) -> QJsonArray {
        QString cmdStr = QStringLiteral("'%1' --socket '%2' --event '%3'").arg(handlerPath, socketPath, eventType);
        QJsonObject hookDef;
        hookDef[QStringLiteral("type")] = QStringLiteral("command");
        hookDef[QStringLiteral("command")] = cmdStr;

        QJsonObject entry;
        entry[QStringLiteral("matcher")] = QStringLiteral("*");
        entry[QStringLiteral("hooks")] = QJsonArray{hookDef};
        return QJsonArray{entry};
    };

    QJsonObject hooks;
    hooks[QStringLiteral("Notification")] = makeHookEntry(QStringLiteral("Notification"));
    hooks[QStringLiteral("Stop")] = makeHookEntry(QStringLiteral("Stop"));
    hooks[QStringLiteral("PreToolUse")] = makeHookEntry(QStringLiteral("PreToolUse"));
    hooks[QStringLiteral("PostToolUse")] = makeHookEntry(QStringLiteral("PostToolUse"));
    hooks[QStringLiteral("PermissionRequest")] = makeHookEntry(QStringLiteral("PermissionRequest"));
    hooks[QStringLiteral("SubagentStart")] = makeHookEntry(QStringLiteral("SubagentStart"));
    hooks[QStringLiteral("SubagentStop")] = makeHookEntry(QStringLiteral("SubagentStop"));
    hooks[QStringLiteral("TeammateIdle")] = makeHookEntry(QStringLiteral("TeammateIdle"));
    hooks[QStringLiteral("TaskCompleted")] = makeHookEntry(QStringLiteral("TaskCompleted"));

    settings[QStringLiteral("hooks")] = hooks;

    // Ensure .claude directory exists
    QDir().mkpath(workDir + QStringLiteral("/.claude"));

    // Write settings
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "SessionManagerPanel: Repaired hooks for session" << sessionId;
    }
}

void SessionManagerPanel::registerSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString sessionId = session->sessionId();
    m_activeSessions[sessionId] = session;

    // Ensure hooks are configured for this session's project
    ensureHooksConfigured(session);

    // Update or create metadata
    if (!m_metadata.contains(sessionId)) {
        SessionMetadata meta;
        meta.sessionId = sessionId;
        meta.sessionName = session->sessionName();
        meta.profileName = session->profileName();
        meta.workingDirectory = session->workingDirectory();
        meta.createdAt = QDateTime::currentDateTime();
        meta.lastAccessed = meta.createdAt;
        meta.isArchived = false;
        // New session - capture initial yolo mode from session (which comes from global settings)
        meta.yoloMode = session->yoloMode();
        meta.doubleYoloMode = session->doubleYoloMode();
        meta.tripleYoloMode = session->tripleYoloMode();
        m_metadata[sessionId] = meta;
    } else {
        // Session was archived/expired, now unarchived - restore yolo mode from metadata
        m_metadata[sessionId].isArchived = false;
        m_metadata[sessionId].isExpired = false;
        m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();

        // Restore per-session yolo mode settings from saved metadata
        session->setYoloMode(m_metadata[sessionId].yoloMode);
        session->setDoubleYoloMode(m_metadata[sessionId].doubleYoloMode);
        session->setTripleYoloMode(m_metadata[sessionId].tripleYoloMode);

        // Restore approval counts and log from saved metadata
        const auto &meta = m_metadata[sessionId];
        if (meta.yoloApprovalCount > 0 || meta.doubleYoloApprovalCount > 0 || meta.tripleYoloApprovalCount > 0) {
            session->restoreApprovalState(meta.yoloApprovalCount, meta.doubleYoloApprovalCount, meta.tripleYoloApprovalCount, meta.approvalLog);
        }

        qDebug() << "SessionManagerPanel: Restored yolo mode for" << sessionId << "- yolo:" << meta.yoloMode << "double:" << meta.doubleYoloMode
                 << "triple:" << meta.tripleYoloMode << "approvals:" << (meta.yoloApprovalCount + meta.doubleYoloApprovalCount + meta.tripleYoloApprovalCount);
    }

    saveMetadata();
    updateTreeWidget();

    // Avoid duplicate signal connections if registerSession is called
    // multiple times for the same session (e.g. on every tab switch).
    disconnect(session, nullptr, this, nullptr);

    // Connect to session finished (PTY died, e.g. tab closed) — fires immediately
    connect(session, &Konsole::Session::finished, this, [this, sessionId]() {
        m_activeSessions.remove(sessionId);
        updateTreeWidget();
    });

    // Connect to session destruction (backup, fires later via deleteLater)
    connect(session, &QObject::destroyed, this, [this, sessionId]() {
        m_activeSessions.remove(sessionId);
        updateTreeWidget();
    });

    // Connect to working directory changes (after run() gets real path from tmux)
    connect(session, &ClaudeSession::workingDirectoryChanged, this, [this, sessionId](const QString &newPath) {
        if (m_metadata.contains(sessionId) && !newPath.isEmpty()) {
            m_metadata[sessionId].workingDirectory = newPath;
            saveMetadata();
            updateTreeWidget();
            qDebug() << "SessionManagerPanel: Updated working directory for" << sessionId << "to" << newPath;
        }
    });

    // Connect to approval count changes to update display (debounced)
    connect(session, &ClaudeSession::approvalCountChanged, this, [this]() {
        scheduleTreeUpdate();
    });

    // Persist approval state on each new approval (debounced to avoid excessive I/O)
    connect(session, &ClaudeSession::approvalLogged, this, [this, sessionId](const ApprovalLogEntry &entry) {
        Q_UNUSED(entry);
        if (m_metadata.contains(sessionId)) {
            if (auto *s = m_activeSessions.value(sessionId)) {
                m_metadata[sessionId].yoloApprovalCount = s->yoloApprovalCount();
                m_metadata[sessionId].doubleYoloApprovalCount = s->doubleYoloApprovalCount();
                m_metadata[sessionId].tripleYoloApprovalCount = s->tripleYoloApprovalCount();
                m_metadata[sessionId].approvalLog = s->approvalLog();
            }
        }
        scheduleMetadataSave();
    });

    // Connect to all yolo mode changes to update display and persist per-session settings
    connect(session, &ClaudeSession::yoloModeChanged, this, [this, sessionId](bool enabled) {
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].yoloMode = enabled;
            saveMetadata();
        }
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::doubleYoloModeChanged, this, [this, sessionId](bool enabled) {
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].doubleYoloMode = enabled;
            saveMetadata();
        }
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::tripleYoloModeChanged, this, [this, sessionId](bool enabled) {
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].tripleYoloMode = enabled;
            saveMetadata();
        }
        scheduleTreeUpdate();
    });

    // Connect to task description changes to update display
    connect(session, &ClaudeSession::taskDescriptionChanged, this, [this]() {
        scheduleTreeUpdate();
    });

    // Connect to subagent/team events to update tree with nested agents
    connect(session, &ClaudeSession::subagentStarted, this, [this]() {
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::subagentStopped, this, [this]() {
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::teamInfoChanged, this, [this]() {
        scheduleTreeUpdate();
    });
}

void SessionManagerPanel::unregisterSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString sessionId = session->sessionId();

    // Save yolo mode settings and approval state before removing session reference
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].yoloMode = session->yoloMode();
        m_metadata[sessionId].doubleYoloMode = session->doubleYoloMode();
        m_metadata[sessionId].tripleYoloMode = session->tripleYoloMode();
        m_metadata[sessionId].yoloApprovalCount = session->yoloApprovalCount();
        m_metadata[sessionId].doubleYoloApprovalCount = session->doubleYoloApprovalCount();
        m_metadata[sessionId].tripleYoloApprovalCount = session->tripleYoloApprovalCount();
        m_metadata[sessionId].approvalLog = session->approvalLog();
        m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
        saveMetadata();
    }

    m_activeSessions.remove(sessionId);

    updateTreeWidget();
}

QList<SessionMetadata> SessionManagerPanel::allSessions() const
{
    return m_metadata.values();
}

QList<SessionMetadata> SessionManagerPanel::pinnedSessions() const
{
    QList<SessionMetadata> result;
    for (const auto &meta : m_metadata) {
        if (meta.isPinned && !meta.isArchived) {
            result.append(meta);
        }
    }
    return result;
}

QList<SessionMetadata> SessionManagerPanel::archivedSessions() const
{
    QList<SessionMetadata> result;
    for (const auto &meta : m_metadata) {
        if (meta.isArchived) {
            result.append(meta);
        }
    }
    return result;
}

void SessionManagerPanel::cleanupStaleSockets()
{
    // Remove stale socket and yolo files from sessions that no longer have live tmux sessions.
    // Uses async tmux query to avoid blocking the GUI thread.
    QString sessionsDir = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions");
    QDir dir(sessionsDir);
    if (!dir.exists()) {
        return;
    }

    // Collect socket files before the async call (filesystem reads are fast)
    QStringList sockFiles = dir.entryList({QStringLiteral("*.sock")}, QDir::Files);
    if (sockFiles.isEmpty()) {
        return;
    }

    auto *tmux = new TmuxManager(nullptr);
    QPointer<SessionManagerPanel> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux, sessionsDir, sockFiles](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();

        if (!guard) {
            return;
        }

        QSet<QString> liveIds;
        for (const auto &info : liveSessions) {
            // Extract ID from session name: konsolai-{profile}-{id}
            QStringList parts = info.name.split(QLatin1Char('-'));
            if (parts.size() >= 3) {
                liveIds.insert(parts.last());
            }
        }

        // Clean up socket and yolo files for dead sessions
        QDir dir(sessionsDir);
        int cleaned = 0;
        for (const QString &sockFile : sockFiles) {
            QString id = sockFile.left(sockFile.length() - 5); // Remove .sock
            if (!liveIds.contains(id)) {
                QFile::remove(dir.filePath(sockFile));
                QString yoloFile = id + QStringLiteral(".yolo");
                if (dir.exists(yoloFile)) {
                    QFile::remove(dir.filePath(yoloFile));
                }
                QString teamYoloFile = id + QStringLiteral(".yolo-team");
                if (dir.exists(teamYoloFile)) {
                    QFile::remove(dir.filePath(teamYoloFile));
                }
                cleaned++;
            }
        }

        if (cleaned > 0) {
            qDebug() << "SessionManagerPanel: Cleaned up" << cleaned << "stale socket/yolo files";
        }
    });
}

void SessionManagerPanel::refresh()
{
    // Discover tmux sessions that aren't tracked, using async tmux query
    // to avoid blocking the GUI thread.
    if (!m_registry) {
        updateTreeWidget();
        return;
    }

    auto *tmux = new TmuxManager(nullptr);
    QPointer<SessionManagerPanel> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();

        if (!guard) {
            return;
        }

        // Feed pre-fetched tmux data to registry (non-blocking)
        if (guard->m_registry) {
            guard->m_registry->refreshOrphanedSessions(liveSessions);
            for (const auto &state : guard->m_registry->orphanedSessions()) {
                if (!guard->m_metadata.contains(state.sessionId)) {
                    SessionMetadata meta;
                    meta.sessionId = state.sessionId;
                    meta.sessionName = state.sessionName;
                    meta.profileName = state.profileName;
                    meta.workingDirectory = state.workingDirectory;
                    meta.lastAccessed = state.lastAccessed;
                    meta.createdAt = state.lastAccessed; // Approximate
                    meta.isArchived = false;
                    meta.isPinned = false;
                    guard->m_metadata[state.sessionId] = meta;
                }
            }
        }

        guard->updateTreeWidget();
    });
}

void SessionManagerPanel::pinSession(const QString &sessionId)
{
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].isPinned = true;
        saveMetadata();
        updateTreeWidget();
    }
}

void SessionManagerPanel::unpinSession(const QString &sessionId)
{
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].isPinned = false;
        saveMetadata();
        updateTreeWidget();
    }
}

void SessionManagerPanel::archiveSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    QString sessionName = m_metadata[sessionId].sessionName;

    // Disconnect any active session signals before removal
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            disconnect(session, nullptr, this, nullptr);
        }
    }

    // Remove from active sessions first
    m_activeSessions.remove(sessionId);

    // Mark as archived
    m_metadata[sessionId].isArchived = true;
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    saveMetadata();

    // Kill the tmux session asynchronously to avoid blocking the event loop
    auto *tmux = new TmuxManager(nullptr);
    tmux->sessionExistsAsync(sessionName, [tmux, sessionName](bool exists) {
        if (exists) {
            // Use sync kill since we're already in an async context
            tmux->killSession(sessionName);
        }
        tmux->deleteLater();
    });

    // Clean up stale socket, yolo, and yolo-team files
    QString socketPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
    if (QFile::exists(socketPath)) {
        QFile::remove(socketPath);
    }
    QString yoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo");
    if (QFile::exists(yoloPath)) {
        QFile::remove(yoloPath);
    }
    QString teamYoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo-team");
    if (QFile::exists(teamYoloPath)) {
        QFile::remove(teamYoloPath);
    }

    // Update the tree widget (async tmux query will run)
    updateTreeWidget();
}

void SessionManagerPanel::closeSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    QString sessionName = m_metadata[sessionId].sessionName;

    // Disconnect any active session signals before removal
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            disconnect(session, nullptr, this, nullptr);
        }
    }

    // Remove from active sessions
    m_activeSessions.remove(sessionId);

    // Update last accessed but do NOT mark as archived — it will appear in Closed
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    saveMetadata();

    // Kill the tmux session asynchronously
    auto *tmux = new TmuxManager(nullptr);
    tmux->sessionExistsAsync(sessionName, [tmux, sessionName](bool exists) {
        if (exists) {
            tmux->killSession(sessionName);
        }
        tmux->deleteLater();
    });

    // Clean up stale socket, yolo, and yolo-team files
    QString socketPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
    if (QFile::exists(socketPath)) {
        QFile::remove(socketPath);
    }
    QString yoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo");
    if (QFile::exists(yoloPath)) {
        QFile::remove(yoloPath);
    }
    QString teamYoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo-team");
    if (QFile::exists(teamYoloPath)) {
        QFile::remove(teamYoloPath);
    }

    // Update the tree widget (session will land in "Closed" since tmux is dead and isArchived is false)
    updateTreeWidget();
}

void SessionManagerPanel::unarchiveSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    const auto &meta = m_metadata[sessionId];

    // Emit signal to create new session with same ID
    Q_EMIT unarchiveRequested(sessionId, meta.workingDirectory);
}

void SessionManagerPanel::markExpired(const QString &sessionName)
{
    // Find metadata by session name
    for (auto it = m_metadata.begin(); it != m_metadata.end(); ++it) {
        if (it->sessionName == sessionName) {
            // Mark as expired but NOT archived - let it go to Closed category
            // User must explicitly archive if they don't want to see it
            it->isExpired = true;
            it->lastAccessed = QDateTime::currentDateTime();
            m_activeSessions.remove(it->sessionId);
            saveMetadata();
            updateTreeWidget();
            qDebug() << "SessionManagerPanel: Marked session as expired (tmux dead):" << sessionName;
            return;
        }
    }
    qDebug() << "SessionManagerPanel: Could not find session to mark expired:" << sessionName;
}

void SessionManagerPanel::dismissSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    m_metadata[sessionId].isDismissed = true;
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    saveMetadata();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Dismissed session:" << sessionId;
}

void SessionManagerPanel::restoreSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    m_metadata[sessionId].isDismissed = false;
    m_metadata[sessionId].isArchived = true; // Restore to Archived state
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    saveMetadata();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Restored dismissed session:" << sessionId;
}

void SessionManagerPanel::purgeSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    // Also remove from the registry
    QString sessionName = m_metadata[sessionId].sessionName;
    if (m_registry && !sessionName.isEmpty()) {
        m_registry->removeSessionState(sessionName);
    }

    m_metadata.remove(sessionId);
    saveMetadata();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Purged session metadata:" << sessionId;
}

void SessionManagerPanel::purgeDismissed()
{
    QStringList toRemove;
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        if (it->isDismissed) {
            toRemove.append(it.key());
        }
    }

    for (const QString &sessionId : toRemove) {
        QString sessionName = m_metadata[sessionId].sessionName;
        if (m_registry && !sessionName.isEmpty()) {
            m_registry->removeSessionState(sessionName);
        }
        m_metadata.remove(sessionId);
    }

    if (!toRemove.isEmpty()) {
        saveMetadata();
        updateTreeWidget();
        qDebug() << "SessionManagerPanel: Purged" << toRemove.size() << "dismissed sessions";
    }
}

void SessionManagerPanel::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)

    if (!item || item == m_pinnedCategory || item == m_activeCategory || item == m_archivedCategory || item == m_closedCategory || item == m_dismissedCategory
        || item == m_discoveredCategory) {
        return;
    }

    // Check if this is a subagent child item (parent is a session item, not a category)
    QTreeWidgetItem *parentItem = item->parent();
    if (parentItem && parentItem->parent() != nullptr) {
        // This is a grandchild of a category → subagent child item
        QString agentId = item->data(0, Qt::UserRole).toString();
        QString parentSessionId = item->data(0, Qt::UserRole + 1).toString();
        if (!agentId.isEmpty() && m_activeSessions.contains(parentSessionId)) {
            ClaudeSession *session = m_activeSessions[parentSessionId];
            if (session) {
                const auto &agents = session->subagents();
                if (agents.contains(agentId)) {
                    const auto &info = agents[agentId];
                    if (!info.transcriptPath.isEmpty() && QFile::exists(info.transcriptPath)) {
                        showSubagentTranscript(info);
                    } else {
                        showSubagentDetails(info);
                    }
                }
            }
        }
        return;
    }

    QString sessionId = item->data(0, Qt::UserRole).toString();
    if (sessionId.isEmpty()) {
        return;
    }

    // Check if this is a discovered session (parent is m_discoveredCategory)
    if (item->parent() == m_discoveredCategory) {
        QString workDir = item->data(0, Qt::UserRole + 1).toString();
        if (!workDir.isEmpty()) {
            Q_EMIT unarchiveRequested(sessionId, workDir);
        }
        return;
    }

    if (!m_metadata.contains(sessionId)) {
        return;
    }

    const auto &meta = m_metadata[sessionId];

    if (meta.isArchived) {
        // Unarchive and attach
        unarchiveSession(sessionId);
    } else if (m_activeSessions.contains(sessionId)) {
        // Active session — focus its tab
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            Q_EMIT focusSessionRequested(session);
        }
    } else {
        // Detached session — reattach
        Q_EMIT attachRequested(meta.sessionName);
    }
}

void SessionManagerPanel::onContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    if (!item || item == m_pinnedCategory || item == m_activeCategory || item == m_archivedCategory || item == m_discoveredCategory) {
        return;
    }

    // Handle subagent child items (grandchild of a category)
    QTreeWidgetItem *parentItem = item->parent();
    if (parentItem && parentItem->parent() != nullptr) {
        QString agentId = item->data(0, Qt::UserRole).toString();
        QString parentSessionId = item->data(0, Qt::UserRole + 1).toString();
        if (agentId.isEmpty() || !m_activeSessions.contains(parentSessionId)) {
            return;
        }
        ClaudeSession *session = m_activeSessions[parentSessionId];
        if (!session) {
            return;
        }
        const auto &agents = session->subagents();
        if (!agents.contains(agentId)) {
            return;
        }
        const auto &info = agents[agentId];

        QMenu menu(this);

        bool hasTranscript = !info.transcriptPath.isEmpty() && QFile::exists(info.transcriptPath);

        QAction *viewTranscript = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("View Transcript"));
        viewTranscript->setEnabled(hasTranscript);
        connect(viewTranscript, &QAction::triggered, this, [this, info]() {
            showSubagentTranscript(info);
        });

        QAction *openExternal = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open Transcript in Editor"));
        openExternal->setEnabled(hasTranscript);
        connect(openExternal, &QAction::triggered, this, [info]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.transcriptPath));
        });

        menu.addSeparator();

        QAction *copyId = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy Agent ID"));
        connect(copyId, &QAction::triggered, this, [info]() {
            QApplication::clipboard()->setText(info.agentId);
        });

        QAction *details = menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), i18n("Show Details"));
        connect(details, &QAction::triggered, this, [this, info]() {
            showSubagentDetails(info);
        });

        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    // Handle right-click on the Closed category header
    if (item == m_closedCategory) {
        int closedCount = m_closedCategory->childCount();
        if (closedCount > 0) {
            QMenu menu(this);
            QAction *archiveAllAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-insert")), i18n("Archive All Closed (%1)", closedCount));
            connect(archiveAllAction, &QAction::triggered, this, [this, closedCount]() {
                auto answer = QMessageBox::question(this,
                                                    i18n("Archive Closed Sessions"),
                                                    i18n("Archive %1 closed session(s)?\n\n"
                                                         "They will be moved to the Archived category.",
                                                         closedCount),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                if (answer == QMessageBox::Yes) {
                    // Collect session IDs first (archiveSession modifies the tree)
                    QStringList toArchive;
                    for (int i = 0; i < m_closedCategory->childCount(); ++i) {
                        QString sid = m_closedCategory->child(i)->data(0, Qt::UserRole).toString();
                        if (!sid.isEmpty()) {
                            toArchive.append(sid);
                        }
                    }
                    for (const auto &sid : toArchive) {
                        archiveSession(sid);
                    }
                }
            });
            menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        }
        return;
    }

    // Handle right-click on the Dismissed category header
    if (item == m_dismissedCategory) {
        // Count dismissed sessions
        int dismissedCount = 0;
        for (const auto &meta : m_metadata) {
            if (meta.isDismissed) {
                dismissedCount++;
            }
        }
        if (dismissedCount > 0) {
            QMenu menu(this);
            QAction *purgeAllAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Purge All Dismissed (%1)", dismissedCount));
            connect(purgeAllAction, &QAction::triggered, this, [this, dismissedCount]() {
                auto answer = QMessageBox::question(this,
                                                    i18n("Purge Dismissed Sessions"),
                                                    i18n("Permanently remove metadata for %1 dismissed session(s)?\n\n"
                                                         "Project folders will NOT be affected.",
                                                         dismissedCount),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                if (answer == QMessageBox::Yes) {
                    purgeDismissed();
                }
            });
            menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        }
        return;
    }

    QString sessionId = item->data(0, Qt::UserRole).toString();
    if (sessionId.isEmpty()) {
        return;
    }

    // Handle discovered sessions
    if (item->parent() == m_discoveredCategory) {
        QString workDir = item->data(0, Qt::UserRole + 1).toString();
        if (workDir.isEmpty()) {
            return;
        }

        QMenu menu(this);
        QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Open Session Here"));
        connect(openAction, &QAction::triggered, this, [this, sessionId, workDir]() {
            Q_EMIT unarchiveRequested(sessionId, workDir);
        });

        menu.addSeparator();

        QAction *trashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), i18n("Move to Trash..."));
        connect(trashAction, &QAction::triggered, this, [this, workDir]() {
            auto answer = QMessageBox::question(this,
                                                i18n("Move to Trash"),
                                                i18n("Move this project folder to the trash?\n\n%1", workDir),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                QFile dir(workDir);
                if (dir.moveToTrash()) {
                    updateTreeWidget();
                } else {
                    QMessageBox::warning(this, i18n("Trash Failed"), i18n("Could not move folder to trash:\n%1", workDir));
                }
            }
        });

        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    if (!m_metadata.contains(sessionId)) {
        return;
    }

    const auto &meta = m_metadata[sessionId];

    QMenu menu(this);

    if (meta.isDismissed) {
        // Dismissed session context menu
        QAction *restoreAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Restore"));
        connect(restoreAction, &QAction::triggered, this, [this, sessionId]() {
            restoreSession(sessionId);
        });

        menu.addSeparator();

        QAction *purgeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Purge"));
        connect(purgeAction, &QAction::triggered, this, [this, sessionId]() {
            auto answer = QMessageBox::question(this,
                                                i18n("Purge Session"),
                                                i18n("Permanently remove this session's metadata?\n\n"
                                                     "Project folder will NOT be affected."),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                purgeSession(sessionId);
            }
        });
    } else if (meta.isArchived) {
        QAction *unarchiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")), i18n("Unarchive && Start"));
        connect(unarchiveAction, &QAction::triggered, this, [this, sessionId]() {
            unarchiveSession(sessionId);
        });

        menu.addSeparator();

        QAction *dismissAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Dismiss"));
        connect(dismissAction, &QAction::triggered, this, [this, sessionId]() {
            dismissSession(sessionId);
        });

        if (!meta.workingDirectory.isEmpty() && QDir(meta.workingDirectory).exists()) {
            QAction *trashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), i18n("Move to Trash..."));
            connect(trashAction, &QAction::triggered, this, [this, sessionId, meta]() {
                auto answer = QMessageBox::question(this,
                                                    i18n("Move to Trash"),
                                                    i18n("Move this project folder to the trash?\n\n%1", meta.workingDirectory),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                if (answer == QMessageBox::Yes) {
                    QFile dir(meta.workingDirectory);
                    if (dir.moveToTrash()) {
                        m_metadata.remove(sessionId);
                        saveMetadata();
                        updateTreeWidget();
                    } else {
                        QMessageBox::warning(this, i18n("Trash Failed"), i18n("Could not move folder to trash:\n%1", meta.workingDirectory));
                    }
                }
            });
        }
    } else {
        // Active or detached session
        bool isActive = m_activeSessions.contains(sessionId);

        if (isActive) {
            ClaudeSession *activeSession = m_activeSessions[sessionId];
            if (activeSession) {
                QAction *focusAction = menu.addAction(QIcon::fromTheme(QStringLiteral("go-jump")), i18n("Focus Tab"));
                connect(focusAction, &QAction::triggered, this, [this, activeSession]() {
                    Q_EMIT focusSessionRequested(activeSession);
                });
            }
        } else {
            QAction *attachAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Attach"));
            connect(attachAction, &QAction::triggered, this, [this, meta]() {
                Q_EMIT attachRequested(meta.sessionName);
            });
        }

        menu.addSeparator();

        if (meta.isPinned) {
            QAction *unpinAction = menu.addAction(QIcon::fromTheme(QStringLiteral("window-unpin")), i18n("Unpin"));
            connect(unpinAction, &QAction::triggered, this, [this, sessionId]() {
                unpinSession(sessionId);
            });
        } else {
            QAction *pinAction = menu.addAction(QIcon::fromTheme(QStringLiteral("pin")), i18n("Pin to Top"));
            connect(pinAction, &QAction::triggered, this, [this, sessionId]() {
                pinSession(sessionId);
            });
        }

        // Set/edit task description
        QAction *descAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Set Description..."));
        connect(descAction, &QAction::triggered, this, [this, sessionId]() {
            editSessionDescription(sessionId);
        });

        // Show approval log for active sessions with approvals
        if (isActive && m_activeSessions.contains(sessionId)) {
            ClaudeSession *activeSession = m_activeSessions[sessionId];
            if (activeSession && activeSession->totalApprovalCount() > 0) {
                QAction *logAction =
                    menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-details")), i18n("View Approval Log (%1)", activeSession->totalApprovalCount()));
                connect(logAction, &QAction::triggered, this, [this, activeSession]() {
                    showApprovalLog(activeSession);
                });
            }
        }

        // Toggle completed agents visibility for sessions with subagents
        if (isActive && m_activeSessions.contains(sessionId)) {
            ClaudeSession *activeSession = m_activeSessions[sessionId];
            if (activeSession && !activeSession->subagents().isEmpty()) {
                bool hiding = m_hideCompletedAgents.contains(sessionId);
                QAction *toggleAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-visible")), i18n("Show Completed Agents"));
                toggleAction->setCheckable(true);
                toggleAction->setChecked(!hiding);
                connect(toggleAction, &QAction::triggered, this, [this, sessionId](bool checked) {
                    if (checked) {
                        m_hideCompletedAgents.remove(sessionId);
                    } else {
                        m_hideCompletedAgents.insert(sessionId);
                    }
                    scheduleTreeUpdate();
                });
            }
        }

        // Restart option for active sessions
        if (isActive && m_activeSessions.contains(sessionId)) {
            ClaudeSession *activeSession = m_activeSessions[sessionId];
            if (activeSession) {
                QAction *restartAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Restart Claude"));
                connect(restartAction, &QAction::triggered, this, [activeSession]() {
                    activeSession->restart();
                });
            }
        }

        menu.addSeparator();

        QAction *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("process-stop")), i18n("Close"));
        connect(closeAction, &QAction::triggered, this, [this, sessionId]() {
            closeSession(sessionId);
        });

        QAction *archiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive"));
        connect(archiveAction, &QAction::triggered, this, [this, sessionId]() {
            archiveSession(sessionId);
        });

        if (!isActive) {
            QAction *dismissAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Dismiss"));
            connect(dismissAction, &QAction::triggered, this, [this, sessionId]() {
                dismissSession(sessionId);
            });
        }
    }

    menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
}

void SessionManagerPanel::onNewSessionClicked()
{
    Q_EMIT newSessionRequested();
}

void SessionManagerPanel::scheduleTreeUpdate()
{
    // Debounce: coalesce rapid-fire signals (e.g. approvalCountChanged fires
    // many times per minute during yolo mode) into a single deferred update.
    if (!m_updateDebounce) {
        m_updateDebounce = new QTimer(this);
        m_updateDebounce->setSingleShot(true);
        connect(m_updateDebounce, &QTimer::timeout, this, &SessionManagerPanel::updateTreeWidget);
    }
    // Restart the 250ms window on each call — only the last one fires.
    m_updateDebounce->start(250);
}

void SessionManagerPanel::scheduleMetadataSave()
{
    if (!m_saveDebounce) {
        m_saveDebounce = new QTimer(this);
        m_saveDebounce->setSingleShot(true);
        connect(m_saveDebounce, &QTimer::timeout, this, &SessionManagerPanel::saveMetadata);
    }
    m_saveDebounce->start(1000);
}

void SessionManagerPanel::updateTreeWidget()
{
    // Guard against overlapping async tmux queries - if one is already running,
    // mark that we need another update after it finishes
    if (m_asyncQueryInFlight) {
        m_asyncQueryPending = true;
        return;
    }

    m_asyncQueryInFlight = true;
    m_asyncQueryPending = false;

    // Async pre-pass: query tmux for live sessions without blocking the GUI,
    // then call updateTreeWidgetWithLiveSessions() with the result.
    // TmuxManager is NOT parented to this, so it survives panel destruction.
    auto *tmux = new TmuxManager(nullptr);

    // Use QPointer to guard against the panel being destroyed before callback fires
    QPointer<SessionManagerPanel> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();

        // Check if the panel was destroyed while waiting for async result
        if (!guard) {
            qDebug() << "SessionManagerPanel: Panel destroyed during async tmux query, skipping update";
            return;
        }

        guard->m_asyncQueryInFlight = false;

        QSet<QString> liveNames;
        for (const auto &info : liveSessions) {
            liveNames.insert(info.name);
        }
        guard->m_cachedLiveNames = liveNames;
        guard->updateTreeWidgetWithLiveSessions(liveNames);

        // If another update was requested while we were querying, run it now
        if (guard->m_asyncQueryPending) {
            guard->updateTreeWidget();
        }
    });
}

void SessionManagerPanel::updateTreeWidgetWithLiveSessions(const QSet<QString> &liveNames)
{
    // Clear existing items (except categories)
    while (m_pinnedCategory->childCount() > 0) {
        delete m_pinnedCategory->takeChild(0);
    }
    while (m_activeCategory->childCount() > 0) {
        delete m_activeCategory->takeChild(0);
    }
    while (m_detachedCategory->childCount() > 0) {
        delete m_detachedCategory->takeChild(0);
    }
    while (m_closedCategory->childCount() > 0) {
        delete m_closedCategory->takeChild(0);
    }
    while (m_archivedCategory->childCount() > 0) {
        delete m_archivedCategory->takeChild(0);
    }
    while (m_dismissedCategory->childCount() > 0) {
        delete m_dismissedCategory->takeChild(0);
    }

    // Note: We no longer auto-archive dead tmux sessions.
    // Dead sessions go to "Closed", user-archived sessions go to "Archived".

    // Sort sessions by last accessed (most recent first)
    QList<SessionMetadata> sortedMeta = m_metadata.values();
    std::sort(sortedMeta.begin(), sortedMeta.end(), [](const SessionMetadata &a, const SessionMetadata &b) {
        return a.lastAccessed > b.lastAccessed;
    });

    // Add sessions to appropriate categories
    // Priority: Dismissed > Archived > Pinned > Active (has tab) > Detached (tmux alive) > Closed (tmux dead)
    for (const auto &meta : sortedMeta) {
        bool isActive = m_activeSessions.contains(meta.sessionId);
        bool tmuxAlive = liveNames.contains(meta.sessionName);

        if (meta.isDismissed) {
            addSessionToTree(meta, m_dismissedCategory);
        } else if (meta.isArchived) {
            addSessionToTree(meta, m_archivedCategory);
        } else if (meta.isPinned) {
            addSessionToTree(meta, m_pinnedCategory);
        } else if (isActive) {
            addSessionToTree(meta, m_activeCategory);
        } else if (tmuxAlive) {
            // Tab closed but tmux still running → Detached
            addSessionToTree(meta, m_detachedCategory);
        } else {
            // tmux session is dead → Closed
            addSessionToTree(meta, m_closedCategory);
        }
    }

    // Add discovered sessions (from project folder scanning)
    while (m_discoveredCategory->childCount() > 0) {
        delete m_discoveredCategory->takeChild(0);
    }
    if (m_registry) {
        // Scan workspace root for Claude footprints
        QString searchRoot;
        auto *settings = KonsolaiSettings::instance();
        if (settings) {
            searchRoot = settings->projectRoot();
        } else {
            searchRoot = QDir::homePath() + QStringLiteral("/projects");
        }
        const auto discovered = m_registry->discoverSessions(searchRoot);
        for (const auto &state : discovered) {
            auto *item = new QTreeWidgetItem(m_discoveredCategory);
            QString displayName = QDir(state.workingDirectory).dirName();
            item->setText(0, displayName);
            item->setData(0, Qt::UserRole, state.sessionId);
            item->setData(0, Qt::UserRole + 1, state.workingDirectory);
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-cloud")));
            item->setToolTip(0, QStringLiteral("%1\n%2\nLast modified: %3").arg(state.profileName, state.workingDirectory, state.lastAccessed.toString()));
        }
    }

    // Stop duration timer if no sessions have active teams
    if (m_durationTimer && m_durationTimer->isActive()) {
        bool anyActiveTeam = false;
        for (auto it = m_activeSessions.constBegin(); it != m_activeSessions.constEnd(); ++it) {
            if (it.value() && it.value()->hasActiveTeam()) {
                anyActiveTeam = true;
                break;
            }
        }
        if (!anyActiveTeam) {
            m_durationTimer->stop();
        }
    }

    // Update category visibility
    m_pinnedCategory->setHidden(m_pinnedCategory->childCount() == 0);
    m_detachedCategory->setHidden(m_detachedCategory->childCount() == 0);
    m_closedCategory->setHidden(m_closedCategory->childCount() == 0);
    m_archivedCategory->setHidden(m_archivedCategory->childCount() == 0);
    m_dismissedCategory->setHidden(m_dismissedCategory->childCount() == 0);
    m_discoveredCategory->setHidden(m_discoveredCategory->childCount() == 0);
}

void SessionManagerPanel::addSessionToTree(const SessionMetadata &meta, QTreeWidgetItem *parent)
{
    auto *item = new QTreeWidgetItem(parent);

    // Display name: project directory or session name
    QString displayName;
    if (!meta.workingDirectory.isEmpty() && meta.workingDirectory != QStringLiteral(".") && meta.workingDirectory != QDir::homePath()) {
        displayName = QDir(meta.workingDirectory).dirName();
    }
    // Fallback to session name if display name is empty or just "." or "build" (which is misleading)
    if (displayName.isEmpty() || displayName == QStringLiteral(".") || displayName == QStringLiteral("build")) {
        displayName = meta.sessionName;
    }

    // Add task description for disambiguation
    // Priority: user-set taskDescription > Claude CLI summary > session ID hash
    bool isActive = m_activeSessions.contains(meta.sessionId);
    QString description;

    // First try user-set task description (from active session)
    if (isActive) {
        ClaudeSession *session = m_activeSessions[meta.sessionId];
        if (session && !session->taskDescription().isEmpty()) {
            description = session->taskDescription();
        }
    }

    // Fallback: look up Claude CLI's auto-generated summary for this project
    if (description.isEmpty() && !meta.workingDirectory.isEmpty() && m_registry) {
        auto conversations = m_registry->readClaudeConversations(meta.workingDirectory);
        if (!conversations.isEmpty()) {
            // Use most recent conversation's summary
            description = conversations.first().summary;
        }
    }

    // Apply description or fall back to session ID
    if (!description.isEmpty()) {
        if (description.length() > 35) {
            description = description.left(32) + QStringLiteral("...");
        }
        displayName += QStringLiteral(" (%1)").arg(description);
    } else if (!meta.sessionId.isEmpty()) {
        displayName += QStringLiteral(" (%1)").arg(meta.sessionId.left(8));
    }

    // Add team badge when subagents exist (active or completed)
    if (isActive) {
        ClaudeSession *activeSession = m_activeSessions[meta.sessionId];
        if (activeSession && !activeSession->subagents().isEmpty()) {
            int activeCount = 0;
            int totalCount = activeSession->subagents().size();
            const auto &agents = activeSession->subagents();
            for (auto it = agents.constBegin(); it != agents.constEnd(); ++it) {
                if (it->state == ClaudeProcess::State::Working || it->state == ClaudeProcess::State::Idle) {
                    activeCount++;
                }
            }
            QString badgeLabel = activeSession->teamName().isEmpty() ? QStringLiteral("team") : activeSession->teamName();
            if (activeCount == 0) {
                displayName += QStringLiteral(" [%1: done]").arg(badgeLabel);
            } else if (activeCount < totalCount) {
                displayName += QStringLiteral(" [%1: %2/%3]").arg(badgeLabel).arg(activeCount).arg(totalCount);
            } else {
                displayName += QStringLiteral(" [%1: %2]").arg(badgeLabel).arg(activeCount);
            }
        }
    }

    // Add GSD badge when .planning/ or ROADMAP.md exists in working directory
    if (!meta.workingDirectory.isEmpty()) {
        QDir workDir(meta.workingDirectory);
        if (workDir.exists(QStringLiteral(".planning")) || workDir.exists(QStringLiteral("ROADMAP.md"))) {
            displayName += QStringLiteral(" [GSD]");
        }
    }

    // Add @host suffix for remote sessions
    if (meta.isRemote && !meta.sshHost.isEmpty()) {
        displayName += QStringLiteral(" @%1").arg(meta.sshHost);
    }

    item->setText(0, displayName);
    item->setData(0, Qt::UserRole, meta.sessionId);

    // Enhanced tooltip for remote sessions
    QString tooltip;
    if (meta.isRemote) {
        QString userHost = meta.sshUsername.isEmpty() ? meta.sshHost : QStringLiteral("%1@%2").arg(meta.sshUsername, meta.sshHost);
        tooltip =
            QStringLiteral("%1\nRemote: %2\nPath: %3\nLast accessed: %4").arg(meta.sessionName, userHost, meta.workingDirectory, meta.lastAccessed.toString());
    } else {
        tooltip = QStringLiteral("%1\n%2\nLast accessed: %3").arg(meta.sessionName, meta.workingDirectory, meta.lastAccessed.toString());
    }
    item->setToolTip(0, tooltip);

    // Add yolo mode and approval count indicators in column 1 (always visible)
    if (isActive) {
        ClaudeSession *session = m_activeSessions[meta.sessionId];
        if (session) {
            // Build rich text with per-bolt colors
            QString boltsHtml;
            if (session->yoloMode()) {
                boltsHtml += QStringLiteral("<span style='color:#FFB300'>ϟ</span>"); // Gold
            }
            if (session->doubleYoloMode()) {
                boltsHtml += QStringLiteral("<span style='color:#42A5F5'>ϟ</span>"); // Light blue
            }
            if (session->tripleYoloMode()) {
                boltsHtml += QStringLiteral("<span style='color:#AB47BC'>ϟ</span>"); // Purple
            }

            // Add approval count
            int count = session->totalApprovalCount();
            if (count > 0 && !boltsHtml.isEmpty()) {
                boltsHtml += QStringLiteral(" [%1]").arg(count);
            } else if (count > 0) {
                boltsHtml = QStringLiteral("[%1]").arg(count);
            }

            // Add velocity + budget ETA when budget controller is active
            if (auto *bc = session->budgetController()) {
                if (bc->budget().hasAnyLimit()) {
                    // Show budget progress: elapsed/limit time | $current/$ceiling
                    QString budgetInfo;
                    if (bc->budget().timeLimitMinutes > 0) {
                        int elapsed = bc->budget().elapsedMinutes();
                        budgetInfo += QStringLiteral(" %1/%2m").arg(elapsed).arg(bc->budget().timeLimitMinutes);
                    }
                    if (bc->budget().costCeilingUSD > 0.0) {
                        double cost = session->tokenUsage().estimatedCostUSD();
                        if (!budgetInfo.isEmpty()) {
                            budgetInfo += QStringLiteral(" |");
                        }
                        budgetInfo += QStringLiteral(" $%1/$%2").arg(cost, 0, 'f', 2).arg(bc->budget().costCeilingUSD, 0, 'f', 2);
                    }
                    if (!budgetInfo.isEmpty()) {
                        boltsHtml += QStringLiteral("<span style='color:gray; font-size:10px'>%1</span>").arg(budgetInfo);
                    }
                }
                // Show velocity if available
                const auto &vel = bc->velocity();
                if (vel.tokensPerMinute() > 0) {
                    boltsHtml += QStringLiteral("<br><span style='color:gray; font-size:9px'>%1</span>").arg(vel.formatVelocity());
                }
            }

            // Add observer warning badge
            if (auto *obs = session->sessionObserver()) {
                int severity = obs->composedSeverity();
                if (severity >= 5) {
                    boltsHtml += QStringLiteral(" <span style='color:#F44336'>⚠ CRITICAL</span>");
                } else if (severity >= 3) {
                    boltsHtml += QStringLiteral(" <span style='color:#FF9800'>⚠</span>");
                } else if (severity > 0) {
                    boltsHtml += QStringLiteral(" <span style='color:#FFC107'>⚠</span>");
                }
            }

            if (!boltsHtml.isEmpty()) {
                auto *label = new QLabel(boltsHtml);
                label->setTextFormat(Qt::RichText);
                m_treeWidget->setItemWidget(item, 1, label);
            }
        }
    }

    // Set icon based on state (remote sessions use network icons)
    if (meta.isArchived && meta.isExpired) {
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
    } else if (meta.isArchived) {
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-grey")));
    } else if (meta.isRemote) {
        // Remote sessions use network icons
        if (isActive) {
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("network-server"), QIcon::fromTheme(QStringLiteral("folder-remote"))));
        } else {
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("network-server"), QIcon::fromTheme(QStringLiteral("folder-remote"))));
        }
    } else if (isActive) {
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-open")));
    } else {
        // Detached but not archived
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
    }

    // Add status indicator (green for local, blue for remote)
    if (isActive) {
        if (meta.isRemote) {
            item->setForeground(0, QBrush(Qt::darkBlue));
        } else {
            item->setForeground(0, QBrush(Qt::darkGreen));
        }
    }

    // Add nested subagent children for active sessions with subagents (active or completed)
    if (isActive) {
        ClaudeSession *session = m_activeSessions[meta.sessionId];
        if (session && !session->subagents().isEmpty()) {
            const auto &subagents = session->subagents();
            bool hideCompleted = m_hideCompletedAgents.contains(meta.sessionId);

            // Sort agents: Working first, Idle second, NotRunning last
            QList<const SubagentInfo *> sorted;
            for (auto it = subagents.constBegin(); it != subagents.constEnd(); ++it) {
                sorted.append(&it.value());
            }
            std::sort(sorted.begin(), sorted.end(), [](const SubagentInfo *a, const SubagentInfo *b) {
                auto rank = [](ClaudeProcess::State s) -> int {
                    if (s == ClaudeProcess::State::Working)
                        return 0;
                    if (s == ClaudeProcess::State::Idle)
                        return 1;
                    return 2; // NotRunning
                };
                return rank(a->state) < rank(b->state);
            });

            bool hasActiveAgents = false;
            for (const auto *infoPtr : sorted) {
                const auto &info = *infoPtr;
                bool isCompleted = (info.state != ClaudeProcess::State::Working && info.state != ClaudeProcess::State::Idle);

                // Skip completed agents if per-session toggle says hide them
                if (isCompleted && hideCompleted) {
                    continue;
                }

                if (!isCompleted) {
                    hasActiveAgents = true;
                }

                auto *childItem = new QTreeWidgetItem(item);

                // Display: agentType (teammateName) — taskSubject (truncated)
                QString childName = info.agentType;
                if (!info.teammateName.isEmpty()) {
                    childName = QStringLiteral("%1 (%2)").arg(info.agentType, info.teammateName);
                }
                if (!info.currentTaskSubject.isEmpty()) {
                    QString truncTask = info.currentTaskSubject;
                    if (truncTask.length() > 30) {
                        truncTask = truncTask.left(27) + QStringLiteral("...");
                    }
                    childName += QStringLiteral(" \u2014 %1").arg(truncTask);
                }
                childItem->setText(0, childName);

                // Store agentId and parent sessionId for context menu / double-click
                childItem->setData(0, Qt::UserRole, info.agentId);
                childItem->setData(0, Qt::UserRole + 1, meta.sessionId);

                // Elapsed duration label in column 1
                QString elapsed = formatElapsed(info.startedAt);
                if (!elapsed.isEmpty()) {
                    auto *durationLabel = new QLabel(elapsed);
                    durationLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
                    m_treeWidget->setItemWidget(childItem, 1, durationLabel);
                }

                // Icon and color by state
                if (info.state == ClaudeProcess::State::Working) {
                    childItem->setIcon(0, QIcon::fromTheme(QStringLiteral("media-playback-start")));
                    childItem->setForeground(0, QBrush(Qt::darkGreen));
                } else if (info.state == ClaudeProcess::State::Idle) {
                    childItem->setIcon(0, QIcon::fromTheme(QStringLiteral("media-playback-pause")));
                    childItem->setForeground(0, QBrush(Qt::gray));
                } else {
                    // Completed (NotRunning) — checkmark icon and dimmed text
                    childItem->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-ok"), QIcon::fromTheme(QStringLiteral("task-complete"))));
                    childItem->setForeground(0, QBrush(QColor(140, 140, 140)));
                }

                // Enhanced tooltip
                QString childTooltip = QStringLiteral("Agent: %1\nID: %2").arg(info.agentType, info.agentId);
                if (!info.teammateName.isEmpty()) {
                    childTooltip += QStringLiteral("\nName: %1").arg(info.teammateName);
                }
                if (!info.currentTaskSubject.isEmpty()) {
                    childTooltip += QStringLiteral("\nTask: %1").arg(info.currentTaskSubject);
                }
                if (info.startedAt.isValid()) {
                    childTooltip += QStringLiteral("\nElapsed: %1").arg(formatElapsed(info.startedAt));
                }
                if (!info.transcriptPath.isEmpty()) {
                    childTooltip += QStringLiteral("\nTranscript: %1").arg(info.transcriptPath);
                }
                if (isCompleted) {
                    childTooltip += QStringLiteral("\nStatus: Completed");
                }
                childItem->setToolTip(0, childTooltip);

                childItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }

            if (hasActiveAgents) {
                item->setExpanded(true);

                // Start duration timer if not already running
                if (!m_durationTimer) {
                    m_durationTimer = new QTimer(this);
                    m_durationTimer->setInterval(10000); // 10 seconds
                    connect(m_durationTimer, &QTimer::timeout, this, &SessionManagerPanel::scheduleTreeUpdate);
                }
                if (!m_durationTimer->isActive()) {
                    m_durationTimer->start();
                }
            }
        }
    }
}

void SessionManagerPanel::loadMetadata()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    QString filePath = dataPath + QStringLiteral("/sessions.json");

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return;
    }

    QJsonArray array = doc.array();
    for (const auto &value : array) {
        QJsonObject obj = value.toObject();
        SessionMetadata meta;
        meta.sessionId = obj[QStringLiteral("sessionId")].toString();
        meta.sessionName = obj[QStringLiteral("sessionName")].toString();
        meta.profileName = obj[QStringLiteral("profileName")].toString();
        meta.workingDirectory = obj[QStringLiteral("workingDirectory")].toString();
        meta.isPinned = obj[QStringLiteral("isPinned")].toBool();
        meta.isArchived = obj[QStringLiteral("isArchived")].toBool();
        meta.isExpired = obj[QStringLiteral("isExpired")].toBool();
        meta.isDismissed = obj[QStringLiteral("isDismissed")].toBool();
        meta.lastAccessed = QDateTime::fromString(obj[QStringLiteral("lastAccessed")].toString(), Qt::ISODate);
        meta.createdAt = QDateTime::fromString(obj[QStringLiteral("createdAt")].toString(), Qt::ISODate);

        // SSH remote session fields
        meta.isRemote = obj[QStringLiteral("isRemote")].toBool();
        meta.sshHost = obj[QStringLiteral("sshHost")].toString();
        meta.sshUsername = obj[QStringLiteral("sshUsername")].toString();
        meta.sshPort = obj[QStringLiteral("sshPort")].toInt(22);

        // Per-session yolo mode settings
        meta.yoloMode = obj[QStringLiteral("yoloMode")].toBool();
        meta.doubleYoloMode = obj[QStringLiteral("doubleYoloMode")].toBool();
        meta.tripleYoloMode = obj[QStringLiteral("tripleYoloMode")].toBool();

        // Approval counts
        meta.yoloApprovalCount = obj[QStringLiteral("yoloApprovalCount")].toInt();
        meta.doubleYoloApprovalCount = obj[QStringLiteral("doubleYoloApprovalCount")].toInt();
        meta.tripleYoloApprovalCount = obj[QStringLiteral("tripleYoloApprovalCount")].toInt();

        // Approval log
        const QJsonArray logArray = obj[QStringLiteral("approvalLog")].toArray();
        for (const auto &logVal : logArray) {
            QJsonObject logObj = logVal.toObject();
            ApprovalLogEntry entry;
            entry.timestamp = QDateTime::fromString(logObj[QStringLiteral("time")].toString(), Qt::ISODate);
            entry.toolName = logObj[QStringLiteral("tool")].toString();
            entry.action = logObj[QStringLiteral("action")].toString();
            entry.yoloLevel = logObj[QStringLiteral("level")].toInt();
            entry.totalTokens = static_cast<quint64>(logObj[QStringLiteral("tokens")].toDouble());
            entry.estimatedCostUSD = logObj[QStringLiteral("cost")].toDouble();
            meta.approvalLog.append(entry);
        }

        // Budget settings
        meta.budgetTimeLimitMinutes = obj[QStringLiteral("budgetTimeLimitMinutes")].toInt();
        meta.budgetCostCeilingUSD = obj[QStringLiteral("budgetCostCeilingUSD")].toDouble();
        meta.budgetTokenCeiling = static_cast<quint64>(obj[QStringLiteral("budgetTokenCeiling")].toDouble());

        if (!meta.sessionId.isEmpty()) {
            m_metadata[meta.sessionId] = meta;
        }
    }
}

void SessionManagerPanel::saveMetadata()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    QString filePath = dataPath + QStringLiteral("/sessions.json");

    QJsonArray array;
    for (const auto &meta : m_metadata) {
        QJsonObject obj;
        obj[QStringLiteral("sessionId")] = meta.sessionId;
        obj[QStringLiteral("sessionName")] = meta.sessionName;
        obj[QStringLiteral("profileName")] = meta.profileName;
        obj[QStringLiteral("workingDirectory")] = meta.workingDirectory;
        obj[QStringLiteral("isPinned")] = meta.isPinned;
        obj[QStringLiteral("isArchived")] = meta.isArchived;
        obj[QStringLiteral("isExpired")] = meta.isExpired;
        obj[QStringLiteral("lastAccessed")] = meta.lastAccessed.toString(Qt::ISODate);
        obj[QStringLiteral("createdAt")] = meta.createdAt.toString(Qt::ISODate);

        // SSH remote session fields
        if (meta.isRemote) {
            obj[QStringLiteral("isRemote")] = true;
            obj[QStringLiteral("sshHost")] = meta.sshHost;
            obj[QStringLiteral("sshUsername")] = meta.sshUsername;
            obj[QStringLiteral("sshPort")] = meta.sshPort;
        }

        // Per-session yolo mode settings (only save if enabled to keep JSON clean)
        if (meta.yoloMode) {
            obj[QStringLiteral("yoloMode")] = true;
        }
        if (meta.doubleYoloMode) {
            obj[QStringLiteral("doubleYoloMode")] = true;
        }
        if (meta.tripleYoloMode) {
            obj[QStringLiteral("tripleYoloMode")] = true;
        }
        if (meta.isDismissed) {
            obj[QStringLiteral("isDismissed")] = true;
        }

        // Approval counts (only save if non-zero)
        int totalApprovals = meta.yoloApprovalCount + meta.doubleYoloApprovalCount + meta.tripleYoloApprovalCount;
        if (totalApprovals > 0) {
            obj[QStringLiteral("yoloApprovalCount")] = meta.yoloApprovalCount;
            obj[QStringLiteral("doubleYoloApprovalCount")] = meta.doubleYoloApprovalCount;
            obj[QStringLiteral("tripleYoloApprovalCount")] = meta.tripleYoloApprovalCount;

            // Approval log (cap at 500 most recent entries to keep JSON manageable)
            if (!meta.approvalLog.isEmpty()) {
                QJsonArray logArray;
                int startIdx = qMax(0, meta.approvalLog.size() - 500);
                for (int i = startIdx; i < meta.approvalLog.size(); ++i) {
                    const auto &entry = meta.approvalLog[i];
                    QJsonObject logObj;
                    logObj[QStringLiteral("time")] = entry.timestamp.toString(Qt::ISODate);
                    logObj[QStringLiteral("tool")] = entry.toolName;
                    logObj[QStringLiteral("action")] = entry.action;
                    logObj[QStringLiteral("level")] = entry.yoloLevel;
                    if (entry.totalTokens > 0) {
                        logObj[QStringLiteral("tokens")] = static_cast<double>(entry.totalTokens);
                        logObj[QStringLiteral("cost")] = entry.estimatedCostUSD;
                    }
                    logArray.append(logObj);
                }
                obj[QStringLiteral("approvalLog")] = logArray;
            }
        }

        // Budget settings (only save if any limit is set)
        if (meta.budgetTimeLimitMinutes > 0) {
            obj[QStringLiteral("budgetTimeLimitMinutes")] = meta.budgetTimeLimitMinutes;
        }
        if (meta.budgetCostCeilingUSD > 0.0) {
            obj[QStringLiteral("budgetCostCeilingUSD")] = meta.budgetCostCeilingUSD;
        }
        if (meta.budgetTokenCeiling > 0) {
            obj[QStringLiteral("budgetTokenCeiling")] = static_cast<double>(meta.budgetTokenCeiling);
        }

        array.append(obj);
    }

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson());
    }
}

SessionMetadata *SessionManagerPanel::findMetadata(const QString &sessionId)
{
    if (m_metadata.contains(sessionId)) {
        return &m_metadata[sessionId];
    }
    return nullptr;
}

QTreeWidgetItem *SessionManagerPanel::findTreeItem(const QString &sessionId)
{
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *category = m_treeWidget->topLevelItem(i);
        for (int j = 0; j < category->childCount(); ++j) {
            QTreeWidgetItem *item = category->child(j);
            if (item->data(0, Qt::UserRole).toString() == sessionId) {
                return item;
            }
        }
    }
    return nullptr;
}

void SessionManagerPanel::showApprovalLog(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    const auto &log = session->approvalLog();

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Approval Log - %1", QDir(session->workingDirectory()).dirName()));
    auto *layout = new QVBoxLayout(&dialog);

    auto *summary = new QLabel(i18n("Total auto-approvals: %1 (Yolo: %2, Double: %3, Triple: %4)",
                                    session->totalApprovalCount(),
                                    session->yoloApprovalCount(),
                                    session->doubleYoloApprovalCount(),
                                    session->tripleYoloApprovalCount()),
                               &dialog);
    layout->addWidget(summary);

    auto *tree = new QTreeWidget(&dialog);
    tree->setHeaderLabels({i18n("Time"), i18n("Tool"), i18n("Action"), i18n("Level"), i18n("Tokens"), i18n("Cost")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);

    // Show most recent first
    for (int i = log.size() - 1; i >= 0; --i) {
        const auto &entry = log[i];
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, entry.timestamp.toString(QStringLiteral("hh:mm:ss")));
        item->setText(1, entry.toolName);
        item->setText(2, entry.action);
        QString levelStr;
        if (entry.yoloLevel == 1) {
            levelStr = QStringLiteral("Yolo");
        } else if (entry.yoloLevel == 2) {
            levelStr = QStringLiteral("Double");
        } else if (entry.yoloLevel == 3) {
            levelStr = QStringLiteral("Triple");
        }
        item->setText(3, levelStr);
        if (entry.totalTokens > 0) {
            // Format tokens compactly: "12.3K", "1.2M"
            double t = static_cast<double>(entry.totalTokens);
            QString tokenStr = t >= 1000000.0 ? QStringLiteral("%1M").arg(t / 1000000.0, 0, 'f', 1)
                : t >= 1000.0                 ? QStringLiteral("%1K").arg(t / 1000.0, 0, 'f', 1)
                                              : QString::number(entry.totalTokens);
            item->setText(4, tokenStr);
            item->setText(5, QStringLiteral("$%1").arg(entry.estimatedCostUSD, 0, 'f', 4));
            item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
            item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(tree);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(700, 400);
    dialog.exec();
}

void SessionManagerPanel::showSubagentTranscript(const SubagentInfo &info)
{
    if (info.transcriptPath.isEmpty() || !QFile::exists(info.transcriptPath)) {
        QMessageBox::information(this, i18n("No Transcript"), i18n("Transcript file is not available yet.\nIt will be available after the agent completes."));
        return;
    }

    QFile file(info.transcriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n("Read Error"), i18n("Could not open transcript file:\n%1", info.transcriptPath));
        return;
    }

    // Parse JSONL and extract readable content
    QString readable;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonDocument lineDoc = QJsonDocument::fromJson(line.toUtf8());
        if (!lineDoc.isObject()) {
            continue;
        }
        QJsonObject obj = lineDoc.object();
        QString type = obj[QStringLiteral("type")].toString();

        if (type == QStringLiteral("assistant")) {
            // Extract assistant message content
            QJsonArray content = obj[QStringLiteral("message")].toObject()[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                    readable += QStringLiteral("[Assistant]\n%1\n\n").arg(b[QStringLiteral("text")].toString());
                } else if (b[QStringLiteral("type")].toString() == QStringLiteral("tool_use")) {
                    readable += QStringLiteral("[Tool: %1]\n").arg(b[QStringLiteral("name")].toString());
                    QString inputStr = QString::fromUtf8(QJsonDocument(b[QStringLiteral("input")].toObject()).toJson(QJsonDocument::Compact));
                    if (inputStr.length() > 200) {
                        inputStr = inputStr.left(197) + QStringLiteral("...");
                    }
                    readable += inputStr + QStringLiteral("\n\n");
                }
            }
        } else if (type == QStringLiteral("tool_result") || type == QStringLiteral("result")) {
            QJsonArray content = obj[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                    QString text = b[QStringLiteral("text")].toString();
                    if (text.length() > 500) {
                        text = text.left(497) + QStringLiteral("...");
                    }
                    readable += QStringLiteral("[Result]\n%1\n\n").arg(text);
                }
            }
        } else if (type == QStringLiteral("human") || type == QStringLiteral("user")) {
            QJsonArray content = obj[QStringLiteral("message")].toObject()[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                    readable += QStringLiteral("[User]\n%1\n\n").arg(b[QStringLiteral("text")].toString());
                }
            }
        }
    }
    file.close();

    if (readable.isEmpty()) {
        // Fallback: show raw JSONL
        QFile raw(info.transcriptPath);
        if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) {
            readable = QString::fromUtf8(raw.readAll());
            raw.close();
        }
    }

    // Build viewer dialog
    QString title = i18n("Transcript \u2014 %1", info.agentType);
    if (!info.teammateName.isEmpty()) {
        title = i18n("Transcript \u2014 %1 (%2)", info.agentType, info.teammateName);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);

    auto *toolbar = new QToolBar(&dialog);
    QAction *openExternalAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open in External Editor"));
    connect(openExternalAction, &QAction::triggered, &dialog, [info]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.transcriptPath));
    });
    layout->addWidget(toolbar);

    auto *textEdit = new QPlainTextEdit(&dialog);
    textEdit->setReadOnly(true);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setStyleHint(QFont::TypeWriter);
    textEdit->setFont(monoFont);
    textEdit->setPlainText(readable);
    layout->addWidget(textEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(700, 500);
    dialog.exec();
}

void SessionManagerPanel::showSubagentDetails(const SubagentInfo &info)
{
    QString title = i18n("Agent Details \u2014 %1", info.agentType);
    if (!info.teammateName.isEmpty()) {
        title = i18n("Agent Details \u2014 %1 (%2)", info.agentType, info.teammateName);
    }

    QString stateStr;
    switch (info.state) {
    case ClaudeProcess::State::Working:
        stateStr = i18n("Working");
        break;
    case ClaudeProcess::State::Idle:
        stateStr = i18n("Idle");
        break;
    case ClaudeProcess::State::NotRunning:
        stateStr = i18n("Not Running");
        break;
    default:
        stateStr = i18n("Unknown");
        break;
    }

    QString details;
    details += QStringLiteral("<b>Agent Type:</b> %1<br>").arg(info.agentType.toHtmlEscaped());
    details += QStringLiteral("<b>Agent ID:</b> %1<br>").arg(info.agentId.toHtmlEscaped());
    if (!info.teammateName.isEmpty()) {
        details += QStringLiteral("<b>Teammate Name:</b> %1<br>").arg(info.teammateName.toHtmlEscaped());
    }
    details += QStringLiteral("<b>State:</b> %1<br>").arg(stateStr);
    if (info.startedAt.isValid()) {
        details += QStringLiteral("<b>Started:</b> %1<br>").arg(info.startedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        details += QStringLiteral("<b>Elapsed:</b> %1<br>").arg(formatElapsed(info.startedAt));
    }
    if (!info.currentTaskSubject.isEmpty()) {
        details += QStringLiteral("<b>Task:</b> %1<br>").arg(info.currentTaskSubject.toHtmlEscaped());
    }
    if (!info.transcriptPath.isEmpty()) {
        details += QStringLiteral("<b>Transcript:</b> %1<br>").arg(info.transcriptPath.toHtmlEscaped());
    }

    QMessageBox::information(this, title, details);
}

void SessionManagerPanel::editSessionDescription(const QString &sessionId)
{
    // Get current description (from active session or empty)
    QString currentDesc;
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            currentDesc = session->taskDescription();
        }
    }

    // Show input dialog
    bool ok = false;
    QString newDesc =
        QInputDialog::getText(this, i18n("Set Session Description"), i18n("Description (shown in tabs and panel):"), QLineEdit::Normal, currentDesc, &ok);

    if (!ok) {
        return; // User cancelled
    }

    // Update active session if exists
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            session->setTaskDescription(newDesc);
        }
    }

    // Also update the registry directly for inactive sessions
    if (m_registry) {
        // Find the session state by sessionId and update it
        for (const auto &state : m_registry->allSessionStates()) {
            if (state.sessionId == sessionId) {
                // Re-register with updated description would work for active sessions
                // For inactive, we need to update the state directly
                // This is handled by setTaskDescription -> registerSession for active ones
                break;
            }
        }
    }

    // Refresh display
    scheduleTreeUpdate();
}

} // namespace Konsolai

#include "moc_SessionManagerPanel.cpp"
