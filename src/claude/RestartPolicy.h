/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_RESTARTPOLICY_H
#define KONSOLAI_RESTARTPOLICY_H

#include "konsoleprivate_export.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace Konsolai
{

/**
 * One session a bulk "Restart All Live" was asked to restart.
 */
struct KONSOLEPRIVATE_EXPORT RestartCandidate {
    QString sessionId;
    QString sessionName; // tmux session name
    bool hasTab = false; // a ClaudeSession is open for it
    bool isRemote = false;
};

/**
 * How a bulk restart handles each live session. Pure data so the menu,
 * its confirmation text, and the executor all agree on the same split.
 */
struct KONSOLEPRIVATE_EXPORT RestartPlan {
    QStringList restartInPlace; // open tab: respawn the pane now
    QStringList reopenThenRestart; // live in tmux, no tab: reopen, then respawn
    QStringList skippedRemote; // restart respawns via LOCAL tmux; the session lives on the SSH host

    int restartCount() const
    {
        return restartInPlace.size() + reopenThenRestart.size();
    }
};

/**
 * Partition candidates. Duplicate session IDs are planned once (a category
 * walk can reach a session twice); input order is preserved within each list.
 */
KONSOLEPRIVATE_EXPORT RestartPlan planRestart(const QVector<RestartCandidate> &candidates);

} // namespace Konsolai

#endif // KONSOLAI_RESTARTPOLICY_H
