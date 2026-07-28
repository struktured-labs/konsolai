/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONMANAGERPANEL_H
#define SESSIONMANAGERPANEL_H

#include "konsoleprivate_export.h"

#include "ClaudeAssistantPromptBuilder.h"
#include "ClaudeSession.h"
#include "ClaudeSessionRegistry.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace Konsolai
{

class ClaudeSessionRegistry;
struct MergeFieldChoices;

/**
 * Session metadata stored persistently
 */
struct KONSOLEPRIVATE_EXPORT SessionMetadata {
    QString sessionId;
    QString sessionName;
    QString profileName;
    QString workingDirectory;
    bool isPinned = false;
    bool isArchived = false;
    bool isExpired = false;
    bool isDismissed = false;
    QDateTime lastAccessed;
    QDateTime createdAt;

    // SSH remote session fields
    bool isRemote = false;
    QString sshHost;
    QString sshUsername;
    int sshPort = 22;

    // Per-session yolo mode settings (persisted across restarts)
    bool yoloMode = false;
    bool doubleYoloMode = false;

    // Approval counts (persisted across restarts)
    int yoloApprovalCount = 0;
    int doubleYoloApprovalCount = 0;

    // Approval log entries (persisted across restarts)
    QVector<ApprovalLogEntry> approvalLog;

    // Budget settings (persisted across restarts)
    int budgetTimeLimitMinutes = 0;
    double budgetCostCeilingUSD = 0.0;
    quint64 budgetTokenCeiling = 0;

    // Claude conversation UUID for --resume across close/reopen cycles
    QString lastResumeSessionId;

    // Human-readable description (first prompt or user-set label)
    QString description;

    // Merge linkage: non-empty if this session was merged into another.
    // Holds the sessionId of the primary session this one was collapsed into.
    QString mergedInto;

    // Agent linkage: non-empty if this session was created from agent attach
    QString agentId;

    // Subagent linkage: true if this session was spawned as a subagent of
    // another Claude session (e.g. from a Task tool call or an agent-fleet
    // dispatched worker).  Hidden from the main tree by default; toggled on
    // via the "Subagents" chip in the filter overflow menu.
    //
    // Set by future subagent-spawn plumbing (not wired to any producer today).
    // The heuristic isSubagentSession() covers the runtime detection cases
    // via sessionName pattern + jsonl-path check even when this flag is false.
    bool isSubagent = false;

    // Which agent CLI backs this session. Persisted so the tree can show the
    // right badge for detached/closed sessions, whose ClaudeSession is gone.
    bool isCodex = false;

    // Persisted subagent/subprocess snapshots (survive restart)
    QVector<SubagentInfo> subagents;
    QVector<SubprocessInfo> subprocesses;
    QMap<int, QString> promptGroupLabels;
    int currentPromptRound = 0;
};

/**
 * SessionManagerPanel provides a collapsible sidebar for managing all Claude sessions.
 *
 * Features:
 * - Shows all sessions: pinned, active, and archived
 * - Pin sessions to keep them at the top
 * - Archive sessions (kills tmux but preserves metadata for later restoration)
 * - Only active sessions appear as tabs
 * - Double-click to attach/open a session
 */
class KONSOLEPRIVATE_EXPORT SessionManagerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SessionManagerPanel(QWidget *parent = nullptr);
    ~SessionManagerPanel() override;

    /**
     * Register an active session
     */
    void registerSession(ClaudeSession *session);

    /**
     * Unregister a session (when tab is closed)
     */
    void unregisterSession(ClaudeSession *session);

    /**
     * Get all session metadata
     */
    QList<SessionMetadata> allSessions() const;

    /**
     * Get metadata for a specific session (const).
     * Returns nullptr if not found.
     */
    const SessionMetadata *sessionMetadata(const QString &sessionId) const;

    /**
     * Check if a session is currently active (has an open tab)
     */
    bool isSessionActive(const QString &sessionId) const;

    /**
     * Get pinned sessions
     */
    QList<SessionMetadata> pinnedSessions() const;

    /**
     * Get archived sessions
     */
    QList<SessionMetadata> archivedSessions() const;

    /**
     * Check if panel is collapsed
     */
    bool isCollapsed() const
    {
        return m_collapsed;
    }

    /**
     * Sum of estimated cost across all sessions for the current week (Mon-Sun)
     */
    double weeklySpentUSD() const;

    /**
     * Sum of estimated cost across all sessions for the current calendar month
     */
    double monthlySpentUSD() const;

public Q_SLOTS:
    /**
     * Toggle panel collapsed state
     */
    void setCollapsed(bool collapsed);

    /**
     * Refresh the session list
     */
    void refresh();

    /**
     * Pin a session
     */
    void pinSession(const QString &sessionId);

    /**
     * Unpin a session
     */
    void unpinSession(const QString &sessionId);

    /**
     * Archive a session (kills tmux, preserves metadata)
     */
    void archiveSession(const QString &sessionId);

    /**
     * Close a session (kills tmux, moves to Closed category without archiving)
     */
    void closeSession(const QString &sessionId);

    /**
     * Unarchive a session (restarts Claude with same session ID)
     */
    void unarchiveSession(const QString &sessionId);

    /**
     * Mark a session as expired (dead tmux backend) and auto-archive it
     */
    void markExpired(const QString &sessionName);

    /**
     * Find session ID by tmux session name.
     * Returns empty string if not found.
     */
    QString sessionIdForName(const QString &sessionName) const;

    /**
     * Update a session's description in persisted metadata (called from tab context menu)
     */
    void updateSessionDescription(const QString &sessionId, const QString &desc);

    /**
     * Dismiss a session (soft delete — hides from normal view, metadata retained)
     */
    void dismissSession(const QString &sessionId);

    /**
     * Restore a dismissed session back to Archived
     */
    void restoreSession(const QString &sessionId);

    /**
     * Purge a session (permanently remove metadata, project files untouched)
     */
    void purgeSession(const QString &sessionId);

    /**
     * Purge all dismissed sessions
     */
    void purgeDismissed();

    /**
     * Merge a set of same-project sessions into a single primary.
     * The primary's metadata is updated with the unioned values per the
     * provided choices. Each non-primary session is marked isDismissed=true
     * and gets mergedInto=<primaryId>. Reversible via unmergeSession().
     *
     * Returns false if validation fails (different workdirs, primary not in
     * selection, fewer than 2 sessions, unknown ids, etc.).
     */
    bool mergeSessions(const QStringList &sessionIds, const QString &primarySessionId, const MergeFieldChoices &choices);

    /**
     * Reverse a prior merge — restore the dismissed session and clear its
     * mergedInto field. Does NOT roll back metadata changes the primary
     * already inherited. Returns false if the session is unknown or wasn't
     * in a merged state.
     */
    bool unmergeSession(const QString &dismissedSessionId);

    /**
     * Predicate used by the context menu to decide whether to surface the
     * "Merge Selected (N)..." action: requires ≥2 non-dismissed known
     * sessions all sharing the same workingDirectory. Returned for tests so
     * the gating logic can be exercised without driving the menu modally.
     */
    bool canOfferMergeForSelection(const QStringList &sessionIds) const;

    /**
     * Predicate used by the context menu to decide whether to surface the
     * "Unmerge..." action for a dismissed session.
     */
    bool canOfferUnmergeForSession(const QString &sessionId) const;

    /**
     * Send a templated message to N active sessions. For each, substitutes the
     * template per-recipient and calls ClaudeSession::sendText() followed by a
     * newline keypress when pressEnterAfterEach is true. Skips any sessionId
     * not currently in m_activeSessions (defensive — caller already filtered).
     *
     * Returns the count of sessions the message was sent to.
     */
    int broadcastMessage(const QStringList &sessionIds, const QString &templateText, bool pressEnterAfterEach);

    /**
     * Predicate used by the context menu to decide whether to surface the
     * "Broadcast Message..." action: requires at least one of the provided
     * session ids to be currently active (in m_activeSessions). Used by tests
     * so the gating logic can be exercised without driving the menu modally.
     */
    bool canOfferBroadcastForSelection(const QStringList &sessionIds) const;

    /**
     * Predicate used by the context menu to decide whether to surface the
     * "Consolidate Duplicates..." action on a project-group node: requires the
     * given workingDirectory to have ≥2 known sessions in m_metadata
     * (regardless of state). Public for testing without driving the menu.
     */
    bool canOfferConsolidateForProject(const QString &workingDirectory) const;

    /**
     * Dissolve a category so its children become top-level:
     *  - If a CategoryAliases entry points AT this category → remove the alias(es)
     *  - Else if a WorkdirCategoryOverrides entry points at this category → remove the override(s)
     *  - Else (LCP-created) → add to SuppressCategories.
     *
     * categoryKey is the raw category name (no "category:" prefix).
     */
    void ungroupCategory(const QString &categoryKey);

    /**
     * Handle a drop event from the tree: one-or-more source composite keys
     * ("group:..." or "category:...") dropped onto a target composite key
     * ("category:...").  Prompts the user once for confirmation and delegates
     * the persistence to applyDropRoutings().
     */
    void handleDropRequest(const QStringList &sourceKeys, const QString &targetCategoryKey);

    /**
     * Apply drop/move routings WITHOUT any confirmation dialog: for each
     * valid source key persist a CategoryAlias ("category:X" sources) or a
     * WorkdirCategoryOverride ("group:<workdir>" sources) pointing at the
     * target category, in one signal-blocked batch. Extracted from
     * handleDropRequest so menu-driven moves — where the menu selection IS
     * the confirmation — reuse the same persistence path. Returns the number
     * of routings applied.
     */
    int applyDropRoutings(const QStringList &sourceKeys, const QString &targetCategoryKey);

    /**
     * Candidate targets for the "Move to Category" submenu: every existing
     * category bucket key (as of the last tree rebuild), sorted, excluding
     * excludeKey (the node's current category). Public for tests.
     */
    QStringList moveTargetCategories(const QString &excludeKey) const;

    /**
     * Create a brand-new empty user category (persisted in
     * KonsolaiSettings::userCategories()) and refresh the tree so it appears
     * at top level immediately.  Prompts the user for the name.
     */
    void createUserCategory();

    /**
     * Fleet launcher: for every unique workdir routed into the given category
     * (per the current m_categoryMap), focus the already-active session if
     * one exists, else resume the newest conversation (.jsonl) for that
     * project. Projects with no conversations are skipped silently (summary
     * via qInfo). Returns the count of focused + launched sessions.
     *
     * The QAction confirms with the user first, then calls this; tests drive
     * it directly.
     */
    int launchLatestInCategory(const QString &categoryKey);

    /**
     * Single-project variant used by the project-group context menu and by
     * launchLatestInCategory(). Returns 1 when a session was focused or a
     * resume was emitted, 0 when the project has no conversations.
     */
    int launchLatestForWorkdir(const QString &workdir);

    /**
     * Rename an existing category.  Under the hood this is stored as a
     * CategoryAliases entry pointing oldKey → newName, which reroutes the
     * bucket everywhere the LCP grouper or a user-created category refers
     * to oldKey.
     */
    void renameCategory(const QString &oldKey);

    /**
     * Open the LLM-assisted reorganize dialog. Constructs the current
     * TreeInventory from m_metadata + m_categoryMap + settings, blocks in
     * exec(), applies the returned proposal atomically on Accept.
     */
    void openReorganizeTreeDialog();

    /**
     * Ask Claude to suggest a name for the given selection (workdirs).
     * Called from context menu / hotkey. Presents the suggestion via a
     * confirmation QInputDialog pre-filled with the suggested value.
     *
     * If accepted, applies it as a category alias (renaming the category the
     * workdirs are grouped under) or, if the projects have no shared category,
     * as workdir overrides — routing each into the new category.
     */
    void suggestCategoryName(const QStringList &workdirs);

    /**
     * Snapshot the current tree state as a TreeInventory. Public for tests
     * and reused by openReorganizeTreeDialog().
     */
    TreeInventory buildTreeInventory() const;

    /**
     * Apply a ReorganizeProposal to KonsolaiSettings in one batch. Public so
     * tests can drive the atomic-apply without a live dialog.
     */
    void applyReorganizeProposal(const ReorganizeProposal &proposal);

    /**
     * Set the agentId on a session's metadata (for agent-originated sessions)
     */
    void setSessionAgentId(const QString &sessionId, const QString &agentId);

    /**
     * Auto-archive closed sessions older than 7 days.
     * Runs periodically on a timer; can also be called manually.
     */
    void autoArchiveOldClosedSessions();

    /**
     * Pause non-essential background timers (called when window becomes inactive)
     */
    void pauseBackgroundTimers();

    /**
     * Resume background timers (called when window becomes active again)
     */
    void resumeBackgroundTimers();

    /**
     * Force a synchronous tree rebuild using cached live session data.
     * Used by tests to avoid async tmux query timing issues.
     */
    void rebuildTreeSync()
    {
        updateTreeWidgetWithLiveSessions(m_cachedLiveNames);
    }

Q_SIGNALS:
    /**
     * Emitted when user wants to attach to a session
     */
    void attachRequested(const QString &sessionName);

    /**
     * Emitted when user wants to reattach to a remote SSH tmux session
     */
    void remoteAttachRequested(const QString &sshHost, const QString &sshUsername, int sshPort, const QString &workDir, const QString &tmuxSessionName);

    /**
     * Emitted when user wants to focus/select the tab for an active session
     */
    void focusSessionRequested(ClaudeSession *session);

    /**
     * Emitted when user wants to create a new session
     */
    void newSessionRequested();

    /**
     * Emitted when user wants to unarchive and start a session
     */
    void unarchiveRequested(const QString &sessionId, const QString &workingDirectory, bool isRemote, const QString &sshHost, const QString &sshUsername, int sshPort);

    /**
     * Emitted when user wants to open a remote SSH session
     */
    void remoteSessionRequested(const QString &sshHost, const QString &sshUsername, int sshPort, const QString &workDir);

    /**
     * Emitted when user wants to create a worktree session from an existing session
     */
    void worktreeSessionRequested(const QString &sourceWorkingDir);

    /**
     * Emitted when user wants to resume a specific conversation in a directory.
     * Works for both local and remote sessions.
     */
    void resumeConversationRequested(const QString &workingDirectory, const QString &conversationId,
                                      const QString &sshHost, const QString &sshUsername, int sshPort);

    /**
     * Emitted when user wants to navigate to an agent in the agent panel
     */
    void showAgentRequested(const QString &agentId);

    /**
     * Emitted when collapsed state changes
     */
    void collapsedChanged(bool collapsed);

    /**
     * Emitted when aggregated usage may have changed (after metadata save)
     */
    void usageAggregateChanged();

public Q_SLOTS:
    /**
     * Dispatch a vim-hotkey action (from SessionTreeWidget) against the
     * current tree item.  Actions: "attach", "archive", "pin", "close",
     * "dismiss", "rename", "new-category".  No-op for actions that don't
     * make sense on the current item's type (e.g. "archive" on a category).
     *
     * Public so tests can drive it directly; production wiring goes through
     * SessionTreeWidget::actionRequested.
     */
    void handleTreeAction(const QString &action);

private Q_SLOTS:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onContextMenu(const QPoint &pos);
    void onNewSessionClicked();

public:
    /**
     * Detect whether a session is a "subagent" — spawned as a worker of
     * another Claude session (Task tool, agent-fleet dispatch, etc.).
     *
     * Priority-ordered detection:
     *   1. meta.isSubagent (persisted flag — future producer)
     *   2. sessionName matches konsolai's own agent-fleet pattern
     *      (agent-<16-hex>)
     *   3. Newest jsonl for this session's project contains "/subagents/"
     *      in its path.
     *
     * Test hook: public so SessionManagerPanelTest can exercise the
     * classifier without driving the full tree pipeline.
     */
    bool isSubagentSession(const SessionMetadata &meta) const;

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void setupUi();
    void deferredInit();
    void showLoadingState();
    void showReadyState();
    void loadMetadata();
    void saveMetadata(bool sync = false);
    void scheduleTreeUpdate(); // debounced — coalesces rapid-fire calls
    void scheduleMetadataSave(); // debounced — coalesces rapid-fire saves
    void updateTreeWidget();
    void updateTreeWidgetWithLiveSessions(const QSet<QString> &liveNames);
    // parent may be nullptr → the session item is attached at top level.
    // Returns the created item so callers (flatten path) can tag/relabel it.
    QTreeWidgetItem *addSessionToTree(const SessionMetadata &meta, QTreeWidgetItem *parent, bool hasSiblings = false);
    void showApprovalLog(ClaudeSession *session);
    void showSessionActivity(const QString &jsonlPath, const QString &workDir);
    void showSessionStructure(const QString &sessionId);
    void showSubagentTranscript(const SubagentInfo &info);
    void showSubagentDetails(const SubagentInfo &info);
    void showSubprocessOutput(const SubprocessInfo &info);
    void editSessionDescription(const QString &sessionId);
    void editSessionBudget(ClaudeSession *session, const QString &sessionId);

    // Prompt for a new category name (with collision check). Returns the
    // trimmed name, or an empty string on cancel/collision. Shared by
    // createUserCategory() and the "Move to Category → New Category…" flow.
    QString promptNewCategoryName();

    // Build the "Move to Category ▸" submenu on `menu` for the given source
    // composite key ("group:<workdir>" or "category:<name>"). Menu-driven
    // moves skip the drop-confirmation dialog — selecting the entry IS the
    // confirmation. currentCategoryKey (may be empty) is excluded from the
    // target list.
    void addMoveToCategorySubmenu(QMenu &menu, const QString &sourceKey, const QString &currentCategoryKey, const QString &title);
    void openBroadcastDialog(const QStringList &activeIds);
    void openConsolidateDialog(const QString &projectKey);
    void cleanupStaleSockets();
    void ensureHooksConfigured(ClaudeSession *session);
    SessionMetadata *findMetadata(const QString &sessionId);
    QTreeWidgetItem *findTreeItem(const QString &sessionId);
    void applyFilter(const QString &text);
    void updateDurationLabels(); // In-place duration label updates without full rebuild
    void refreshSessionItemLabel(const QString &sessionId); // In-place label/icon update for one item

    // Tree expansion state preservation
    void saveTreeState();
    void restoreTreeState();
    QString compositeKeyForItem(QTreeWidgetItem *item) const;
    bool shouldAutoExpand(const QString &key, const QString &sessionId, bool hasActiveChildren) const;
    void pruneStaleKeys();
    bool isTreeInteractionActive() const;

    // Project-group helpers (replaces state categories).
    QTreeWidgetItem *ensureProjectGroup(const QString &workingDirectory);
    QString projectGroupKey(const QString &workingDirectory) const;

    // Returns the top-level category item this workdir routes into (creating
    // it on demand), or nullptr when the project is standalone. Extracted
    // from ensureProjectGroup so the single-session flatten path can attach
    // session leaves directly to the category without a wrapper group.
    QTreeWidgetItem *ensureCategoryItemFor(const QString &workingDirectory);

    // Sub-label for a project inside a multi-project category: basename with
    // the category prefix stripped ("cowir-battle" in "cowir" → "battle").
    static QString projectSubLabel(const QString &workingDirectory, const QString &catKey);

    // Category-grouping (longest common prefix among project basenames).
    // Builds m_categoryMap (workdir → categoryKey) from a snapshot of workdirs.
    // O(N^2) on project count; fine for hundreds of projects.
public:
    static QStringList projectTokens(const QString &workingDirectory);
    static QHash<QString, QString> buildCategoryMap(const QList<QString> &workingDirectories);

private:
    // Live count: number of distinct projects sharing this category key (m_categoryMap).
    int categoryProjectCount(const QString &categoryKey) const;
    QString stateTokenFor(const SessionMetadata &meta, bool isLive) const;
    QIcon stateIcon(const QString &token) const;
    QString stateLabel(const QString &token) const;
    int sessionCountByState(const QString &token) const;

    // Filter chip toolbar.
    void buildFilterChips(QHBoxLayout *into);
    void onFilterChipToggled(const QString &token, bool on);
    void persistVisibleStates();

    // Sync chip UI (primary QToolButtons + overflow QActions) to reflect the
    // current m_visibleStates.  Uses QSignalBlocker so setChecked doesn't
    // refire onFilterChipToggled.  Called at the end of every rebuild so the
    // chip UI never drifts from m_visibleStates.
    void syncChipUiToVisibleStates();

    QTreeWidget *m_treeWidget = nullptr;
    QLabel *m_emptyStateLabel = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QPushButton *m_newSessionButton = nullptr;
    QPushButton *m_collapseButton = nullptr;

    // Project-group items, keyed by workingDirectory. Replaces the state-category items.
    QHash<QString, QTreeWidgetItem *> m_projectGroups;

    // Category map: workingDirectory → category key (longest common token prefix).
    // Rebuilt at the start of each updateTreeWidgetWithLiveSessions().
    QHash<QString, QString> m_categoryMap;

    // Top-level category items, keyed by category key (e.g. "cowir", "penta-dragon").
    // Only created when at least 2 projects share the category key.
    QHash<QString, QTreeWidgetItem *> m_categoryGroups;

    // Visible state tokens — controls which sessions are rendered.
    // Tokens: active/detached/pinned/closed/archived/dismissed/discovered
    QSet<QString> m_visibleStates;

    // Filter chip row above the tree.
    QWidget *m_filterChipsRow = nullptr;
    QHash<QString, QToolButton *> m_filterChips;

    QProgressBar *m_loadingBar = nullptr;
    bool m_initialized = false;
    QVector<QPointer<ClaudeSession>> m_pendingRegistrations;

    QMap<QString, SessionMetadata> m_metadata;
    QMap<QString, QPointer<ClaudeSession>> m_activeSessions;
    ClaudeSessionRegistry *m_registry = nullptr;
    bool m_collapsed = false;

    // Debounce timer for updateTreeWidget — coalesces rapid-fire signals
    QTimer *m_updateDebounce = nullptr;

    // Debounce timer for saveMetadata — coalesces rapid-fire approval events
    QTimer *m_saveDebounce = nullptr;

    // Periodic timer to update elapsed duration for subagent items
    QTimer *m_durationTimer = nullptr;

    // Guard against overlapping async tmux queries
    bool m_asyncQueryInFlight = false;
    bool m_asyncQueryPending = false;

    // Cached live session names from last async tmux query
    QSet<QString> m_cachedLiveNames;

    // Memoized isSubagentSession() results keyed by sessionId.  The heuristic's
    // jsonl-path check hits disk, so we cache — invalidated whenever metadata
    // for that session changes.  Mutable so it's writable from const method.
    mutable QHash<QString, bool> m_subagentClassificationCache;

    // Cached remote live session names (queried via SSH, refreshed less frequently)
    QSet<QString> m_cachedRemoteLiveNames;
    QTimer *m_remoteTmuxTimer = nullptr;
    void refreshRemoteTmuxSessions();
    void refreshCachesAsync(); // background thread for discoverSessions + readClaudeConversations

    // Cache conversations per working directory to avoid disk I/O during tree rebuilds
    QHash<QString, QList<ClaudeConversation>> m_conversationCache; // workDir → conversations

    // Cache git branch names per working directory (TTL-based, refreshed every 60s)
    QHash<QString, QString> m_gitBranchCache; // workDir → branch name

    // TTL timers for cache invalidation
    QTimer *m_gitCacheTimer = nullptr; // 60s TTL for git branch cache
    QTimer *m_convCacheTimer = nullptr; // 120s TTL for conversation + discovered + GSD caches

    // Cached discoverSessions() results (invalidated on 120s timer and session register/unregister)
    QList<ClaudeSessionState> m_cachedDiscoveredSessions;
    bool m_discoveredCacheValid = false;

    // Smart signal filtering: skip rebuilds when state hasn't visually changed
    QHash<QString, ClaudeProcess::State> m_lastKnownState;
    QHash<QString, int> m_lastKnownApprovalCount;

    // GSD badge cache (workDir → has .planning/ or ROADMAP.md)
    QHash<QString, bool> m_gsdBadgeCache;

    // Per-session toggle: sessions in this set hide completed (NotRunning) agents
    QSet<QString> m_hideCompletedAgents;

    // Sessions explicitly closed via closeSession() — forced to "Closed" category
    // even if tmux hasn't died yet (async kill race)
    QSet<QString> m_explicitlyClosed;

    // --- Tree expansion state preservation ---

    // Composite key → isExpanded, captured before each tree rebuild
    QHash<QString, bool> m_expansionState;

    // Composite keys seen in at least one rebuild (distinguishes new items from existing)
    QSet<QString> m_knownItems;

    // Sessions the user has muted (suppresses auto-expand). Ephemeral, not persisted.
    QSet<QString> m_mutedSessions;

    // Saved scroll position and selection key for restoration after rebuild
    int m_savedScrollPosition = 0;
    QString m_savedSelectedKey;

    // Whether a tree update is pending due to interaction deferral
    bool m_pendingUpdate = false;
    QTimer *m_deferRetryTimer = nullptr;

    // Track pending async tmux kill operations for wait cursor management.
    // setOverrideCursor pushes to a stack — we use a counter so batch operations
    // (Archive All, Close All) only push/pop once.
    int m_pendingAsyncKills = 0;

    // Auto-archive closed sessions older than threshold
    QTimer *m_autoArchiveTimer = nullptr;

    // Whether background timers are paused (window inactive)
    bool m_timersPaused = false;

    // Whether an async cache refresh is in flight (prevents overlapping refreshes)
    bool m_cacheRefreshInFlight = false;

    // Whether a metadata save was deferred during timer pause
    bool m_pendingSave = false;
};

} // namespace Konsolai

#endif // SESSIONMANAGERPANEL_H
