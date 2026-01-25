/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeMenu.h"
#include "ClaudeSession.h"
#include "ClaudeSessionRegistry.h"
#include "ClaudeSessionState.h"

#include <KLocalizedString>
#include <QActionGroup>

namespace Konsolai
{

ClaudeMenu::ClaudeMenu(QWidget *parent)
    : QMenu(i18n("&Claude"), parent)
    , m_registry(ClaudeSessionRegistry::instance())
{
    createActions();
    createReattachMenu();

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
    // Approve Permission
    m_approveAction = addAction(i18n("&Approve Permission"));
    m_approveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    m_approveAction->setToolTip(i18n("Approve the pending permission request"));
    connect(m_approveAction, &QAction::triggered, this, &ClaudeMenu::onApprove);

    // Deny Permission
    m_denyAction = addAction(i18n("&Deny Permission"));
    m_denyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    m_denyAction->setToolTip(i18n("Deny the pending permission request"));
    connect(m_denyAction, &QAction::triggered, this, &ClaudeMenu::onDeny);

    addSeparator();

    // Stop Claude
    m_stopAction = addAction(i18n("&Stop Claude"));
    m_stopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_stopAction->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_stopAction->setToolTip(i18n("Stop the current Claude operation (Ctrl+C)"));
    connect(m_stopAction, &QAction::triggered, this, &ClaudeMenu::onStop);

    // Restart Claude
    m_restartAction = addAction(i18n("&Restart Claude"));
    m_restartAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_restartAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_restartAction->setToolTip(i18n("Restart the Claude session"));
    connect(m_restartAction, &QAction::triggered, this, &ClaudeMenu::onRestart);

    addSeparator();

    // Yolo Mode - auto-approve all permissions
    m_yoloModeAction = addAction(i18n("&Yolo Mode (Auto-Approve)"));
    m_yoloModeAction->setCheckable(true);
    m_yoloModeAction->setChecked(m_yoloMode);
    m_yoloModeAction->setToolTip(i18n("Automatically approve all permission requests"));
    m_yoloModeAction->setIcon(QIcon::fromTheme(QStringLiteral("security-low")));
    connect(m_yoloModeAction, &QAction::toggled, this, &ClaudeMenu::onYoloModeToggled);

    // Double Yolo Mode - auto-accept completions
    m_doubleYoloModeAction = addAction(i18n("Double &Yolo Mode (Auto-Complete)"));
    m_doubleYoloModeAction->setCheckable(true);
    m_doubleYoloModeAction->setChecked(m_doubleYoloMode);
    m_doubleYoloModeAction->setToolTip(i18n("Automatically accept tab completions"));
    m_doubleYoloModeAction->setIcon(QIcon::fromTheme(QStringLiteral("security-low")));
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
        connect(m_activeSession, &QObject::destroyed,
                this, [this]() {
                    m_activeSession = nullptr;
                    updateActionStates();
                });
    }

    updateActionStates();
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
    if (m_activeSession) {
        m_activeSession->detach();
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
    bool isWaiting = hasSession &&
                     (m_activeSession->claudeState() == ClaudeProcess::State::WaitingInput);

    // Permission actions only available when waiting for input
    m_approveAction->setEnabled(isWaiting);
    m_denyAction->setEnabled(isWaiting);

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

    // Add action for each orphaned session
    for (const ClaudeSessionState &state : orphans) {
        QString displayName = state.sessionName;
        if (!state.profileName.isEmpty()) {
            displayName = QStringLiteral("%1 (%2)").arg(state.profileName, state.sessionId);
        }

        QString timeAgo = state.lastAccessed.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        QString tooltip = QStringLiteral("Last accessed: %1\nWorking directory: %2")
                              .arg(timeAgo, state.workingDirectory);

        QAction *action = m_reattachMenu->addAction(displayName);
        action->setData(state.sessionName);
        action->setToolTip(tooltip);
        connect(action, &QAction::triggered, this, &ClaudeMenu::onReattachSession);
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

    Q_EMIT doubleYoloModeChanged(enabled);
}

} // namespace Konsolai

#include "moc_ClaudeMenu.cpp"
