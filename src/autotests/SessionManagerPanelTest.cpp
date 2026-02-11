/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "SessionManagerPanelTest.h"

// Qt
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/SessionManagerPanel.h"

using namespace Konsolai;

static QString sessionsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/sessions.json");
}

static void writeTestSessions(const QJsonArray &sessions)
{
    QString path = sessionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(sessions).toJson());
    file.close();
}

static QJsonObject makeSession(const QString &id, const QString &name, bool pinned = false, bool archived = false, bool expired = false)
{
    QJsonObject obj;
    obj[QStringLiteral("sessionId")] = id;
    obj[QStringLiteral("sessionName")] = name;
    obj[QStringLiteral("profileName")] = QStringLiteral("Test");
    obj[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/project");
    obj[QStringLiteral("isPinned")] = pinned;
    obj[QStringLiteral("isArchived")] = archived;
    obj[QStringLiteral("isExpired")] = expired;
    obj[QStringLiteral("lastAccessed")] = QStringLiteral("2025-06-01T12:00:00");
    obj[QStringLiteral("createdAt")] = QStringLiteral("2025-06-01T10:00:00");
    return obj;
}

void SessionManagerPanelTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void SessionManagerPanelTest::cleanupTestCase()
{
}

void SessionManagerPanelTest::cleanup()
{
    QFile::remove(sessionsFilePath());
}

// ============================================================
// Metadata filtering
// ============================================================

void SessionManagerPanelTest::testAllSessionsEmpty()
{
    SessionManagerPanel panel;
    // With no sessions file, should have no sessions
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testAllSessionsLoaded()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("aaa11111"), QStringLiteral("konsolai-test-aaa11111")));
    sessions.append(makeSession(QStringLiteral("bbb22222"), QStringLiteral("konsolai-test-bbb22222")));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.allSessions().size(), 2);
}

void SessionManagerPanelTest::testPinnedSessionsFilter()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("pin11111"), QStringLiteral("konsolai-test-pin11111"), true));
    sessions.append(makeSession(QStringLiteral("nop22222"), QStringLiteral("konsolai-test-nop22222"), false));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> pinned = panel.pinnedSessions();
    QCOMPARE(pinned.size(), 1);
    QCOMPARE(pinned[0].sessionId, QStringLiteral("pin11111"));
}

void SessionManagerPanelTest::testArchivedSessionsFilter()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("arc11111"), QStringLiteral("konsolai-test-arc11111"), false, true));
    sessions.append(makeSession(QStringLiteral("act22222"), QStringLiteral("konsolai-test-act22222"), false, false));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> archived = panel.archivedSessions();
    QCOMPARE(archived.size(), 1);
    QCOMPARE(archived[0].sessionId, QStringLiteral("arc11111"));
}

void SessionManagerPanelTest::testPinnedExcludesArchived()
{
    // A session that is both pinned and archived should NOT appear in pinnedSessions
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("both1111"), QStringLiteral("konsolai-test-both1111"), true, true));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.pinnedSessions().size(), 0);
    QCOMPARE(panel.archivedSessions().size(), 1);
}

// ============================================================
// Pin/Unpin
// ============================================================

void SessionManagerPanelTest::testPinSession()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("pin11111"), QStringLiteral("konsolai-test-pin11111"), false));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.pinnedSessions().size(), 0);

    panel.pinSession(QStringLiteral("pin11111"));
    QCOMPARE(panel.pinnedSessions().size(), 1);
}

void SessionManagerPanelTest::testUnpinSession()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("unp11111"), QStringLiteral("konsolai-test-unp11111"), true));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.pinnedSessions().size(), 1);

    panel.unpinSession(QStringLiteral("unp11111"));
    QCOMPARE(panel.pinnedSessions().size(), 0);
}

void SessionManagerPanelTest::testPinNonexistentSession()
{
    SessionManagerPanel panel;
    // Should be a no-op, not crash
    panel.pinSession(QStringLiteral("nonexistent"));
    QCOMPARE(panel.pinnedSessions().size(), 0);
}

// ============================================================
// Archive
// ============================================================

void SessionManagerPanelTest::testArchiveSession()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("arc11111"), QStringLiteral("konsolai-test-arc11111"), false, false));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.archivedSessions().size(), 0);

    panel.archiveSession(QStringLiteral("arc11111"));
    QCOMPARE(panel.archivedSessions().size(), 1);
    QVERIFY(panel.archivedSessions()[0].isArchived);
}

void SessionManagerPanelTest::testArchiveNonexistentSession()
{
    SessionManagerPanel panel;
    // Should be a no-op, not crash
    panel.archiveSession(QStringLiteral("nonexistent"));
    QCOMPARE(panel.archivedSessions().size(), 0);
}

// ============================================================
// Close (new feature)
// ============================================================

void SessionManagerPanelTest::testCloseSessionNotArchived()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("cls11111"), QStringLiteral("konsolai-test-cls11111"), false, false));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    panel.closeSession(QStringLiteral("cls11111"));

    // Should NOT be archived
    QCOMPARE(panel.archivedSessions().size(), 0);

    // Should still be in allSessions
    QCOMPARE(panel.allSessions().size(), 1);

    // Verify lastAccessed was updated (should be recent)
    QList<SessionMetadata> all = panel.allSessions();
    QVERIFY(all[0].lastAccessed.secsTo(QDateTime::currentDateTime()) < 5);
}

void SessionManagerPanelTest::testCloseNonexistentSession()
{
    SessionManagerPanel panel;
    // Should be a no-op, not crash
    panel.closeSession(QStringLiteral("nonexistent"));
    QCOMPARE(panel.allSessions().size(), 0);
}

// ============================================================
// Mark expired
// ============================================================

void SessionManagerPanelTest::testMarkExpired()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("exp11111"), QStringLiteral("konsolai-test-exp11111"), false, false, false));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    panel.markExpired(QStringLiteral("konsolai-test-exp11111"));

    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].isExpired);
    QVERIFY(!all[0].isArchived); // expired != archived
}

void SessionManagerPanelTest::testMarkExpiredUnknownSession()
{
    SessionManagerPanel panel;
    // Should be a no-op, not crash
    panel.markExpired(QStringLiteral("konsolai-nonexistent-12345678"));
}

// ============================================================
// Collapsed state
// ============================================================

void SessionManagerPanelTest::testCollapsedToggle()
{
    SessionManagerPanel panel;
    QVERIFY(!panel.isCollapsed());

    panel.setCollapsed(true);
    QVERIFY(panel.isCollapsed());

    panel.setCollapsed(false);
    QVERIFY(!panel.isCollapsed());
}

void SessionManagerPanelTest::testCollapsedSignal()
{
    SessionManagerPanel panel;
    QSignalSpy spy(&panel, &SessionManagerPanel::collapsedChanged);

    panel.setCollapsed(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    panel.setCollapsed(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void SessionManagerPanelTest::testCollapsedIdempotent()
{
    SessionManagerPanel panel;
    QSignalSpy spy(&panel, &SessionManagerPanel::collapsedChanged);

    panel.setCollapsed(false); // Already not collapsed
    QCOMPARE(spy.count(), 0); // No signal emitted
}

// ============================================================
// Metadata persistence round-trip
// ============================================================

void SessionManagerPanelTest::testMetadataPersistence()
{
    // Write, modify, save, re-read
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("per11111"), QStringLiteral("konsolai-test-per11111"), false));
    writeTestSessions(sessions);

    {
        SessionManagerPanel panel;
        panel.pinSession(QStringLiteral("per11111"));
        // Destructor calls saveMetadata()
    }

    // New panel should load the saved state
    SessionManagerPanel panel2;
    QCOMPARE(panel2.pinnedSessions().size(), 1);
    QCOMPARE(panel2.pinnedSessions()[0].sessionId, QStringLiteral("per11111"));
}

void SessionManagerPanelTest::testMetadataYoloPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("yol11111"), QStringLiteral("konsolai-test-yol11111"));
    s[QStringLiteral("yoloMode")] = true;
    s[QStringLiteral("doubleYoloMode")] = true;
    s[QStringLiteral("tripleYoloMode")] = false;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].yoloMode);
    QVERIFY(all[0].doubleYoloMode);
    QVERIFY(!all[0].tripleYoloMode);
}

void SessionManagerPanelTest::testMetadataSshFields()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("ssh11111"), QStringLiteral("konsolai-test-ssh11111"));
    s[QStringLiteral("isRemote")] = true;
    s[QStringLiteral("sshHost")] = QStringLiteral("dev.example.com");
    s[QStringLiteral("sshUsername")] = QStringLiteral("user");
    s[QStringLiteral("sshPort")] = 2222;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].isRemote);
    QCOMPARE(all[0].sshHost, QStringLiteral("dev.example.com"));
    QCOMPARE(all[0].sshUsername, QStringLiteral("user"));
    QCOMPARE(all[0].sshPort, 2222);
}

QTEST_MAIN(SessionManagerPanelTest)

#include "moc_SessionManagerPanelTest.cpp"
