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
#include <QColor>
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
#include <QSet>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace Konsolai
{

static const QString SETTINGS_GROUP = QStringLiteral("SessionManager");

SessionManagerPanel::SessionManagerPanel(QWidget *parent)
    : QWidget(parent)
    , m_registry(ClaudeSessionRegistry::instance())
{
    setupUi();
    loadMetadata();
    refresh();
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

void SessionManagerPanel::registerSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString sessionId = session->sessionId();
    m_activeSessions[sessionId] = session;

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
        m_metadata[sessionId] = meta;
    } else {
        // Session was archived/expired, now unarchived
        m_metadata[sessionId].isArchived = false;
        m_metadata[sessionId].isExpired = false;
        m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    }

    saveMetadata();
    updateTreeWidget();

    // Avoid duplicate signal connections if registerSession is called
    // multiple times for the same session (e.g. on every tab switch).
    disconnect(session, nullptr, this, nullptr);

    // Connect to session destruction
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

    // Connect to all yolo mode changes to update display (debounced)
    connect(session, &ClaudeSession::yoloModeChanged, this, [this](bool) {
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::doubleYoloModeChanged, this, [this](bool) {
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::tripleYoloModeChanged, this, [this](bool) {
        scheduleTreeUpdate();
    });

    // Connect to task description changes to update display
    connect(session, &ClaudeSession::taskDescriptionChanged, this, [this]() {
        scheduleTreeUpdate();
    });
}

void SessionManagerPanel::unregisterSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString sessionId = session->sessionId();
    m_activeSessions.remove(sessionId);

    // Update last accessed time
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
        saveMetadata();
    }

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

void SessionManagerPanel::refresh()
{
    // Discover tmux sessions that aren't tracked
    if (m_registry) {
        m_registry->refreshOrphanedSessions();
        for (const auto &state : m_registry->orphanedSessions()) {
            if (!m_metadata.contains(state.sessionId)) {
                SessionMetadata meta;
                meta.sessionId = state.sessionId;
                meta.sessionName = state.sessionName;
                meta.profileName = state.profileName;
                meta.workingDirectory = state.workingDirectory;
                meta.lastAccessed = state.lastAccessed;
                meta.createdAt = state.lastAccessed; // Approximate
                meta.isArchived = false;
                meta.isPinned = false;
                m_metadata[state.sessionId] = meta;
            }
        }
    }

    updateTreeWidget();
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

    // Kill the tmux session if it exists
    TmuxManager tmux;
    QString sessionName = m_metadata[sessionId].sessionName;
    if (tmux.sessionExists(sessionName)) {
        tmux.killSession(sessionName);
    }

    // Remove from active sessions
    m_activeSessions.remove(sessionId);

    // Mark as archived
    m_metadata[sessionId].isArchived = true;
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();

    saveMetadata();
    updateTreeWidget();

    // Refresh registry
    if (m_registry) {
        m_registry->refreshOrphanedSessions();
    }
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

void SessionManagerPanel::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)

    if (!item || item == m_pinnedCategory || item == m_activeCategory || item == m_archivedCategory || item == m_closedCategory
        || item == m_discoveredCategory) {
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
    } else {
        // Attach to existing session
        Q_EMIT attachRequested(meta.sessionName);
    }
}

void SessionManagerPanel::onContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    if (!item || item == m_pinnedCategory || item == m_activeCategory || item == m_archivedCategory || item == m_closedCategory
        || item == m_discoveredCategory) {
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

    if (meta.isArchived) {
        QAction *unarchiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")), i18n("Unarchive && Start"));
        connect(unarchiveAction, &QAction::triggered, this, [this, sessionId]() {
            unarchiveSession(sessionId);
        });

        if (!meta.workingDirectory.isEmpty() && QDir(meta.workingDirectory).exists()) {
            menu.addSeparator();

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

        if (!isActive) {
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

        menu.addSeparator();

        QAction *archiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive"));
        connect(archiveAction, &QAction::triggered, this, [this, sessionId]() {
            archiveSession(sessionId);
        });
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

void SessionManagerPanel::updateTreeWidget()
{
    // Async pre-pass: query tmux for live sessions without blocking the GUI,
    // then call updateTreeWidgetWithLiveSessions() with the result.
    // Use a heap-allocated TmuxManager so it outlives this stack frame.
    auto *tmux = new TmuxManager(this);
    tmux->listKonsolaiSessionsAsync([this, tmux](const QList<TmuxManager::SessionInfo> &liveSessions) {
        QSet<QString> liveNames;
        for (const auto &info : liveSessions) {
            liveNames.insert(info.name);
        }
        m_cachedLiveNames = liveNames;
        updateTreeWidgetWithLiveSessions(liveNames);
        tmux->deleteLater();
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

    // Note: We no longer auto-archive dead tmux sessions.
    // Dead sessions go to "Closed", user-archived sessions go to "Archived".

    // Sort sessions by last accessed (most recent first)
    QList<SessionMetadata> sortedMeta = m_metadata.values();
    std::sort(sortedMeta.begin(), sortedMeta.end(), [](const SessionMetadata &a, const SessionMetadata &b) {
        return a.lastAccessed > b.lastAccessed;
    });

    // Add sessions to appropriate categories
    // Priority: Archived > Pinned > Active (has tab) > Detached (tmux alive) > Closed (tmux dead)
    for (const auto &meta : sortedMeta) {
        bool isActive = m_activeSessions.contains(meta.sessionId);
        bool tmuxAlive = liveNames.contains(meta.sessionName);

        if (meta.isArchived) {
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

    // Update category visibility
    m_pinnedCategory->setHidden(m_pinnedCategory->childCount() == 0);
    m_detachedCategory->setHidden(m_detachedCategory->childCount() == 0);
    m_closedCategory->setHidden(m_closedCategory->childCount() == 0);
    m_archivedCategory->setHidden(m_archivedCategory->childCount() == 0);
    m_discoveredCategory->setHidden(m_discoveredCategory->childCount() == 0);
}

void SessionManagerPanel::addSessionToTree(const SessionMetadata &meta, QTreeWidgetItem *parent)
{
    auto *item = new QTreeWidgetItem(parent);

    // Display name: project directory or session name
    QString displayName;
    if (!meta.workingDirectory.isEmpty() && meta.workingDirectory != QStringLiteral(".")) {
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
            QString indicators;

            // Add yolo mode indicator
            if (session->tripleYoloMode()) {
                indicators = QStringLiteral("⚡⚡⚡");
            } else if (session->doubleYoloMode()) {
                indicators = QStringLiteral("⚡⚡");
            } else if (session->yoloMode()) {
                indicators = QStringLiteral("⚡");
            }

            // Add approval count
            int count = session->totalApprovalCount();
            if (count > 0) {
                if (!indicators.isEmpty()) {
                    indicators += QStringLiteral(" ");
                }
                indicators += QStringLiteral("[%1]").arg(count);
            }

            if (!indicators.isEmpty()) {
                item->setText(1, indicators);
                // Use a distinct color for the count (blue-ish)
                item->setForeground(1, QBrush(QColor(0x3d, 0x9d, 0xf3))); // Light blue
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
        meta.lastAccessed = QDateTime::fromString(obj[QStringLiteral("lastAccessed")].toString(), Qt::ISODate);
        meta.createdAt = QDateTime::fromString(obj[QStringLiteral("createdAt")].toString(), Qt::ISODate);

        // SSH remote session fields
        meta.isRemote = obj[QStringLiteral("isRemote")].toBool();
        meta.sshHost = obj[QStringLiteral("sshHost")].toString();
        meta.sshUsername = obj[QStringLiteral("sshUsername")].toString();
        meta.sshPort = obj[QStringLiteral("sshPort")].toInt(22);

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
    tree->setHeaderLabels({i18n("Time"), i18n("Tool"), i18n("Action"), i18n("Level")});
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
    }

    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(tree);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(550, 400);
    dialog.exec();
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
