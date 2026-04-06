/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONLINKFILTER_H
#define SESSIONLINKFILTER_H

#include "RegExpFilter.h"
#include "konsoleprivate_export.h"

namespace Konsole
{

/**
 * A filter which matches @session-name patterns in terminal text and creates
 * clickable hotspots that focus the referenced Claude session's tab.
 *
 * The regex matches @word patterns (e.g., @project-name, @session-id) but
 * excludes email addresses by requiring whitespace or start-of-line before @.
 *
 * Only creates hotspots when the captured name fuzzy-matches a real session
 * in ClaudeSessionRegistry.
 */
class KONSOLEPRIVATE_EXPORT SessionLinkFilter : public RegExpFilter
{
public:
    SessionLinkFilter();

    QSharedPointer<HotSpot> newHotSpot(int startLine, int startColumn, int endLine, int endColumn, const QStringList &capturedTexts) override;

    static const QRegularExpression SessionLinkRegExp;
};

} // namespace Konsole

#endif // SESSIONLINKFILTER_H
