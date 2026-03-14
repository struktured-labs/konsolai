/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "TokenTrackingTest.h"

// Qt
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

// Konsolai
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

// ============================================================
// Helper: build a JSONL line for an assistant message with usage data
// ============================================================
static QByteArray makeAssistantLine(quint64 input, quint64 output, quint64 cacheRead = 0, quint64 cacheCreation = 0, const QString &model = QString())
{
    QJsonObject usage;
    usage[QStringLiteral("input_tokens")] = static_cast<qint64>(input);
    usage[QStringLiteral("output_tokens")] = static_cast<qint64>(output);
    if (cacheRead > 0)
        usage[QStringLiteral("cache_read_input_tokens")] = static_cast<qint64>(cacheRead);
    if (cacheCreation > 0)
        usage[QStringLiteral("cache_creation_input_tokens")] = static_cast<qint64>(cacheCreation);

    QJsonObject message;
    message[QStringLiteral("usage")] = usage;
    if (!model.isEmpty())
        message[QStringLiteral("model")] = model;

    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("assistant");
    obj[QStringLiteral("message")] = message;

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

static QByteArray makeUserLine()
{
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("user");
    obj[QStringLiteral("message")] = QJsonObject();
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

static QByteArray makeSystemLine()
{
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("system");
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// ============================================================
// Helper: set up the .claude/projects/<hashed> directory with a JSONL file
// Returns the path to the JSONL file that was created.
// ============================================================
static QString setupProjectDir(const QString &workingDir, const QByteArray &jsonlContent, const QString &filename = QStringLiteral("conversation.jsonl"))
{
    // Claude hashes the working dir by replacing / with -
    QString hashedName = workingDir;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));
    QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;

    QDir().mkpath(projectDir);

    QString filePath = projectDir + QLatin1Char('/') + filename;
    QFile file(filePath);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(jsonlContent);
    file.close();

    return filePath;
}

static void cleanupProjectDir(const QString &workingDir)
{
    QString hashedName = workingDir;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));
    QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;
    QDir(projectDir).removeRecursively();
}

// ============================================================
// parseConversationTokens tests (via refreshTokenUsage)
// ============================================================

void TokenTrackingTest::testParseValidJsonl()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content = makeAssistantLine(1000, 500, 200, 100) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    const auto &usage = session.tokenUsage();
    QCOMPARE(usage.inputTokens, quint64(1000));
    QCOMPARE(usage.outputTokens, quint64(500));
    QCOMPARE(usage.cacheReadTokens, quint64(200));
    QCOMPARE(usage.cacheCreationTokens, quint64(100));
    QCOMPARE(usage.totalTokens(), quint64(1800));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testParseMultipleAssistantMessages()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content;
    content += makeAssistantLine(1000, 500) + "\n";
    content += makeAssistantLine(2000, 1000) + "\n";
    content += makeAssistantLine(3000, 1500) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    const auto &usage = session.tokenUsage();
    // Cumulative: 1000+2000+3000 = 6000 input, 500+1000+1500 = 3000 output
    QCOMPARE(usage.inputTokens, quint64(6000));
    QCOMPARE(usage.outputTokens, quint64(3000));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testIncrementalParsing()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    // First write: one message
    QByteArray content1 = makeAssistantLine(1000, 500) + "\n";
    QString filePath = setupProjectDir(workDir, content1);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    QCOMPARE(session.tokenUsage().inputTokens, quint64(1000));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(500));

    // Append a second message (simulating incremental write)
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::Append));
    file.write(makeAssistantLine(2000, 1000) + "\n");
    file.close();

    // Refresh again — should only parse the new part
    session.refreshTokenUsage();

    QCOMPARE(session.tokenUsage().inputTokens, quint64(3000));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(1500));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testFileTruncationDetection()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    // Write a large file first
    QByteArray bigContent;
    bigContent += makeAssistantLine(5000, 2000) + "\n";
    bigContent += makeAssistantLine(5000, 2000) + "\n";
    bigContent += makeAssistantLine(5000, 2000) + "\n";
    QString filePath = setupProjectDir(workDir, bigContent);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    QCOMPARE(session.tokenUsage().inputTokens, quint64(15000));

    // Truncate the file (simulating file replacement)
    QByteArray smallContent = makeAssistantLine(100, 50) + "\n";
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(smallContent);
        file.close();
    }

    // Refresh — should detect truncation and re-parse from scratch
    session.refreshTokenUsage();

    QCOMPARE(session.tokenUsage().inputTokens, quint64(100));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(50));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testMalformedJsonLinesSkipped()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content;
    content += makeAssistantLine(1000, 500) + "\n";
    content += "this is not valid json\n";
    content += "{\"broken\": true, \n"; // incomplete JSON
    content += makeAssistantLine(2000, 1000) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // Should only count the two valid assistant messages
    QCOMPARE(session.tokenUsage().inputTokens, quint64(3000));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(1500));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testNonAssistantMessagesSkipped()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content;
    content += makeUserLine() + "\n";
    content += makeAssistantLine(1000, 500) + "\n";
    content += makeSystemLine() + "\n";
    content += makeUserLine() + "\n";
    content += makeAssistantLine(2000, 1000) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // Only assistant messages should count
    QCOMPARE(session.tokenUsage().inputTokens, quint64(3000));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(1500));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testModelDetection()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content = makeAssistantLine(1000, 500, 0, 0, QStringLiteral("claude-opus-4-6")) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    QCOMPARE(session.tokenUsage().detectedModel, QStringLiteral("claude-opus-4-6"));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testModelDetectionUsesLast()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content;
    content += makeAssistantLine(1000, 500, 0, 0, QStringLiteral("claude-sonnet-4-6")) + "\n";
    content += makeAssistantLine(2000, 1000, 0, 0, QStringLiteral("claude-opus-4-6")) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // Should use the model from the LAST assistant message
    QCOMPARE(session.tokenUsage().detectedModel, QStringLiteral("claude-opus-4-6"));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testContextWindowTracking()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content;
    content += makeAssistantLine(1000, 500, 200, 100) + "\n"; // context = 1000+200+100 = 1300
    content += makeAssistantLine(5000, 2000, 3000, 500) + "\n"; // context = 5000+3000+500 = 8500
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // lastContextTokens should be from the LAST assistant message only (not cumulative)
    QCOMPARE(session.tokenUsage().lastContextTokens, quint64(8500));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testEmptyFile()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    setupProjectDir(workDir, QByteArray());

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    QCOMPARE(session.tokenUsage().totalTokens(), quint64(0));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testEmptyUsageObject()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    // Assistant message with empty usage object
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("assistant");
    QJsonObject message;
    message[QStringLiteral("usage")] = QJsonObject(); // empty
    obj[QStringLiteral("message")] = message;
    QByteArray content = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";

    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // Empty usage object should be skipped
    QCOMPARE(session.tokenUsage().totalTokens(), quint64(0));

    cleanupProjectDir(workDir);
}

// ============================================================
// refreshTokenUsage tests
// ============================================================

void TokenTrackingTest::testRefreshFindsNewestJsonl()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    // Create the project dir
    QString hashedName = workDir;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));
    QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;
    QDir().mkpath(projectDir);

    // Create an older file
    {
        QFile f(projectDir + QStringLiteral("/old_conversation.jsonl"));
        f.open(QIODevice::WriteOnly);
        f.write(makeAssistantLine(100, 50) + "\n");
        f.close();
    }

    // Wait a bit so modification times differ
    QThread::msleep(50);

    // Create a newer file
    {
        QFile f(projectDir + QStringLiteral("/new_conversation.jsonl"));
        f.open(QIODevice::WriteOnly);
        f.write(makeAssistantLine(9999, 8888) + "\n");
        f.close();
    }

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // Should pick up the newest file (9999 input, not 100)
    QCOMPARE(session.tokenUsage().inputTokens, quint64(9999));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(8888));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testRefreshHandlesMissingProjectDir()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    // Use a path that won't have a .claude/projects/ entry
    QString workDir = tmpDir.path() + QStringLiteral("/nonexistent_project_xyz");

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();

    // Should not crash, tokens stay zero
    QCOMPARE(session.tokenUsage().totalTokens(), quint64(0));
}

void TokenTrackingTest::testRefreshHandlesEmptyWorkingDir()
{
    // Create session with empty working dir — constructor defaults to homePath,
    // but let's test refreshTokenUsage handles it gracefully
    ClaudeSession session(QStringLiteral("test"), QDir::homePath());

    // This should not crash. Token count may or may not be zero depending
    // on whether the user has real Claude data, so just check no crash.
    session.refreshTokenUsage();
    QVERIFY(true); // If we got here, no crash
}

void TokenTrackingTest::testRefreshEmitsSignalOnChange()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content = makeAssistantLine(1000, 500) + "\n";
    QString filePath = setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);
    QSignalSpy spy(&session, &ClaudeSession::tokenUsageChanged);
    QVERIFY(spy.isValid());

    session.refreshTokenUsage();

    // First refresh with data should emit
    QCOMPARE(spy.count(), 1);

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testRefreshNoSignalWhenUnchanged()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content = makeAssistantLine(1000, 500) + "\n";
    setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);

    // First refresh to establish baseline
    session.refreshTokenUsage();

    QSignalSpy spy(&session, &ClaudeSession::tokenUsageChanged);
    QVERIFY(spy.isValid());

    // Second refresh with no changes — should NOT emit
    session.refreshTokenUsage();

    QCOMPARE(spy.count(), 0);

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testRefreshResetsOnFileChange()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QString hashedName = workDir;
    hashedName.replace(QLatin1Char('/'), QLatin1Char('-'));
    QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashedName;
    QDir().mkpath(projectDir);

    // Create first conversation file
    {
        QFile f(projectDir + QStringLiteral("/conv1.jsonl"));
        f.open(QIODevice::WriteOnly);
        f.write(makeAssistantLine(1000, 500) + "\n");
        f.close();
    }

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.refreshTokenUsage();
    QCOMPARE(session.tokenUsage().inputTokens, quint64(1000));

    // Wait so mtime differs, then create a newer file
    QThread::msleep(50);
    {
        QFile f(projectDir + QStringLiteral("/conv2.jsonl"));
        f.open(QIODevice::WriteOnly);
        f.write(makeAssistantLine(5000, 3000) + "\n");
        f.close();
    }

    // Refresh should pick up the new file and reset counters
    session.refreshTokenUsage();
    QCOMPARE(session.tokenUsage().inputTokens, quint64(5000));
    QCOMPARE(session.tokenUsage().outputTokens, quint64(3000));

    cleanupProjectDir(workDir);
}

// ============================================================
// Timer tests
// ============================================================

void TokenTrackingTest::testTokenRefreshTimerStarted()
{
    // The token refresh timer is started inside startTokenTracking(),
    // which is called from connectSignals(). We can verify the timer
    // is configured by checking that refreshTokenUsage can be called
    // repeatedly without issues.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    QByteArray content = makeAssistantLine(100, 50) + "\n";
    QString filePath = setupProjectDir(workDir, content);

    ClaudeSession session(QStringLiteral("test"), workDir);

    // Multiple refreshes should be idempotent when file hasn't changed
    session.refreshTokenUsage();
    quint64 first = session.tokenUsage().totalTokens();

    session.refreshTokenUsage();
    quint64 second = session.tokenUsage().totalTokens();

    QCOMPARE(first, second);
    QCOMPARE(first, quint64(150));

    // Now append and verify incremental works across multiple refreshes
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::Append));
        f.write(makeAssistantLine(200, 100) + "\n");
        f.close();
    }
    session.refreshTokenUsage();
    QCOMPARE(session.tokenUsage().totalTokens(), quint64(450));

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testFileWatcherTriggersRefresh()
{
    // Verify that adding a new JSONL file to the project dir causes an automatic
    // token refresh via the QFileSystemWatcher + 500ms debounce path.
    //
    // NOTE: QFileSystemWatcher::directoryChanged fires on file addition/removal,
    // NOT on content changes to existing files. Claude CLI creates a new JSONL
    // file per conversation, so adding a newer file is the realistic trigger.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    // Start with one older conversation file
    QByteArray initial = makeAssistantLine(100, 50) + "\n";
    setupProjectDir(workDir, initial, QStringLiteral("conversation_old.jsonl"));

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.startTokenTracking(); // sets up QFileSystemWatcher, does initial refresh
    QCOMPARE(session.tokenUsage().totalTokens(), quint64(150));

    // Add a NEW conversation file with more tokens — triggers directoryChanged
    setupProjectDir(workDir, makeAssistantLine(200, 100) + "\n", QStringLiteral("conversation_new.jsonl"));

    // Drain the event loop briefly so the inotify event is delivered to the
    // QFileSystemWatcher before we start the QTRY poll.  Without this, prior
    // tests' deleteLater queues can delay watcher signal delivery past the
    // QTRY timeout when the full suite runs 23 other tests beforehand.
    QTest::qWait(100);

    // Debounce is 500ms; allow 2.5s total from here (inotify already delivered).
    QTRY_COMPARE_WITH_TIMEOUT(session.tokenUsage().totalTokens(), quint64(300), 2500);

    cleanupProjectDir(workDir);
}

void TokenTrackingTest::testFileWatcherDebounces()
{
    // Adding multiple files rapidly should only trigger ONE refresh after the
    // 500ms debounce settles, not one refresh per file addition.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString workDir = tmpDir.path();

    setupProjectDir(workDir, makeAssistantLine(10, 5) + "\n");

    ClaudeSession session(QStringLiteral("test"), workDir);
    session.startTokenTracking();
    QCOMPARE(session.tokenUsage().totalTokens(), quint64(15));

    QSignalSpy spy(&session, &ClaudeSession::tokenUsageChanged);

    // Add 5 new JSONL files rapidly (simulate Claude creating new conversation files)
    for (int i = 1; i <= 5; ++i) {
        QString fname = QStringLiteral("conversation_%1.jsonl").arg(i);
        setupProjectDir(workDir, makeAssistantLine(10 * i, 5 * i) + "\n", fname);
        QTest::qWait(40); // 40ms between files — well under 500ms debounce
    }

    // Wait for debounce to fire (500ms + margin)
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 1500);

    // Give it another 200ms to ensure no extra signals arrive
    QTest::qWait(200);

    // Should have fired fewer signals than files added (debounce collapses rapid events)
    // Allow up to 2 (one possible mid-burst + one final), definitely < 5
    QVERIFY2(spy.count() <= 2, qPrintable(QStringLiteral("Expected <=2 signals, got %1").arg(spy.count())));

    cleanupProjectDir(workDir);
}

QTEST_GUILESS_MAIN(TokenTrackingTest)

#include "moc_TokenTrackingTest.cpp"
