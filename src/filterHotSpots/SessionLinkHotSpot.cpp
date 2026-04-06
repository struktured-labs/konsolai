/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionLinkHotSpot.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>

#include <KLocalizedString>
#include <QIcon>

#include "claude/ClaudeSession.h"
#include "claude/ClaudeSessionRegistry.h"
#include "konsoledebug.h"
#include "terminalDisplay/TerminalDisplay.h"

using namespace Konsole;

SessionLinkHotSpot::SessionLinkHotSpot(int startLine,
                                       int startColumn,
                                       int endLine,
                                       int endColumn,
                                       const QStringList &capturedTexts,
                                       const QString &matchedSessionName)
    : RegExpFilterHotSpot(startLine, startColumn, endLine, endColumn, capturedTexts)
    , _matchedSessionName(matchedSessionName)
{
    // Must be Link type for isUrl() to return true — gates all mouse interaction
    setType(Link);
}

SessionLinkHotSpot::~SessionLinkHotSpot() = default;

void SessionLinkHotSpot::activate(QObject *object)
{
    const QString &actionName = object != nullptr ? object->objectName() : QString();

    if (actionName == QLatin1String("copy-session-name")) {
        QApplication::clipboard()->setText(_matchedSessionName);
        return;
    }

    // Default action: focus the session's tab
    auto *registry = Konsolai::ClaudeSessionRegistry::instance();
    if (registry == nullptr) {
        return;
    }

    auto *session = registry->findSession(_matchedSessionName);
    if (session != nullptr) {
        const auto views = session->views();
        if (!views.isEmpty()) {
            views.first()->setFocus();
            return;
        }
    }

    // Session not found in active sessions — check if it's a detached tmux session
    const auto allStates = registry->allSessionStates();
    for (const auto &state : allStates) {
        if (state.sessionName == _matchedSessionName && !state.isAttached) {
            qCDebug(KonsoleDebug) << "SessionLinkHotSpot: session" << _matchedSessionName << "is detached (tmux); auto-attach not yet implemented";
            return;
        }
    }

    qCDebug(KonsoleDebug) << "SessionLinkHotSpot: session" << _matchedSessionName << "not found";
}

QList<QAction *> SessionLinkHotSpot::actions()
{
    auto *focusAction = new QAction(this);
    focusAction->setText(i18n("Focus Session \"%1\"", _matchedSessionName));
    focusAction->setIcon(QIcon::fromTheme(QStringLiteral("go-jump")));
    focusAction->setObjectName(QStringLiteral("focus-action"));

    auto *copyAction = new QAction(this);
    copyAction->setText(i18n("Copy Session Name"));
    copyAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copyAction->setObjectName(QStringLiteral("copy-session-name"));

    QObject::connect(focusAction, &QAction::triggered, this, [this, focusAction] {
        activate(focusAction);
    });
    QObject::connect(copyAction, &QAction::triggered, this, [this, copyAction] {
        activate(copyAction);
    });

    return {focusAction, copyAction};
}
