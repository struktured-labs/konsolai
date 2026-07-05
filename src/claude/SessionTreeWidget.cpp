/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionTreeWidget.h"

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QTimer>

namespace Konsolai
{

namespace
{

// Composite keys live at UserRole+6. Sessions use "s:", groups use "group:",
// categories use "category:" — see SessionManagerPanel.
constexpr int COMPOSITE_KEY_ROLE = Qt::UserRole + 6;

QString compositeKey(const QTreeWidgetItem *item)
{
    return item ? item->data(0, COMPOSITE_KEY_ROLE).toString() : QString();
}

bool isCategory(const QString &key)
{
    return key.startsWith(QStringLiteral("category:"));
}

bool isGroup(const QString &key)
{
    return key.startsWith(QStringLiteral("group:"));
}

bool isDraggable(const QString &key)
{
    return isCategory(key) || isGroup(key);
}

} // namespace

SessionTreeWidget::SessionTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
    , m_pendingPrefix(QLatin1Char('\0'))
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDropIndicatorShown(true);
    setAutoExpandDelay(500);

    // Per-instance single-shot timer for vim multi-key prefix (gg, dd).
    // Parented to `this` so Qt cleans it up when the widget is destroyed —
    // no explicit teardown needed.  A per-widget timer is required (not
    // static) so multiple SessionTreeWidgets in the same process don't
    // stomp on each other's pending state.
    m_pendingPrefixTimer = new QTimer(this);
    m_pendingPrefixTimer->setSingleShot(true);
    m_pendingPrefixTimer->setInterval(PENDING_PREFIX_TIMEOUT_MS);
    connect(m_pendingPrefixTimer, &QTimer::timeout, this, [this]() {
        m_pendingPrefix = QLatin1Char('\0');
    });
}

void SessionTreeWidget::clearPendingPrefix()
{
    m_pendingPrefix = QLatin1Char('\0');
    if (m_pendingPrefixTimer) {
        m_pendingPrefixTimer->stop();
    }
}

void SessionTreeWidget::startPendingPrefix(QChar prefix)
{
    m_pendingPrefix = prefix;
    if (m_pendingPrefixTimer) {
        m_pendingPrefixTimer->start();
    }
}

QStringList SessionTreeWidget::mimeTypes() const
{
    return {QString::fromLatin1(MIME_TYPE)};
}

Qt::DropActions SessionTreeWidget::supportedDropActions() const
{
    // We treat the drop as a "reparent" — MoveAction is what Qt sends by
    // default for InternalMove and matches user expectation.
    return Qt::MoveAction;
}

QMimeData *SessionTreeWidget::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    // Multi-select: encode every draggable item in the selection.  Non-
    // draggable items (session leaves, subagents, etc.) are silently dropped
    // from the payload — a mixed selection still succeeds for the draggable
    // subset.  If nothing draggable remains, refuse the drag entirely.
    QStringList draggableKeys;
    for (auto *item : items) {
        const QString key = compositeKey(item);
        if (isDraggable(key)) {
            draggableKeys << key;
        }
    }
    if (draggableKeys.isEmpty()) {
        return nullptr;
    }
    auto *data = new QMimeData();
    // Newline-separated so a comma or other punctuation in a category name
    // doesn't corrupt the payload.  Category keys never contain newlines.
    data->setData(QString::fromLatin1(MIME_TYPE), draggableKeys.join(QLatin1Char('\n')).toUtf8());
    return data;
}

namespace
{

// Parse newline-separated composite keys back into a QStringList, filtering
// out any that aren't draggable (defensive — the sender should have already
// filtered).
QStringList parseSourceKeys(const QByteArray &raw)
{
    const QString joined = QString::fromUtf8(raw);
    QStringList out;
    for (const QString &k : joined.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (isDraggable(k)) {
            out << k;
        }
    }
    return out;
}

} // namespace

bool SessionTreeWidget::canAcceptDropStatic(const QString &sourceKey, QTreeWidgetItem *targetItem) const
{
    if (sourceKey.isEmpty() || !isDraggable(sourceKey)) {
        return false;
    }
    // A category-target is required. Empty target (drop between top-level items)
    // and non-category targets are rejected.
    if (!targetItem) {
        return false;
    }
    const QString targetKey = compositeKey(targetItem);
    if (!isCategory(targetKey)) {
        return false;
    }
    if (targetKey == sourceKey) {
        return false; // no self-drop
    }
    return true;
}

bool SessionTreeWidget::dropMimeData(QTreeWidgetItem *parent, int index, const QMimeData *data, Qt::DropAction action)
{
    Q_UNUSED(index);
    Q_UNUSED(action);
    if (!data || !data->hasFormat(QString::fromLatin1(MIME_TYPE))) {
        return false;
    }
    const QStringList sourceKeys = parseSourceKeys(data->data(QString::fromLatin1(MIME_TYPE)));
    if (sourceKeys.isEmpty() || !parent) {
        return false;
    }
    const QString targetKey = compositeKey(parent);
    if (!isCategory(targetKey)) {
        return false;
    }
    // Any source that points at the target itself is filtered out — no
    // self-drop. If nothing valid remains, the drop is a no-op.
    QStringList filtered;
    for (const QString &k : sourceKeys) {
        if (k != targetKey) {
            filtered << k;
        }
    }
    if (filtered.isEmpty()) {
        return false;
    }
    // Defer emission — the panel will trigger a full tree rebuild which
    // invalidates the QTreeWidgetItems. Doing that mid-drop leaves Qt's
    // DnD internals dangling on freed items.
    QMetaObject::invokeMethod(
        this,
        [this, filtered, targetKey]() {
            Q_EMIT dropRequested(filtered, targetKey);
        },
        Qt::QueuedConnection);
    // Return false so QTreeWidget doesn't actually perform the default
    // in-model row move — the panel owns the source of truth (settings +
    // metadata) and will trigger a full rebuild.
    return false;
}

void SessionTreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(QString::fromLatin1(MIME_TYPE))) {
        QTreeWidget::dragMoveEvent(event);
        return;
    }
    const QStringList sourceKeys = parseSourceKeys(event->mimeData()->data(QString::fromLatin1(MIME_TYPE)));
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    // Accept if at least one source is droppable onto the target.  A
    // partially-valid selection is still allowed — invalid entries are
    // silently ignored during handling.
    bool anyAccepted = false;
    for (const QString &k : sourceKeys) {
        if (canAcceptDropStatic(k, target)) {
            anyAccepted = true;
            break;
        }
    }
    if (anyAccepted) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SessionTreeWidget::dropEvent(QDropEvent *event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(QString::fromLatin1(MIME_TYPE))) {
        // Not our custom payload — fall back to default behavior (no-op for
        // internal move onto a non-accepting target).
        event->ignore();
        return;
    }

    const QStringList sourceKeys = parseSourceKeys(event->mimeData()->data(QString::fromLatin1(MIME_TYPE)));
    if (sourceKeys.isEmpty()) {
        event->ignore();
        return;
    }

    // Resolve target — the item under the drop position. dropEvent is called
    // AFTER canDropMimeData, but we re-validate defensively.
    QTreeWidgetItem *targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }
    const QString targetKey = compositeKey(targetItem);
    if (!isCategory(targetKey)) {
        event->ignore();
        return;
    }

    // Drop any source key that is the target itself.
    QStringList filtered;
    for (const QString &k : sourceKeys) {
        if (k != targetKey) {
            filtered << k;
        }
    }
    if (filtered.isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();

    // Defer signal emission — the panel will trigger a full tree rebuild
    // which invalidates all tree items. Doing that inside dropEvent leaves
    // Qt's DnD internals dangling. queueing avoids that.
    QMetaObject::invokeMethod(
        this,
        [this, filtered, targetKey]() {
            Q_EMIT dropRequested(filtered, targetKey);
        },
        Qt::QueuedConnection);
}

// ============================================================
// Vim-style hotkey handling
// ============================================================
//
// Bare keys only: any Ctrl/Alt/Meta modifier falls through to the base class
// so upstream shortcuts (Ctrl+Alt+A, Ctrl+F, arrow keys, PgUp/PgDn) still
// work as expected.  Shift is allowed (needed to distinguish `g` from `G`).
//
// The vim mappings translate into one of three effects:
//   1. Direct navigation (j/k → forward as Up/Down, h/l → collapse/expand,
//      Enter on a category → toggle expand).
//   2. Signal to the panel via topRequested/bottomRequested/focusFilterRequested/
//      escapePressed for widget-scoped concerns the panel owns.
//   3. Signal to the panel via actionRequested(action) for context-sensitive
//      operations on the current item (archive, pin, close, dismiss, rename,
//      new-category, attach).
//
// Multi-key sequences (gg, dd) use m_pendingPrefix + PENDING_PREFIX_TIMEOUT_MS
// timer.  A key that isn't a valid follow-up cancels the pending state and is
// handled normally in the same event.
void SessionTreeWidget::keyPressEvent(QKeyEvent *event)
{
    if (!event) {
        QTreeWidget::keyPressEvent(event);
        return;
    }

    // Ctrl/Alt/Meta chords bypass vim mode entirely — let the base class and
    // KActionCollection dispatch them.  Shift is intentionally allowed so
    // uppercase G can be distinguished from lowercase g.
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        clearPendingPrefix();
        QTreeWidget::keyPressEvent(event);
        return;
    }

    const int key = event->key();

    // --- Multi-key sequences: gg (top) and dd (dismiss) ---

    if (key == Qt::Key_G && !(mods & Qt::ShiftModifier)) {
        if (m_pendingPrefix == QLatin1Char('g')) {
            clearPendingPrefix();
            Q_EMIT topRequested();
        } else {
            startPendingPrefix(QLatin1Char('g'));
        }
        event->accept();
        return;
    }
    if (key == Qt::Key_D) {
        if (m_pendingPrefix == QLatin1Char('d')) {
            clearPendingPrefix();
            Q_EMIT actionRequested(QStringLiteral("dismiss"));
        } else {
            startPendingPrefix(QLatin1Char('d'));
        }
        event->accept();
        return;
    }

    // Any other handled key cancels a pending prefix — we don't want a stray
    // `g` followed by `j` to feel weirdly latched.
    clearPendingPrefix();

    // --- Single-shot navigation keys ---

    switch (key) {
    case Qt::Key_J: {
        QKeyEvent forwarded(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QTreeWidget::keyPressEvent(&forwarded);
        event->accept();
        return;
    }
    case Qt::Key_K: {
        QKeyEvent forwarded(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QTreeWidget::keyPressEvent(&forwarded);
        event->accept();
        return;
    }
    case Qt::Key_H: {
        QTreeWidgetItem *cur = currentItem();
        if (cur && cur->isExpanded()) {
            cur->setExpanded(false);
        } else if (cur && cur->parent()) {
            // Already collapsed / a leaf — move focus to the parent.
            setCurrentItem(cur->parent());
        }
        event->accept();
        return;
    }
    case Qt::Key_L: {
        QTreeWidgetItem *cur = currentItem();
        if (cur && cur->childCount() > 0) {
            cur->setExpanded(true);
        }
        event->accept();
        return;
    }
    case Qt::Key_G: {
        // Shift+G — jump to bottom. (Bare `g` is handled above as multi-key.)
        Q_EMIT bottomRequested();
        event->accept();
        return;
    }
    case Qt::Key_Slash: {
        Q_EMIT focusFilterRequested();
        event->accept();
        return;
    }
    case Qt::Key_Escape: {
        Q_EMIT escapePressed();
        clearSelection();
        event->accept();
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        QTreeWidgetItem *cur = currentItem();
        if (!cur) {
            QTreeWidget::keyPressEvent(event);
            return;
        }
        const QString composite = cur->data(0, Qt::UserRole + 6).toString();
        const bool isSession = composite.startsWith(QStringLiteral("s:"));
        if (isSession) {
            Q_EMIT actionRequested(QStringLiteral("attach"));
        } else {
            // Category / group / discovered leaf without a composite key —
            // treat as expand toggle so navigation feels consistent.
            cur->setExpanded(!cur->isExpanded());
        }
        event->accept();
        return;
    }
    case Qt::Key_A:
        Q_EMIT actionRequested(QStringLiteral("archive"));
        event->accept();
        return;
    case Qt::Key_P:
        Q_EMIT actionRequested(QStringLiteral("pin"));
        event->accept();
        return;
    case Qt::Key_C:
        Q_EMIT actionRequested(QStringLiteral("close"));
        event->accept();
        return;
    case Qt::Key_X:
        Q_EMIT actionRequested(QStringLiteral("dismiss"));
        event->accept();
        return;
    case Qt::Key_R:
        Q_EMIT actionRequested(QStringLiteral("rename"));
        event->accept();
        return;
    case Qt::Key_N:
        Q_EMIT actionRequested(QStringLiteral("new-category"));
        event->accept();
        return;
    default:
        break;
    }

    // Unhandled: fall through so arrow keys, PgUp/PgDn, Home/End, typeahead
    // search, etc. all keep working.
    QTreeWidget::keyPressEvent(event);
}

} // namespace Konsolai

#include "moc_SessionTreeWidget.cpp"
