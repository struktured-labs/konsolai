/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_SETTINGS_H
#define KONSOLAI_SETTINGS_H

#include "konsoleprivate_export.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <KSharedConfig>

namespace Konsolai
{

/**
 * KonsolaiSettings manages application-wide settings.
 *
 * Settings include:
 * - Default project root directory
 * - Git remote root URL
 * - GitHub API key (stored securely)
 * - Default Claude model
 */
class KONSOLEPRIVATE_EXPORT KonsolaiSettings : public QObject
{
    Q_OBJECT

public:
    static KonsolaiSettings *instance();

    explicit KonsolaiSettings(QObject *parent = nullptr);
    ~KonsolaiSettings() override;

    /**
     * Default project root directory (e.g., ~/projects)
     */
    QString projectRoot() const;
    void setProjectRoot(const QString &path);

    /**
     * Git remote root URL (e.g., git@github.com:username/)
     */
    QString gitRemoteRoot() const;
    void setGitRemoteRoot(const QString &url);

    /**
     * GitHub API token
     */
    QString githubApiToken() const;
    void setGithubApiToken(const QString &token);

    /**
     * Default Claude model
     */
    QString defaultModel() const;
    void setDefaultModel(const QString &model);

    /**
     * Permission mode passed to `claude --permission-mode`.
     *
     * Konsolai's yolo mode drives the TUI to click through prompts; letting the
     * CLI apply its own policy is both cheaper and less fragile, so this is the
     * preferred auto-approval path. One of: acceptEdits, auto, bypassPermissions,
     * manual, dontAsk, plan. Empty means "pass nothing, use the CLI default".
     */
    QString claudePermissionMode() const;
    void setClaudePermissionMode(const QString &mode);

    /** Model slug for Codex sessions (`codex -m`). */
    QString codexModel() const;
    void setCodexModel(const QString &model);

    /** Reasoning effort for Codex (`-c model_reasoning_effort=...`). */
    QString codexEffort() const;
    void setCodexEffort(const QString &effort);

    /**
     * Codex approval policy (`codex -a`): untrusted, on-request, or never.
     * "never" is Codex's auto-approving mode — it does not prompt, and command
     * failures are returned to the model instead of to the user.
     */
    QString codexApprovalPolicy() const;
    void setCodexApprovalPolicy(const QString &policy);

    /**
     * Codex sandbox (`codex -s`): read-only, workspace-write, or
     * danger-full-access. Pairs with the approval policy — auto-approving
     * without a sandbox means unrestricted command execution.
     */
    QString codexSandbox() const;
    void setCodexSandbox(const QString &sandbox);

    /**
     * Extra command-line arguments passed to every Claude session by default.
     * Free-form whitespace-separated string. Per-session overrides are stored
     * on ClaudeSession itself and take precedence when non-empty.
     *
     * Default:
     *   --dangerously-load-development-channels plugin:session-intercom@struktured-labs
     * so the session-intercom plugin's channel-based async notifications
     * actually bind (the plugin's MCP server lives under the plugin namespace,
     * not as a top-level server entry).
     */
    QString extraClaudeArgs() const;
    void setExtraClaudeArgs(const QString &args);

    /**
     * Git mode for new sessions:
     * 0 = Initialize new repository
     * 1 = Create as worktree
     * 2 = Use current branch (existing repo)
     * 3 = None
     */
    int gitMode() const;
    void setGitMode(int mode);

    /**
     * Source repository for creating worktrees.
     * When set, new sessions create worktrees from this repo.
     * Path to the main git repository (e.g., ~/projects/main-repo)
     */
    QString worktreeSourceRepo() const;
    void setWorktreeSourceRepo(const QString &path);

    /**
     * Yolo mode (auto-approve) default
     */
    bool yoloMode() const;
    void setYoloMode(bool enabled);

    /**
     * Double yolo mode (auto-complete) default
     */
    bool doubleYoloMode() const;
    void setDoubleYoloMode(bool enabled);

    /**
     * Try suggestions first in double yolo mode
     */
    bool trySuggestionsFirst() const;
    void setTrySuggestionsFirst(bool enabled);

    // ========== Budget Defaults ==========

    /**
     * Default time limit in minutes (0 = unlimited)
     */
    int defaultTimeLimitMinutes() const;
    void setDefaultTimeLimitMinutes(int minutes);

    /**
     * Default cost ceiling in USD (0.0 = unlimited)
     */
    double defaultCostCeilingUSD() const;
    void setDefaultCostCeilingUSD(double cost);

    /**
     * Default budget policy: 0 = Soft, 1 = Hard
     */
    int defaultBudgetPolicy() const;
    void setDefaultBudgetPolicy(int policy);

    /**
     * Default token ceiling (0 = unlimited)
     */
    quint64 defaultTokenCeiling() const;
    void setDefaultTokenCeiling(quint64 tokens);

    /**
     * Budget warning threshold as percentage (default: 80%)
     */
    double budgetWarningThresholdPercent() const;
    void setBudgetWarningThresholdPercent(double percent);

    /**
     * Weekly spending budget in USD (0.0 = unlimited)
     */
    double weeklyBudgetUSD() const;
    void setWeeklyBudgetUSD(double budget);

    /**
     * Monthly spending budget in USD (0.0 = unlimited)
     */
    double monthlyBudgetUSD() const;
    void setMonthlyBudgetUSD(double budget);

    // ========== Notification Settings ==========

    bool notificationAudioEnabled() const;
    void setNotificationAudioEnabled(bool enabled);

    bool notificationDesktopEnabled() const;
    void setNotificationDesktopEnabled(bool enabled);

    bool notificationSystemTrayEnabled() const;
    void setNotificationSystemTrayEnabled(bool enabled);

    bool notificationInTerminalEnabled() const;
    void setNotificationInTerminalEnabled(bool enabled);

    double notificationAudioVolume() const;
    void setNotificationAudioVolume(double volume);

    bool notificationYoloEnabled() const;
    void setNotificationYoloEnabled(bool enabled);

    // ========== Agent Fleet Settings ==========

    /**
     * Path to agent-fleet installation (auto-detected if empty)
     */
    QString agentFleetPath() const;
    void setAgentFleetPath(const QString &path);

    // ========== Session Tree View ==========

    /**
     * Which session states are visible in the tree.
     * Stored as comma-separated lowercase tokens in [SessionTree] VisibleStates.
     * Default: "active,detached,pinned" — the noisy/long-tail states
     * (closed, archived, dismissed, discovered) are hidden by default and
     * toggled on via filter chips above the session tree.
     *
     * Recognized tokens: pinned, active, detached, closed, archived,
     * dismissed, discovered.
     */
    QStringList visibleSessionStates() const;
    void setVisibleSessionStates(const QStringList &states);

    /**
     * User-defined category-to-category renames. Applied AFTER LCP grouping so
     * "cowardly-irregular" (LCP result) can be re-routed to "cowir".
     * Stored as "source1=target1,source2=target2" in [SessionTree] CategoryAliases.
     */
    QHash<QString, QString> categoryAliases() const;
    void setCategoryAliases(const QHash<QString, QString> &aliases);
    void addCategoryAlias(const QString &source, const QString &target);
    void removeCategoryAlias(const QString &source);

    /**
     * Per-workdir category override. Wins over CategoryAliases and LCP alike.
     * Stored as "workdir1=cat1,workdir2=cat2" in [SessionTree] WorkdirCategoryOverrides.
     */
    QHash<QString, QString> workdirCategoryOverrides() const;
    void setWorkdirCategoryOverrides(const QHash<QString, QString> &overrides);
    void addWorkdirCategoryOverride(const QString &workdir, const QString &category);
    void removeWorkdirCategoryOverride(const QString &workdir);

    /**
     * Category names that should NEVER auto-group even if LCP would create them.
     * Used by the "Ungroup" action on LCP-created categories.
     * Stored as comma-separated list in [SessionTree] SuppressCategories.
     */
    QStringList suppressedCategories() const;
    void setSuppressedCategories(const QStringList &names);
    void addSuppressedCategory(const QString &name);
    void removeSuppressedCategory(const QString &name);

    /**
     * User-created empty categories.  These render as top-level tree items even
     * when they contain zero projects, so users can drop projects into them via
     * DnD.  Stored as comma-separated list in [SessionTree] UserCategories.
     */
    QStringList userCategories() const;
    void setUserCategories(const QStringList &names);
    void addUserCategory(const QString &name);
    void removeUserCategory(const QString &name);

    // ========== Letta Settings ==========

    /**
     * Base URL for the Letta server. Empty falls back to the LETTA_BASE_URL
     * environment variable, then to http://localhost:8283.
     */
    QString lettaBaseUrl() const;
    void setLettaBaseUrl(const QString &url);

    /**
     * Bearer token for the Letta server. Empty falls back to the
     * LETTA_API_KEY environment variable, then no auth.
     */
    QString lettaApiKey() const;
    void setLettaApiKey(const QString &key);

    /**
     * Whether to enable the Letta agent provider in the Agents panel.
     */
    bool lettaEnabled() const;
    void setLettaEnabled(bool enabled);

    /**
     * Last-used SSH host (for wizard pre-fill)
     */
    QString lastSshHost() const;
    void setLastSshHost(const QString &host);

    /**
     * Last-used SSH username (for wizard pre-fill)
     */
    QString lastSshUsername() const;
    void setLastSshUsername(const QString &username);

    /**
     * Last-used SSH port (for wizard pre-fill)
     */
    int lastSshPort() const;
    void setLastSshPort(int port);

    /**
     * Save settings to disk
     */
    void save();

    /**
     * Detect the best default project root by checking common directories.
     * Checks ~/projects, ~/code, ~/workspace, ~/src in order,
     * falling back to home directory if none exist.
     */
    static QString detectDefaultProjectRoot();

Q_SIGNALS:
    void settingsChanged();

private:
    KSharedConfig::Ptr m_config;
};

} // namespace Konsolai

#endif // KONSOLAI_SETTINGS_H
