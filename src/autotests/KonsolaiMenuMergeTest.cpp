/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QTemporaryDir>
#include <QTest>

#include <KXMLGUIFactory>

#include "../MainWindow.h"
#include "../ViewManager.h"
#include "../session/SessionController.h"

using namespace Konsole;

/**
 * Guards the XMLGUI merge invariant behind the duplicate-View-menu bug.
 *
 * Each SessionController is a KXMLGUIClient carrying sessionui.rc, whose
 * <MenuBar> section merges into the shell's View menu. Exactly one may be
 * merged at a time. When two are merged, both contribute their
 * session-view-operations group and every monitor/font/clear-history entry
 * appears twice.
 *
 * The old code decided whether to swap clients by comparing ACTION COUNTS
 * between the outgoing and incoming controller, and tracked "who is merged"
 * via _pluggedController. Skipping advanced _pluggedController without
 * touching the factory, so the two disagreed — and the next real swap removed
 * a client that had never been added while adding another, stranding the
 * first.
 */
class KonsolaiMenuMergeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testActiveControllerIsTheMergedOne();
};

namespace
{

/**
 * SessionControllers currently merged into this window's factory.
 *
 * Identified by the .rc rather than by RTTI. Both spellings count:
 * SessionController loads sessionui.rc normally and partui.rc when it believes
 * it is a KPart — which is what happens in a test binary, since the shell's
 * own .rc is named after the executable and is not found. The merge machinery
 * under test is the same either way; only the merged document differs.
 */
int mergedSessionClients(MainWindow &mw)
{
    if (!mw.guiFactory()) {
        return -1; // distinguishable from "none merged"
    }
    int n = 0;
    const auto clients = mw.guiFactory()->clients();
    for (const auto *c : clients) {
        const QString rc = c->xmlFile();
        if (rc == QLatin1String("sessionui.rc") || rc == QLatin1String("partui.rc")) {
            ++n;
        }
    }
    return n;
}

}

void KonsolaiMenuMergeTest::testActiveControllerIsTheMergedOne()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MainWindow mw;
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));
    auto *vm = mw.viewManager();
    QVERIFY(vm);

    // CONTROL: before any session exists, no session client is merged. If this
    // is already non-zero the counter is measuring the wrong thing and every
    // assertion below would be meaningless.
    QCOMPARE(mergedSessionClients(mw), 0);

    const QString profileName = vm->defaultProfile();
    vm->newSession(profileName, dir.path());

    auto *first = vm->activeViewController();
    QVERIFY(first);

    // activeViewChanged() has run and evaluated skipGuiFactorySwap. If that
    // condition is ever wrong in the "skip" direction, addClient() does not
    // happen and this window's menubar carries no session entries at all.
    QVERIFY2(first->factory() == mw.guiFactory(), "the active controller must be merged into THIS window's factory");

    // Exactly one merged session client. Two means every session entry renders
    // twice in View, which is the reported symptom.
    QCOMPARE(mergedSessionClients(mw), 1);

    // A second session must not add a second merged client.
    vm->newSession(profileName, dir.path());
    QCOMPARE(mergedSessionClients(mw), 1);

    // NOT COVERED HERE, deliberately, rather than papered over: the original
    // strand needed a tab SWITCH (activate A, activate B, activate C). This
    // harness would not drive one — setCurrentSession() leaves
    // activeViewController() unchanged even with the window shown and two
    // views present, because view activation wants real focus. The AT-SPI
    // suite is the instrument for that half: open View against a live
    // instance and assert no entry appears twice.
}

QTEST_MAIN(KonsolaiMenuMergeTest)
#include "KonsolaiMenuMergeTest.moc"
