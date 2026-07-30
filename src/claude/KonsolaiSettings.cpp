/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "KonsolaiSettings.h"

#include <KConfigGroup>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

namespace Konsolai
{

KonsolaiSettings *s_settingsInstance = nullptr;

KonsolaiSettings *KonsolaiSettings::instance()
{
    return s_settingsInstance;
}

KonsolaiSettings::KonsolaiSettings(QObject *parent)
    : QObject(parent)
{
    if (!s_settingsInstance) {
        s_settingsInstance = this;
    }

    // Load config from ~/.config/konsolai/konsolairc
    m_config = KSharedConfig::openConfig(QStringLiteral("konsolairc"));
}

KonsolaiSettings::~KonsolaiSettings()
{
    save();
    if (s_settingsInstance == this) {
        s_settingsInstance = nullptr;
    }
}

QString KonsolaiSettings::projectRoot() const
{
    KConfigGroup group(m_config, QStringLiteral("General"));
    QString defaultRoot = detectDefaultProjectRoot();
    return group.readEntry("ProjectRoot", defaultRoot);
}

QString KonsolaiSettings::detectDefaultProjectRoot()
{
    const QString home = QDir::homePath();
    const QStringList candidates = {
        home + QStringLiteral("/projects"),
        home + QStringLiteral("/code"),
        home + QStringLiteral("/workspace"),
        home + QStringLiteral("/src"),
    };

    for (const QString &dir : candidates) {
        if (QDir(dir).exists()) {
            return dir;
        }
    }

    return home;
}

void KonsolaiSettings::setProjectRoot(const QString &path)
{
    KConfigGroup group(m_config, QStringLiteral("General"));
    group.writeEntry("ProjectRoot", path);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::gitRemoteRoot() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    // Default to git@github.com:<username>/
    QString defaultRemote = QStringLiteral("git@github.com:%1/").arg(qEnvironmentVariable("USER"));
    return group.readEntry("RemoteRoot", defaultRemote);
}

void KonsolaiSettings::setGitRemoteRoot(const QString &url)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("RemoteRoot", url);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::githubApiToken() const
{
    KConfigGroup group(m_config, QStringLiteral("GitHub"));
    return group.readEntry("ApiToken", QString());
}

void KonsolaiSettings::setGithubApiToken(const QString &token)
{
    KConfigGroup group(m_config, QStringLiteral("GitHub"));
    group.writeEntry("ApiToken", token);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::defaultModel() const
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    // The [1m] suffix opts into the 1M-token context beta. Bare "claude-opus-5"
    // would be the 200K window; the default deliberately requests 1M.
    return group.readEntry("DefaultModel", QStringLiteral("claude-opus-5[1m]"));
}

QString KonsolaiSettings::claudePermissionMode() const
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    // auto lets the CLI decide approvals itself, which is the point of not using
    // konsolai's yolo mode: the CLI has more context about a given tool call than
    // a terminal-scraping approver does. acceptEdits was the previous default and
    // still prompts for anything beyond file edits.
    // Valid values (from `claude --help`, verified 2026-07-29): acceptEdits, auto,
    // bypassPermissions, manual, dontAsk, plan. The CLI rejects anything else
    // outright, so a typo here breaks session launch rather than degrading it.
    return group.readEntry("PermissionMode", QStringLiteral("auto"));
}

void KonsolaiSettings::setClaudePermissionMode(const QString &mode)
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    group.writeEntry("PermissionMode", mode);
    m_config->sync();
}

QString KonsolaiSettings::codexModel() const
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    return group.readEntry("Model", QStringLiteral("gpt-5.6-sol"));
}

void KonsolaiSettings::setCodexModel(const QString &model)
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    group.writeEntry("Model", model);
    m_config->sync();
}

QString KonsolaiSettings::codexEffort() const
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    return group.readEntry("Effort", QStringLiteral("xhigh"));
}

void KonsolaiSettings::setCodexEffort(const QString &effort)
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    group.writeEntry("Effort", effort);
    m_config->sync();
}

QString KonsolaiSettings::codexApprovalPolicy() const
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    // Empty by default: pass no -a flag and let Codex apply its own default,
    // which is the "Approve for me" profile (only prompts for actions detected
    // as potentially unsafe).
    //
    // Do NOT set this to "never" expecting more autonomy — verified against
    // codex 0.145, passing `-a never -s workspace-write` selects the *stricter*
    // "Ask for approval" profile, which prompts for internet access and any
    // edit outside the workspace. The profiles are not a function of these two
    // flags alone.
    return group.readEntry("ApprovalPolicy", QString());
}

void KonsolaiSettings::setCodexApprovalPolicy(const QString &policy)
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    group.writeEntry("ApprovalPolicy", policy);
    m_config->sync();
}

QString KonsolaiSettings::codexSandbox() const
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    // Empty by default — see codexApprovalPolicy(). Passing -s alongside -a
    // pins the stricter "Ask for approval" profile rather than widening it.
    return group.readEntry("Sandbox", QString());
}

void KonsolaiSettings::setCodexSandbox(const QString &sandbox)
{
    KConfigGroup group(m_config, QStringLiteral("Codex"));
    group.writeEntry("Sandbox", sandbox);
    m_config->sync();
}

void KonsolaiSettings::setDefaultModel(const QString &model)
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    group.writeEntry("DefaultModel", model);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::extraClaudeArgs() const
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    // The channel target is the plugin's MCP namespace, NOT a top-level entry
    // in ~/.claude.json / .mcp.json. "server:session-intercom" only matches a
    // user-installed top-level MCP server (which we don't ship), so it
    // silently no-ops. The plugin namespace below is what actually binds.
    return group.readEntry("ExtraArgs", QStringLiteral("--dangerously-load-development-channels plugin:session-intercom@struktured-labs"));
}

void KonsolaiSettings::setExtraClaudeArgs(const QString &args)
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    group.writeEntry("ExtraArgs", args);
    Q_EMIT settingsChanged();
}

int KonsolaiSettings::gitMode() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    // Default to "Nothing (use current branch)" — GitCurrentBranch. Creating
    // repos or worktrees up front pre-commits to a layout; leaving git alone
    // lets Claude decide what (if anything) to do once the session is running.
    return group.readEntry("GitMode", 2);
}

void KonsolaiSettings::setGitMode(int mode)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("GitMode", mode);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::worktreeSourceRepo() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    return group.readEntry("WorktreeSourceRepo", QString());
}

void KonsolaiSettings::setWorktreeSourceRepo(const QString &path)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("WorktreeSourceRepo", path);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::yoloMode() const
{
    KConfigGroup group(m_config, QStringLiteral("YoloMode"));
    return group.readEntry("Enabled", false);
}

void KonsolaiSettings::setYoloMode(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("YoloMode"));
    group.writeEntry("Enabled", enabled);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::doubleYoloMode() const
{
    KConfigGroup group(m_config, QStringLiteral("YoloMode"));
    return group.readEntry("DoubleEnabled", false);
}

void KonsolaiSettings::setDoubleYoloMode(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("YoloMode"));
    group.writeEntry("DoubleEnabled", enabled);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::trySuggestionsFirst() const
{
    KConfigGroup group(m_config, QStringLiteral("YoloMode"));
    return group.readEntry("TrySuggestionsFirst", true);
}

void KonsolaiSettings::setTrySuggestionsFirst(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("YoloMode"));
    group.writeEntry("TrySuggestionsFirst", enabled);
    Q_EMIT settingsChanged();
}

// ========== Budget Defaults ==========

int KonsolaiSettings::defaultTimeLimitMinutes() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return group.readEntry("TimeLimitMinutes", 0);
}

void KonsolaiSettings::setDefaultTimeLimitMinutes(int minutes)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("TimeLimitMinutes", minutes);
    Q_EMIT settingsChanged();
}

double KonsolaiSettings::defaultCostCeilingUSD() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return group.readEntry("CostCeilingUSD", 0.0);
}

void KonsolaiSettings::setDefaultCostCeilingUSD(double cost)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("CostCeilingUSD", cost);
    Q_EMIT settingsChanged();
}

int KonsolaiSettings::defaultBudgetPolicy() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return group.readEntry("Policy", 0); // 0 = Soft
}

void KonsolaiSettings::setDefaultBudgetPolicy(int policy)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("Policy", policy);
    Q_EMIT settingsChanged();
}

quint64 KonsolaiSettings::defaultTokenCeiling() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return static_cast<quint64>(group.readEntry("TokenCeiling", 0));
}

void KonsolaiSettings::setDefaultTokenCeiling(quint64 tokens)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("TokenCeiling", static_cast<qint64>(tokens));
    Q_EMIT settingsChanged();
}

double KonsolaiSettings::budgetWarningThresholdPercent() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return group.readEntry("WarningThresholdPercent", 80.0);
}

void KonsolaiSettings::setBudgetWarningThresholdPercent(double percent)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("WarningThresholdPercent", percent);
    Q_EMIT settingsChanged();
}

double KonsolaiSettings::weeklyBudgetUSD() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return group.readEntry("WeeklyBudgetUSD", 0.0);
}

void KonsolaiSettings::setWeeklyBudgetUSD(double budget)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("WeeklyBudgetUSD", budget);
    Q_EMIT settingsChanged();
}

double KonsolaiSettings::monthlyBudgetUSD() const
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    return group.readEntry("MonthlyBudgetUSD", 0.0);
}

void KonsolaiSettings::setMonthlyBudgetUSD(double budget)
{
    KConfigGroup group(m_config, QStringLiteral("Budget"));
    group.writeEntry("MonthlyBudgetUSD", budget);
    Q_EMIT settingsChanged();
}

// ========== Notification Settings ==========

bool KonsolaiSettings::notificationAudioEnabled() const
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    return group.readEntry("AudioEnabled", true);
}

void KonsolaiSettings::setNotificationAudioEnabled(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    group.writeEntry("AudioEnabled", enabled);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::notificationDesktopEnabled() const
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    return group.readEntry("DesktopEnabled", true);
}

void KonsolaiSettings::setNotificationDesktopEnabled(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    group.writeEntry("DesktopEnabled", enabled);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::notificationSystemTrayEnabled() const
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    return group.readEntry("SystemTrayEnabled", true);
}

void KonsolaiSettings::setNotificationSystemTrayEnabled(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    group.writeEntry("SystemTrayEnabled", enabled);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::notificationInTerminalEnabled() const
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    return group.readEntry("InTerminalEnabled", true);
}

void KonsolaiSettings::setNotificationInTerminalEnabled(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    group.writeEntry("InTerminalEnabled", enabled);
    Q_EMIT settingsChanged();
}

double KonsolaiSettings::notificationAudioVolume() const
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    return group.readEntry("AudioVolume", 0.7);
}

void KonsolaiSettings::setNotificationAudioVolume(double volume)
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    group.writeEntry("AudioVolume", volume);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::notificationYoloEnabled() const
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    return group.readEntry("YoloNotificationsEnabled", false);
}

void KonsolaiSettings::setNotificationYoloEnabled(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Notifications"));
    group.writeEntry("YoloNotificationsEnabled", enabled);
    Q_EMIT settingsChanged();
}

// ========== Session Tree View ==========

QStringList KonsolaiSettings::visibleSessionStates() const
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    // Default includes "closed" because typical users have many sessions whose
    // claude process isn't running right now but which they still expect to
    // see in the tree. Archived / dismissed / discovered stay opt-in (overflow
    // menu) because they're long-tail and noisy.
    const QStringList defaults = {QStringLiteral("active"), QStringLiteral("detached"), QStringLiteral("pinned"), QStringLiteral("closed")};
    return group.readEntry("VisibleStates", defaults);
}

void KonsolaiSettings::setVisibleSessionStates(const QStringList &states)
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    group.writeEntry("VisibleStates", states);
    Q_EMIT settingsChanged();
}

namespace
{

// Serialize a hash to "k1=v1,k2=v2" pairs suitable for KConfigGroup::writeEntry(QStringList).
QStringList serializeHash(const QHash<QString, QString> &hash)
{
    QStringList out;
    out.reserve(hash.size());
    for (auto it = hash.cbegin(); it != hash.cend(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        out.append(it.key() + QLatin1Char('=') + it.value());
    }
    std::sort(out.begin(), out.end()); // deterministic order on disk
    return out;
}

QHash<QString, QString> parseHash(const QStringList &pairs)
{
    QHash<QString, QString> out;
    for (const QString &pair : pairs) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString k = pair.left(eq);
        const QString v = pair.mid(eq + 1);
        if (!k.isEmpty()) {
            out.insert(k, v);
        }
    }
    return out;
}

} // namespace

QHash<QString, QString> KonsolaiSettings::categoryAliases() const
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    return parseHash(group.readEntry("CategoryAliases", QStringList{}));
}

void KonsolaiSettings::setCategoryAliases(const QHash<QString, QString> &aliases)
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    group.writeEntry("CategoryAliases", serializeHash(aliases));
    Q_EMIT settingsChanged();
}

void KonsolaiSettings::addCategoryAlias(const QString &source, const QString &target)
{
    if (source.isEmpty() || target.isEmpty() || source == target) {
        return;
    }
    QHash<QString, QString> h = categoryAliases();
    h.insert(source, target);
    setCategoryAliases(h);
}

void KonsolaiSettings::removeCategoryAlias(const QString &source)
{
    QHash<QString, QString> h = categoryAliases();
    if (h.remove(source) > 0) {
        setCategoryAliases(h);
    }
}

QHash<QString, QString> KonsolaiSettings::workdirCategoryOverrides() const
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    return parseHash(group.readEntry("WorkdirCategoryOverrides", QStringList{}));
}

void KonsolaiSettings::setWorkdirCategoryOverrides(const QHash<QString, QString> &overrides)
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    group.writeEntry("WorkdirCategoryOverrides", serializeHash(overrides));
    Q_EMIT settingsChanged();
}

void KonsolaiSettings::addWorkdirCategoryOverride(const QString &workdir, const QString &category)
{
    if (workdir.isEmpty() || category.isEmpty()) {
        return;
    }
    QHash<QString, QString> h = workdirCategoryOverrides();
    h.insert(workdir, category);
    setWorkdirCategoryOverrides(h);
}

void KonsolaiSettings::removeWorkdirCategoryOverride(const QString &workdir)
{
    QHash<QString, QString> h = workdirCategoryOverrides();
    if (h.remove(workdir) > 0) {
        setWorkdirCategoryOverrides(h);
    }
}

QStringList KonsolaiSettings::suppressedCategories() const
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    return group.readEntry("SuppressCategories", QStringList{});
}

void KonsolaiSettings::setSuppressedCategories(const QStringList &names)
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    // Deduplicate + sort for deterministic on-disk state.
    QStringList clean;
    QSet<QString> seen;
    for (const QString &n : names) {
        if (n.isEmpty() || seen.contains(n)) {
            continue;
        }
        seen.insert(n);
        clean.append(n);
    }
    std::sort(clean.begin(), clean.end());
    group.writeEntry("SuppressCategories", clean);
    Q_EMIT settingsChanged();
}

void KonsolaiSettings::addSuppressedCategory(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    QStringList list = suppressedCategories();
    if (list.contains(name)) {
        return;
    }
    list.append(name);
    setSuppressedCategories(list);
}

void KonsolaiSettings::removeSuppressedCategory(const QString &name)
{
    QStringList list = suppressedCategories();
    if (list.removeAll(name) > 0) {
        setSuppressedCategories(list);
    }
}

QStringList KonsolaiSettings::userCategories() const
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    return group.readEntry("UserCategories", QStringList{});
}

void KonsolaiSettings::setUserCategories(const QStringList &names)
{
    KConfigGroup group(m_config, QStringLiteral("SessionTree"));
    // Deduplicate + sort for deterministic on-disk state (mirrors
    // setSuppressedCategories).
    QStringList clean;
    QSet<QString> seen;
    for (const QString &n : names) {
        if (n.isEmpty() || seen.contains(n)) {
            continue;
        }
        seen.insert(n);
        clean.append(n);
    }
    std::sort(clean.begin(), clean.end());
    group.writeEntry("UserCategories", clean);
    Q_EMIT settingsChanged();
}

void KonsolaiSettings::addUserCategory(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    QStringList list = userCategories();
    if (list.contains(name)) {
        return;
    }
    list.append(name);
    setUserCategories(list);
}

void KonsolaiSettings::removeUserCategory(const QString &name)
{
    QStringList list = userCategories();
    if (list.removeAll(name) > 0) {
        setUserCategories(list);
    }
}

// ========== Agent Fleet Settings ==========

QString KonsolaiSettings::agentFleetPath() const
{
    KConfigGroup group(m_config, QStringLiteral("AgentFleet"));
    return group.readEntry("FleetPath", QString());
}

void KonsolaiSettings::setAgentFleetPath(const QString &path)
{
    KConfigGroup group(m_config, QStringLiteral("AgentFleet"));
    group.writeEntry("FleetPath", path);
    Q_EMIT settingsChanged();
}

// ========== Letta Settings ==========

QString KonsolaiSettings::lettaBaseUrl() const
{
    KConfigGroup group(m_config, QStringLiteral("Letta"));
    return group.readEntry("BaseUrl", QString());
}

void KonsolaiSettings::setLettaBaseUrl(const QString &url)
{
    KConfigGroup group(m_config, QStringLiteral("Letta"));
    group.writeEntry("BaseUrl", url);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::lettaApiKey() const
{
    KConfigGroup group(m_config, QStringLiteral("Letta"));
    return group.readEntry("ApiKey", QString());
}

void KonsolaiSettings::setLettaApiKey(const QString &key)
{
    KConfigGroup group(m_config, QStringLiteral("Letta"));
    group.writeEntry("ApiKey", key);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::lettaEnabled() const
{
    KConfigGroup group(m_config, QStringLiteral("Letta"));
    return group.readEntry("Enabled", true);
}

void KonsolaiSettings::setLettaEnabled(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Letta"));
    group.writeEntry("Enabled", enabled);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::lastSshHost() const
{
    KConfigGroup group(m_config, QStringLiteral("SSH"));
    return group.readEntry("LastHost", QString());
}

void KonsolaiSettings::setLastSshHost(const QString &host)
{
    KConfigGroup group(m_config, QStringLiteral("SSH"));
    group.writeEntry("LastHost", host);
}

QString KonsolaiSettings::lastSshUsername() const
{
    KConfigGroup group(m_config, QStringLiteral("SSH"));
    return group.readEntry("LastUsername", QString());
}

void KonsolaiSettings::setLastSshUsername(const QString &username)
{
    KConfigGroup group(m_config, QStringLiteral("SSH"));
    group.writeEntry("LastUsername", username);
}

int KonsolaiSettings::lastSshPort() const
{
    KConfigGroup group(m_config, QStringLiteral("SSH"));
    return group.readEntry("LastPort", 22);
}

void KonsolaiSettings::setLastSshPort(int port)
{
    KConfigGroup group(m_config, QStringLiteral("SSH"));
    group.writeEntry("LastPort", port);
}

void KonsolaiSettings::save()
{
    m_config->sync();
}

} // namespace Konsolai

#include "moc_KonsolaiSettings.cpp"
