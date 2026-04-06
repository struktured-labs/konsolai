/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONLINKHOTSPOT_H
#define SESSIONLINKHOTSPOT_H

#include "RegExpFilterHotspot.h"

#include <QList>
#include <QString>

class QAction;

namespace Konsole
{

/**
 * Hotspot type created by SessionLinkFilter instances.
 *
 * Clicking activates (focuses) the matched Claude session's tab.
 * If the session is no longer active, activation is a no-op.
 *
 * The hotspot stores the session name as a QString (not a pointer)
 * because the session may be destroyed between hotspot creation and activation.
 */
class KONSOLEPRIVATE_EXPORT SessionLinkHotSpot : public RegExpFilterHotSpot
{
public:
    SessionLinkHotSpot(int startLine, int startColumn, int endLine, int endColumn, const QStringList &capturedTexts, const QString &matchedSessionName);
    ~SessionLinkHotSpot() override;

    QList<QAction *> actions() override;

    /**
     * Activates the hotspot: focuses the matched session's tab.
     * If the session is no longer active, this is a no-op.
     */
    void activate(QObject *object = nullptr) override;

    /**
     * Returns the session name this hotspot links to.
     */
    QString matchedSessionName() const
    {
        return _matchedSessionName;
    }

private:
    QString _matchedSessionName;
};

} // namespace Konsole

#endif // SESSIONLINKHOTSPOT_H
