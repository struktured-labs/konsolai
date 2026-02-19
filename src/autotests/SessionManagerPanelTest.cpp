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

// ============================================================
// Dismiss / Restore / Purge lifecycle
// ============================================================

void SessionManagerPanelTest::testDismissSession()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("dis11111"), QStringLiteral("konsolai-test-dis11111"), false, true));
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.archivedSessions().size(), 1);

    panel.dismissSession(QStringLiteral("dis11111"));

    // Still isArchived=true, but now also isDismissed=true
    // archivedSessions() returns all isArchived regardless of isDismissed
    QCOMPARE(panel.archivedSessions().size(), 1);
    QCOMPARE(panel.allSessions().size(), 1);
    QVERIFY(panel.allSessions()[0].isDismissed);
    QVERIFY(panel.allSessions()[0].isArchived);
}

void SessionManagerPanelTest::testDismissNonexistentSession()
{
    SessionManagerPanel panel;
    panel.dismissSession(QStringLiteral("nonexistent"));
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testRestoreSession()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("rst11111"), QStringLiteral("konsolai-test-rst11111"), false, true);
    s[QStringLiteral("isDismissed")] = true;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    // archivedSessions() includes dismissed (isArchived is still true)
    QCOMPARE(panel.archivedSessions().size(), 1);
    QVERIFY(panel.allSessions()[0].isDismissed);

    panel.restoreSession(QStringLiteral("rst11111"));

    // Should no longer be dismissed, still archived
    QCOMPARE(panel.archivedSessions().size(), 1);
    QVERIFY(!panel.archivedSessions()[0].isDismissed);
    QVERIFY(panel.archivedSessions()[0].isArchived);
}

void SessionManagerPanelTest::testRestoreNonexistentSession()
{
    SessionManagerPanel panel;
    panel.restoreSession(QStringLiteral("nonexistent"));
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testPurgeSession()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("prg11111"), QStringLiteral("konsolai-test-prg11111"), false, true);
    s[QStringLiteral("isDismissed")] = true;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.allSessions().size(), 1);

    panel.purgeSession(QStringLiteral("prg11111"));
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testPurgeNonexistentSession()
{
    SessionManagerPanel panel;
    panel.purgeSession(QStringLiteral("nonexistent"));
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testPurgeDismissed()
{
    QJsonArray sessions;
    QJsonObject s1 = makeSession(QStringLiteral("pd111111"), QStringLiteral("konsolai-test-pd111111"), false, true);
    s1[QStringLiteral("isDismissed")] = true;
    QJsonObject s2 = makeSession(QStringLiteral("pd222222"), QStringLiteral("konsolai-test-pd222222"), false, true);
    s2[QStringLiteral("isDismissed")] = true;
    QJsonObject s3 = makeSession(QStringLiteral("pd333333"), QStringLiteral("konsolai-test-pd333333"), false, true);
    // s3 is archived but NOT dismissed
    sessions.append(s1);
    sessions.append(s2);
    sessions.append(s3);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QCOMPARE(panel.allSessions().size(), 3);

    panel.purgeDismissed();

    // Only the non-dismissed one should remain
    QCOMPARE(panel.allSessions().size(), 1);
    QCOMPARE(panel.allSessions()[0].sessionId, QStringLiteral("pd333333"));
}

void SessionManagerPanelTest::testDismissRestorePurgeRoundTrip()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("rnd11111"), QStringLiteral("konsolai-test-rnd11111"), false, true));
    writeTestSessions(sessions);

    SessionManagerPanel panel;

    // Start: archived
    QCOMPARE(panel.archivedSessions().size(), 1);

    // Dismiss — still in archivedSessions() since isArchived stays true
    panel.dismissSession(QStringLiteral("rnd11111"));
    QCOMPARE(panel.archivedSessions().size(), 1);
    QVERIFY(panel.allSessions()[0].isDismissed);

    // Restore
    panel.restoreSession(QStringLiteral("rnd11111"));
    QCOMPARE(panel.archivedSessions().size(), 1);
    QVERIFY(!panel.allSessions()[0].isDismissed);

    // Dismiss again and purge
    panel.dismissSession(QStringLiteral("rnd11111"));
    panel.purgeSession(QStringLiteral("rnd11111"));
    QCOMPARE(panel.allSessions().size(), 0);
}

// ============================================================
// Additional metadata persistence tests
// ============================================================

void SessionManagerPanelTest::testMetadataBudgetPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("bgt11111"), QStringLiteral("konsolai-test-bgt11111"));
    s[QStringLiteral("budgetTimeLimitMinutes")] = 60;
    s[QStringLiteral("budgetCostCeilingUSD")] = 5.50;
    s[QStringLiteral("budgetTokenCeiling")] = 100000;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].budgetTimeLimitMinutes, 60);
    QCOMPARE(all[0].budgetCostCeilingUSD, 5.50);
    QCOMPARE(all[0].budgetTokenCeiling, static_cast<quint64>(100000));
}

void SessionManagerPanelTest::testMetadataCorruptedJson()
{
    // Write garbage to the sessions file
    QString path = sessionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not valid json {{{");
    file.close();

    // Panel should handle gracefully — no sessions, no crash
    SessionManagerPanel panel;
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testMetadataMissingFields()
{
    // Session with minimal fields (missing many optional ones)
    QJsonArray sessions;
    QJsonObject s;
    s[QStringLiteral("sessionId")] = QStringLiteral("min11111");
    s[QStringLiteral("sessionName")] = QStringLiteral("konsolai-test-min11111");
    // No workingDirectory, no isPinned, no isArchived, etc.
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].sessionId, QStringLiteral("min11111"));
    // Defaults should be applied
    QVERIFY(!all[0].isPinned);
    QVERIFY(!all[0].isArchived);
    QVERIFY(!all[0].isDismissed);
    QVERIFY(!all[0].isRemote);
    QVERIFY(!all[0].yoloMode);
    QCOMPARE(all[0].budgetTimeLimitMinutes, 0);
    QCOMPARE(all[0].budgetCostCeilingUSD, 0.0);
    QCOMPARE(all[0].budgetTokenCeiling, static_cast<quint64>(0));
}

void SessionManagerPanelTest::testMetadataApprovalCountPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("apv11111"), QStringLiteral("konsolai-test-apv11111"));
    s[QStringLiteral("yoloApprovalCount")] = 42;
    s[QStringLiteral("doubleYoloApprovalCount")] = 7;
    s[QStringLiteral("tripleYoloApprovalCount")] = 3;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].yoloApprovalCount, 42);
    QCOMPARE(all[0].doubleYoloApprovalCount, 7);
    QCOMPARE(all[0].tripleYoloApprovalCount, 3);
}

// ============================================================
// Full round-trip with ALL fields
// ============================================================

void SessionManagerPanelTest::testMetadataAllFieldsRoundTrip()
{
    // Create a session with EVERY field populated
    QJsonArray sessions;
    QJsonObject s;
    s[QStringLiteral("sessionId")] = QStringLiteral("all11111");
    s[QStringLiteral("sessionName")] = QStringLiteral("konsolai-test-all11111");
    s[QStringLiteral("profileName")] = QStringLiteral("FullProfile");
    s[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/big-project");
    s[QStringLiteral("isPinned")] = true;
    s[QStringLiteral("isArchived")] = false;
    s[QStringLiteral("isExpired")] = false;
    s[QStringLiteral("isDismissed")] = false;
    s[QStringLiteral("lastAccessed")] = QStringLiteral("2025-12-25T23:59:59");
    s[QStringLiteral("createdAt")] = QStringLiteral("2025-01-01T00:00:00");
    // SSH fields
    s[QStringLiteral("isRemote")] = true;
    s[QStringLiteral("sshHost")] = QStringLiteral("prod.example.com");
    s[QStringLiteral("sshUsername")] = QStringLiteral("deployer");
    s[QStringLiteral("sshPort")] = 2222;
    // Yolo fields
    s[QStringLiteral("yoloMode")] = true;
    s[QStringLiteral("doubleYoloMode")] = true;
    s[QStringLiteral("tripleYoloMode")] = true;
    // Approval counts
    s[QStringLiteral("yoloApprovalCount")] = 100;
    s[QStringLiteral("doubleYoloApprovalCount")] = 50;
    s[QStringLiteral("tripleYoloApprovalCount")] = 25;
    // Budget fields
    s[QStringLiteral("budgetTimeLimitMinutes")] = 120;
    s[QStringLiteral("budgetCostCeilingUSD")] = 10.99;
    s[QStringLiteral("budgetTokenCeiling")] = 500000;
    sessions.append(s);
    writeTestSessions(sessions);

    // Load, modify, save, and reload
    {
        SessionManagerPanel panel;
        QList<SessionMetadata> all = panel.allSessions();
        QCOMPARE(all.size(), 1);

        const auto &m = all[0];
        // Verify all fields loaded correctly
        QCOMPARE(m.sessionId, QStringLiteral("all11111"));
        QCOMPARE(m.sessionName, QStringLiteral("konsolai-test-all11111"));
        QCOMPARE(m.profileName, QStringLiteral("FullProfile"));
        QCOMPARE(m.workingDirectory, QStringLiteral("/home/user/big-project"));
        QVERIFY(m.isPinned);
        QVERIFY(!m.isArchived);
        QVERIFY(!m.isExpired);
        QVERIFY(!m.isDismissed);
        QVERIFY(m.isRemote);
        QCOMPARE(m.sshHost, QStringLiteral("prod.example.com"));
        QCOMPARE(m.sshUsername, QStringLiteral("deployer"));
        QCOMPARE(m.sshPort, 2222);
        QVERIFY(m.yoloMode);
        QVERIFY(m.doubleYoloMode);
        QVERIFY(m.tripleYoloMode);
        QCOMPARE(m.yoloApprovalCount, 100);
        QCOMPARE(m.doubleYoloApprovalCount, 50);
        QCOMPARE(m.tripleYoloApprovalCount, 25);
        QCOMPARE(m.budgetTimeLimitMinutes, 120);
        QCOMPARE(m.budgetCostCeilingUSD, 10.99);
        QCOMPARE(m.budgetTokenCeiling, static_cast<quint64>(500000));

        // Panel destructor saves metadata
    }

    // Reload and verify persistence
    {
        SessionManagerPanel panel2;
        QList<SessionMetadata> all = panel2.allSessions();
        QCOMPARE(all.size(), 1);

        const auto &m = all[0];
        QCOMPARE(m.sessionId, QStringLiteral("all11111"));
        QVERIFY(m.isPinned);
        QVERIFY(m.isRemote);
        QCOMPARE(m.sshHost, QStringLiteral("prod.example.com"));
        QCOMPARE(m.sshPort, 2222);
        QVERIFY(m.yoloMode);
        QVERIFY(m.doubleYoloMode);
        QVERIFY(m.tripleYoloMode);
        QCOMPARE(m.yoloApprovalCount, 100);
        QCOMPARE(m.doubleYoloApprovalCount, 50);
        QCOMPARE(m.tripleYoloApprovalCount, 25);
        QCOMPARE(m.budgetTimeLimitMinutes, 120);
        QCOMPARE(m.budgetCostCeilingUSD, 10.99);
        QCOMPARE(m.budgetTokenCeiling, static_cast<quint64>(500000));
    }
}

void SessionManagerPanelTest::testMetadataApprovalLogRoundTrip()
{
    // Create session with approval log entries
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("log11111"), QStringLiteral("konsolai-test-log11111"));

    QJsonArray logEntries;
    {
        QJsonObject entry;
        entry[QStringLiteral("time")] = QStringLiteral("2025-06-15T10:00:00");
        entry[QStringLiteral("tool")] = QStringLiteral("Bash");
        entry[QStringLiteral("action")] = QStringLiteral("auto-approved");
        entry[QStringLiteral("level")] = 1;
        entry[QStringLiteral("tokens")] = 5000;
        entry[QStringLiteral("cost")] = 0.05;
        logEntries.append(entry);
    }
    {
        QJsonObject entry;
        entry[QStringLiteral("time")] = QStringLiteral("2025-06-15T10:01:00");
        entry[QStringLiteral("tool")] = QStringLiteral("suggestion");
        entry[QStringLiteral("action")] = QStringLiteral("auto-accepted");
        entry[QStringLiteral("level")] = 2;
        entry[QStringLiteral("tokens")] = 10000;
        entry[QStringLiteral("cost")] = 0.10;
        logEntries.append(entry);
    }

    s[QStringLiteral("approvalLog")] = logEntries;
    s[QStringLiteral("yoloApprovalCount")] = 1;
    s[QStringLiteral("doubleYoloApprovalCount")] = 1;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel panel;
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].yoloApprovalCount, 1);
    QCOMPARE(all[0].doubleYoloApprovalCount, 1);
    QCOMPARE(all[0].approvalLog.size(), 2);

    // Verify first entry
    const auto &e1 = all[0].approvalLog[0];
    QCOMPARE(e1.toolName, QStringLiteral("Bash"));
    QCOMPARE(e1.action, QStringLiteral("auto-approved"));
    QCOMPARE(e1.yoloLevel, 1);
    QCOMPARE(e1.totalTokens, quint64(5000));
    QCOMPARE(e1.estimatedCostUSD, 0.05);

    // Verify second entry
    const auto &e2 = all[0].approvalLog[1];
    QCOMPARE(e2.toolName, QStringLiteral("suggestion"));
    QCOMPARE(e2.yoloLevel, 2);
}

void SessionManagerPanelTest::testMetadataMultipleSessionsRoundTrip()
{
    // Test that multiple sessions with different field combinations all persist correctly
    QJsonArray sessions;

    // Session 1: pinned, local, no yolo
    QJsonObject s1 = makeSession(QStringLiteral("mul11111"), QStringLiteral("konsolai-test-mul11111"), true, false);

    // Session 2: archived, remote, yolo enabled
    QJsonObject s2 = makeSession(QStringLiteral("mul22222"), QStringLiteral("konsolai-test-mul22222"), false, true);
    s2[QStringLiteral("isRemote")] = true;
    s2[QStringLiteral("sshHost")] = QStringLiteral("dev.example.com");
    s2[QStringLiteral("yoloMode")] = true;

    // Session 3: dismissed, with budget
    QJsonObject s3 = makeSession(QStringLiteral("mul33333"), QStringLiteral("konsolai-test-mul33333"), false, true);
    s3[QStringLiteral("isDismissed")] = true;
    s3[QStringLiteral("budgetTimeLimitMinutes")] = 30;
    s3[QStringLiteral("budgetCostCeilingUSD")] = 2.50;

    sessions.append(s1);
    sessions.append(s2);
    sessions.append(s3);
    writeTestSessions(sessions);

    {
        SessionManagerPanel panel;
        QList<SessionMetadata> all = panel.allSessions();
        QCOMPARE(all.size(), 3);
        // Panel destructor saves
    }

    // Reload
    SessionManagerPanel panel2;
    QList<SessionMetadata> all = panel2.allSessions();
    QCOMPARE(all.size(), 3);

    // Find each session and verify
    SessionMetadata *m1 = nullptr;
    SessionMetadata *m2 = nullptr;
    SessionMetadata *m3 = nullptr;
    for (auto &m : all) {
        if (m.sessionId == QStringLiteral("mul11111"))
            m1 = &m;
        else if (m.sessionId == QStringLiteral("mul22222"))
            m2 = &m;
        else if (m.sessionId == QStringLiteral("mul33333"))
            m3 = &m;
    }

    QVERIFY(m1);
    QVERIFY(m1->isPinned);
    QVERIFY(!m1->isRemote);

    QVERIFY(m2);
    QVERIFY(m2->isArchived);
    QVERIFY(m2->isRemote);
    QCOMPARE(m2->sshHost, QStringLiteral("dev.example.com"));
    QVERIFY(m2->yoloMode);

    QVERIFY(m3);
    QVERIFY(m3->isDismissed);
    QCOMPARE(m3->budgetTimeLimitMinutes, 30);
    QCOMPARE(m3->budgetCostCeilingUSD, 2.50);
}

void SessionManagerPanelTest::testMetadataSaveLoadIdempotent()
{
    // Save, load, save again — result should be identical
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("idem1111"), QStringLiteral("konsolai-test-idem1111"), true, false);
    s[QStringLiteral("yoloMode")] = true;
    s[QStringLiteral("budgetTimeLimitMinutes")] = 45;
    sessions.append(s);
    writeTestSessions(sessions);

    // First load & save
    {
        SessionManagerPanel panel;
        QCOMPARE(panel.allSessions().size(), 1);
    }

    // Read file after first save
    QFile file1(sessionsFilePath());
    QVERIFY(file1.open(QIODevice::ReadOnly));
    QByteArray data1 = file1.readAll();
    file1.close();

    // Second load & save
    {
        SessionManagerPanel panel;
        QCOMPARE(panel.allSessions().size(), 1);
    }

    // Read file after second save
    QFile file2(sessionsFilePath());
    QVERIFY(file2.open(QIODevice::ReadOnly));
    QByteArray data2 = file2.readAll();
    file2.close();

    // Parse both and compare session count and key fields (exact byte comparison
    // may differ due to JSON key ordering or whitespace)
    QJsonDocument doc1 = QJsonDocument::fromJson(data1);
    QJsonDocument doc2 = QJsonDocument::fromJson(data2);
    QVERIFY(!doc1.isNull());
    QVERIFY(!doc2.isNull());

    QJsonArray arr1 = doc1.array();
    QJsonArray arr2 = doc2.array();
    QCOMPARE(arr1.size(), arr2.size());
    QCOMPARE(arr1.size(), 1);

    QJsonObject o1 = arr1[0].toObject();
    QJsonObject o2 = arr2[0].toObject();
    QCOMPARE(o1.value(QStringLiteral("sessionId")).toString(), o2.value(QStringLiteral("sessionId")).toString());
    QCOMPARE(o1.value(QStringLiteral("isPinned")).toBool(), o2.value(QStringLiteral("isPinned")).toBool());
    QCOMPARE(o1.value(QStringLiteral("yoloMode")).toBool(), o2.value(QStringLiteral("yoloMode")).toBool());
    QCOMPARE(o1.value(QStringLiteral("budgetTimeLimitMinutes")).toInt(), o2.value(QStringLiteral("budgetTimeLimitMinutes")).toInt());
}

QTEST_MAIN(SessionManagerPanelTest)

#include "moc_SessionManagerPanelTest.cpp"
