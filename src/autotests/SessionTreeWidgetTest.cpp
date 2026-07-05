/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionTreeWidgetTest.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidgetItem>

#include "../claude/SessionTreeWidget.h"

using namespace Konsolai;

namespace
{

constexpr int COMPOSITE_KEY_ROLE = Qt::UserRole + 6;

// Helper: create a tree populated with a top-level category holding two
// project groups; the first group contains one session leaf. Returns the
// widget (heap-allocated; test takes ownership).
struct TreeFixture {
    SessionTreeWidget *tree;
    QTreeWidgetItem *catA;
    QTreeWidgetItem *catB;
    QTreeWidgetItem *groupA1;
    QTreeWidgetItem *groupA2;
    QTreeWidgetItem *sessionLeaf;
};

TreeFixture buildFixture()
{
    auto *tree = new SessionTreeWidget();

    auto *catA = new QTreeWidgetItem(tree);
    catA->setText(0, QStringLiteral("cowir"));
    catA->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("category:cowir"));

    auto *catB = new QTreeWidgetItem(tree);
    catB->setText(0, QStringLiteral("penta-dragon"));
    catB->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("category:penta-dragon"));

    auto *groupA1 = new QTreeWidgetItem(catA);
    groupA1->setText(0, QStringLiteral("alpha"));
    groupA1->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("group:/home/u/cowir-alpha"));

    auto *groupA2 = new QTreeWidgetItem(catA);
    groupA2->setText(0, QStringLiteral("beta"));
    groupA2->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("group:/home/u/cowir-beta"));

    auto *sessionLeaf = new QTreeWidgetItem(groupA1);
    sessionLeaf->setText(0, QStringLiteral("some-session"));
    sessionLeaf->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("s:abc123"));

    tree->expandAll();
    return {tree, catA, catB, groupA1, groupA2, sessionLeaf};
}

} // namespace

// The QTreeWidget::mimeData / canDrop overrides on QTreeWidget are protected,
// so we test them through a helper subclass that exposes them.
class TreeProbe : public SessionTreeWidget
{
public:
    QMimeData *probeMimeData(const QList<QTreeWidgetItem *> &items) const
    {
        return mimeData(items);
    }
};

void SessionTreeWidgetTest::testMimeDataContainsCompositeKey()
{
    auto fx = buildFixture();
    auto probe = std::make_unique<TreeProbe>();

    // Attach the category items to the probe temporarily via the same key —
    // we only need mimeData to serialize the composite key from the item.
    auto *cat = new QTreeWidgetItem(probe.get());
    cat->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("category:cowir"));

    QMimeData *data = probe->probeMimeData({cat});
    QVERIFY(data != nullptr);
    QVERIFY(data->hasFormat(QString::fromLatin1(SessionTreeWidget::MIME_TYPE)));
    // Single-item selection encodes the one composite key (newline-separated
    // format still works for the degenerate list-of-1 case).
    QCOMPARE(QString::fromUtf8(data->data(QString::fromLatin1(SessionTreeWidget::MIME_TYPE))), QStringLiteral("category:cowir"));
    delete data;
    delete fx.tree;
}

void SessionTreeWidgetTest::testCanDropOnCategoryTarget()
{
    auto fx = buildFixture();

    // Group dropped onto a different category is accepted.
    QVERIFY(fx.tree->canAcceptDropStatic(QStringLiteral("group:/home/u/cowir-alpha"), fx.catB));
    // Category dropped onto another category is accepted.
    QVERIFY(fx.tree->canAcceptDropStatic(QStringLiteral("category:cowir"), fx.catB));

    delete fx.tree;
}

void SessionTreeWidgetTest::testCantDropOnProjectGroup()
{
    auto fx = buildFixture();
    // Groups aren't valid drop targets.
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("group:/home/u/cowir-alpha"), fx.groupA2));
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("category:cowir"), fx.groupA1));
    delete fx.tree;
}

void SessionTreeWidgetTest::testCantDropOnSessionLeaf()
{
    auto fx = buildFixture();
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("group:/home/u/cowir-alpha"), fx.sessionLeaf));
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("category:cowir"), fx.sessionLeaf));
    delete fx.tree;
}

void SessionTreeWidgetTest::testCantDropOnSelf()
{
    auto fx = buildFixture();
    // Same category dropped on itself is rejected.
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("category:cowir"), fx.catA));
    delete fx.tree;
}

void SessionTreeWidgetTest::testDropEmitsSignalWithSourceAndTarget()
{
    auto fx = buildFixture();

    QSignalSpy spy(fx.tree, &SessionTreeWidget::dropRequested);
    QVERIFY(spy.isValid());

    // Invoke the dropRequested signal indirectly by driving the queued
    // emission path: build a QMimeData payload and call the model's
    // dropMimeData through the tree's own QTreeWidget interface.
    // The subclass emits dropRequested via QueuedConnection, so we need to
    // spin the event loop after the call.
    QMimeData mime;
    mime.setData(QString::fromLatin1(SessionTreeWidget::MIME_TYPE), QByteArrayLiteral("group:/home/u/cowir-alpha"));

    // dropMimeData is protected — invoke via the model's public interface.
    QAbstractItemModel *model = fx.tree->model();
    // "index" arg is the row within parent; we drop *onto* the category (not
    // between rows), so use index=-1 or 0. QTreeWidget forwards this through
    // to its private model → SessionTreeWidget::dropMimeData.
    const QModelIndex targetIdx = fx.tree->indexFromItem(fx.catB, 0);
    const bool result = model->dropMimeData(&mime, Qt::MoveAction, -1, -1, targetIdx);
    Q_UNUSED(result);

    // Signal is emitted via QueuedConnection to avoid dangling QTreeWidgetItem
    // pointers during a rebuild — spin the event loop to deliver.
    QTRY_COMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    // Signal payload is (QStringList sourceKeys, QString targetCategoryKey).
    const QStringList sources = args.at(0).toStringList();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.first(), QStringLiteral("group:/home/u/cowir-alpha"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("category:penta-dragon"));

    delete fx.tree;
}

void SessionTreeWidgetTest::testMimeDataIsNullForSessionLeaves()
{
    auto probe = std::make_unique<TreeProbe>();
    auto *leaf = new QTreeWidgetItem(probe.get());
    leaf->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("s:sessionid1"));

    QMimeData *data = probe->probeMimeData({leaf});
    QVERIFY2(data == nullptr, "Session leaves must not be draggable — mimeData should be null");
}

void SessionTreeWidgetTest::testCanDropRejectsInvalidSource()
{
    auto fx = buildFixture();
    // An empty or invalid source is always rejected.
    QVERIFY(!fx.tree->canAcceptDropStatic(QString(), fx.catB));
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("s:some-session"), fx.catB));
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("nonsense"), fx.catB));
    // Null target is rejected.
    QVERIFY(!fx.tree->canAcceptDropStatic(QStringLiteral("category:cowir"), nullptr));
    delete fx.tree;
}

// ============================================================
// Multi-select drag
// ============================================================

void SessionTreeWidgetTest::testMimeDataEncodesMultipleSourcesNewlineSeparated()
{
    auto probe = std::make_unique<TreeProbe>();

    // Two draggable items in the selection: one category, one group.
    auto *cat = new QTreeWidgetItem(probe.get());
    cat->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("category:cowir"));
    auto *grp = new QTreeWidgetItem(probe.get());
    grp->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("group:/home/u/alpha"));

    QMimeData *data = probe->probeMimeData({cat, grp});
    QVERIFY(data != nullptr);
    QVERIFY(data->hasFormat(QString::fromLatin1(SessionTreeWidget::MIME_TYPE)));

    const QString payload = QString::fromUtf8(data->data(QString::fromLatin1(SessionTreeWidget::MIME_TYPE)));
    const QStringList parts = payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(parts.size(), 2);
    QVERIFY(parts.contains(QStringLiteral("category:cowir")));
    QVERIFY(parts.contains(QStringLiteral("group:/home/u/alpha")));

    delete data;
}

void SessionTreeWidgetTest::testDropEmitsQStringListSignal()
{
    auto fx = buildFixture();

    QSignalSpy spy(fx.tree, &SessionTreeWidget::dropRequested);
    QVERIFY(spy.isValid());

    // Two source keys in a newline-separated payload.
    QMimeData mime;
    mime.setData(QString::fromLatin1(SessionTreeWidget::MIME_TYPE), QByteArrayLiteral("group:/home/u/cowir-alpha\ngroup:/home/u/cowir-beta"));

    QAbstractItemModel *model = fx.tree->model();
    const QModelIndex targetIdx = fx.tree->indexFromItem(fx.catB, 0);
    const bool result = model->dropMimeData(&mime, Qt::MoveAction, -1, -1, targetIdx);
    Q_UNUSED(result);

    QTRY_COMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    const QStringList sources = args.at(0).toStringList();
    QCOMPARE(sources.size(), 2);
    QVERIFY(sources.contains(QStringLiteral("group:/home/u/cowir-alpha")));
    QVERIFY(sources.contains(QStringLiteral("group:/home/u/cowir-beta")));
    QCOMPARE(args.at(1).toString(), QStringLiteral("category:penta-dragon"));

    delete fx.tree;
}

void SessionTreeWidgetTest::testMimeDataFiltersUndraggableItemsFromSelection()
{
    auto probe = std::make_unique<TreeProbe>();

    // Mixed selection: one draggable group + one session leaf (undraggable).
    auto *grp = new QTreeWidgetItem(probe.get());
    grp->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("group:/home/u/alpha"));
    auto *leaf = new QTreeWidgetItem(probe.get());
    leaf->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("s:session-id"));

    QMimeData *data = probe->probeMimeData({grp, leaf});
    QVERIFY(data != nullptr); // Draggable subset survives.

    const QString payload = QString::fromUtf8(data->data(QString::fromLatin1(SessionTreeWidget::MIME_TYPE)));
    const QStringList parts = payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts.first(), QStringLiteral("group:/home/u/alpha"));

    delete data;

    // A selection with ONLY undraggable items still returns nullptr.
    auto *leafOnly = new QTreeWidgetItem(probe.get());
    leafOnly->setData(0, COMPOSITE_KEY_ROLE, QStringLiteral("s:another"));
    QMimeData *empty = probe->probeMimeData({leafOnly});
    QVERIFY2(empty == nullptr, "Selection of only undraggable items should return null");
}

// ============================================================
// Vim-style hotkey navigation
// ============================================================

namespace
{

// Send a synthetic key press to the widget.  We call keyPressEvent directly
// (which is protected but the widget is our subclass), so we can drive it
// without a windowed event loop — QTest::keyPress requires the widget to be
// shown+focused, which flakes in QTEST_MAIN environments.
void sendKey(SessionTreeWidget *tree, int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = {})
{
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(tree, &ev);
}

} // namespace

void SessionTreeWidgetTest::testJKMapsToArrowNavigation()
{
    auto fx = buildFixture();
    fx.tree->setCurrentItem(fx.catA);

    // Baseline: catA is the current item.
    QCOMPARE(fx.tree->currentItem(), fx.catA);

    // j → Down — goes to the first child (groupA1) since catA is expanded.
    sendKey(fx.tree, Qt::Key_J);
    QCOMPARE(fx.tree->currentItem(), fx.groupA1);

    // k → Up — back to catA.
    sendKey(fx.tree, Qt::Key_K);
    QCOMPARE(fx.tree->currentItem(), fx.catA);

    delete fx.tree;
}

void SessionTreeWidgetTest::testHLCollapsesAndExpands()
{
    auto fx = buildFixture();
    fx.tree->setCurrentItem(fx.catA);
    QVERIFY(fx.catA->isExpanded());

    // h → collapse the current item.
    sendKey(fx.tree, Qt::Key_H);
    QVERIFY(!fx.catA->isExpanded());

    // l → expand again.
    sendKey(fx.tree, Qt::Key_L);
    QVERIFY(fx.catA->isExpanded());

    delete fx.tree;
}

void SessionTreeWidgetTest::testGGGoesToFirstItem_MultiKeySequence()
{
    auto fx = buildFixture();

    QSignalSpy topSpy(fx.tree, &SessionTreeWidget::topRequested);
    QVERIFY(topSpy.isValid());

    // First 'g' → sets pending prefix, no emission yet.
    sendKey(fx.tree, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"));
    QCOMPARE(topSpy.count(), 0);
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('g')));

    // Second 'g' within 500ms → fires topRequested and clears pending.
    sendKey(fx.tree, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"));
    QCOMPARE(topSpy.count(), 1);
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('\0')));

    delete fx.tree;
}

void SessionTreeWidgetTest::testGGSingleGTimesOut()
{
    auto fx = buildFixture();

    QSignalSpy topSpy(fx.tree, &SessionTreeWidget::topRequested);
    QVERIFY(topSpy.isValid());

    // First 'g'.
    sendKey(fx.tree, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"));
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('g')));

    // Wait past the 500ms timeout — QTest::qWait spins the event loop so
    // the QTimer fires.
    QTest::qWait(SessionTreeWidget::PENDING_PREFIX_TIMEOUT_MS + 100);
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('\0')));
    QCOMPARE(topSpy.count(), 0);

    delete fx.tree;
}

void SessionTreeWidgetTest::testGCapitalGoesToLast()
{
    auto fx = buildFixture();

    QSignalSpy bottomSpy(fx.tree, &SessionTreeWidget::bottomRequested);
    QVERIFY(bottomSpy.isValid());

    // Shift+G → immediate bottomRequested, no pending state.
    sendKey(fx.tree, Qt::Key_G, Qt::ShiftModifier, QStringLiteral("G"));
    QCOMPARE(bottomSpy.count(), 1);
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('\0')));

    delete fx.tree;
}

void SessionTreeWidgetTest::testSlashEmitsFocusFilterRequested()
{
    auto fx = buildFixture();
    QSignalSpy spy(fx.tree, &SessionTreeWidget::focusFilterRequested);
    QVERIFY(spy.isValid());

    sendKey(fx.tree, Qt::Key_Slash);
    QCOMPARE(spy.count(), 1);

    delete fx.tree;
}

void SessionTreeWidgetTest::testEscapeEmitsEscapePressed()
{
    auto fx = buildFixture();
    fx.tree->setCurrentItem(fx.catA);
    QSignalSpy spy(fx.tree, &SessionTreeWidget::escapePressed);
    QVERIFY(spy.isValid());

    sendKey(fx.tree, Qt::Key_Escape);
    QCOMPARE(spy.count(), 1);
    // clearSelection() is also called — currentItem may be reset to null.
    QVERIFY(fx.tree->selectedItems().isEmpty());

    delete fx.tree;
}

void SessionTreeWidgetTest::testDDDismissesCurrentSession_MultiKeySequence()
{
    auto fx = buildFixture();
    QSignalSpy spy(fx.tree, &SessionTreeWidget::actionRequested);
    QVERIFY(spy.isValid());

    // First 'd' → pending state.
    sendKey(fx.tree, Qt::Key_D);
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('d')));
    QCOMPARE(spy.count(), 0);

    // Second 'd' → fires "dismiss".
    sendKey(fx.tree, Qt::Key_D);
    QCOMPARE(fx.tree->pendingPrefix(), QChar(QLatin1Char('\0')));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("dismiss"));

    delete fx.tree;
}

void SessionTreeWidgetTest::testActionKeys_SingleShotEmissions()
{
    struct Case {
        int key;
        QString expected;
    };
    const QList<Case> cases = {
        {Qt::Key_A, QStringLiteral("archive")},
        {Qt::Key_P, QStringLiteral("pin")},
        {Qt::Key_C, QStringLiteral("close")},
        {Qt::Key_X, QStringLiteral("dismiss")},
        {Qt::Key_R, QStringLiteral("rename")},
        {Qt::Key_N, QStringLiteral("new-category")},
    };
    for (const auto &c : cases) {
        auto fx = buildFixture();
        QSignalSpy spy(fx.tree, &SessionTreeWidget::actionRequested);
        QVERIFY(spy.isValid());
        sendKey(fx.tree, c.key);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), c.expected);
        delete fx.tree;
    }
}

void SessionTreeWidgetTest::testEnterOnLeafEmitsAttach()
{
    auto fx = buildFixture();
    fx.tree->setCurrentItem(fx.sessionLeaf);

    QSignalSpy spy(fx.tree, &SessionTreeWidget::actionRequested);
    QVERIFY(spy.isValid());

    sendKey(fx.tree, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("attach"));

    delete fx.tree;
}

void SessionTreeWidgetTest::testEnterOnCategoryTogglesExpanded()
{
    auto fx = buildFixture();
    fx.tree->setCurrentItem(fx.catA);
    QVERIFY(fx.catA->isExpanded());

    QSignalSpy spy(fx.tree, &SessionTreeWidget::actionRequested);
    QVERIFY(spy.isValid());

    // First Enter → collapse.
    sendKey(fx.tree, Qt::Key_Return);
    QVERIFY(!fx.catA->isExpanded());
    QCOMPARE(spy.count(), 0); // no signal for category

    // Second Enter → expand.
    sendKey(fx.tree, Qt::Key_Return);
    QVERIFY(fx.catA->isExpanded());
    QCOMPARE(spy.count(), 0);

    delete fx.tree;
}

void SessionTreeWidgetTest::testCtrlModifiedKeyFallsThroughToBase()
{
    // Ctrl+A must NOT emit "archive" — it's the standard Select All shortcut
    // and must remain reachable through the base class.  This regression
    // guard makes sure the vim layer doesn't hijack modifier chords.
    auto fx = buildFixture();
    QSignalSpy spy(fx.tree, &SessionTreeWidget::actionRequested);
    QVERIFY(spy.isValid());

    sendKey(fx.tree, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(spy.count(), 0);

    delete fx.tree;
}

QTEST_MAIN(Konsolai::SessionTreeWidgetTest)

#include "SessionTreeWidgetTest.moc"
