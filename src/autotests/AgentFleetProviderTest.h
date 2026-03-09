/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AGENTFLEETPROVIDERTEST_H
#define AGENTFLEETPROVIDERTEST_H

#include <QObject>
#include <QTemporaryDir>

namespace Konsolai
{

class AgentFleetProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    // Provider identity
    void testName();
    void testInterfaceVersion();

    // Availability
    void testIsAvailable_WithGoalsDir();
    void testIsAvailable_WithoutGoalsDir();
    void testIsAvailable_EmptyPath();

    // Auto-detection
    void testDetectFleetPath_NotFound();

    // Goal parsing
    void testParseGoalYaml_BasicFields();
    void testParseGoalYaml_AllFields();
    void testParseGoalYaml_QuotedValues();
    void testParseGoalYaml_TildeExpansion();
    void testParseGoalYaml_NameFallbackToId();
    void testParseGoalYaml_IgnoresComments();

    // Agent listing
    void testAgents_Empty();
    void testAgents_MultipleGoals();
    void testAgents_CachesResults();
    void testAgents_CacheInvalidation();

    // Status parsing
    void testAgentStatus_NoStateFile();
    void testAgentStatus_IdleState();
    void testAgentStatus_RunningState();
    void testAgentStatus_ErrorState();
    void testAgentStatus_BudgetState();
    void testAgentStatus_PausedState();
    void testAgentStatus_WithSessionId();
    void testAgentStatus_WithLastRun();

    // Brief parsing
    void testBrief_NoBriefFile();
    void testBrief_WithDirection();
    void testBrief_WithSteeringNotes();
    void testBrief_DoneFlag();

    // Daily budget spend
    void testDailySpend_NoBudgetFile();
    void testDailySpend_WithSpend();

    // Reports
    void testRecentReports_NoReportsDir();
    void testRecentReports_WithReports();
    void testRecentReports_LimitsCount();

    // Run results
    void testRecentResults_NoResultsDir();
    void testRecentResults_WithResults();
    void testLastResult_Empty();
    void testLastResult_WithResult();

    // Attach info
    void testAttachInfo_TmuxSessionName();
    void testAttachInfo_WorkingDirectory();
    void testAttachInfo_ResumeSessionId();
    void testAttachInfo_ResumeSessionId_Empty();

    // CRUD operations
    void testCreateAgent_WritesYaml();
    void testDeleteAgent_RemovesFile();
    void testUpdateAgent_RewritesYaml();

    // Schedule control
    void testPauseSchedule_SetsState();
    void testResumeSchedule_SetsState();

    // Session reset
    void testResetSession_ClearsSessionId();

    // Aggregate spending
    void testTotalDailySpend();
    void testTotalDailyBudget();

    // File watcher signals
    void testAgentsReloaded_OnDirectoryChange();

private:
    void writeGoalYaml(const QString &name, const QString &content);
    void writeSessionState(const QString &name, const QByteArray &json);
    void writeBrief(const QString &name, const QByteArray &json);
    void writeBudget(const QString &name, const QByteArray &json);
    void writeReport(const QString &projectDir, const QString &filename, const QString &content);
    void writeRunResult(const QString &agentId, const QString &filename, const QByteArray &json);

    QTemporaryDir *m_tempDir = nullptr;
    QString m_fleetPath;
    QString m_configDir;
};

}

#endif // AGENTFLEETPROVIDERTEST_H
