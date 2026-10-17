/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RestartPolicy.h"

#include <QSet>

namespace Konsolai
{

RestartPlan planRestart(const QVector<RestartCandidate> &candidates)
{
    RestartPlan plan;
    QSet<QString> seen;
    for (const RestartCandidate &c : candidates) {
        if (c.sessionId.isEmpty() || seen.contains(c.sessionId)) {
            continue;
        }
        seen.insert(c.sessionId);

        if (c.isRemote) {
            plan.skippedRemote.append(c.sessionId);
        } else if (c.hasTab) {
            plan.restartInPlace.append(c.sessionId);
        } else {
            plan.reopenThenRestart.append(c.sessionId);
        }
    }
    return plan;
}

} // namespace Konsolai
