/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSION_TREE_WIDGET_H
#define SESSION_TREE_WIDGET_H

#include "konsoleprivate_export.h"

#include <QTreeWidget>

class QDropEvent;
class QKeyEvent;
class QMimeData;
class QTimer;

namespace Konsolai
{

/**
 * SessionTreeWidget — QTreeWidget subclass with custom drag-drop for
 * category-to-category reparenting and vim-style keyboard navigation.
 *
 * Drag-drop:
 * Only category and project-group items (identified by their UserRole+6
 * composite key prefix "category:" / "group:") are draggable, and only
 * category items are valid drop targets. The widget encodes the source's
 * composite key in a custom MIME type and emits dropRequested() with the
 * source and target composite keys so the enclosing panel can confirm and
 * persist the alias.
 *
 * MIME payload format: raw UTF-8 bytes of the source composite key under
 * MIME type "application/x-konsolai-tree-node".
 *
 * Keyboard:
 * Vim-style bare-key bindings are handled in keyPressEvent(). The widget
 * translates them into either navigation actions on itself (j/k/h/l/gg/G,
 * Enter on category/group) or high-level action signals that the parent
 * panel resolves against the current item (actionRequested, focusFilterRequested,
 * escapePressed, topRequested, bottomRequested).
 *
 * Multi-key sequences (gg, dd) use a 500ms pending-prefix timer that is a
 * per-widget-instance member — cleaned up in the destructor by the timer
 * being parented to `this` (Qt handles the delete).
 */
class KONSOLEPRIVATE_EXPORT SessionTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    static constexpr const char *MIME_TYPE = "application/x-konsolai-tree-node";
    // Multi-key vim sequences (gg, dd) time out this many ms after the first key.
    static constexpr int PENDING_PREFIX_TIMEOUT_MS = 500;

    explicit SessionTreeWidget(QWidget *parent = nullptr);
    ~SessionTreeWidget() override = default;

    // Test-only: expose canDrop check without needing a live QDrag session.
    // Returns true iff a drop of an item with the given source composite key
    // would be accepted onto the given target item (or empty target).
    bool canAcceptDropStatic(const QString &sourceKey, QTreeWidgetItem *targetItem) const;

    // Test-only: inspect the pending prefix (\0 when idle).
    QChar pendingPrefix() const
    {
        return m_pendingPrefix;
    }

Q_SIGNALS:
    /**
     * Emitted after a valid drop when at least one source composite key and a
     * target category were successfully resolved. The parent panel is
     * responsible for confirming with the user and persisting the aliases /
     * overrides.
     *
     * Source keys: each is either "category:<name>" or "group:<workdir>".
     *              A single-item list is legal.  The panel loops and applies
     *              each entry's persistence rule.
     * Target key:  "category:<name>" (drops onto other node types are rejected)
     */
    void dropRequested(const QStringList &sourceKeys, const QString &targetCategoryKey);

    /**
     * Vim-style action requested by the user via keyboard.  The panel is the
     * source of truth for what "current item" means and how to apply the
     * action, so the widget emits and lets the panel handle it.  Actions:
     * "attach", "archive", "pin", "close", "dismiss", "rename", "new-category".
     */
    void actionRequested(const QString &action);

    /** User pressed `/` — panel should focus the filter search box. */
    void focusFilterRequested();

    /** User pressed Esc — panel should clear filter and tree selection. */
    void escapePressed();

    /** User pressed `gg` — panel should select the first visible item. */
    void topRequested();

    /** User pressed `G` — panel should select the last visible (leaf) item. */
    void bottomRequested();

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    // QTreeWidget's virtual — checks + accepts an incoming payload.
    bool dropMimeData(QTreeWidgetItem *parent, int index, const QMimeData *data, Qt::DropAction action) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    Qt::DropActions supportedDropActions() const override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // Pending vim prefix ('\0' = none). Reset by m_pendingPrefixTimer after
    // PENDING_PREFIX_TIMEOUT_MS if no follow-up key arrives.
    QChar m_pendingPrefix;
    QTimer *m_pendingPrefixTimer = nullptr;

    void clearPendingPrefix();
    void startPendingPrefix(QChar prefix);
};

} // namespace Konsolai

#endif // SESSION_TREE_WIDGET_H
