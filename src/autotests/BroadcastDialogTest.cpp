/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BroadcastDialogTest.h"

#include <QAction>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTextEdit>
#include <QToolButton>

#include "../claude/BroadcastDialog.h"
#include "../claude/BroadcastPolicy.h"

using namespace Konsolai;

namespace
{

QList<BroadcastRecipient> threeRecipients()
{
    QList<BroadcastRecipient> r;
    BroadcastRecipient a;
    a.sessionId = QStringLiteral("alpha");
    a.displayName = QStringLiteral("Alpha One");
    a.workingDirectory = QStringLiteral("/home/u/cowir-pcc-base");
    a.tmuxSession = QStringLiteral("konsolai-x-alpha");
    a.category = QStringLiteral("cowir");
    r.append(a);

    BroadcastRecipient b;
    b.sessionId = QStringLiteral("beta");
    b.displayName = QStringLiteral("Beta Two");
    b.workingDirectory = QStringLiteral("/home/u/cowir-frontend");
    b.tmuxSession = QStringLiteral("konsolai-x-beta");
    b.category = QStringLiteral("cowir");
    r.append(b);

    BroadcastRecipient g;
    g.sessionId = QStringLiteral("gamma");
    g.displayName = QStringLiteral("Gamma Three");
    g.workingDirectory = QStringLiteral("/home/u/cowir-api");
    g.tmuxSession = QStringLiteral("konsolai-x-gamma");
    g.category = QStringLiteral("cowir");
    r.append(g);
    return r;
}

} // namespace

void BroadcastDialogTest::testPreviewUpdatesOnMessageEdit()
{
    BroadcastDialog dlg(threeRecipients(), nullptr);
    auto *msg = dlg.findChild<QPlainTextEdit *>(QStringLiteral("broadcastMessageEdit"));
    auto *prev = dlg.findChild<QTextEdit *>(QStringLiteral("broadcastPreviewView"));
    QVERIFY(msg);
    QVERIFY(prev);

    msg->setPlainText(QStringLiteral("Hello {session_name}!"));
    // QPlainTextEdit emits textChanged synchronously on setPlainText, but make
    // sure the preview pulled the latest text by reading it.
    QCOMPARE(prev->toPlainText(), QStringLiteral("Hello Alpha One!"));

    msg->setPlainText(QStringLiteral("Project: {project}"));
    QCOMPARE(prev->toPlainText(), QStringLiteral("Project: cowir"));
}

void BroadcastDialogTest::testPreviewUsesFirstCheckedRecipient()
{
    BroadcastDialog dlg(threeRecipients(), nullptr);
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("broadcastRecipientsList"));
    auto *msg = dlg.findChild<QPlainTextEdit *>(QStringLiteral("broadcastMessageEdit"));
    auto *prev = dlg.findChild<QTextEdit *>(QStringLiteral("broadcastPreviewView"));
    QVERIFY(list);
    QVERIFY(msg);
    QVERIFY(prev);
    QCOMPARE(list->count(), 3);

    msg->setPlainText(QStringLiteral("Hi {session_name}"));
    QCOMPARE(prev->toPlainText(), QStringLiteral("Hi Alpha One"));

    // Uncheck the first — preview must shift to Beta Two
    list->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(prev->toPlainText(), QStringLiteral("Hi Beta Two"));

    // Uncheck the second — preview must shift to Gamma Three
    list->item(1)->setCheckState(Qt::Unchecked);
    QCOMPARE(prev->toPlainText(), QStringLiteral("Hi Gamma Three"));
}

void BroadcastDialogTest::testBroadcastDisabledWhenNoneChecked()
{
    BroadcastDialog dlg(threeRecipients(), nullptr);
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("broadcastRecipientsList"));
    auto *confirm = dlg.findChild<QPushButton *>(QStringLiteral("broadcastConfirmButton"));
    QVERIFY(list);
    QVERIFY(confirm);

    // Initial state: all checked → broadcast enabled.
    QVERIFY(confirm->isEnabled());

    for (int i = 0; i < list->count(); ++i) {
        list->item(i)->setCheckState(Qt::Unchecked);
    }
    QVERIFY(!confirm->isEnabled());

    // Re-check one → broadcast re-enabled.
    list->item(2)->setCheckState(Qt::Checked);
    QVERIFY(confirm->isEnabled());
}

void BroadcastDialogTest::testHelpButtonInsertsVarAtCursor()
{
    BroadcastDialog dlg(threeRecipients(), nullptr);
    auto *help = dlg.findChild<QToolButton *>(QStringLiteral("broadcastHelpButton"));
    auto *msg = dlg.findChild<QPlainTextEdit *>(QStringLiteral("broadcastMessageEdit"));
    QVERIFY(help);
    QVERIFY(msg);
    QVERIFY(help->menu());

    msg->setPlainText(QStringLiteral("hello "));
    auto cursor = msg->textCursor();
    cursor.movePosition(QTextCursor::End);
    msg->setTextCursor(cursor);

    // Find the {project} action and trigger it.
    QAction *projectAction = nullptr;
    const auto actions = help->menu()->actions();
    for (auto *a : actions) {
        if (a->text() == QStringLiteral("{project}")) {
            projectAction = a;
            break;
        }
    }
    QVERIFY(projectAction);
    projectAction->trigger();

    QCOMPARE(msg->toPlainText(), QStringLiteral("hello {project}"));
}

void BroadcastDialogTest::testSelectedSessionIdsExcludesUnchecked()
{
    BroadcastDialog dlg(threeRecipients(), nullptr);
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("broadcastRecipientsList"));
    QVERIFY(list);
    QCOMPARE(dlg.selectedSessionIds(), QStringList({QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}));

    list->item(1)->setCheckState(Qt::Unchecked); // uncheck Beta
    QCOMPARE(dlg.selectedSessionIds(), QStringList({QStringLiteral("alpha"), QStringLiteral("gamma")}));

    list->item(0)->setCheckState(Qt::Unchecked); // uncheck Alpha
    QCOMPARE(dlg.selectedSessionIds(), QStringList({QStringLiteral("gamma")}));
}

void BroadcastDialogTest::testReturnsTemplateUnsubstituted()
{
    BroadcastDialog dlg(threeRecipients(), nullptr);
    auto *msg = dlg.findChild<QPlainTextEdit *>(QStringLiteral("broadcastMessageEdit"));
    QVERIFY(msg);

    const QString tmpl = QStringLiteral("Hello {session_name} — proj {project}");
    msg->setPlainText(tmpl);

    // messageTemplate() returns the RAW template — not the previewed text.
    QCOMPARE(dlg.messageTemplate(), tmpl);

    // substitutedMessages() returns one per selected (all 3 by default).
    // Note: the em-dash in tmpl is a literal U+2014 (3-byte UTF-8). We compare
    // against the same em-dash, not the raw byte sequence — `\xNN` escapes in
    // QStringLiteral are interpreted as Latin-1 codepoints, not UTF-8 bytes.
    const QStringList subs = dlg.substitutedMessages();
    QCOMPARE(subs.size(), 3);
    QCOMPARE(subs[0], QStringLiteral("Hello Alpha One — proj cowir"));
    QCOMPARE(subs[1], QStringLiteral("Hello Beta Two — proj cowir"));
    QCOMPARE(subs[2], QStringLiteral("Hello Gamma Three — proj cowir"));
}

QTEST_MAIN(BroadcastDialogTest)
#include "moc_BroadcastDialogTest.cpp"
