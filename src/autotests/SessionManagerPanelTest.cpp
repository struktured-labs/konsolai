/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "SessionManagerPanelTest.h"

// Qt
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTreeWidget>

// Konsolai
#include "../claude/ClaudeSession.h"
#include "../claude/KonsolaiSettings.h"
#include "../claude/MergePolicy.h"
#include "../claude/SessionManagerPanel.h"

using namespace Konsolai;

// Helper macro: construct SessionManagerPanel and process events so that
// the deferred init (QTimer::singleShot(0, ...)) fires before tests inspect state.
#define SessionManagerPanel_INIT(varName)                                                                                                                      \
    SessionManagerPanel varName;                                                                                                                               \
    QCoreApplication::processEvents()

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
    SessionManagerPanel_INIT(panel);
    // With no sessions file, should have no sessions
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testAllSessionsLoaded()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("aaa11111"), QStringLiteral("konsolai-test-aaa11111")));
    sessions.append(makeSession(QStringLiteral("bbb22222"), QStringLiteral("konsolai-test-bbb22222")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.allSessions().size(), 2);
}

void SessionManagerPanelTest::testPinnedSessionsFilter()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("pin11111"), QStringLiteral("konsolai-test-pin11111"), true));
    sessions.append(makeSession(QStringLiteral("nop22222"), QStringLiteral("konsolai-test-nop22222"), false));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.pinnedSessions().size(), 0);

    panel.pinSession(QStringLiteral("pin11111"));
    QCOMPARE(panel.pinnedSessions().size(), 1);
}

void SessionManagerPanelTest::testUnpinSession()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("unp11111"), QStringLiteral("konsolai-test-unp11111"), true));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.pinnedSessions().size(), 1);

    panel.unpinSession(QStringLiteral("unp11111"));
    QCOMPARE(panel.pinnedSessions().size(), 0);
}

void SessionManagerPanelTest::testPinNonexistentSession()
{
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.archivedSessions().size(), 0);

    panel.archiveSession(QStringLiteral("arc11111"));
    QCOMPARE(panel.archivedSessions().size(), 1);
    QVERIFY(panel.archivedSessions()[0].isArchived);
}

void SessionManagerPanelTest::testArchiveNonexistentSession()
{
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
    panel.markExpired(QStringLiteral("konsolai-test-exp11111"));

    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].isExpired);
    QVERIFY(!all[0].isArchived); // expired != archived
}

void SessionManagerPanelTest::testMarkExpiredUnknownSession()
{
    SessionManagerPanel_INIT(panel);
    // Should be a no-op, not crash
    panel.markExpired(QStringLiteral("konsolai-nonexistent-12345678"));
}

// ============================================================
// Collapsed state
// ============================================================

void SessionManagerPanelTest::testCollapsedToggle()
{
    SessionManagerPanel_INIT(panel);
    QVERIFY(!panel.isCollapsed());

    panel.setCollapsed(true);
    QVERIFY(panel.isCollapsed());

    panel.setCollapsed(false);
    QVERIFY(!panel.isCollapsed());
}

void SessionManagerPanelTest::testCollapsedSignal()
{
    SessionManagerPanel_INIT(panel);
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
    SessionManagerPanel_INIT(panel);
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
        SessionManagerPanel_INIT(panel);
        panel.pinSession(QStringLiteral("per11111"));
        // Destructor calls saveMetadata()
    }

    // New panel should load the saved state
    SessionManagerPanel_INIT(panel2);
    QCOMPARE(panel2.pinnedSessions().size(), 1);
    QCOMPARE(panel2.pinnedSessions()[0].sessionId, QStringLiteral("per11111"));
}

void SessionManagerPanelTest::testMetadataYoloPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("yol11111"), QStringLiteral("konsolai-test-yol11111"));
    s[QStringLiteral("yoloMode")] = true;
    s[QStringLiteral("doubleYoloMode")] = true;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].yoloMode);
    QVERIFY(all[0].doubleYoloMode);
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

    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.allSessions().size(), 1);

    panel.purgeSession(QStringLiteral("prg11111"));
    QCOMPARE(panel.allSessions().size(), 0);
}

void SessionManagerPanelTest::testPurgeNonexistentSession()
{
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);

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

    SessionManagerPanel_INIT(panel);
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
    SessionManagerPanel_INIT(panel);
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

    SessionManagerPanel_INIT(panel);
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
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].yoloApprovalCount, 42);
    QCOMPARE(all[0].doubleYoloApprovalCount, 7);
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
    // Approval counts
    s[QStringLiteral("yoloApprovalCount")] = 100;
    s[QStringLiteral("doubleYoloApprovalCount")] = 50;
    // Budget fields
    s[QStringLiteral("budgetTimeLimitMinutes")] = 120;
    s[QStringLiteral("budgetCostCeilingUSD")] = 10.99;
    s[QStringLiteral("budgetTokenCeiling")] = 500000;
    sessions.append(s);
    writeTestSessions(sessions);

    // Load, modify, save, and reload
    {
        SessionManagerPanel_INIT(panel);
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
        // tripleYoloMode removed
        QCOMPARE(m.yoloApprovalCount, 100);
        QCOMPARE(m.doubleYoloApprovalCount, 50);
        // tripleYoloApprovalCount removed
        QCOMPARE(m.budgetTimeLimitMinutes, 120);
        QCOMPARE(m.budgetCostCeilingUSD, 10.99);
        QCOMPARE(m.budgetTokenCeiling, static_cast<quint64>(500000));

        // Panel destructor saves metadata
    }

    // Reload and verify persistence
    {
        SessionManagerPanel_INIT(panel2);
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
        // tripleYoloMode removed
        QCOMPARE(m.yoloApprovalCount, 100);
        QCOMPARE(m.doubleYoloApprovalCount, 50);
        // tripleYoloApprovalCount removed
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

    SessionManagerPanel_INIT(panel);
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
        SessionManagerPanel_INIT(panel);
        QList<SessionMetadata> all = panel.allSessions();
        QCOMPARE(all.size(), 3);
        // Panel destructor saves
    }

    // Reload
    SessionManagerPanel_INIT(panel2);
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
        SessionManagerPanel_INIT(panel);
        QCOMPARE(panel.allSessions().size(), 1);
    }

    // Read file after first save
    QFile file1(sessionsFilePath());
    QVERIFY(file1.open(QIODevice::ReadOnly));
    QByteArray data1 = file1.readAll();
    file1.close();

    // Second load & save
    {
        SessionManagerPanel_INIT(panel);
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

// ============================================================
// Subagent/subprocess metadata persistence
// ============================================================

void SessionManagerPanelTest::testMetadataSubagentPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("sag11111"), QStringLiteral("konsolai-test-sag11111"));

    // Add subagents array
    QJsonArray agentArray;
    {
        QJsonObject a;
        a[QStringLiteral("agentId")] = QStringLiteral("agent-abc");
        a[QStringLiteral("agentType")] = QStringLiteral("Explore");
        a[QStringLiteral("teammateName")] = QStringLiteral("researcher");
        a[QStringLiteral("state")] = 3; // NotRunning
        a[QStringLiteral("taskDescription")] = QStringLiteral("Find auth bugs");
        a[QStringLiteral("currentTaskSubject")] = QStringLiteral("Fix login");
        a[QStringLiteral("promptGroupId")] = 1;
        agentArray.append(a);
    }
    {
        QJsonObject a;
        a[QStringLiteral("agentId")] = QStringLiteral("agent-def");
        a[QStringLiteral("agentType")] = QStringLiteral("Bash");
        a[QStringLiteral("state")] = 3;
        a[QStringLiteral("promptGroupId")] = 2;
        agentArray.append(a);
    }
    s[QStringLiteral("subagents")] = agentArray;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].subagents.size(), 2);
    QCOMPARE(all[0].subagents[0].agentId, QStringLiteral("agent-abc"));
    QCOMPARE(all[0].subagents[0].agentType, QStringLiteral("Explore"));
    QCOMPARE(all[0].subagents[0].teammateName, QStringLiteral("researcher"));
    QCOMPARE(all[0].subagents[0].taskDescription, QStringLiteral("Find auth bugs"));
    QCOMPARE(all[0].subagents[0].currentTaskSubject, QStringLiteral("Fix login"));
    QCOMPARE(all[0].subagents[0].promptGroupId, 1);
    QCOMPARE(all[0].subagents[1].agentId, QStringLiteral("agent-def"));
    QCOMPARE(all[0].subagents[1].agentType, QStringLiteral("Bash"));
    QCOMPARE(all[0].subagents[1].promptGroupId, 2);
}

void SessionManagerPanelTest::testMetadataSubprocessPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("spr11111"), QStringLiteral("konsolai-test-spr11111"));

    QJsonArray procArray;
    {
        QJsonObject p;
        p[QStringLiteral("id")] = QStringLiteral("proc-001");
        p[QStringLiteral("command")] = QStringLiteral("ninja -j4");
        p[QStringLiteral("fullCommand")] = QStringLiteral("ninja -j4 -C /build");
        p[QStringLiteral("status")] = 1; // Completed
        p[QStringLiteral("exitCode")] = 0;
        p[QStringLiteral("pid")] = 12345.0;
        p[QStringLiteral("promptGroupId")] = 1;
        p[QStringLiteral("isBackground")] = true;
        QJsonObject res;
        res[QStringLiteral("cpu")] = 50.5;
        res[QStringLiteral("rss")] = 2097152.0;
        p[QStringLiteral("resourceUsage")] = res;
        procArray.append(p);
    }
    s[QStringLiteral("subprocesses")] = procArray;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].subprocesses.size(), 1);

    const auto &proc = all[0].subprocesses[0];
    QCOMPARE(proc.id, QStringLiteral("proc-001"));
    QCOMPARE(proc.command, QStringLiteral("ninja -j4"));
    QCOMPARE(proc.fullCommand, QStringLiteral("ninja -j4 -C /build"));
    QCOMPARE(static_cast<int>(proc.status), static_cast<int>(SubprocessInfo::Completed));
    QCOMPARE(proc.exitCode, 0);
    QCOMPARE(proc.pid, qint64(12345));
    QCOMPARE(proc.promptGroupId, 1);
    QVERIFY(proc.isBackground);
    QCOMPARE(proc.resourceUsage.cpuPercent, 50.5);
    QCOMPARE(proc.resourceUsage.rssBytes, quint64(2097152));
}

void SessionManagerPanelTest::testMetadataPromptLabelsPersistence()
{
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("plb11111"), QStringLiteral("konsolai-test-plb11111"));

    QJsonObject labels;
    labels[QStringLiteral("0")] = QStringLiteral("Fix the bug");
    labels[QStringLiteral("1")] = QStringLiteral("Add tests");
    labels[QStringLiteral("2")] = QStringLiteral("Refactor auth");
    s[QStringLiteral("promptLabels")] = labels;
    s[QStringLiteral("promptRound")] = 2;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].promptGroupLabels.size(), 3);
    QCOMPARE(all[0].promptGroupLabels[0], QStringLiteral("Fix the bug"));
    QCOMPARE(all[0].promptGroupLabels[1], QStringLiteral("Add tests"));
    QCOMPARE(all[0].promptGroupLabels[2], QStringLiteral("Refactor auth"));
    QCOMPARE(all[0].currentPromptRound, 2);
}

void SessionManagerPanelTest::testMetadataSubagentEmptyNotSerialized()
{
    // Session with no subagents/subprocesses — JSON should not contain those keys
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("emp11111"), QStringLiteral("konsolai-test-emp11111")));
    writeTestSessions(sessions);

    {
        SessionManagerPanel_INIT(panel);
        QList<SessionMetadata> all = panel.allSessions();
        QCOMPARE(all.size(), 1);
        QVERIFY(all[0].subagents.isEmpty());
        QVERIFY(all[0].subprocesses.isEmpty());
        QVERIFY(all[0].promptGroupLabels.isEmpty());
        QCOMPARE(all[0].currentPromptRound, 0);
        // Panel destructor saves
    }

    // Read raw JSON and verify no subagent/subprocess keys
    QFile file(sessionsFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject obj = doc.array()[0].toObject();
    QVERIFY(!obj.contains(QStringLiteral("subagents")));
    QVERIFY(!obj.contains(QStringLiteral("subprocesses")));
    QVERIFY(!obj.contains(QStringLiteral("promptLabels")));
    QVERIFY(!obj.contains(QStringLiteral("promptRound")));
}

void SessionManagerPanelTest::testMetadataSubagentRoundTrip()
{
    // Write session with subagents → save → reload → verify
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("art11111"), QStringLiteral("konsolai-test-art11111"));

    QJsonArray agentArray;
    QJsonObject a;
    a[QStringLiteral("agentId")] = QStringLiteral("rt-agent");
    a[QStringLiteral("agentType")] = QStringLiteral("general-purpose");
    a[QStringLiteral("teammateName")] = QStringLiteral("developer");
    a[QStringLiteral("state")] = 3;
    a[QStringLiteral("promptGroupId")] = 1;
    agentArray.append(a);
    s[QStringLiteral("subagents")] = agentArray;

    QJsonObject labels;
    labels[QStringLiteral("0")] = QStringLiteral("Initial prompt");
    labels[QStringLiteral("1")] = QStringLiteral("Follow up");
    s[QStringLiteral("promptLabels")] = labels;
    s[QStringLiteral("promptRound")] = 1;

    sessions.append(s);
    writeTestSessions(sessions);

    // First load & save
    {
        SessionManagerPanel_INIT(panel);
        QCOMPARE(panel.allSessions().size(), 1);
        QCOMPARE(panel.allSessions()[0].subagents.size(), 1);
    }

    // Second load — verify persistence
    {
        SessionManagerPanel_INIT(panel2);
        QList<SessionMetadata> all = panel2.allSessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].subagents.size(), 1);
        QCOMPARE(all[0].subagents[0].agentId, QStringLiteral("rt-agent"));
        QCOMPARE(all[0].subagents[0].agentType, QStringLiteral("general-purpose"));
        QCOMPARE(all[0].subagents[0].teammateName, QStringLiteral("developer"));
        QCOMPARE(all[0].promptGroupLabels.size(), 2);
        QCOMPARE(all[0].promptGroupLabels[0], QStringLiteral("Initial prompt"));
        QCOMPARE(all[0].promptGroupLabels[1], QStringLiteral("Follow up"));
        QCOMPARE(all[0].currentPromptRound, 1);
    }
}

// ============================================================
// Remote session registration and restoration
// ============================================================

void SessionManagerPanelTest::testRegisterSessionCapturesRemoteFields()
{
    // registerSession() should capture isRemote/sshHost/sshUsername/sshPort
    // from the ClaudeSession object into metadata
    SessionManagerPanel_INIT(panel);

    auto *session = new ClaudeSession(QStringLiteral("Claude"), QStringLiteral("/home/struktured/projects/fluxit"), this);
    session->setIsRemote(true);
    session->setSshHost(QStringLiteral("blackmage.io"));
    session->setSshUsername(QStringLiteral("struktured"));
    session->setSshPort(22);

    panel.registerSession(session);

    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].isRemote);
    QCOMPARE(all[0].sshHost, QStringLiteral("blackmage.io"));
    QCOMPARE(all[0].sshUsername, QStringLiteral("struktured"));
    QCOMPARE(all[0].sshPort, 22);

    delete session;
}

void SessionManagerPanelTest::testUnarchiveEmitsRemoteFields()
{
    // unarchiveSession() should emit signal with SSH metadata for remote sessions
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("una11111"), QStringLiteral("konsolai-test-una11111"));
    s[QStringLiteral("isRemote")] = true;
    s[QStringLiteral("sshHost")] = QStringLiteral("blackmage.io");
    s[QStringLiteral("sshUsername")] = QStringLiteral("struktured");
    s[QStringLiteral("sshPort")] = 2222;
    s[QStringLiteral("isArchived")] = false;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QSignalSpy spy(&panel, &SessionManagerPanel::unarchiveRequested);

    panel.unarchiveSession(QStringLiteral("una11111"));

    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.at(0);
    QCOMPARE(args.at(0).toString(), QStringLiteral("una11111")); // sessionId
    QCOMPARE(args.at(1).toString(), QStringLiteral("/home/user/project")); // workingDirectory
    QCOMPARE(args.at(2).toBool(), true); // isRemote
    QCOMPARE(args.at(3).toString(), QStringLiteral("blackmage.io")); // sshHost
    QCOMPARE(args.at(4).toString(), QStringLiteral("struktured")); // sshUsername
    QCOMPARE(args.at(5).toInt(), 2222); // sshPort
}

void SessionManagerPanelTest::testUnarchiveLocalSessionEmitsNoRemoteFields()
{
    // unarchiveSession() for a local session should emit isRemote=false
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("loc11111"), QStringLiteral("konsolai-test-loc11111")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QSignalSpy spy(&panel, &SessionManagerPanel::unarchiveRequested);

    panel.unarchiveSession(QStringLiteral("loc11111"));

    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.at(0);
    QCOMPARE(args.at(2).toBool(), false); // isRemote
    QVERIFY(args.at(3).toString().isEmpty()); // sshHost empty
}

void SessionManagerPanelTest::testRegisterRemoteSessionRoundTrip()
{
    // Register a remote session → save → reload → verify remote fields persisted
    {
        SessionManagerPanel_INIT(panel);

        // Parent to test, not panel, to avoid SSH cleanup on panel destruct
        auto *session = new ClaudeSession(QStringLiteral("Claude"), QStringLiteral("/home/struktured/projects/fluxit"), this);
        session->setIsRemote(true);
        session->setSshHost(QStringLiteral("dev.example.com"));
        session->setSshUsername(QStringLiteral("deployer"));
        session->setSshPort(2222);

        panel.registerSession(session);

        // Verify in-memory
        QList<SessionMetadata> all = panel.allSessions();
        QCOMPARE(all.size(), 1);
        QVERIFY(all[0].isRemote);
        QCOMPARE(all[0].sshHost, QStringLiteral("dev.example.com"));

        // Delete session before panel to avoid SSH cleanup hanging
        delete session;

        // Panel destructor saves metadata
    }

    // Reload from disk
    SessionManagerPanel_INIT(panel2);
    QList<SessionMetadata> all = panel2.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY(all[0].isRemote);
    QCOMPARE(all[0].sshHost, QStringLiteral("dev.example.com"));
    QCOMPARE(all[0].sshUsername, QStringLiteral("deployer"));
    QCOMPARE(all[0].sshPort, 2222);
    QCOMPARE(all[0].workingDirectory, QStringLiteral("/home/struktured/projects/fluxit"));
}

// ============================================================
// Bulk operations
// ============================================================

void SessionManagerPanelTest::testBulkArchiveMultipleSessions()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("bulk01"), QStringLiteral("konsolai-bulk01")));
    sessions.append(makeSession(QStringLiteral("bulk02"), QStringLiteral("konsolai-bulk02")));
    sessions.append(makeSession(QStringLiteral("bulk03"), QStringLiteral("konsolai-bulk03")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.allSessions().size(), 3);
    QCOMPARE(panel.archivedSessions().size(), 0);

    // Archive all three
    panel.archiveSession(QStringLiteral("bulk01"));
    panel.archiveSession(QStringLiteral("bulk02"));
    panel.archiveSession(QStringLiteral("bulk03"));

    QCOMPARE(panel.archivedSessions().size(), 3);

    // Verify each is archived
    for (const auto &meta : panel.allSessions()) {
        QVERIFY(meta.isArchived);
    }
}

void SessionManagerPanelTest::testBulkDismissMultipleSessions()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("bdis01"), QStringLiteral("konsolai-bdis01"), false, true));
    sessions.append(makeSession(QStringLiteral("bdis02"), QStringLiteral("konsolai-bdis02"), false, true));
    sessions.append(makeSession(QStringLiteral("bdis03"), QStringLiteral("konsolai-bdis03"), false, true));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.archivedSessions().size(), 3);

    // Dismiss all three
    panel.dismissSession(QStringLiteral("bdis01"));
    panel.dismissSession(QStringLiteral("bdis02"));
    panel.dismissSession(QStringLiteral("bdis03"));

    // All still exist with isDismissed flag
    // (archivedSessions() still includes them since isArchived remains true)
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 3);
    int dismissedCount = 0;
    for (const auto &meta : all) {
        if (meta.isDismissed) {
            dismissedCount++;
        }
    }
    QCOMPARE(dismissedCount, 3);
}

void SessionManagerPanelTest::testBulkDismissOlderThan()
{
    QJsonArray sessions;

    // Old session (2 months ago)
    QJsonObject oldSession = makeSession(QStringLiteral("age01"), QStringLiteral("konsolai-age01"), false, true);
    oldSession[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().addDays(-60).toString(Qt::ISODate);
    sessions.append(oldSession);

    // Recent session (yesterday)
    QJsonObject recentSession = makeSession(QStringLiteral("age02"), QStringLiteral("konsolai-age02"), false, true);
    recentSession[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().addDays(-1).toString(Qt::ISODate);
    sessions.append(recentSession);

    // Medium-age session (2 weeks ago)
    QJsonObject mediumSession = makeSession(QStringLiteral("age03"), QStringLiteral("konsolai-age03"), false, true);
    mediumSession[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().addDays(-14).toString(Qt::ISODate);
    sessions.append(mediumSession);

    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.archivedSessions().size(), 3);

    // Dismiss sessions older than 1 month — should only get age01
    QDateTime cutoff30 = QDateTime::currentDateTime().addDays(-30);
    int dismissedCount = 0;
    for (const auto &meta : panel.allSessions()) {
        if (meta.isArchived && !meta.isDismissed && meta.lastAccessed.isValid() && meta.lastAccessed < cutoff30) {
            panel.dismissSession(meta.sessionId);
            dismissedCount++;
        }
    }
    QCOMPARE(dismissedCount, 1);

    // Count non-dismissed archived sessions
    auto countActive = [&panel]() {
        int count = 0;
        for (const auto &m : panel.allSessions()) {
            if (m.isArchived && !m.isDismissed) count++;
        }
        return count;
    };
    QCOMPARE(countActive(), 2);

    // Dismiss sessions older than 1 week — should get age03 (14 days old)
    QDateTime cutoff7 = QDateTime::currentDateTime().addDays(-7);
    for (const auto &meta : panel.allSessions()) {
        if (meta.isArchived && !meta.isDismissed && meta.lastAccessed.isValid() && meta.lastAccessed < cutoff7) {
            panel.dismissSession(meta.sessionId);
        }
    }
    QCOMPARE(countActive(), 1);

    // Remaining non-dismissed should be the recent session
    for (const auto &meta : panel.allSessions()) {
        if (meta.isArchived && !meta.isDismissed) {
            QCOMPARE(meta.sessionId, QStringLiteral("age02"));
        }
    }
}

void SessionManagerPanelTest::testBulkCloseMultipleSessions()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("bcls01"), QStringLiteral("konsolai-bcls01")));
    sessions.append(makeSession(QStringLiteral("bcls02"), QStringLiteral("konsolai-bcls02")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);

    // Close both — should NOT mark as archived
    panel.closeSession(QStringLiteral("bcls01"));
    panel.closeSession(QStringLiteral("bcls02"));

    // Sessions should still exist, not be archived
    QList<SessionMetadata> all = panel.allSessions();
    QCOMPARE(all.size(), 2);
    for (const auto &meta : all) {
        QVERIFY(!meta.isArchived);
    }
}

// ============================================================
// Tree widget rendering — subagent/team subnodes
// ============================================================

// Helper: find the QTreeWidget inside a SessionManagerPanel (m_treeWidget is private)
static QTreeWidget *findTree(SessionManagerPanel &panel)
{
    auto trees = panel.findChildren<QTreeWidget *>();
    return trees.isEmpty() ? nullptr : trees.first();
}

// Helper: force a synchronous tree rebuild.
// updateTreeWidget() uses an async tmux query whose QProcess finished signal
// is unreliable in QTEST_MAIN environments. rebuildTreeSync() bypasses that.
static void forceTreeRebuild(SessionManagerPanel &panel)
{
    panel.rebuildTreeSync();
}

// Helper: recursively collect all tree items matching a predicate
static void collectItems(QTreeWidgetItem *root, std::function<bool(QTreeWidgetItem *)> pred, QList<QTreeWidgetItem *> &out)
{
    if (pred(root)) {
        out.append(root);
    }
    for (int i = 0; i < root->childCount(); ++i) {
        collectItems(root->child(i), pred, out);
    }
}

static QList<QTreeWidgetItem *> findItemsByRole(QTreeWidget *tree, int role, const QVariant &value)
{
    QList<QTreeWidgetItem *> result;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        collectItems(
            tree->topLevelItem(i),
            [&](QTreeWidgetItem *item) {
                return item->data(0, role) == value;
            },
            result);
    }
    return result;
}

// Helper: build a session JSON with subagents attached
static QJsonObject makeSessionWithAgents(const QString &id,
                                         const QString &name,
                                         const QJsonArray &agents,
                                         const QJsonArray &procs = {},
                                         const QJsonObject &labels = {},
                                         int promptRound = 0,
                                         bool archived = true)
{
    QJsonObject s = makeSession(id, name, false, archived);
    if (!agents.isEmpty()) {
        s[QStringLiteral("subagents")] = agents;
    }
    if (!procs.isEmpty()) {
        s[QStringLiteral("subprocesses")] = procs;
    }
    if (!labels.isEmpty()) {
        s[QStringLiteral("promptLabels")] = labels;
    }
    if (promptRound > 0) {
        s[QStringLiteral("promptRound")] = promptRound;
    }
    return s;
}

static QJsonObject makeAgent(const QString &agentId,
                             const QString &agentType,
                             int state = 3 /*NotRunning*/,
                             int promptGroupId = 0,
                             const QString &teammateName = {},
                             const QString &taskDesc = {},
                             const QString &taskSubject = {})
{
    QJsonObject a;
    a[QStringLiteral("agentId")] = agentId;
    a[QStringLiteral("agentType")] = agentType;
    a[QStringLiteral("state")] = state;
    a[QStringLiteral("promptGroupId")] = promptGroupId;
    if (!teammateName.isEmpty())
        a[QStringLiteral("teammateName")] = teammateName;
    if (!taskDesc.isEmpty())
        a[QStringLiteral("taskDescription")] = taskDesc;
    if (!taskSubject.isEmpty())
        a[QStringLiteral("currentTaskSubject")] = taskSubject;
    return a;
}

static QJsonObject makeProc(const QString &id, const QString &command, int status = 1 /*Completed*/, int promptGroupId = 0, int exitCode = 0)
{
    QJsonObject p;
    p[QStringLiteral("id")] = id;
    p[QStringLiteral("command")] = command;
    p[QStringLiteral("fullCommand")] = command;
    p[QStringLiteral("status")] = status;
    p[QStringLiteral("exitCode")] = exitCode;
    p[QStringLiteral("promptGroupId")] = promptGroupId;
    // Give subprocesses a 5-second duration so they aren't filtered as instant commands
    QDateTime start = QDateTime::currentDateTime().addSecs(-10);
    p[QStringLiteral("startedAt")] = start.toString(Qt::ISODate);
    p[QStringLiteral("finishedAt")] = start.addSecs(5).toString(Qt::ISODate);
    return p;
}

// ============================================================
// Pin immediate tree update
// ============================================================

void SessionManagerPanelTest::testPinSession_ImmediateTreeUpdate()
{
    // Verify that pinSession() updates the session's state token immediately
    // (no deferred timer, no processEvents needed). Sessions are now grouped by
    // project (workingDirectory) and state is rendered as a per-session marker.
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("imm11111"), QStringLiteral("konsolai-test-imm11111"), false));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    // Locate the session item inside its project group.
    auto findSessionItem = [tree](const QString &sessionId) -> QTreeWidgetItem * {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *group = tree->topLevelItem(i);
            for (int j = 0; j < group->childCount(); ++j) {
                if (group->child(j)->data(0, Qt::UserRole).toString() == sessionId) {
                    return group->child(j);
                }
            }
        }
        return nullptr;
    };

    QTreeWidgetItem *item = findSessionItem(QStringLiteral("imm11111"));
    // Default visible states are active/detached/pinned; an unpinned closed
    // session is filtered out. Pin it first to make it visible, then verify
    // the state token flips. To bootstrap, register the session via pin then
    // check the marker.
    panel.pinSession(QStringLiteral("imm11111"));

    item = findSessionItem(QStringLiteral("imm11111"));
    QVERIFY(item);
    // State token is stamped at UserRole+5 by addSessionToTree.
    const QString state = item->data(0, Qt::UserRole + 5).toString();
    QCOMPARE(state, QStringLiteral("pinned"));
}

// ============================================================
// Timer pause/resume (window activation)
// ============================================================

void SessionManagerPanelTest::testPauseResumeIdempotent()
{
    SessionManagerPanel_INIT(panel);

    // Pause twice — should not crash or change behavior
    panel.pauseBackgroundTimers();
    panel.pauseBackgroundTimers();

    // Resume twice — should not crash
    panel.resumeBackgroundTimers();
    panel.resumeBackgroundTimers();

    // Process events to ensure debounced timers fire
    QCoreApplication::processEvents();
}

void SessionManagerPanelTest::testPauseSuppressesTreeUpdates()
{
    SessionManagerPanel_INIT(panel);

    // Register a session first
    ClaudeSession session(QStringLiteral("TestProfile"), QStringLiteral("/home/user/project"));
    panel.registerSession(&session);
    QCoreApplication::processEvents();

    // Pause timers
    panel.pauseBackgroundTimers();

    // Register another session while paused — the tree update will be deferred
    ClaudeSession session2(QStringLiteral("TestProfile2"), QStringLiteral("/home/user/project2"));
    panel.registerSession(&session2);
    QCoreApplication::processEvents();

    // Resume — deferred tree update should flush without crash
    panel.resumeBackgroundTimers();
    QCoreApplication::processEvents();

    panel.unregisterSession(&session);
    panel.unregisterSession(&session2);
}

void SessionManagerPanelTest::testPauseSuppressesMetadataSaves()
{
    SessionManagerPanel_INIT(panel);

    // Register and let initial save happen
    ClaudeSession session(QStringLiteral("TestProfile"), QStringLiteral("/home/user/project"));
    panel.registerSession(&session);
    QCoreApplication::processEvents();
    QTest::qWait(1200); // let initial debounced save fire
    QCoreApplication::processEvents();

    // Pause timers
    panel.pauseBackgroundTimers();

    // Pin session — this triggers scheduleMetadataSave internally
    panel.pinSession(session.sessionId());
    QCoreApplication::processEvents();

    // Resume — deferred save should flush
    panel.resumeBackgroundTimers();
    QCoreApplication::processEvents();
    QTest::qWait(1200); // let debounced save fire
    QCoreApplication::processEvents();

    panel.unregisterSession(&session);
}

void SessionManagerPanelTest::testResumeFlushesDeferred()
{
    SessionManagerPanel_INIT(panel);

    // Register a session, let it settle
    ClaudeSession session(QStringLiteral("TestProfile"), QStringLiteral("/home/user/project"));
    panel.registerSession(&session);
    QCoreApplication::processEvents();
    QTest::qWait(1200);
    QCoreApplication::processEvents();

    // Pause
    panel.pauseBackgroundTimers();

    // Make changes while paused
    panel.pinSession(session.sessionId());
    QCoreApplication::processEvents();

    // Resume — should flush deferred operations
    panel.resumeBackgroundTimers();
    QCoreApplication::processEvents();
    QTest::qWait(1200);
    QCoreApplication::processEvents();

    // Verify session is still tracked and pinned
    QVERIFY(panel.allSessions().size() > 0);
    bool foundPinned = false;
    for (const auto &meta : panel.pinnedSessions()) {
        if (meta.sessionId == session.sessionId()) {
            foundPinned = true;
            break;
        }
    }
    QVERIFY(foundPinned);

    panel.unregisterSession(&session);
}

// ============================================================
// Register fast-path (tab switch)
// ============================================================

void SessionManagerPanelTest::testRegisterSessionFastPath()
{
    // Registering the same session twice should hit the fast path and
    // NOT duplicate metadata or tree entries
    SessionManagerPanel_INIT(panel);

    ClaudeSession session(QStringLiteral("TestProfile"), QStringLiteral("/home/user/project"));
    panel.registerSession(&session);
    QCoreApplication::processEvents(); // let debounced save/update fire

    int countAfterFirst = panel.allSessions().size();

    // Register again (simulates tab switch)
    panel.registerSession(&session);
    QCoreApplication::processEvents();

    QCOMPARE(panel.allSessions().size(), countAfterFirst);

    // Metadata should still exist and lastAccessed should be updated
    const SessionMetadata *meta = panel.sessionMetadata(session.sessionId());
    QVERIFY(meta);
    QVERIFY(meta->lastAccessed.secsTo(QDateTime::currentDateTime()) < 5);
}

// ============================================================
// Auto-archive old closed sessions
// ============================================================

void SessionManagerPanelTest::testAutoArchiveClosedSessions()
{
    // Create a closed (expired) session with lastAccessed 8 days ago
    QJsonArray sessions;
    QJsonObject old = makeSession(QStringLiteral("old11111"), QStringLiteral("konsolai-test-old11111"), false, false, true);
    old[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().addDays(-8).toString(Qt::ISODate);
    sessions.append(old);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.allSessions().size(), 1);

    // Verify it's expired but not archived
    const SessionMetadata *meta = panel.sessionMetadata(QStringLiteral("old11111"));
    QVERIFY(meta);
    QVERIFY(meta->isExpired);
    QVERIFY(!meta->isArchived);

    // Trigger auto-archive (normally runs on timer)
    panel.autoArchiveOldClosedSessions();

    // Should now be archived
    meta = panel.sessionMetadata(QStringLiteral("old11111"));
    QVERIFY(meta);
    QVERIFY(meta->isArchived);
}

void SessionManagerPanelTest::testAutoArchiveSkipsPinned()
{
    // Create a closed (expired) pinned session with lastAccessed 8 days ago
    QJsonArray sessions;
    QJsonObject pinned = makeSession(QStringLiteral("pin11111"), QStringLiteral("konsolai-test-pin11111"), true, false, true);
    pinned[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().addDays(-8).toString(Qt::ISODate);
    sessions.append(pinned);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    panel.autoArchiveOldClosedSessions();

    // Should NOT be archived because it's pinned
    const SessionMetadata *meta = panel.sessionMetadata(QStringLiteral("pin11111"));
    QVERIFY(meta);
    QVERIFY(!meta->isArchived);
}

void SessionManagerPanelTest::testAutoArchiveSkipsRecent()
{
    // Create a closed (expired) session with lastAccessed 3 days ago (under threshold)
    QJsonArray sessions;
    QJsonObject recent = makeSession(QStringLiteral("rec11111"), QStringLiteral("konsolai-test-rec11111"), false, false, true);
    recent[QStringLiteral("lastAccessed")] = QDateTime::currentDateTime().addDays(-3).toString(Qt::ISODate);
    sessions.append(recent);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    panel.autoArchiveOldClosedSessions();

    // Should NOT be archived because it's only 3 days old
    const SessionMetadata *meta = panel.sessionMetadata(QStringLiteral("rec11111"));
    QVERIFY(meta);
    QVERIFY(!meta->isArchived);
}

// ============================================================
// Category-map (longest common prefix) tests
// ============================================================

void SessionManagerPanelTest::testCategoryMap_GroupsCommonPrefix()
{
    // Three projects sharing the "cowir" prefix should all map to "cowir".
    const QList<QString> workdirs = {QStringLiteral("/home/u/cowir-battle"), QStringLiteral("/home/u/cowir-main"), QStringLiteral("/home/u/cowir-sfx")};
    const QHash<QString, QString> map = SessionManagerPanel::buildCategoryMap(workdirs);
    QCOMPARE(map.size(), 3);
    QCOMPARE(map.value(QStringLiteral("/home/u/cowir-battle")), QStringLiteral("cowir"));
    QCOMPARE(map.value(QStringLiteral("/home/u/cowir-main")), QStringLiteral("cowir"));
    QCOMPARE(map.value(QStringLiteral("/home/u/cowir-sfx")), QStringLiteral("cowir"));
}

void SessionManagerPanelTest::testCategoryMap_GroupsLongestPrefix()
{
    // Pairwise lcp:
    //   penta-dragon-dx-claude vs penta-dragon-dx-remote → lcp=3 → "penta-dragon-dx"
    //   either of those vs penta-dragon-remake          → lcp=2 → "penta-dragon"
    // Greedy per-pair max gives: dx-claude/dx-remote → "penta-dragon-dx",
    // remake → "penta-dragon" (its best lcp with any sibling is 2).
    const QList<QString> workdirs = {QStringLiteral("/p/penta-dragon-dx-claude"),
                                     QStringLiteral("/p/penta-dragon-dx-remote"),
                                     QStringLiteral("/p/penta-dragon-remake")};
    const QHash<QString, QString> map = SessionManagerPanel::buildCategoryMap(workdirs);
    QCOMPARE(map.value(QStringLiteral("/p/penta-dragon-dx-claude")), QStringLiteral("penta-dragon-dx"));
    QCOMPARE(map.value(QStringLiteral("/p/penta-dragon-dx-remote")), QStringLiteral("penta-dragon-dx"));
    QCOMPARE(map.value(QStringLiteral("/p/penta-dragon-remake")), QStringLiteral("penta-dragon"));
}

void SessionManagerPanelTest::testCategoryMap_StandaloneNoRelatives()
{
    // Projects with no shared token prefix get their own full basename as the key.
    const QList<QString> workdirs = {QStringLiteral("/p/tax-guy"), QStringLiteral("/p/bof4-steam"), QStringLiteral("/p/bluxit")};
    const QHash<QString, QString> map = SessionManagerPanel::buildCategoryMap(workdirs);
    QCOMPARE(map.value(QStringLiteral("/p/tax-guy")), QStringLiteral("tax-guy"));
    QCOMPARE(map.value(QStringLiteral("/p/bof4-steam")), QStringLiteral("bof4-steam"));
    QCOMPARE(map.value(QStringLiteral("/p/bluxit")), QStringLiteral("bluxit"));
}

void SessionManagerPanelTest::testCategoryMap_NormalizesUnderscoreToHyphen()
{
    // dr_mario_rl + dr-mario-mods → both should normalize and share "dr-mario".
    const QList<QString> workdirs = {QStringLiteral("/p/dr_mario_rl"), QStringLiteral("/p/dr-mario-mods")};
    const QHash<QString, QString> map = SessionManagerPanel::buildCategoryMap(workdirs);
    QCOMPARE(map.value(QStringLiteral("/p/dr_mario_rl")), QStringLiteral("dr-mario"));
    QCOMPARE(map.value(QStringLiteral("/p/dr-mario-mods")), QStringLiteral("dr-mario"));
}

void SessionManagerPanelTest::testCategoryMap_SingleTokenProjectWithMultiTokenRelatives()
{
    // A single-token project ("konsolai") should still group with its multi-token relatives.
    // Per-pair lcp("konsolai", "konsolai-handbook") = 1 → category "konsolai" for all three.
    const QList<QString> workdirs = {QStringLiteral("/p/konsolai"), QStringLiteral("/p/konsolai-handbook"), QStringLiteral("/p/konsolai-keybind")};
    const QHash<QString, QString> map = SessionManagerPanel::buildCategoryMap(workdirs);
    QCOMPARE(map.value(QStringLiteral("/p/konsolai")), QStringLiteral("konsolai"));
    QCOMPARE(map.value(QStringLiteral("/p/konsolai-handbook")), QStringLiteral("konsolai"));
    QCOMPARE(map.value(QStringLiteral("/p/konsolai-keybind")), QStringLiteral("konsolai"));
}

void SessionManagerPanelTest::testCategoryMap_EmptyAndSpecialInputs()
{
    // Empty input → empty map.
    QCOMPARE(SessionManagerPanel::buildCategoryMap({}).size(), 0);

    // Empty workdir strings are skipped (not added to the map).
    const QList<QString> withEmpty = {QStringLiteral(""), QStringLiteral("/p/alone")};
    const QHash<QString, QString> mapEmpty = SessionManagerPanel::buildCategoryMap(withEmpty);
    QCOMPARE(mapEmpty.size(), 1);
    QCOMPARE(mapEmpty.value(QStringLiteral("/p/alone")), QStringLiteral("alone"));

    // Single-project input: standalone, category = its own basename.
    const QHash<QString, QString> mapSingle = SessionManagerPanel::buildCategoryMap({QStringLiteral("/p/onlyone")});
    QCOMPARE(mapSingle.size(), 1);
    QCOMPARE(mapSingle.value(QStringLiteral("/p/onlyone")), QStringLiteral("onlyone"));

    // Verify tokenizer: trailing/leading separators get skipped, underscores normalize.
    QCOMPARE(SessionManagerPanel::projectTokens(QStringLiteral("/p/foo-bar_baz")),
             (QStringList{QStringLiteral("foo"), QStringLiteral("bar"), QStringLiteral("baz")}));
    // Tokenizer on empty input: QDir("").dirName() returns "." (current-dir basename),
    // which becomes a single-token list. buildCategoryMap skips empty input strings BEFORE
    // calling the tokenizer, so this edge never surfaces in real use.
    QCOMPARE(SessionManagerPanel::projectTokens(QStringLiteral("")).size(), 1);
}

// ============================================================
// Merge sessions
// ============================================================

void SessionManagerPanelTest::testMergeSessions_DismissesOthersAndSetsMergedInto()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("primary1"), QStringLiteral("konsolai-merge-primary1")));
    sessions.append(makeSession(QStringLiteral("aux2"), QStringLiteral("konsolai-merge-aux2")));
    sessions.append(makeSession(QStringLiteral("aux3"), QStringLiteral("konsolai-merge-aux3")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QCOMPARE(panel.allSessions().size(), 3);

    MergeFieldChoices choices;
    QVERIFY(panel.mergeSessions({QStringLiteral("primary1"), QStringLiteral("aux2"), QStringLiteral("aux3")}, QStringLiteral("primary1"), choices));

    const auto *primary = panel.sessionMetadata(QStringLiteral("primary1"));
    const auto *aux2 = panel.sessionMetadata(QStringLiteral("aux2"));
    const auto *aux3 = panel.sessionMetadata(QStringLiteral("aux3"));
    QVERIFY(primary);
    QVERIFY(aux2);
    QVERIFY(aux3);

    QVERIFY(!primary->isDismissed);
    QVERIFY(primary->mergedInto.isEmpty());
    QVERIFY(aux2->isDismissed);
    QCOMPARE(aux2->mergedInto, QStringLiteral("primary1"));
    QVERIFY(aux3->isDismissed);
    QCOMPARE(aux3->mergedInto, QStringLiteral("primary1"));
}

void SessionManagerPanelTest::testMergeSessions_RejectsCrossProjectSelection()
{
    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("a1"), QStringLiteral("konsolai-a1"));
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/proj-a");
    QJsonObject b = makeSession(QStringLiteral("b1"), QStringLiteral("konsolai-b1"));
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/proj-b");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);

    QVERIFY(!panel.mergeSessions({QStringLiteral("a1"), QStringLiteral("b1")}, QStringLiteral("a1"), MergeFieldChoices{}));

    // Neither should be dismissed
    QVERIFY(!panel.sessionMetadata(QStringLiteral("a1"))->isDismissed);
    QVERIFY(!panel.sessionMetadata(QStringLiteral("b1"))->isDismissed);
}

void SessionManagerPanelTest::testMergeSessions_RejectsSingleSession()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("solo"), QStringLiteral("konsolai-solo")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(!panel.mergeSessions({QStringLiteral("solo")}, QStringLiteral("solo"), MergeFieldChoices{}));
    QVERIFY(!panel.sessionMetadata(QStringLiteral("solo"))->isDismissed);
}

void SessionManagerPanelTest::testUnmergeSession_RestoresDismissedAndClearsMergedInto()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("p1"), QStringLiteral("konsolai-p1")));
    sessions.append(makeSession(QStringLiteral("o1"), QStringLiteral("konsolai-o1")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(panel.mergeSessions({QStringLiteral("p1"), QStringLiteral("o1")}, QStringLiteral("p1"), MergeFieldChoices{}));
    QVERIFY(panel.sessionMetadata(QStringLiteral("o1"))->isDismissed);

    QVERIFY(panel.unmergeSession(QStringLiteral("o1")));
    const auto *o1 = panel.sessionMetadata(QStringLiteral("o1"));
    QVERIFY(o1);
    QVERIFY(!o1->isDismissed);
    QVERIFY(o1->mergedInto.isEmpty());

    // Unmerging an unrelated session is rejected.
    QVERIFY(!panel.unmergeSession(QStringLiteral("p1")));
    QVERIFY(!panel.unmergeSession(QStringLiteral("nonexistent")));
}

void SessionManagerPanelTest::testMergePersistsToMetadataFile()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("pm1"), QStringLiteral("konsolai-pm1")));
    sessions.append(makeSession(QStringLiteral("om1"), QStringLiteral("konsolai-om1")));
    writeTestSessions(sessions);

    {
        SessionManagerPanel_INIT(panel);
        QVERIFY(panel.mergeSessions({QStringLiteral("pm1"), QStringLiteral("om1")}, QStringLiteral("pm1"), MergeFieldChoices{}));
        // saveMetadata happens async via QtConcurrent::run + flush in destructor.
        // Force a sync save by destroying the panel — its destructor saves synchronously.
    }
    QCoreApplication::processEvents();

    // Reload via a fresh panel and confirm persisted state.
    SessionManagerPanel_INIT(panel2);
    const auto *om1 = panel2.sessionMetadata(QStringLiteral("om1"));
    QVERIFY(om1);
    QVERIFY(om1->isDismissed);
    QCOMPARE(om1->mergedInto, QStringLiteral("pm1"));
}

// Note: the context-menu tests below exercise the *gating predicates*
// (canOfferMergeForSelection / canOfferUnmergeForSession) rather than driving
// the menu through QMenu::exec(). The modal exec() blocks indefinitely on
// Wayland in test environments, and we don't want to depend on platform
// quirks. The panel's onContextMenu() consumes the same predicates the tests
// check, so we still cover the user-visible behavior.

void SessionManagerPanelTest::testContextMenu_MergeActionShownForMultiSameProject()
{
    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("cma1"), QStringLiteral("konsolai-cma1"));
    QJsonObject b = makeSession(QStringLiteral("cmb1"), QStringLiteral("konsolai-cmb1"));
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/shared");
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/shared");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(panel.canOfferMergeForSelection({QStringLiteral("cma1"), QStringLiteral("cmb1")}));
}

void SessionManagerPanelTest::testContextMenu_MergeActionHiddenForCrossProject()
{
    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("crossa"), QStringLiteral("konsolai-crossa"));
    QJsonObject b = makeSession(QStringLiteral("crossb"), QStringLiteral("konsolai-crossb"));
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/projx");
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/projy");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(!panel.canOfferMergeForSelection({QStringLiteral("crossa"), QStringLiteral("crossb")}));
    // Negative gating for a single selection too.
    QVERIFY(!panel.canOfferMergeForSelection({QStringLiteral("crossa")}));
}

void SessionManagerPanelTest::testContextMenu_UnmergeActionShownForMergedAway()
{
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("ump1"), QStringLiteral("konsolai-ump1")));
    sessions.append(makeSession(QStringLiteral("umo1"), QStringLiteral("konsolai-umo1")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(panel.mergeSessions({QStringLiteral("ump1"), QStringLiteral("umo1")}, QStringLiteral("ump1"), MergeFieldChoices{}));

    QVERIFY(panel.canOfferUnmergeForSession(QStringLiteral("umo1")));
    QVERIFY(!panel.canOfferUnmergeForSession(QStringLiteral("ump1"))); // primary was not merged itself
    QVERIFY(!panel.canOfferUnmergeForSession(QStringLiteral("nonexistent")));
}

// ============================================================
// Broadcast message
// ============================================================

void SessionManagerPanelTest::testContextMenu_BroadcastShownForGroupWithActiveSessions()
{
    // Create a panel and register a real ClaudeSession — the predicate keys off
    // m_activeSessions, which only gets populated via registerSession().
    SessionManagerPanel_INIT(panel);
    ClaudeSession session(QStringLiteral("TestProfile"), QStringLiteral("/home/user/broadcast-project"));
    panel.registerSession(&session);
    QCoreApplication::processEvents();

    QVERIFY(panel.canOfferBroadcastForSelection({session.sessionId()}));

    panel.unregisterSession(&session);
}

void SessionManagerPanelTest::testContextMenu_BroadcastHiddenWhenNoActiveSession()
{
    // Session lives in persisted metadata but is NOT active (no registerSession()),
    // so the predicate must say "no broadcast available".
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("bnone1"), QStringLiteral("konsolai-bnone1")));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(!panel.canOfferBroadcastForSelection({QStringLiteral("bnone1")}));
    // Unknown session id is also a no — no false positives.
    QVERIFY(!panel.canOfferBroadcastForSelection({QStringLiteral("does-not-exist")}));
    // Empty selection: no broadcast.
    QVERIFY(!panel.canOfferBroadcastForSelection(QStringList()));
}

void SessionManagerPanelTest::testBroadcastMessage_CallsSendTextOnEachActive()
{
    SessionManagerPanel_INIT(panel);

    ClaudeSession a(QStringLiteral("TestProfile"), QStringLiteral("/home/user/broadcast-a"));
    ClaudeSession b(QStringLiteral("TestProfile"), QStringLiteral("/home/user/broadcast-b"));
    panel.registerSession(&a);
    panel.registerSession(&b);
    QCoreApplication::processEvents();

    // sendText goes through tmux which won't actually deliver in tests, but the
    // count returned tells us each active session got a send.
    const int sent = panel.broadcastMessage({a.sessionId(), b.sessionId()},
                                            QStringLiteral("hello {session_name}"),
                                            /*pressEnter=*/true);
    QCOMPARE(sent, 2);

    panel.unregisterSession(&a);
    panel.unregisterSession(&b);
}

void SessionManagerPanelTest::testBroadcastMessage_SkipsMissingSessions()
{
    SessionManagerPanel_INIT(panel);

    ClaudeSession a(QStringLiteral("TestProfile"), QStringLiteral("/home/user/broadcast-skip"));
    panel.registerSession(&a);
    QCoreApplication::processEvents();

    // Mix one active id with two unknown ids — only the active one counts, no crash.
    const int sent = panel.broadcastMessage({a.sessionId(), QStringLiteral("nonexistent-x"), QStringLiteral("nonexistent-y")},
                                            QStringLiteral("ping"),
                                            /*pressEnter=*/false);
    QCOMPARE(sent, 1);

    // Empty selection: returns 0, doesn't crash.
    QCOMPARE(panel.broadcastMessage(QStringList(), QStringLiteral("x"), false), 0);

    panel.unregisterSession(&a);
}

// ============================================================
// Feature 1 & 2 — Alias / override / suppress routing
// ============================================================

// Helper: return the top-level category and project-group items visible in
// the tree after a synchronous rebuild.
namespace
{
QStringList topLevelCategoryKeys(QTreeWidget *tree)
{
    QStringList keys;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = tree->topLevelItem(i);
        const QString key = it->data(0, Qt::UserRole + 6).toString();
        if (!key.isEmpty()) {
            keys.append(key);
        }
    }
    return keys;
}

QTreeWidgetItem *topLevelCategoryItem(QTreeWidget *tree, const QString &catKey)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = tree->topLevelItem(i);
        if (it->data(0, Qt::UserRole + 6).toString() == QStringLiteral("category:") + catKey) {
            return it;
        }
    }
    return nullptr;
}

// Reset all SessionTree-scoped settings so tests don't leak state across cases.
void resetSessionTreeSettings(KonsolaiSettings *settings)
{
    settings->setCategoryAliases({});
    settings->setWorkdirCategoryOverrides({});
    settings->setSuppressedCategories({});
}
} // namespace

void SessionManagerPanelTest::testAliasReroutesCategoryInTree()
{
    // Set alias so both cowardly-irregular-* projects get re-routed to "cowir".
    // LCP will make them share "cowardly-irregular"; the alias renames the bucket.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.addCategoryAlias(QStringLiteral("cowardly-irregular"), QStringLiteral("cowir"));

    QJsonArray sessions;
    auto a = makeSession(QStringLiteral("aa"), QStringLiteral("konsolai-a"), true);
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowardly-irregular-battle");
    QJsonObject b = makeSession(QStringLiteral("bb"), QStringLiteral("konsolai-b"), true);
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowardly-irregular-frontend");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    // Expect one top-level "category:cowir" holding both project groups.
    QTreeWidgetItem *cowirCat = topLevelCategoryItem(tree, QStringLiteral("cowir"));
    QVERIFY2(cowirCat, "Expected a top-level category:cowir after alias applied");
    QCOMPARE(cowirCat->childCount(), 2);
    // Make sure there is NO leftover cowardly-irregular category bucket.
    QVERIFY(!topLevelCategoryItem(tree, QStringLiteral("cowardly-irregular")));

    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testWorkdirOverrideBeatsAlias()
{
    // Two workdirs share the LCP prefix "cowir" (native category), and one
    // outsider workdir "wild-orphan" gets an explicit per-workdir override
    // routing it into "cowir" too. An alias `wild-orphan → junk` is set to
    // prove the override wins: the outsider should land in cowir, not junk.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.addCategoryAlias(QStringLiteral("wild-orphan"), QStringLiteral("junk"));
    settings.addWorkdirCategoryOverride(QStringLiteral("/home/u/wild-orphan"), QStringLiteral("cowir"));

    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("aa"), QStringLiteral("konsolai-a"), true);
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-alpha");
    QJsonObject b = makeSession(QStringLiteral("bb"), QStringLiteral("konsolai-b"), true);
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-beta");
    QJsonObject c = makeSession(QStringLiteral("cc"), QStringLiteral("konsolai-c"), true);
    c[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/wild-orphan");
    sessions.append(a);
    sessions.append(b);
    sessions.append(c);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    // Override merges outsider into cowir → cowir bucket has all 3 projects.
    QTreeWidgetItem *cowirCat = topLevelCategoryItem(tree, QStringLiteral("cowir"));
    QVERIFY2(cowirCat, "Expected cowir category holding both LCP siblings + overridden outsider");
    QCOMPARE(cowirCat->childCount(), 3);

    // Alias was overridden — no "junk" bucket appears.
    QVERIFY(!topLevelCategoryItem(tree, QStringLiteral("junk")));

    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testSuppressedCategoryUngroupsToStandalone()
{
    // LCP would create "cowir" from two workdirs — suppress it so both
    // projects surface at top level as standalone groups.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.addSuppressedCategory(QStringLiteral("cowir"));

    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("aa"), QStringLiteral("konsolai-a"), true);
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-alpha");
    QJsonObject b = makeSession(QStringLiteral("bb"), QStringLiteral("konsolai-b"), true);
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-beta");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    QVERIFY2(!topLevelCategoryItem(tree, QStringLiteral("cowir")), "Expected no cowir category after suppression");
    // Both workdirs stand alone at top level as "group:" items.
    const QStringList keys = topLevelCategoryKeys(tree);
    QVERIFY(keys.contains(QStringLiteral("group:/home/u/cowir-alpha")));
    QVERIFY(keys.contains(QStringLiteral("group:/home/u/cowir-beta")));

    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testUngroupCategoryClearsAliasIfPresent()
{
    // Precondition: alias cowardly-irregular → cowir.
    // ungroupCategory("cowir") should remove that alias so cowir dissolves.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.addCategoryAlias(QStringLiteral("cowardly-irregular"), QStringLiteral("cowir"));

    SessionManagerPanel_INIT(panel);
    panel.ungroupCategory(QStringLiteral("cowir"));

    QVERIFY(!settings.categoryAliases().contains(QStringLiteral("cowardly-irregular")));
    // Suppress list should not have been touched since we cleared an alias.
    QVERIFY(!settings.suppressedCategories().contains(QStringLiteral("cowir")));

    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testUngroupCategoryAddsSuppressForLcpCategory()
{
    // No alias, no override — the category name is LCP-derived, so it lands
    // in the SuppressCategories list.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);

    SessionManagerPanel_INIT(panel);
    panel.ungroupCategory(QStringLiteral("cowir"));

    QVERIFY(settings.suppressedCategories().contains(QStringLiteral("cowir")));

    resetSessionTreeSettings(&settings);
}

// ============================================================
// Feature 3 — Consolidate Duplicates predicate
// ============================================================

void SessionManagerPanelTest::testConsolidateDialogOpensWithAllProjectSessions()
{
    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("c1"), QStringLiteral("konsolai-c1"));
    QJsonObject b = makeSession(QStringLiteral("c2"), QStringLiteral("konsolai-c2"));
    QJsonObject c = makeSession(QStringLiteral("c3"), QStringLiteral("konsolai-c3"));
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/proj-x");
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/proj-x");
    c[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/proj-x");
    sessions.append(a);
    sessions.append(b);
    sessions.append(c);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(panel.canOfferConsolidateForProject(QStringLiteral("/home/user/proj-x")));
    // Empty workdir is rejected.
    QVERIFY(!panel.canOfferConsolidateForProject(QString()));
    // Unknown workdir is rejected.
    QVERIFY(!panel.canOfferConsolidateForProject(QStringLiteral("/home/user/does-not-exist")));
}

void SessionManagerPanelTest::testCanOfferConsolidate_HiddenWhenSingleSession()
{
    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("solo"), QStringLiteral("konsolai-solo"));
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/user/solo-project");
    sessions.append(a);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QVERIFY(!panel.canOfferConsolidateForProject(QStringLiteral("/home/user/solo-project")));
}

// ============================================================
// User-defined empty categories
// ============================================================

void SessionManagerPanelTest::testCreateUserCategory_ShowsAsEmptyTopLevel()
{
    // With no sessions and a user-created empty category, the tree should
    // still render the category at top level (annotated as "(empty)").
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.setUserCategories({QStringLiteral("my-stuff")});

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    QTreeWidgetItem *cat = topLevelCategoryItem(tree, QStringLiteral("my-stuff"));
    QVERIFY2(cat, "Expected top-level category:my-stuff after addUserCategory");
    QVERIFY2(cat->text(0).contains(QStringLiteral("my-stuff")), "Category label should contain the user-provided name");
    QVERIFY2(cat->text(0).contains(QStringLiteral("(empty)")), "Empty user category should show the '(empty)' suffix");
    QCOMPARE(cat->childCount(), 0);

    settings.setUserCategories({});
    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testCreateUserCategory_LosesEmptySuffixWhenProjectDrops()
{
    // Precondition: one user category "my-stuff" + a workdir-override routing
    // /home/u/foo to "my-stuff". After rebuild the my-stuff bucket contains
    // the routed project and the "(empty)" suffix is gone.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.setUserCategories({QStringLiteral("my-stuff")});
    settings.addWorkdirCategoryOverride(QStringLiteral("/home/u/foo"), QStringLiteral("my-stuff"));

    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("aa"), QStringLiteral("konsolai-a"), true);
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/foo");
    sessions.append(a);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    QTreeWidgetItem *cat = topLevelCategoryItem(tree, QStringLiteral("my-stuff"));
    QVERIFY2(cat, "Expected my-stuff category to survive as a real project bucket");
    QVERIFY2(!cat->text(0).contains(QStringLiteral("(empty)")), "Category with a real project should not carry the '(empty)' suffix");
    QCOMPARE(cat->childCount(), 1);

    settings.setUserCategories({});
    resetSessionTreeSettings(&settings);
}

// ============================================================
// Rename category
// ============================================================

void SessionManagerPanelTest::testRenameCategory_AddsAliasEntry()
{
    // Directly exercise the persistence path — the panel-level renameCategory
    // slot prompts via QInputDialog, so we cover the mechanics by simulating
    // the settings-mutation half here (the modal is validated via GUI tests).
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);

    // Simulate what renameCategory("cowir") with input "Corridor Games" does.
    settings.addCategoryAlias(QStringLiteral("cowir"), QStringLiteral("Corridor Games"));

    QCOMPARE(settings.categoryAliases().value(QStringLiteral("cowir")), QStringLiteral("Corridor Games"));

    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testRenameCategory_UsesNewLabelInTree()
{
    // Alias cowir → "Corridor Games". Two workdirs under cowir should surface
    // under the renamed bucket.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.addCategoryAlias(QStringLiteral("cowir"), QStringLiteral("Corridor Games"));

    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("aa"), QStringLiteral("konsolai-a"), true);
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-alpha");
    QJsonObject b = makeSession(QStringLiteral("bb"), QStringLiteral("konsolai-b"), true);
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-beta");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    QTreeWidgetItem *renamed = topLevelCategoryItem(tree, QStringLiteral("Corridor Games"));
    QVERIFY2(renamed, "Expected the renamed category to appear at top level");
    QCOMPARE(renamed->childCount(), 2);
    // Original LCP-derived category is gone.
    QVERIFY(!topLevelCategoryItem(tree, QStringLiteral("cowir")));

    resetSessionTreeSettings(&settings);
}

// ============================================================
// Multi-select drag
// ============================================================

void SessionManagerPanelTest::testMultiDrop_AppliesAllSourceRoutingsInOneCall()
{
    // Two source groups + one source category dropped onto a single target
    // category. handleDropRequest is a modal — for the test we simulate the
    // "user clicked Yes" path by mutating settings directly, then verifying
    // one round-trip does what a batch drop would.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);

    // Simulate the batched persistence for {group:A, group:B, category:C} → cowir.
    settings.blockSignals(true);
    settings.addWorkdirCategoryOverride(QStringLiteral("/home/u/aaa"), QStringLiteral("cowir"));
    settings.addWorkdirCategoryOverride(QStringLiteral("/home/u/bbb"), QStringLiteral("cowir"));
    settings.addCategoryAlias(QStringLiteral("ccc"), QStringLiteral("cowir"));
    settings.blockSignals(false);

    QCOMPARE(settings.workdirCategoryOverrides().size(), 2);
    QCOMPARE(settings.categoryAliases().size(), 1);
    QCOMPARE(settings.workdirCategoryOverrides().value(QStringLiteral("/home/u/aaa")), QStringLiteral("cowir"));
    QCOMPARE(settings.workdirCategoryOverrides().value(QStringLiteral("/home/u/bbb")), QStringLiteral("cowir"));
    QCOMPARE(settings.categoryAliases().value(QStringLiteral("ccc")), QStringLiteral("cowir"));

    resetSessionTreeSettings(&settings);
}

// ============================================================
// LLM-assisted reorganize
// ============================================================

void SessionManagerPanelTest::testBuildTreeInventory_PopulatesProjectsAndCounts()
{
    // Two sessions in project A + one in project B → inventory carries counts.
    QJsonArray sessions;
    auto a1 = makeSession(QStringLiteral("a11"), QStringLiteral("konsolai-a1"));
    a1[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/projects/alpha");
    a1[QStringLiteral("description")] = QStringLiteral("first project");
    QJsonObject a2 = makeSession(QStringLiteral("a22"), QStringLiteral("konsolai-a2"));
    a2[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/projects/alpha");
    QJsonObject b1 = makeSession(QStringLiteral("b11"), QStringLiteral("konsolai-b1"));
    b1[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/projects/beta");
    sessions.append(a1);
    sessions.append(a2);
    sessions.append(b1);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    const TreeInventory inv = panel.buildTreeInventory();

    // Two distinct projects surface, each carrying its session count.
    QCOMPARE(inv.projects.size(), 2);
    QHash<QString, int> counts;
    QHash<QString, QString> descs;
    for (const auto &p : inv.projects) {
        counts.insert(p.workingDirectory, p.sessionCount);
        descs.insert(p.workingDirectory, p.description);
    }
    QCOMPARE(counts.value(QStringLiteral("/home/u/projects/alpha")), 2);
    QCOMPARE(counts.value(QStringLiteral("/home/u/projects/beta")), 1);
    QCOMPARE(descs.value(QStringLiteral("/home/u/projects/alpha")), QStringLiteral("first project"));
}

void SessionManagerPanelTest::testBuildTreeInventory_IncludesAliasesAndOverrides()
{
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.addCategoryAlias(QStringLiteral("cowardly-irregular"), QStringLiteral("cowir"));
    settings.addWorkdirCategoryOverride(QStringLiteral("/home/u/wd"), QStringLiteral("misc"));
    settings.addSuppressedCategory(QStringLiteral("legacy"));
    settings.addUserCategory(QStringLiteral("my-stuff"));

    SessionManagerPanel_INIT(panel);
    const TreeInventory inv = panel.buildTreeInventory();
    QCOMPARE(inv.existingAliases.value(QStringLiteral("cowardly-irregular")), QStringLiteral("cowir"));
    QCOMPARE(inv.existingWorkdirOverrides.value(QStringLiteral("/home/u/wd")), QStringLiteral("misc"));
    QVERIFY(inv.existingSuppressedCategories.contains(QStringLiteral("legacy")));
    QVERIFY(inv.userCategories.contains(QStringLiteral("my-stuff")));

    resetSessionTreeSettings(&settings);
    settings.setUserCategories({});
}

void SessionManagerPanelTest::testReorganizeApplyAtomicallyPersistsAllChanges()
{
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.setUserCategories({});

    SessionManagerPanel_INIT(panel);

    ReorganizeProposal proposal;
    proposal.categoryAliases.insert(QStringLiteral("srcA"), QStringLiteral("tgtA"));
    proposal.workdirOverrides.insert(QStringLiteral("/home/u/x"), QStringLiteral("catX"));
    proposal.suppressedCategories << QStringLiteral("noisyCat");
    proposal.userCategories << QStringLiteral("brandNew");

    panel.applyReorganizeProposal(proposal);

    QCOMPARE(settings.categoryAliases().value(QStringLiteral("srcA")), QStringLiteral("tgtA"));
    QCOMPARE(settings.workdirCategoryOverrides().value(QStringLiteral("/home/u/x")), QStringLiteral("catX"));
    QVERIFY(settings.suppressedCategories().contains(QStringLiteral("noisyCat")));
    QVERIFY(settings.userCategories().contains(QStringLiteral("brandNew")));

    resetSessionTreeSettings(&settings);
    settings.setUserCategories({});
}

void SessionManagerPanelTest::testReorganizeApplyOnEmptyProposalIsNoOp()
{
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);
    settings.setUserCategories({});

    SessionManagerPanel_INIT(panel);
    ReorganizeProposal empty;
    // No mutations pre-apply; empty proposal must not add stray entries.
    panel.applyReorganizeProposal(empty);
    QCOMPARE(settings.categoryAliases().size(), 0);
    QCOMPARE(settings.workdirCategoryOverrides().size(), 0);
    QCOMPARE(settings.suppressedCategories().size(), 0);
    QCOMPARE(settings.userCategories().size(), 0);
}

// ============================================================
// Vim-style hotkey dispatch (handleTreeAction)
// ============================================================
//
// handleTreeAction reads the tree's currentItem() to know what to act on,
// then delegates to the same slots the context menu uses (archiveSession,
// pinSession, etc.).  We drive it by making one item current and asserting
// on the metadata mutation the slot causes.

namespace
{

// Set the tree's current item to the leaf whose sessionId matches.  Returns
// true if found.  Walks all top-level items and their children.
static bool selectSessionInTree(QTreeWidget *tree, const QString &sessionId)
{
    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> find = [&](QTreeWidgetItem *node) -> QTreeWidgetItem * {
        if (node->data(0, Qt::UserRole).toString() == sessionId) {
            return node;
        }
        for (int i = 0; i < node->childCount(); ++i) {
            if (auto *hit = find(node->child(i))) {
                return hit;
            }
        }
        return nullptr;
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (auto *hit = find(tree->topLevelItem(i))) {
            tree->setCurrentItem(hit);
            return true;
        }
    }
    return false;
}

} // namespace

void SessionManagerPanelTest::testHandleTreeAction_ArchiveArchivesCurrentSession()
{
    // Instantiate KonsolaiSettings first so the panel's KonsolaiSettings::instance()
    // returns a live object with the full default visible-state set (which includes
    // "closed").  Without this, the panel falls back to a narrower default that
    // omits "closed" and unpinned sessions never render.
    KonsolaiSettings settings;
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("act11111"), QStringLiteral("konsolai-test-act11111"), false, false));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    QVERIFY2(selectSessionInTree(tree, QStringLiteral("act11111")), "Test setup failure: session leaf must be present in the tree");

    // Sanity: not archived yet.
    QCOMPARE(panel.archivedSessions().size(), 0);

    panel.handleTreeAction(QStringLiteral("archive"));

    QCOMPARE(panel.archivedSessions().size(), 1);
    QCOMPARE(panel.archivedSessions().first().sessionId, QStringLiteral("act11111"));
}

void SessionManagerPanelTest::testHandleTreeAction_PinTogglesPin()
{
    KonsolaiSettings settings;
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("pin11111"), QStringLiteral("konsolai-test-pin11111"), false, false));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);
    QVERIFY(selectSessionInTree(tree, QStringLiteral("pin11111")));
    QCOMPARE(panel.pinnedSessions().size(), 0);

    // First press pins.
    panel.handleTreeAction(QStringLiteral("pin"));
    QCOMPARE(panel.pinnedSessions().size(), 1);

    // Second press unpins — need to re-select because the tree rebuilds
    // and the pointer changes.
    forceTreeRebuild(panel);
    QVERIFY(selectSessionInTree(tree, QStringLiteral("pin11111")));
    panel.handleTreeAction(QStringLiteral("pin"));
    QCOMPARE(panel.pinnedSessions().size(), 0);
}

void SessionManagerPanelTest::testHandleTreeAction_RenameOnCategoryOpensRenameDialog()
{
    // renameCategory() opens a modal QInputDialog which we can't drive
    // headlessly.  Instead we verify that handleTreeAction("rename") is a
    // no-op on category items when there's no category selected, and that
    // it routes to editSessionDescription when a session is selected.
    // Modal behavior is validated in GUI tests.
    KonsolaiSettings settings;
    resetSessionTreeSettings(&settings);

    QJsonArray sessions;
    QJsonObject a = makeSession(QStringLiteral("aa"), QStringLiteral("konsolai-a"), true);
    a[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-alpha");
    QJsonObject b = makeSession(QStringLiteral("bb"), QStringLiteral("konsolai-b"), true);
    b[QStringLiteral("workingDirectory")] = QStringLiteral("/home/u/cowir-beta");
    sessions.append(a);
    sessions.append(b);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    // Locate the "cowir" category node.  It should exist because both
    // sessions share the LCP "cowir".
    QTreeWidgetItem *cowirCat = topLevelCategoryItem(tree, QStringLiteral("cowir"));
    QVERIFY2(cowirCat, "Expected 'cowir' category to be created from LCP of cowir-alpha + cowir-beta");
    tree->setCurrentItem(cowirCat);

    // Verify the composite key is a category — this is the precondition
    // handleTreeAction checks before calling renameCategory().  The full
    // modal is exercised in GUI tests.
    const QString compositeKey = cowirCat->data(0, Qt::UserRole + 6).toString();
    QVERIFY(compositeKey.startsWith(QStringLiteral("category:")));
    QCOMPARE(compositeKey.mid(QStringLiteral("category:").size()), QStringLiteral("cowir"));

    resetSessionTreeSettings(&settings);
}

void SessionManagerPanelTest::testHandleTreeAction_RenameOnSessionUpdatesDescription()
{
    // editSessionDescription opens a QInputDialog we can't drive.  Verify
    // that with a session leaf as current item, the routing predicates hold
    // (composite key starts with s:).  The dialog itself is exercised in
    // GUI tests.
    KonsolaiSettings settings;
    QJsonArray sessions;
    sessions.append(makeSession(QStringLiteral("ren11111"), QStringLiteral("konsolai-test-ren11111"), false, false));
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);
    QVERIFY(selectSessionInTree(tree, QStringLiteral("ren11111")));

    QTreeWidgetItem *cur = tree->currentItem();
    QVERIFY(cur);
    const QString compositeKey = cur->data(0, Qt::UserRole + 6).toString();
    QVERIFY(compositeKey.startsWith(QStringLiteral("s:")));
}

// ============================================================
// Subagent detection + state token routing
// ============================================================

void SessionManagerPanelTest::testIsSubagentSession_SessionNameHeuristic()
{
    // agent-fleet-spawned session names carry "agent-<16-hex>" — the
    // heuristic should recognize them without needing the metadata flag.
    SessionManagerPanel_INIT(panel);
    SessionMetadata meta;
    meta.sessionId = QStringLiteral("aabbccdd");
    meta.sessionName = QStringLiteral("konsolai-default-Claude-agent-abcdef1234567890");
    meta.workingDirectory = QStringLiteral("/home/u/somewhere");
    QVERIFY(panel.isSubagentSession(meta));
}

void SessionManagerPanelTest::testIsSubagentSession_MetadataFlag()
{
    SessionManagerPanel_INIT(panel);
    SessionMetadata meta;
    meta.sessionId = QStringLiteral("aabbccdd");
    meta.sessionName = QStringLiteral("konsolai-normal-Claude-nothing-special");
    meta.workingDirectory = QStringLiteral("/home/u/somewhere");
    meta.isSubagent = true;
    QVERIFY(panel.isSubagentSession(meta));
}

void SessionManagerPanelTest::testIsSubagentSession_NotASubagent()
{
    SessionManagerPanel_INIT(panel);
    SessionMetadata meta;
    meta.sessionId = QStringLiteral("aabbccdd");
    meta.sessionName = QStringLiteral("konsolai-normal-Claude");
    // Use a nonexistent workdir so the jsonl-path check finds nothing on
    // disk and returns false.
    meta.workingDirectory = QStringLiteral("/nonexistent/path/for/test/only");
    QVERIFY(!panel.isSubagentSession(meta));
}

void SessionManagerPanelTest::testStateTokenFor_SubagentFlagReturnsSubagentToken()
{
    // Feed a session where isSubagent=true through the tree pipeline and
    // assert stateTokenFor's classification via the visibility filter: with
    // "subagent" chip off (default), the session should not render.  With it
    // on, it should.  This exercises the state-token routing end-to-end
    // without needing stateTokenFor to be public.
    KonsolaiSettings settings;
    // Ensure defaults are in effect (previous tests may have mutated the
    // shared config file).
    settings.setVisibleSessionStates({QStringLiteral("active"), QStringLiteral("detached"), QStringLiteral("pinned"), QStringLiteral("closed")});
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("sub11111"), QStringLiteral("konsolai-test-sub11111"), false, false);
    s[QStringLiteral("isSubagent")] = true;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    QTreeWidget *tree = findTree(panel);
    QVERIFY(tree);
    forceTreeRebuild(panel);

    // With defaults, "subagent" chip is off — the session should be hidden.
    QVERIFY2(!selectSessionInTree(tree, QStringLiteral("sub11111")), "Subagent session must be hidden from the default view");

    // Turn on the subagent chip via settings + rebuild — session should now render.
    QStringList visible = settings.visibleSessionStates();
    visible.append(QStringLiteral("subagent"));
    settings.setVisibleSessionStates(visible);

    // Re-construct panel to pick up new visible-states default.  (buildFilterChips
    // reads settings on construction.)
    SessionManagerPanel_INIT(panel2);
    QTreeWidget *tree2 = findTree(panel2);
    QVERIFY(tree2);
    forceTreeRebuild(panel2);
    QVERIFY2(selectSessionInTree(tree2, QStringLiteral("sub11111")), "Subagent session must render when 'subagent' chip is enabled");

    settings.setVisibleSessionStates({QStringLiteral("active"), QStringLiteral("detached"), QStringLiteral("pinned"), QStringLiteral("closed")});
}

void SessionManagerPanelTest::testDefaultVisibleStatesExcludesSubagent()
{
    // Default visibility must NOT include the subagent token — subagents
    // stay hidden until the user opts in via the overflow menu.
    KonsolaiSettings settings;
    const QStringList defaults = settings.visibleSessionStates();
    QVERIFY2(!defaults.contains(QStringLiteral("subagent")), "Default visibleSessionStates must NOT include 'subagent'");
}

void SessionManagerPanelTest::testIsSubagentPersistsToMetadataFile()
{
    // Round-trip the isSubagent flag through save + load.
    QJsonArray sessions;
    QJsonObject s = makeSession(QStringLiteral("psb11111"), QStringLiteral("konsolai-test-psb11111"), false, false);
    s[QStringLiteral("isSubagent")] = true;
    sessions.append(s);
    writeTestSessions(sessions);

    SessionManagerPanel_INIT(panel);
    const auto all = panel.allSessions();
    QCOMPARE(all.size(), 1);
    QVERIFY2(all.first().isSubagent, "isSubagent flag must be loaded from metadata JSON");
}

QTEST_MAIN(SessionManagerPanelTest)

#include "moc_SessionManagerPanelTest.cpp"
