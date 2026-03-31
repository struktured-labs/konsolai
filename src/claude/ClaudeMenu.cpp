/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeMenu.h"
#include "ClaudeSession.h"
#include "ClaudeSessionRegistry.h"
#include "ClaudeSessionState.h"
#include "KonsolaiSettings.h"
#include "NotificationManager.h"
#include "TmuxManager.h"

#include <KLocalizedString>
#include <KNotifyConfigWidget>
#include <QActionGroup>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>

namespace Konsolai
{

static QIcon coloredBoltIcon(const QColor &color, int count = 1, int size = 16)
{
    QPixmap pixmap(size * count, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font = painter.font();
    font.setPixelSize(size);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(color);
    QString text;
    for (int i = 0; i < count; ++i) {
        text += QStringLiteral("ϟ");
    }
    painter.drawText(QRect(0, 0, size * count, size), Qt::AlignCenter, text);
    return QIcon(pixmap);
}

ClaudeMenu::ClaudeMenu(QWidget *parent)
    : QMenu(i18n("&Claude"), parent)
    , m_registry(ClaudeSessionRegistry::instance())
{
    setObjectName(QStringLiteral("claudeMenu"));

    // Load persisted yolo mode settings before creating actions
    // so the checkboxes start in the correct state.
    auto *settings = KonsolaiSettings::instance();
    if (settings) {
        m_yoloMode = settings->yoloMode();
        m_doubleYoloMode = settings->doubleYoloMode();
    }

    createActions();
    createReattachMenu();
    createNotificationMenu();

    // Update menu before showing
    connect(this, &QMenu::aboutToShow, this, &ClaudeMenu::onAboutToShow);

    // Watch for orphaned session changes
    if (m_registry) {
        connect(m_registry, &ClaudeSessionRegistry::orphanedSessionsChanged,
                this, &ClaudeMenu::onOrphanedSessionsChanged);
    }

    updateActionStates();
}

ClaudeMenu::~ClaudeMenu() = default;

void ClaudeMenu::createActions()
{
    // Approve Permission (shortcut set via KActionCollection in MainWindow)
    m_approveAction = addAction(i18n("&Approve Permission"));
    m_approveAction->setObjectName(QStringLiteral("claudeApprove"));
    m_approveAction->setToolTip(i18n("Approve the pending permission request"));
    connect(m_approveAction, &QAction::triggered, this, &ClaudeMenu::onApprove);

    // Deny Permission
    m_denyAction = addAction(i18n("&Deny Permission"));
    m_denyAction->setObjectName(QStringLiteral("claudeDeny"));
    m_denyAction->setToolTip(i18n("Deny the pending permission request"));
    connect(m_denyAction, &QAction::triggered, this, &ClaudeMenu::onDeny);

    addSeparator();

    // Stop Claude
    m_stopAction = addAction(i18n("&Stop Claude"));
    m_stopAction->setObjectName(QStringLiteral("claudeStop"));
    m_stopAction->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_stopAction->setToolTip(i18n("Stop the current Claude operation"));
    connect(m_stopAction, &QAction::triggered, this, &ClaudeMenu::onStop);

    // Restart Claude
    m_restartAction = addAction(i18n("&Restart Claude"));
    m_restartAction->setObjectName(QStringLiteral("claudeRestart"));
    m_restartAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_restartAction->setToolTip(i18n("Restart the Claude session"));
    connect(m_restartAction, &QAction::triggered, this, &ClaudeMenu::onRestart);

    addSeparator();

    // Yolo Mode - auto-approve all permissions (gold)
    m_yoloModeAction = addAction(coloredBoltIcon(QColor(0xFF, 0xB3, 0x00), 1), i18n("&Yolo Mode (Auto-Approve)"));
    m_yoloModeAction->setObjectName(QStringLiteral("claudeYolo"));
    m_yoloModeAction->setCheckable(true);
    m_yoloModeAction->setChecked(m_yoloMode);
    m_yoloModeAction->setToolTip(i18n("Automatically approve all permission requests"));
    connect(m_yoloModeAction, &QAction::toggled, this, &ClaudeMenu::onYoloModeToggled);

    // Double Yolo Mode - auto-accept completions (light blue)
    m_doubleYoloModeAction = addAction(coloredBoltIcon(QColor(0x42, 0xA5, 0xF5), 1), i18n("Double &Yolo Mode (Auto-Complete)"));
    m_doubleYoloModeAction->setObjectName(QStringLiteral("claudeDoubleYolo"));
    m_doubleYoloModeAction->setCheckable(true);
    m_doubleYoloModeAction->setChecked(m_doubleYoloMode);
    m_doubleYoloModeAction->setToolTip(i18n("Automatically accept tab completions"));
    connect(m_doubleYoloModeAction, &QAction::toggled, this, &ClaudeMenu::onDoubleYoloModeToggled);

    addSeparator();

    // Detach Session
    m_detachAction = addAction(i18n("De&tach Session"));
    m_detachAction->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_detachAction->setToolTip(i18n("Detach from the Claude session (keeps running in background)"));
    connect(m_detachAction, &QAction::triggered, this, &ClaudeMenu::onDetach);

    // Kill Session
    m_killAction = addAction(i18n("&Kill Session"));
    m_killAction->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_killAction->setToolTip(i18n("Kill the Claude session completely"));
    connect(m_killAction, &QAction::triggered, this, &ClaudeMenu::onKill);

    addSeparator();

    // Archive All Sessions
    m_archiveAllAction = addAction(i18n("&Archive All Detached Sessions"));
    m_archiveAllAction->setIcon(QIcon::fromTheme(QStringLiteral("archive-remove")));
    m_archiveAllAction->setToolTip(i18n("Kill all detached tmux sessions"));
    connect(m_archiveAllAction, &QAction::triggered, this, &ClaudeMenu::onArchiveAll);

    addSeparator();

    // Clear All Stale Hooks
    m_clearStaleHooksAction = addAction(i18n("Clear All Stale &Hooks"));
    m_clearStaleHooksAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear-all")));
    m_clearStaleHooksAction->setToolTip(i18n("Remove orphaned hook entries from all known working directories"));
    connect(m_clearStaleHooksAction, &QAction::triggered, this, &ClaudeMenu::onClearStaleHooks);

    // Configure Hooks
    m_configureHooksAction = addAction(i18n("Configure &Hooks..."));
    m_configureHooksAction->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    m_configureHooksAction->setToolTip(i18n("Configure Claude hooks integration"));
    connect(m_configureHooksAction, &QAction::triggered, this, &ClaudeMenu::onConfigureHooks);
}

void ClaudeMenu::createReattachMenu()
{
    m_reattachMenu = new QMenu(i18n("&Reattach Session"), this);
    m_reattachMenu->setIcon(QIcon::fromTheme(QStringLiteral("document-open-recent")));
    m_reattachMenu->setToolTip(i18n("Reattach to a detached Claude session"));

    // Insert before "Configure Hooks"
    insertMenu(m_configureHooksAction, m_reattachMenu);

    updateReattachMenu();
}

void ClaudeMenu::setActiveSession(ClaudeSession *session)
{
    if (m_activeSession == session) {
        return;
    }

    // Disconnect old session
    if (m_activeSession) {
        disconnect(m_activeSession, nullptr, this, nullptr);
    }

    m_activeSession = session;

    // Connect new session signals
    if (m_activeSession) {
        connect(m_activeSession, &ClaudeSession::stateChanged,
                this, &ClaudeMenu::updateActionStates);
        connect(m_activeSession, &QObject::destroyed, this, [this]() {
            m_activeSession = nullptr;
            updateActionStates();
            syncYoloModesFromSession();
        });

        // Sync yolo modes with per-session settings.
        // Use blockSignals to prevent setChecked from firing toggled → onYoloModeToggled
        // → setYoloMode, which could write back to the wrong session during transitions.
        connect(m_activeSession, &ClaudeSession::yoloModeChanged, this, [this](bool enabled) {
            if (m_yoloModeAction) {
                m_yoloModeAction->blockSignals(true);
                m_yoloModeAction->setChecked(enabled);
                m_yoloModeAction->blockSignals(false);
            }
            m_yoloMode = enabled;
        });
        connect(m_activeSession, &ClaudeSession::doubleYoloModeChanged, this, [this](bool enabled) {
            if (m_doubleYoloModeAction) {
                m_doubleYoloModeAction->blockSignals(true);
                m_doubleYoloModeAction->setChecked(enabled);
                m_doubleYoloModeAction->blockSignals(false);
            }
            m_doubleYoloMode = enabled;
        });
    }

    updateActionStates();
    syncYoloModesFromSession();
}

void ClaudeMenu::onApprove()
{
    if (m_activeSession) {
        m_activeSession->approvePermission();
    }
}

void ClaudeMenu::onDeny()
{
    if (m_activeSession) {
        m_activeSession->denyPermission();
    }
}

void ClaudeMenu::onStop()
{
    if (m_activeSession) {
        m_activeSession->stop();
    }
}

void ClaudeMenu::onRestart()
{
    if (m_activeSession) {
        m_activeSession->restart();
    }
}

void ClaudeMenu::onDetach()
{
    qDebug() << "ClaudeMenu::onDetach() - m_activeSession:" << static_cast<void *>(m_activeSession);
    if (m_activeSession) {
        qDebug() << "  Session name:" << m_activeSession->sessionName();
        m_activeSession->detach();
    } else {
        qDebug() << "  No active session!";
    }
}

void ClaudeMenu::onKill()
{
    if (m_activeSession) {
        m_activeSession->kill();
    }
}

void ClaudeMenu::onAboutToShow()
{
    updateActionStates();
    updateReattachMenu();
    syncNotificationToggles();
}

void ClaudeMenu::onReattachSession()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (action) {
        QString sessionName = action->data().toString();
        Q_EMIT reattachRequested(sessionName);
    }
}

void ClaudeMenu::onConfigureHooks()
{
    Q_EMIT configureHooksRequested();
}

void ClaudeMenu::onOrphanedSessionsChanged()
{
    updateReattachMenu();
}

void ClaudeMenu::updateActionStates()
{
    bool hasSession = (m_activeSession != nullptr);

    // Permission actions available when there's an active Claude session
    // (We can't reliably detect "waiting" state without hooks, so always enable)
    m_approveAction->setEnabled(hasSession);
    m_denyAction->setEnabled(hasSession);

    // Other actions available when session exists
    m_stopAction->setEnabled(hasSession);
    m_restartAction->setEnabled(hasSession);
    m_detachAction->setEnabled(hasSession);
    m_killAction->setEnabled(hasSession);
}

void ClaudeMenu::updateReattachMenu()
{
    m_reattachMenu->clear();

    if (!m_registry) {
        m_reattachMenu->addAction(i18n("No registry available"))->setEnabled(false);
        return;
    }

    QList<ClaudeSessionState> orphans = m_registry->orphanedSessions();

    if (orphans.isEmpty()) {
        m_reattachMenu->addAction(i18n("No detached sessions"))->setEnabled(false);
        return;
    }

    // Add submenu for each orphaned session with reattach and archive options
    for (const ClaudeSessionState &state : orphans) {
        QString displayName = state.sessionName;
        if (!state.profileName.isEmpty()) {
            displayName = QStringLiteral("%1 (%2)").arg(state.profileName, state.sessionId);
        }

        QString timeAgo = state.lastAccessed.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        QString tooltip = QStringLiteral("Last accessed: %1\nWorking directory: %2")
                              .arg(timeAgo, state.workingDirectory);

        // Create submenu for this session
        QMenu *sessionMenu = m_reattachMenu->addMenu(displayName);
        sessionMenu->setToolTip(tooltip);

        // Reattach action
        QAction *reattachAction = sessionMenu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reattach"));
        reattachAction->setData(state.sessionName);
        connect(reattachAction, &QAction::triggered, this, &ClaudeMenu::onReattachSession);

        // Archive action
        QAction *archiveAction = sessionMenu->addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive (Kill)"));
        archiveAction->setData(state.sessionName);
        connect(archiveAction, &QAction::triggered, this, &ClaudeMenu::onArchiveSession);
    }

    // Add separator and "Archive All" if there are multiple sessions
    if (orphans.size() > 1) {
        m_reattachMenu->addSeparator();
        QAction *archiveAllAction = m_reattachMenu->addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive All..."));
        connect(archiveAllAction, &QAction::triggered, this, &ClaudeMenu::onArchiveAll);
    }
}

void ClaudeMenu::onYoloModeToggled(bool checked)
{
    setYoloMode(checked);
}

void ClaudeMenu::onDoubleYoloModeToggled(bool checked)
{
    setDoubleYoloMode(checked);
}

void ClaudeMenu::setYoloMode(bool enabled)
{
    if (m_yoloMode == enabled) {
        return;
    }

    m_yoloMode = enabled;

    if (m_yoloModeAction) {
        m_yoloModeAction->setChecked(enabled);
    }

    // Update per-session setting
    if (m_activeSession) {
        m_activeSession->setYoloMode(enabled);
    }

    // Persist
    if (auto *s = KonsolaiSettings::instance()) {
        s->setYoloMode(enabled);
        s->save();
    }

    Q_EMIT yoloModeChanged(enabled);
}

void ClaudeMenu::setDoubleYoloMode(bool enabled)
{
    if (m_doubleYoloMode == enabled) {
        return;
    }

    m_doubleYoloMode = enabled;

    if (m_doubleYoloModeAction) {
        m_doubleYoloModeAction->setChecked(enabled);
    }

    // Update per-session setting
    if (m_activeSession) {
        m_activeSession->setDoubleYoloMode(enabled);
    }

    // Persist
    if (auto *s = KonsolaiSettings::instance()) {
        s->setDoubleYoloMode(enabled);
        s->save();
    }

    Q_EMIT doubleYoloModeChanged(enabled);
}

void ClaudeMenu::syncYoloModesFromSession()
{
    if (m_activeSession) {
        // Sync both the boolean fields and the checkbox state from the active session.
        // This ensures isYoloMode() etc. return the per-session value.
        m_yoloMode = m_activeSession->yoloMode();
        m_doubleYoloMode = m_activeSession->doubleYoloMode();

        if (m_yoloModeAction) {
            m_yoloModeAction->blockSignals(true);
            m_yoloModeAction->setChecked(m_yoloMode);
            m_yoloModeAction->blockSignals(false);
        }
        if (m_doubleYoloModeAction) {
            m_doubleYoloModeAction->blockSignals(true);
            m_doubleYoloModeAction->setChecked(m_doubleYoloMode);
            m_doubleYoloModeAction->blockSignals(false);
        }
    } else {
        // No session - show global defaults
        if (m_yoloModeAction) {
            m_yoloModeAction->blockSignals(true);
            m_yoloModeAction->setChecked(m_yoloMode);
            m_yoloModeAction->blockSignals(false);
        }
        if (m_doubleYoloModeAction) {
            m_doubleYoloModeAction->blockSignals(true);
            m_doubleYoloModeAction->setChecked(m_doubleYoloMode);
            m_doubleYoloModeAction->blockSignals(false);
        }
    }
}

void ClaudeMenu::onArchiveAll()
{
    if (!m_registry) {
        return;
    }

    QList<ClaudeSessionState> orphans = m_registry->orphanedSessions();
    if (orphans.isEmpty()) {
        return;
    }

    // Confirm with user
    int count = orphans.size();
    QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                              i18n("Archive All Sessions"),
                                                              i18n("This will permanently kill %1 detached tmux session(s).\n\nAre you sure?", count),
                                                              QMessageBox::Yes | QMessageBox::No,
                                                              QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    // Kill all orphaned sessions asynchronously — chain kills sequentially,
    // then refresh the registry when all are done.
    auto remaining = std::make_shared<QStringList>();
    for (const ClaudeSessionState &state : orphans) {
        remaining->append(state.sessionName);
    }
    auto *tmux = new TmuxManager(nullptr);
    auto registry = m_registry;

    std::function<void()> killNext;
    killNext = [tmux, remaining, registry, killNext]() {
        if (remaining->isEmpty()) {
            tmux->deleteLater();
            if (registry) {
                registry->refreshOrphanedSessionsAsync();
            }
            return;
        }
        QString name = remaining->takeFirst();
        tmux->killSessionAsync(name, [killNext](bool) {
            killNext();
        });
    };
    killNext();
}

void ClaudeMenu::onArchiveSession()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    QString sessionName = action->data().toString();
    if (sessionName.isEmpty()) {
        return;
    }

    // Kill the tmux session asynchronously, then refresh registry
    auto *tmux = new TmuxManager(nullptr);
    auto registry = m_registry;
    tmux->killSessionAsync(sessionName, [tmux, registry](bool) {
        tmux->deleteLater();
        if (registry) {
            registry->refreshOrphanedSessionsAsync();
        }
    });
}

void ClaudeMenu::onClearStaleHooks()
{
    if (!m_registry) {
        return;
    }

    // Collect unique working directories from all known sessions
    QSet<QString> workDirs;
    const auto states = m_registry->allSessionStates();
    for (const auto &state : states) {
        if (!state.workingDirectory.isEmpty()) {
            workDirs.insert(state.workingDirectory);
        }
    }
    // Also include active sessions
    const auto active = m_registry->activeSessions();
    for (auto *session : active) {
        if (session && !session->workingDirectory().isEmpty()) {
            workDirs.insert(session->workingDirectory());
        }
    }

    int cleared = 0;
    for (const QString &workDir : workDirs) {
        QString settingsPath = workDir + QStringLiteral("/.claude/settings.local.json");
        if (!QFile::exists(settingsPath)) {
            continue;
        }

        // Check if any hook entries reference sockets that don't exist
        QFile file(settingsPath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (!doc.isObject()) {
            continue;
        }
        QJsonObject settings = doc.object();
        if (!settings.contains(QStringLiteral("hooks"))) {
            continue;
        }

        QJsonObject hooks = settings[QStringLiteral("hooks")].toObject();
        bool hasStale = false;

        // Check each hook entry for stale socket references
        for (const QString &key : hooks.keys()) {
            QJsonArray entries = hooks[key].toArray();
            for (const auto &entry : entries) {
                QString entryStr = QString::fromUtf8(QJsonDocument(entry.toObject()).toJson());
                // Look for socket paths in the command
                static const QRegularExpression socketRx(QStringLiteral("(/[^ \"]+\\.sock)"));
                auto match = socketRx.match(entryStr);
                if (match.hasMatch()) {
                    QString sockPath = match.captured(1);
                    if (!QFile::exists(sockPath)) {
                        hasStale = true;
                        break;
                    }
                }
            }
            if (hasStale) {
                break;
            }
        }

        if (hasStale) {
            ClaudeSession::removeHooksForWorkDir(workDir);
            ++cleared;

            // Re-install hooks for any active session in this workDir
            for (auto *session : active) {
                if (session && session->workingDirectory() == workDir) {
                    // Emit signal so SessionManagerPanel can re-install
                    // For now, just log — the session panel's periodic check will repair
                    qDebug() << "ClaudeMenu: Cleared stale hooks for" << workDir << "- active session will need hook repair";
                }
            }
        }
    }

    qDebug() << "ClaudeMenu: Cleared stale hooks from" << cleared << "working directories";

    if (cleared == 0) {
        QMessageBox::information(this, i18n("Clear Stale Hooks"), i18n("No stale hooks found."));
    } else {
        QMessageBox::information(
            this,
            i18n("Clear Stale Hooks"),
            i18n("Cleared stale hooks from %1 working directory(ies).\n\nActive sessions will automatically re-install their hooks.", cleared));
    }
}

void ClaudeMenu::createNotificationMenu()
{
    m_notificationMenu = new QMenu(i18n("&Notifications"), this);
    m_notificationMenu->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-notification")));

    // Configure Events... (opens standard KDE notification config dialog)
    m_configureNotificationsAction = m_notificationMenu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure &Events..."));
    connect(m_configureNotificationsAction, &QAction::triggered, this, &ClaudeMenu::onConfigureNotifications);

    m_notificationMenu->addSeparator();

    // Channel toggles
    m_soundToggleAction = m_notificationMenu->addAction(i18n("&Sound"));
    m_soundToggleAction->setCheckable(true);
    m_soundToggleAction->setData(static_cast<int>(NotificationManager::Channel::Audio));
    connect(m_soundToggleAction, &QAction::toggled, this, &ClaudeMenu::onToggleNotificationChannel);

    m_desktopToggleAction = m_notificationMenu->addAction(i18n("&Desktop Popups"));
    m_desktopToggleAction->setCheckable(true);
    m_desktopToggleAction->setData(static_cast<int>(NotificationManager::Channel::Desktop));
    connect(m_desktopToggleAction, &QAction::toggled, this, &ClaudeMenu::onToggleNotificationChannel);

    m_systemTrayToggleAction = m_notificationMenu->addAction(i18n("System &Tray"));
    m_systemTrayToggleAction->setCheckable(true);
    m_systemTrayToggleAction->setData(static_cast<int>(NotificationManager::Channel::SystemTray));
    connect(m_systemTrayToggleAction, &QAction::toggled, this, &ClaudeMenu::onToggleNotificationChannel);

    m_inTerminalToggleAction = m_notificationMenu->addAction(i18n("&In-Terminal Overlay"));
    m_inTerminalToggleAction->setCheckable(true);
    m_inTerminalToggleAction->setData(static_cast<int>(NotificationManager::Channel::InTerminal));
    connect(m_inTerminalToggleAction, &QAction::toggled, this, &ClaudeMenu::onToggleNotificationChannel);

    m_notificationMenu->addSeparator();

    // Yolo notification toggle
    m_yoloNotifyToggleAction = m_notificationMenu->addAction(i18n("&Yolo Approval Sounds"));
    m_yoloNotifyToggleAction->setCheckable(true);
    m_yoloNotifyToggleAction->setToolTip(i18n("Play a subtle sound when yolo mode auto-approves an action"));
    connect(m_yoloNotifyToggleAction, &QAction::toggled, this, [](bool checked) {
        if (auto *mgr = NotificationManager::instance()) {
            mgr->setYoloNotificationsEnabled(checked);
        }
    });

    syncNotificationToggles();

    // Insert before "Configure Hooks..."
    insertMenu(m_configureHooksAction, m_notificationMenu);
}

void ClaudeMenu::syncNotificationToggles()
{
    auto *mgr = NotificationManager::instance();
    if (!mgr) {
        return;
    }

    auto block = [](QAction *a, bool checked) {
        a->blockSignals(true);
        a->setChecked(checked);
        a->blockSignals(false);
    };

    block(m_soundToggleAction, mgr->isChannelEnabled(NotificationManager::Channel::Audio));
    block(m_desktopToggleAction, mgr->isChannelEnabled(NotificationManager::Channel::Desktop));
    block(m_systemTrayToggleAction, mgr->isChannelEnabled(NotificationManager::Channel::SystemTray));
    block(m_inTerminalToggleAction, mgr->isChannelEnabled(NotificationManager::Channel::InTerminal));
    block(m_yoloNotifyToggleAction, mgr->yoloNotificationsEnabled());
}

void ClaudeMenu::onConfigureNotifications()
{
    KNotifyConfigWidget::configure(this, QStringLiteral("konsolai"));
}

void ClaudeMenu::onToggleNotificationChannel()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    auto *mgr = NotificationManager::instance();
    if (!mgr) {
        return;
    }

    auto channel = static_cast<NotificationManager::Channel>(action->data().toInt());
    mgr->enableChannel(channel, action->isChecked());
    mgr->saveSettings();
}

} // namespace Konsolai

#include "moc_ClaudeMenu.cpp"
