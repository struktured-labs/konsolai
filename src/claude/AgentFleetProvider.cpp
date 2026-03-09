/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AgentFleetProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace Konsolai
{

AgentFleetProvider::AgentFleetProvider(const QString &fleetPath, QObject *parent)
    : AgentProvider(parent)
    , m_fleetPath(fleetPath.isEmpty() ? detectFleetPath() : fleetPath)
{
    // File system watcher for real-time updates
    m_watcher = new QFileSystemWatcher(this);

    // Watch sessions directory for state changes
    QString sessDir = sessionsDir();
    if (QDir(sessDir).exists()) {
        m_watcher->addPath(sessDir);
    }

    // Watch goals directory for agent additions/removals
    QString gDir = goalsDir();
    if (QDir(gDir).exists()) {
        m_watcher->addPath(gDir);
    }

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &AgentFleetProvider::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &AgentFleetProvider::onDirectoryChanged);

    // 30s fallback timer
    m_fallbackTimer = new QTimer(this);
    m_fallbackTimer->setInterval(30000);
    connect(m_fallbackTimer, &QTimer::timeout, this, &AgentFleetProvider::reloadAgents);
    m_fallbackTimer->start();
}

AgentFleetProvider::~AgentFleetProvider() = default;

int AgentFleetProvider::interfaceVersion() const
{
    return 1;
}

QString AgentFleetProvider::name() const
{
    return QStringLiteral("agent-fleet");
}

bool AgentFleetProvider::isAvailable() const
{
    return !m_fleetPath.isEmpty() && QDir(goalsDir()).exists();
}

QString AgentFleetProvider::detectFleetPath()
{
    // Check common locations
    QStringList candidates = {
        QDir::homePath() + QStringLiteral("/projects/agent-fleet"),
        QDir::homePath() + QStringLiteral("/agent-fleet"),
        QDir::homePath() + QStringLiteral("/.agent-fleet"),
    };

    for (const QString &path : candidates) {
        if (QDir(path + QStringLiteral("/goals")).exists()) {
            return path;
        }
    }

    return {};
}

QString AgentFleetProvider::fleetPath() const
{
    return m_fleetPath;
}

void AgentFleetProvider::setConfigDir(const QString &dir)
{
    m_configDirOverride = dir;
}

QString AgentFleetProvider::configDir() const
{
    if (!m_configDirOverride.isEmpty()) {
        return m_configDirOverride;
    }
    return QDir::homePath() + QStringLiteral("/.config/agent-fleet");
}

QString AgentFleetProvider::sessionsDir() const
{
    return configDir() + QStringLiteral("/sessions");
}

QString AgentFleetProvider::budgetsDir() const
{
    return configDir() + QStringLiteral("/budgets");
}

QString AgentFleetProvider::briefsDir() const
{
    return configDir() + QStringLiteral("/briefs");
}

QString AgentFleetProvider::goalsDir() const
{
    return m_fleetPath + QStringLiteral("/goals");
}

QList<AgentInfo> AgentFleetProvider::agents() const
{
    if (m_cacheValid) {
        return m_cachedAgents;
    }

    m_cachedAgents.clear();

    QDir dir(goalsDir());
    if (!dir.exists()) {
        return m_cachedAgents;
    }

    const QStringList yamlFiles = dir.entryList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")}, QDir::Files, QDir::Name);

    for (const QString &file : yamlFiles) {
        AgentInfo info = parseGoalYaml(dir.absoluteFilePath(file));
        if (!info.id.isEmpty()) {
            m_cachedAgents.append(info);
        }
    }

    m_cacheValid = true;
    return m_cachedAgents;
}

AgentInfo AgentFleetProvider::parseGoalYaml(const QString &filePath) const
{
    AgentInfo info;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return info;
    }

    // Simple YAML parser for flat key-value pairs
    // agent-fleet goals are simple YAML: key: value per line
    info.id = QFileInfo(filePath).baseName();
    info.provider = name();

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        int colonPos = line.indexOf(QLatin1Char(':'));
        if (colonPos < 0) {
            continue;
        }

        QString key = line.left(colonPos).trimmed();
        QString value = line.mid(colonPos + 1).trimmed();

        // Strip quotes
        if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
            value = value.mid(1, value.length() - 2);
        }

        if (key == QLatin1String("name")) {
            info.name = value;
        } else if (key == QLatin1String("project") || key == QLatin1String("working_directory")) {
            info.project = value;
            // Expand ~ to home
            if (info.project.startsWith(QLatin1Char('~'))) {
                info.project = QDir::homePath() + info.project.mid(1);
            }
        } else if (key == QLatin1String("schedule")) {
            info.schedule = value;
        } else if (key == QLatin1String("goal") || key == QLatin1String("standing_goal")) {
            info.goal = value;
        } else if (key == QLatin1String("model")) {
            info.budget.model = value;
        } else if (key == QLatin1String("max_cost") || key == QLatin1String("per_run_budget")) {
            info.budget.perRunUSD = value.toDouble();
        } else if (key == QLatin1String("daily_budget")) {
            info.budget.dailyUSD = value.toDouble();
        }
    }

    // Default name to id if not specified
    if (info.name.isEmpty()) {
        info.name = info.id;
    }

    return info;
}

AgentStatus AgentFleetProvider::agentStatus(const QString &id) const
{
    AgentStatus status = parseSessionState(id);
    status.brief = parseBrief(id);
    status.dailySpentUSD = parseDailyBudgetSpend(id);
    return status;
}

AgentStatus AgentFleetProvider::parseSessionState(const QString &agentId) const
{
    AgentStatus status;

    QString path = sessionsDir() + QLatin1Char('/') + agentId + QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return status;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    QString stateStr = obj.value(QStringLiteral("state")).toString();
    if (stateStr == QLatin1String("running")) {
        status.state = AgentStatus::Running;
    } else if (stateStr == QLatin1String("budget")) {
        status.state = AgentStatus::Budget;
    } else if (stateStr == QLatin1String("error")) {
        status.state = AgentStatus::Error;
    } else if (stateStr == QLatin1String("paused")) {
        status.state = AgentStatus::Paused;
    } else {
        status.state = AgentStatus::Idle;
    }

    status.sessionId = obj.value(QStringLiteral("session_id")).toString();
    status.lastSummary = obj.value(QStringLiteral("last_summary")).toString();
    status.runCount = obj.value(QStringLiteral("run_count")).toInt();

    QString lastRunStr = obj.value(QStringLiteral("last_run")).toString();
    if (!lastRunStr.isEmpty()) {
        status.lastRun = QDateTime::fromString(lastRunStr, Qt::ISODate);
    }

    return status;
}

AgentBrief AgentFleetProvider::parseBrief(const QString &agentId) const
{
    AgentBrief brief;

    QString path = briefsDir() + QLatin1Char('/') + agentId + QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return brief;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    brief.direction = obj.value(QStringLiteral("direction")).toString();
    brief.isDone = obj.value(QStringLiteral("done")).toBool();

    QJsonArray notes = obj.value(QStringLiteral("steering_notes")).toArray();
    for (const QJsonValue &v : notes) {
        brief.steeringNotes.append(v.toString());
    }

    return brief;
}

double AgentFleetProvider::parseDailyBudgetSpend(const QString &agentId) const
{
    QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QString path = budgetsDir() + QLatin1Char('/') + date + QLatin1Char('/') + agentId + QStringLiteral(".json");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0.0;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object().value(QStringLiteral("total_cost_usd")).toDouble();
}

bool AgentFleetProvider::triggerRun(const QString &id, const QString &task)
{
    QStringList args = {QStringLiteral("trigger"), id};
    if (!task.isEmpty()) {
        args << task;
    }
    return runFleetCommand(args);
}

bool AgentFleetProvider::setBrief(const QString &id, const QString &direction)
{
    return runFleetCommand({QStringLiteral("brief"), id, direction});
}

bool AgentFleetProvider::addSteeringNote(const QString &id, const QString &note)
{
    return runFleetCommand({QStringLiteral("steer"), id, note});
}

bool AgentFleetProvider::markBriefDone(const QString &id)
{
    return runFleetCommand({QStringLiteral("done"), id});
}

QList<AgentReport> AgentFleetProvider::recentReports(const QString &id, int count) const
{
    // Find agent's project dir
    const QList<AgentInfo> allAgents = agents();
    for (const AgentInfo &info : allAgents) {
        if (info.id == id) {
            return parseReports(info.project, count);
        }
    }
    return {};
}

QList<AgentReport> AgentFleetProvider::parseReports(const QString &projectDir, int count) const
{
    QList<AgentReport> reports;
    if (projectDir.isEmpty()) {
        return reports;
    }

    QDir dir(projectDir + QStringLiteral("/reports"));
    if (!dir.exists()) {
        return reports;
    }

    QStringList mdFiles = dir.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Time);

    for (int i = 0; i < qMin(count, mdFiles.size()); ++i) {
        AgentReport report;
        report.filePath = dir.absoluteFilePath(mdFiles[i]);
        report.title = QFileInfo(mdFiles[i]).baseName();
        report.timestamp = QFileInfo(report.filePath).lastModified();

        QFile f(report.filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            report.content = QString::fromUtf8(f.readAll());
        }

        reports.append(report);
    }

    return reports;
}

QList<AgentRunResult> AgentFleetProvider::recentResults(const QString &id, int count) const
{
    return parseRunResults(id, count);
}

QList<AgentRunResult> AgentFleetProvider::parseRunResults(const QString &agentId, int count) const
{
    QList<AgentRunResult> results;

    // Results stored in config dir per agent
    QString resultsPath = configDir() + QStringLiteral("/results/") + agentId;
    QDir dir(resultsPath);
    if (!dir.exists()) {
        return results;
    }

    QStringList jsonFiles = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);

    for (int i = 0; i < qMin(count, jsonFiles.size()); ++i) {
        QFile file(dir.absoluteFilePath(jsonFiles[i]));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject obj = doc.object();

        AgentRunResult result;
        QString statusStr = obj.value(QStringLiteral("status")).toString();
        if (statusStr == QLatin1String("error")) {
            result.status = AgentRunResult::Error;
        } else if (statusStr == QLatin1String("budget")) {
            result.status = AgentRunResult::Budget;
        } else if (statusStr == QLatin1String("timeout")) {
            result.status = AgentRunResult::Timeout;
        } else {
            result.status = AgentRunResult::Ok;
        }

        result.summary = obj.value(QStringLiteral("summary")).toString();
        result.fullOutput = obj.value(QStringLiteral("output")).toString();
        result.costUSD = obj.value(QStringLiteral("cost_usd")).toDouble();
        result.exitCode = obj.value(QStringLiteral("exit_code")).toInt();
        result.sessionId = obj.value(QStringLiteral("session_id")).toString();
        result.conversationPath = obj.value(QStringLiteral("conversation_path")).toString();

        QString ts = obj.value(QStringLiteral("timestamp")).toString();
        if (!ts.isEmpty()) {
            result.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        }

        results.append(result);
    }

    return results;
}

AgentRunResult AgentFleetProvider::lastResult(const QString &id) const
{
    QList<AgentRunResult> results = recentResults(id, 1);
    return results.isEmpty() ? AgentRunResult() : results.first();
}

AgentAttachInfo AgentFleetProvider::attachInfo(const QString &id) const
{
    AgentAttachInfo info;

    // agent-fleet runs each agent in a tmux session named "af-{agentId}"
    info.tmuxSessionName = QStringLiteral("af-") + id;
    info.canAttach = true;

    // Find working directory and agent name from agent config
    info.agentId = id;
    const QList<AgentInfo> allAgents = agents();
    for (const AgentInfo &agent : allAgents) {
        if (agent.id == id) {
            info.workingDirectory = agent.project;
            info.agentName = agent.name;
            break;
        }
    }

    // Read session_id from state file for --resume support
    AgentStatus status = parseSessionState(id);
    if (!status.sessionId.isEmpty()) {
        info.resumeSessionId = status.sessionId;
    }

    return info;
}

bool AgentFleetProvider::createAgent(const AgentConfig &config)
{
    // Write a new YAML goal file
    QString path = goalsDir() + QLatin1Char('/') + config.name + QStringLiteral(".yaml");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << "name: " << config.name << "\n";
    out << "project: " << config.project << "\n";
    out << "goal: \"" << config.goal << "\"\n";
    out << "schedule: \"" << config.schedule << "\"\n";
    if (!config.budget.model.isEmpty()) {
        out << "model: " << config.budget.model << "\n";
    }
    if (config.budget.perRunUSD > 0.0) {
        out << "max_cost: " << config.budget.perRunUSD << "\n";
    }
    if (config.budget.dailyUSD > 0.0) {
        out << "daily_budget: " << config.budget.dailyUSD << "\n";
    }
    if (config.timeoutSeconds > 0) {
        out << "timeout: " << config.timeoutSeconds << "\n";
    }
    if (!config.permissionMode.isEmpty()) {
        out << "permission_mode: " << config.permissionMode << "\n";
    }

    file.close();
    m_cacheValid = false;
    Q_EMIT agentsReloaded();
    return true;
}

bool AgentFleetProvider::updateAgent(const QString &id, const AgentConfig &config)
{
    // Delete old and recreate
    QString oldPath = goalsDir() + QLatin1Char('/') + id + QStringLiteral(".yaml");
    if (QFile::exists(oldPath) && id != config.name) {
        QFile::remove(oldPath);
    }
    return createAgent(config);
}

bool AgentFleetProvider::deleteAgent(const QString &id)
{
    QString path = goalsDir() + QLatin1Char('/') + id + QStringLiteral(".yaml");
    bool ok = QFile::remove(path);
    if (ok) {
        m_cacheValid = false;
        Q_EMIT agentsReloaded();
    }
    return ok;
}

bool AgentFleetProvider::pauseSchedule(const QString &id)
{
    // Write a pause marker to the session state
    QString path = sessionsDir() + QLatin1Char('/') + id + QStringLiteral(".json");

    QFile file(path);
    QJsonObject obj;
    if (file.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    obj[QStringLiteral("state")] = QStringLiteral("paused");

    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson());
    return true;
}

bool AgentFleetProvider::resumeSchedule(const QString &id)
{
    QString path = sessionsDir() + QLatin1Char('/') + id + QStringLiteral(".json");

    QFile file(path);
    QJsonObject obj;
    if (file.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    obj[QStringLiteral("state")] = QStringLiteral("idle");

    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson());
    return true;
}

bool AgentFleetProvider::resetSession(const QString &id)
{
    QString path = sessionsDir() + QLatin1Char('/') + id + QStringLiteral(".json");

    QFile file(path);
    QJsonObject obj;
    if (file.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    obj.remove(QStringLiteral("session_id"));

    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson());
    return true;
}

double AgentFleetProvider::totalDailySpendUSD() const
{
    double total = 0.0;
    const QList<AgentInfo> allAgents = agents();
    for (const AgentInfo &info : allAgents) {
        total += parseDailyBudgetSpend(info.id);
    }
    return total;
}

double AgentFleetProvider::totalDailyBudgetUSD() const
{
    double total = 0.0;
    const QList<AgentInfo> allAgents = agents();
    for (const AgentInfo &info : allAgents) {
        total += info.budget.dailyUSD;
    }
    return total;
}

bool AgentFleetProvider::runFleetCommand(const QStringList &args, int timeoutMs) const
{
    QString binary = m_fleetPath + QStringLiteral("/agent-fleet");
    if (!QFile::exists(binary)) {
        // Try PATH
        binary = QStandardPaths::findExecutable(QStringLiteral("agent-fleet"));
        if (binary.isEmpty()) {
            return false;
        }
    }

    QProcess proc;
    proc.start(binary, args);
    if (!proc.waitForFinished(timeoutMs)) {
        return false;
    }
    return proc.exitCode() == 0;
}

void AgentFleetProvider::onFileChanged(const QString &path)
{
    Q_UNUSED(path);
    m_cacheValid = false;

    // Determine which agent changed from filename
    QString baseName = QFileInfo(path).baseName();
    Q_EMIT agentChanged(baseName);
}

void AgentFleetProvider::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path);
    reloadAgents();
}

void AgentFleetProvider::reloadAgents()
{
    m_cacheValid = false;
    Q_EMIT agentsReloaded();
}

} // namespace Konsolai

#include "moc_AgentFleetProvider.cpp"
