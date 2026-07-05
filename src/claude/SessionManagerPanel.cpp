/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionManagerPanel.h"
#include "BroadcastDialog.h"
#include "BroadcastPolicy.h"
#include "ClaudeAssistant.h"
#include "ClaudeAssistantPromptBuilder.h"
#include "ClaudeConversationPicker.h"
#include "ClaudeSession.h"
#include "ClaudeSessionRegistry.h"
#include "KonsolaiSettings.h"
#include "MergePolicy.h"
#include "MergeSessionsDialog.h"
#include "NotificationManager.h"
#include "ReorganizeTreeDialog.h"
#include "SessionTreeWidget.h"
#include "TmuxManager.h"
#include "TreeToolbar.h"

#include <limits>

#include <KLocalizedString>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace Konsolai
{

// Result of background cache refresh (discoverSessions + readClaudeConversations)
struct CacheRefreshResult {
    QList<ClaudeSessionState> discovered;
    QHash<QString, QList<ClaudeConversation>> conversations;
};

static const QString SETTINGS_GROUP = QStringLiteral("SessionManager");

// All possible session state tokens, in display/sort order.
static const QStringList ALL_STATE_TOKENS = {QStringLiteral("active"),
                                             QStringLiteral("detached"),
                                             QStringLiteral("pinned"),
                                             QStringLiteral("closed"),
                                             QStringLiteral("archived"),
                                             QStringLiteral("dismissed"),
                                             QStringLiteral("discovered"),
                                             QStringLiteral("subagent")};

static QString formatElapsed(const QDateTime &start)
{
    if (!start.isValid()) {
        return {};
    }
    qint64 secs = start.secsTo(QDateTime::currentDateTime());
    if (secs < 0) {
        secs = 0;
    }
    if (secs < 60) {
        return QStringLiteral("%1s").arg(secs);
    }
    qint64 mins = secs / 60;
    qint64 remSecs = secs % 60;
    if (mins < 60) {
        return QStringLiteral("%1m %2s").arg(mins).arg(remSecs);
    }
    qint64 hours = mins / 60;
    qint64 remMins = mins % 60;
    return QStringLiteral("%1h %2m").arg(hours).arg(remMins);
}

SessionManagerPanel::SessionManagerPanel(QWidget *parent)
    : QWidget(parent)
    , m_registry(ClaudeSessionRegistry::instance())
{
    setupUi();
    showLoadingState();
    // Defer heavy init (metadata parse, tmux queries, timers) so the event loop
    // can paint the window first, making startup feel snappy.
    QTimer::singleShot(0, this, &SessionManagerPanel::deferredInit);
}

void SessionManagerPanel::deferredInit()
{
    loadMetadata();
    cleanupStaleSockets(); // async — returns immediately
    refreshRemoteTmuxSessions(); // async SSH query for remote session liveness
    refresh(); // async — returns immediately

    // Periodically refresh remote tmux session liveness (every 60s)
    m_remoteTmuxTimer = new QTimer(this);
    m_remoteTmuxTimer->setInterval(60000);
    connect(m_remoteTmuxTimer, &QTimer::timeout, this, &SessionManagerPanel::refreshRemoteTmuxSessions);
    m_remoteTmuxTimer->start();

    // TTL-based cache invalidation timers
    m_gitCacheTimer = new QTimer(this);
    m_gitCacheTimer->setInterval(60000); // 60s TTL for git branch cache
    connect(m_gitCacheTimer, &QTimer::timeout, this, [this]() {
        m_gitBranchCache.clear();
    });
    m_gitCacheTimer->start();

    m_convCacheTimer = new QTimer(this);
    m_convCacheTimer->setInterval(120000); // 120s — refresh caches in background (no UI freeze)
    connect(m_convCacheTimer, &QTimer::timeout, this, [this]() {
        m_gsdBadgeCache.clear(); // cheap, no I/O
        refreshCachesAsync(); // heavy I/O runs on thread pool
    });
    m_convCacheTimer->start();

    // Pre-populate caches on startup
    refreshCachesAsync();

    // Auto-archive closed sessions every 5 minutes
    m_autoArchiveTimer = new QTimer(this);
    m_autoArchiveTimer->setInterval(300000); // 5 minutes
    connect(m_autoArchiveTimer, &QTimer::timeout, this, &SessionManagerPanel::autoArchiveOldClosedSessions);
    m_autoArchiveTimer->start();
    // Run once on startup after a short delay
    QTimer::singleShot(10000, this, &SessionManagerPanel::autoArchiveOldClosedSessions);

    // Process any sessions that registered before init completed
    m_initialized = true;
    for (const auto &session : std::as_const(m_pendingRegistrations)) {
        if (session) {
            registerSession(session.data());
        }
    }
    m_pendingRegistrations.clear();

    showReadyState();
}

SessionManagerPanel::~SessionManagerPanel()
{
    // Stop all timers to prevent callbacks during/after destruction
    if (m_updateDebounce) {
        m_updateDebounce->stop();
    }
    if (m_saveDebounce) {
        m_saveDebounce->stop();
    }
    if (m_durationTimer) {
        m_durationTimer->stop();
    }
    if (m_deferRetryTimer) {
        m_deferRetryTimer->stop();
    }
    if (m_remoteTmuxTimer) {
        m_remoteTmuxTimer->stop();
    }
    if (m_gitCacheTimer) {
        m_gitCacheTimer->stop();
    }
    if (m_convCacheTimer) {
        m_convCacheTimer->stop();
    }

    // Block signals during destruction — saveMetadata() emits usageAggregateChanged(),
    // and connected slots in MainWindow may dereference already-destroyed sibling widgets
    // (e.g. ClaudeStatusWidget destroyed before SessionManagerPanel in deleteChildren()).
    blockSignals(true);
    saveMetadata(/*sync=*/true);

    // Null out m_treeWidget before base QWidget destructor runs deleteChildren(),
    // which can trigger QProcess::finished → scheduleTreeUpdate → isTreeInteractionActive
    // accessing already-destroyed child widgets.
    m_treeWidget = nullptr;
}

void SessionManagerPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Header with collapse button and new session button
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(4, 4, 4, 4);

    m_collapseButton = new QPushButton(this);
    m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-collapse-left")));
    m_collapseButton->setFlat(true);
    m_collapseButton->setFixedSize(24, 24);
    m_collapseButton->setToolTip(i18n("Toggle Session Panel"));
    connect(m_collapseButton, &QPushButton::clicked, this, [this]() {
        setCollapsed(!m_collapsed);
    });
    headerLayout->addWidget(m_collapseButton);

    // Note: Title "Sessions" is shown in dock widget title bar, no need for duplicate label here

    headerLayout->addStretch();

    // Notification settings button
    auto *notifyButton = new QPushButton(this);
    notifyButton->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-notification")));
    notifyButton->setFlat(true);
    notifyButton->setFixedSize(24, 24);
    notifyButton->setToolTip(i18n("Notification Settings"));
    connect(notifyButton, &QPushButton::clicked, this, [this]() {
        auto *mgr = NotificationManager::instance();
        if (!mgr) {
            return;
        }

        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(i18n("Notification Settings"));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        auto *vbox = new QVBoxLayout(dlg);

        auto *audioCheck = new QCheckBox(i18n("Sound alerts"), dlg);
        audioCheck->setChecked(mgr->isChannelEnabled(NotificationManager::Channel::Audio));
        vbox->addWidget(audioCheck);

        auto *volLayout = new QHBoxLayout();
        volLayout->addSpacing(20);
        auto *volLabel = new QLabel(i18n("Volume:"), dlg);
        volLayout->addWidget(volLabel);
        auto *volSlider = new QSlider(Qt::Horizontal, dlg);
        volSlider->setRange(0, 100);
        volSlider->setValue(static_cast<int>(mgr->audioVolume() * 100));
        volSlider->setEnabled(audioCheck->isChecked());
        volLayout->addWidget(volSlider);
        vbox->addLayout(volLayout);

        auto *desktopCheck = new QCheckBox(i18n("Desktop notifications"), dlg);
        desktopCheck->setChecked(mgr->isChannelEnabled(NotificationManager::Channel::Desktop));
        vbox->addWidget(desktopCheck);

        auto *terminalCheck = new QCheckBox(i18n("In-terminal overlay"), dlg);
        terminalCheck->setChecked(mgr->isChannelEnabled(NotificationManager::Channel::InTerminal));
        vbox->addWidget(terminalCheck);

        auto *trayCheck = new QCheckBox(i18n("System tray status"), dlg);
        trayCheck->setChecked(mgr->isChannelEnabled(NotificationManager::Channel::SystemTray));
        vbox->addWidget(trayCheck);

        auto *yoloCheck = new QCheckBox(i18n("Yolo approval sounds"), dlg);
        yoloCheck->setChecked(mgr->yoloNotificationsEnabled());
        vbox->addWidget(yoloCheck);

        connect(audioCheck, &QCheckBox::toggled, volSlider, &QSlider::setEnabled);

        auto *testSoundBtn = new QPushButton(i18n("Test Sound"), dlg);
        connect(testSoundBtn, &QPushButton::clicked, mgr, [mgr]() {
            mgr->playSound(NotificationManager::NotificationType::TaskComplete);
        });
        vbox->addWidget(testSoundBtn);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
        vbox->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, dlg, [=]() {
            mgr->enableChannel(NotificationManager::Channel::Audio, audioCheck->isChecked());
            mgr->enableChannel(NotificationManager::Channel::Desktop, desktopCheck->isChecked());
            mgr->enableChannel(NotificationManager::Channel::InTerminal, terminalCheck->isChecked());
            mgr->enableChannel(NotificationManager::Channel::SystemTray, trayCheck->isChecked());
            mgr->setAudioVolume(volSlider->value() / 100.0);
            mgr->setYoloNotificationsEnabled(yoloCheck->isChecked());
            mgr->saveSettings();
            dlg->accept();
        });

        dlg->resize(300, 250);
        dlg->show();
    });
    headerLayout->addWidget(notifyButton);

    m_newSessionButton = new QPushButton(this);
    m_newSessionButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_newSessionButton->setFlat(true);
    m_newSessionButton->setFixedSize(24, 24);
    m_newSessionButton->setToolTip(i18n("New Claude Session"));
    connect(m_newSessionButton, &QPushButton::clicked, this, &SessionManagerPanel::onNewSessionClicked);
    headerLayout->addWidget(m_newSessionButton);

    // "New Category…" — creates an empty user-defined top-level category that
    // renders in the tree until the user drags projects into it.
    auto *newCategoryButton = new QToolButton(this);
    newCategoryButton->setObjectName(QStringLiteral("newCategoryButton"));
    newCategoryButton->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
    newCategoryButton->setAutoRaise(true);
    newCategoryButton->setFixedSize(24, 24);
    newCategoryButton->setToolTip(i18n("New Category…"));
    connect(newCategoryButton, &QToolButton::clicked, this, &SessionManagerPanel::createUserCategory);
    headerLayout->addWidget(newCategoryButton);

    layout->addLayout(headerLayout);

    // Search/filter bar
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("sessionFilter"));
    m_filterEdit->setPlaceholderText(i18n("Filter sessions..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), QLineEdit::LeadingPosition);
    m_filterEdit->setContentsMargins(4, 0, 4, 0);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &SessionManagerPanel::applyFilter);
    layout->addWidget(m_filterEdit);

    // Filter chip toolbar (state-token visibility toggles)
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setContentsMargins(4, 2, 4, 2);
    chipsRow->setSpacing(4);
    buildFilterChips(chipsRow);
    chipsRow->addStretch();
    m_filterChipsRow = new QWidget(this);
    m_filterChipsRow->setObjectName(QStringLiteral("sessionFilterChips"));
    m_filterChipsRow->setLayout(chipsRow);
    layout->addWidget(m_filterChipsRow);

    // Loading progress bar (shown during deferred init, hidden after)
    m_loadingBar = new QProgressBar(this);
    m_loadingBar->setRange(0, 0); // indeterminate
    m_loadingBar->setTextVisible(false);
    m_loadingBar->setFixedHeight(4);
    m_loadingBar->setVisible(false);
    layout->addWidget(m_loadingBar);

    // Tree widget for sessions — custom subclass adds drag-drop reparenting
    // for category/project-group nodes.
    auto *sessionTree = new SessionTreeWidget(this);
    m_treeWidget = sessionTree;
    m_treeWidget->setObjectName(QStringLiteral("sessionTree"));
    m_treeWidget->setColumnCount(2);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeWidget->setIndentation(12);

    // Wire the drop signal to the alias/override persistence handler.
    connect(sessionTree, &SessionTreeWidget::dropRequested, this, &SessionManagerPanel::handleDropRequest);
    // Vim-style keyboard signals. actionRequested → handleTreeAction dispatches
    // per-item; topRequested / bottomRequested / focusFilterRequested /
    // escapePressed are handled inline because they only touch widget state.
    connect(sessionTree, &SessionTreeWidget::actionRequested, this, &SessionManagerPanel::handleTreeAction);
    connect(sessionTree, &SessionTreeWidget::focusFilterRequested, this, [this]() {
        if (m_filterEdit) {
            m_filterEdit->setFocus();
            m_filterEdit->selectAll();
        }
    });
    connect(sessionTree, &SessionTreeWidget::escapePressed, this, [this]() {
        if (m_filterEdit && !m_filterEdit->text().isEmpty()) {
            m_filterEdit->clear();
        }
        if (m_treeWidget) {
            m_treeWidget->clearSelection();
        }
    });
    connect(sessionTree, &SessionTreeWidget::topRequested, this, [this]() {
        if (!m_treeWidget || m_treeWidget->topLevelItemCount() == 0) {
            return;
        }
        QTreeWidgetItem *first = m_treeWidget->topLevelItem(0);
        m_treeWidget->setCurrentItem(first);
        m_treeWidget->scrollToItem(first);
    });
    connect(sessionTree, &SessionTreeWidget::bottomRequested, this, [this]() {
        if (!m_treeWidget || m_treeWidget->topLevelItemCount() == 0) {
            return;
        }
        // Walk to the deepest last-visible descendant of the last top-level
        // item so `G` behaves like vim (last _visible_ line, respecting the
        // current expand/collapse state).
        QTreeWidgetItem *last = m_treeWidget->topLevelItem(m_treeWidget->topLevelItemCount() - 1);
        while (last && last->isExpanded() && last->childCount() > 0) {
            last = last->child(last->childCount() - 1);
        }
        if (last) {
            m_treeWidget->setCurrentItem(last);
            m_treeWidget->scrollToItem(last);
        }
    });
    // Auto-expand groups when a drag hovers over them. Redundant with the
    // subclass ctor's own setAutoExpandDelay(500), but explicit is fine.
    m_treeWidget->setAutoExpandDelay(500);
    // Column 0: session name (stretches), Column 1: indicators (fixed width, right-aligned)
    m_treeWidget->header()->setStretchLastSection(false);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SessionManagerPanel::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &SessionManagerPanel::onContextMenu);

    // Detect when user stops interacting with tree to flush deferred updates
    m_treeWidget->viewport()->installEventFilter(this);
    m_treeWidget->installEventFilter(this);

    // Panel-level `/` shortcut — focuses the filter box no matter where the
    // focus currently sits inside the panel.  Uses WidgetWithChildrenShortcut
    // so it doesn't leak into other docks.
    auto *slashShortcut = new QShortcut(QKeySequence(Qt::Key_Slash), this);
    slashShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(slashShortcut, &QShortcut::activated, this, [this]() {
        if (m_filterEdit) {
            m_filterEdit->setFocus();
            m_filterEdit->selectAll();
        }
    });

    // Filter box event filter: Esc clears text and returns focus to tree.
    // The QLineEdit swallows Esc otherwise (nothing happens), which is a
    // dead end for keyboard-only users.
    m_filterEdit->installEventFilter(this);

    m_treeWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *treeToolbar = new TreeToolbar(m_treeWidget, this);
    layout->addWidget(treeToolbar);
    layout->addWidget(m_treeWidget, 1 /* stretch */);

    // Empty state overlay
    m_emptyStateLabel = new QLabel(m_treeWidget);
    m_emptyStateLabel->setText(i18n("No Claude sessions yet\nOpen a Claude tab to get started"));
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setStyleSheet(QStringLiteral("color: #757575; font-style: italic; padding: 40px;"));
    m_emptyStateLabel->setWordWrap(true);
    m_emptyStateLabel->setVisible(true);

    // Categories are no longer created — sessions are grouped by project (workingDirectory)
    // and state is rendered as a per-session icon. See ensureProjectGroup() and stateIcon().
    setMinimumWidth(200);
}

// ============================================================
// Filter chips toolbar + state-token helpers
// ============================================================

void SessionManagerPanel::buildFilterChips(QHBoxLayout *into)
{
    auto *settings = KonsolaiSettings::instance();
    const QStringList visible =
        settings ? settings->visibleSessionStates() : QStringList{QStringLiteral("active"), QStringLiteral("detached"), QStringLiteral("pinned")};
    for (const QString &v : visible) {
        m_visibleStates.insert(v);
    }

    struct Chip {
        QString token;
        QString label;
        QString iconName;
    };
    // Primary chips: rendered inline. The four most common states; long-tail
    // (archived / dismissed / discovered) go into the overflow hamburger so the
    // row stays compact.
    const QList<Chip> primary = {
        {QStringLiteral("active"), i18n("Active"), QStringLiteral("media-playback-start")},
        {QStringLiteral("detached"), i18n("Detached"), QStringLiteral("media-playback-pause")},
        {QStringLiteral("closed"), i18n("Closed"), QStringLiteral("window-close")},
        {QStringLiteral("pinned"), i18n("Pinned"), QStringLiteral("pin")},
    };
    // Overflow: state tokens reachable only through the hamburger menu. Not added to m_filterChips
    // (so anything that looks up a chip by token gracefully no-ops for these).
    const QList<Chip> overflow = {
        {QStringLiteral("archived"), i18n("Archived"), QStringLiteral("archive-remove")},
        {QStringLiteral("dismissed"), i18n("Dismissed"), QStringLiteral("edit-clear-history")},
        {QStringLiteral("discovered"), i18n("Discovered"), QStringLiteral("edit-find")},
        // Subagent sessions (spawned by Task tool / agent-fleet worker) are
        // hidden from the default view — they'd otherwise clutter the tree
        // with worker sessions the user doesn't drive directly.  Toggle on
        // from the overflow to inspect.
        {QStringLiteral("subagent"), i18n("Subagents"), QStringLiteral("system-run")},
    };
    for (const Chip &c : primary) {
        auto *btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("filterChip_") + c.token);
        btn->setText(c.label);
        btn->setIcon(QIcon::fromTheme(c.iconName));
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setCheckable(true);
        btn->setChecked(m_visibleStates.contains(c.token));
        btn->setAutoRaise(true);
        btn->setToolTip(i18n("Show %1 sessions", c.label));
        connect(btn, &QToolButton::toggled, this, [this, token = c.token](bool on) {
            onFilterChipToggled(token, on);
        });
        m_filterChips.insert(c.token, btn);
        into->addWidget(btn);
    }

    // Overflow hamburger: instant-popup QMenu with one checkable action per overflow state.
    auto *overflowBtn = new QToolButton(this);
    overflowBtn->setObjectName(QStringLiteral("filterChipsOverflow"));
    overflowBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-more-symbolic"), QIcon::fromTheme(QStringLiteral("application-menu"))));
    overflowBtn->setToolTip(i18n("More session filters"));
    overflowBtn->setAutoRaise(true);
    overflowBtn->setPopupMode(QToolButton::InstantPopup);

    auto *overflowMenu = new QMenu(overflowBtn);
    for (const Chip &c : overflow) {
        QAction *action = overflowMenu->addAction(QIcon::fromTheme(c.iconName), c.label);
        action->setCheckable(true);
        action->setChecked(m_visibleStates.contains(c.token));
        action->setObjectName(QStringLiteral("filterChipOverflow_") + c.token);
        action->setToolTip(i18n("Show %1 sessions", c.label));
        connect(action, &QAction::toggled, this, [this, token = c.token](bool on) {
            onFilterChipToggled(token, on);
        });
        // Keep the menu open after each toggle so the user can flip multiple at once.
        // (Qt closes the menu by default; QMenu's mouse handling re-opens on next click.)
    }
    overflowBtn->setMenu(overflowMenu);
    into->addWidget(overflowBtn);
}

void SessionManagerPanel::onFilterChipToggled(const QString &token, bool on)
{
    if (on) {
        m_visibleStates.insert(token);
    } else {
        m_visibleStates.remove(token);
    }
    persistVisibleStates();
    scheduleTreeUpdate();
}

void SessionManagerPanel::persistVisibleStates()
{
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }
    QStringList list(m_visibleStates.cbegin(), m_visibleStates.cend());
    std::sort(list.begin(), list.end());
    settings->setVisibleSessionStates(list);
}

QString SessionManagerPanel::stateTokenFor(const SessionMetadata &meta, bool isLive) const
{
    // Subagent classification wins over dismissed/archived/live/pinned — a
    // subagent session should be filed under the "Subagents" bucket even if
    // it happens to also be dismissed or archived, so the user's toggle for
    // "hide subagents" applies uniformly.
    if (isSubagentSession(meta)) {
        return QStringLiteral("subagent");
    }
    if (meta.isDismissed) {
        return QStringLiteral("dismissed");
    }
    if (meta.isArchived) {
        return QStringLiteral("archived");
    }
    if (isLive) {
        // "active" wins over "pinned" for live sessions; pinned badge is shown separately.
        return QStringLiteral("active");
    }
    if (meta.isPinned) {
        return QStringLiteral("pinned");
    }
    return QStringLiteral("closed");
}

QIcon SessionManagerPanel::stateIcon(const QString &token) const
{
    if (token == QLatin1String("active")) {
        return QIcon::fromTheme(QStringLiteral("media-playback-start"));
    }
    if (token == QLatin1String("detached")) {
        return QIcon::fromTheme(QStringLiteral("media-playback-pause"));
    }
    if (token == QLatin1String("pinned")) {
        return QIcon::fromTheme(QStringLiteral("pin"));
    }
    if (token == QLatin1String("closed")) {
        return QIcon::fromTheme(QStringLiteral("window-close"));
    }
    if (token == QLatin1String("archived")) {
        return QIcon::fromTheme(QStringLiteral("archive-remove"));
    }
    if (token == QLatin1String("dismissed")) {
        return QIcon::fromTheme(QStringLiteral("edit-clear-history"));
    }
    if (token == QLatin1String("discovered")) {
        return QIcon::fromTheme(QStringLiteral("edit-find"));
    }
    if (token == QLatin1String("subagent")) {
        return QIcon::fromTheme(QStringLiteral("system-run"));
    }
    return {};
}

QString SessionManagerPanel::stateLabel(const QString &token) const
{
    if (token == QLatin1String("active")) {
        return i18n("Active");
    }
    if (token == QLatin1String("detached")) {
        return i18n("Detached");
    }
    if (token == QLatin1String("pinned")) {
        return i18n("Pinned");
    }
    if (token == QLatin1String("closed")) {
        return i18n("Closed");
    }
    if (token == QLatin1String("archived")) {
        return i18n("Archived");
    }
    if (token == QLatin1String("dismissed")) {
        return i18n("Dismissed");
    }
    if (token == QLatin1String("discovered")) {
        return i18n("Discovered");
    }
    if (token == QLatin1String("subagent")) {
        return i18n("Subagents");
    }
    return {};
}

int SessionManagerPanel::sessionCountByState(const QString &token) const
{
    int n = 0;
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        const auto &meta = it.value();
        const bool isLive = m_activeSessions.contains(meta.sessionId) || m_cachedLiveNames.contains(meta.sessionName)
            || (meta.isRemote && m_cachedRemoteLiveNames.contains(meta.sessionName));
        if (stateTokenFor(meta, isLive) == token) {
            ++n;
        }
    }
    return n;
}

QString SessionManagerPanel::projectGroupKey(const QString &workingDirectory) const
{
    return workingDirectory.isEmpty() ? QStringLiteral("<no-project>") : workingDirectory;
}

// Static. Tokenize a workdir basename for category prefix matching.
// Lowercased; underscores are normalized to hyphens before splitting on '-'.
QStringList SessionManagerPanel::projectTokens(const QString &workingDirectory)
{
    QString base = QDir(workingDirectory).dirName().toLower();
    base.replace(QLatin1Char('_'), QLatin1Char('-'));
    return base.split(QLatin1Char('-'), Qt::SkipEmptyParts);
}

// Static. For each workdir, find its category key = longest token prefix it
// shares with at least one OTHER workdir in the input set. If no other workdir
// shares any prefix tokens, the workdir's own full basename is its key (it
// becomes a standalone top-level entry rather than getting wrapped).
// O(N^2) on project count; fine for hundreds of projects.
//
// Note: this is a per-pair best-LCP. With workdirs `penta-dragon-dx-claude`,
// `penta-dragon-dx-remote`, `penta-dragon-remake`, the first two share lcp=3,
// the third only shares lcp=2 with each. So the first two get category
// "penta-dragon-dx" and the third gets "penta-dragon" — different keys, so
// they end up in DIFFERENT top-level categories. Acceptable: the per-pair
// greedy gives the tightest grouping per project, at the cost of occasional
// asymmetric placement.
QHash<QString, QString> SessionManagerPanel::buildCategoryMap(const QList<QString> &workingDirectories)
{
    QHash<QString, QStringList> tokens;
    tokens.reserve(workingDirectories.size());
    for (const QString &wd : workingDirectories) {
        if (wd.isEmpty()) {
            continue;
        }
        tokens.insert(wd, projectTokens(wd));
    }

    QHash<QString, QString> map;
    map.reserve(tokens.size());

    for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
        const QStringList &my = it.value();
        if (my.isEmpty()) {
            map.insert(it.key(), it.key());
            continue;
        }

        int bestLcp = 0;
        for (auto other = tokens.cbegin(); other != tokens.cend(); ++other) {
            if (other.key() == it.key()) {
                continue;
            }
            const QStringList &ot = other.value();
            const int n = qMin(my.size(), ot.size());
            int lcp = 0;
            for (int i = 0; i < n; ++i) {
                if (my[i] != ot[i]) {
                    break;
                }
                ++lcp;
            }
            if (lcp > bestLcp) {
                bestLcp = lcp;
            }
        }

        if (bestLcp >= 1) {
            map.insert(it.key(), my.mid(0, bestLcp).join(QLatin1Char('-')));
        } else {
            // Standalone: use full basename (joined) as its own key.
            map.insert(it.key(), my.join(QLatin1Char('-')));
        }
    }
    return map;
}

int SessionManagerPanel::categoryProjectCount(const QString &categoryKey) const
{
    int n = 0;
    for (auto it = m_categoryMap.cbegin(); it != m_categoryMap.cend(); ++it) {
        if (it.value() == categoryKey) {
            ++n;
        }
    }
    return n;
}

QTreeWidgetItem *SessionManagerPanel::ensureProjectGroup(const QString &workingDirectory)
{
    const QString key = projectGroupKey(workingDirectory);
    auto it = m_projectGroups.find(key);
    if (it != m_projectGroups.end()) {
        return it.value();
    }

    // Determine whether this workdir belongs in a multi-project category bucket.
    // m_categoryMap is rebuilt at the start of updateTreeWidgetWithLiveSessions
    // from the full set of m_metadata workdirs (skipping empty ones).
    // A user-created empty category also counts as a valid bucket target even
    // if only one project routes here — the LCP >=2 rule would otherwise
    // standalone it, defeating the "drop into my-stuff" flow.
    QString catKey = m_categoryMap.value(workingDirectory);
    const bool userCreatedBucket = !catKey.isEmpty() && m_categoryGroups.contains(catKey);
    const bool isMulti = !workingDirectory.isEmpty() && !catKey.isEmpty() && (userCreatedBucket || categoryProjectCount(catKey) >= 2);

    QTreeWidgetItem *parentItem = nullptr;
    QString label;

    if (isMulti) {
        // Ensure a top-level category bucket exists.
        auto cit = m_categoryGroups.find(catKey);
        if (cit == m_categoryGroups.end()) {
            auto *cat = new QTreeWidgetItem(m_treeWidget);
            cat->setText(0, catKey);
            cat->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-symbolic"), QIcon::fromTheme(QStringLiteral("folder"))));
            cat->setToolTip(0, i18n("Category: %1", catKey));
            cat->setFlags(Qt::ItemIsEnabled);
            cat->setExpanded(true);
            cat->setData(0, Qt::UserRole + 6, QString(QStringLiteral("category:") + catKey));
            cit = m_categoryGroups.insert(catKey, cat);
        }
        parentItem = cit.value();

        // Project sub-label: basename with the category prefix stripped.
        const QString base = QDir(workingDirectory).dirName();
        QString lower = base.toLower();
        lower.replace(QLatin1Char('_'), QLatin1Char('-'));
        if (lower.startsWith(catKey + QLatin1Char('-'))) {
            label = base.mid(catKey.size() + 1);
        } else if (lower == catKey) {
            // Single-token project (e.g. "konsolai") sitting in a multi-project
            // category alongside "konsolai-handbook", "konsolai-keybind", etc.
            // Show its full basename so the user can spot it.
            label = base;
        } else {
            label = base;
        }
        if (label.isEmpty()) {
            label = base;
        }
    } else {
        // Standalone project: rendered at top level (no category wrapper).
        parentItem = nullptr;
        label = workingDirectory.isEmpty() ? i18n("(no project)") : QDir(workingDirectory).dirName();
        if (label.isEmpty()) {
            label = workingDirectory;
        }
    }

    auto *group = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(m_treeWidget);
    group->setText(0, label);
    group->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
    group->setToolTip(0, workingDirectory);
    group->setFlags(Qt::ItemIsEnabled);
    group->setExpanded(true);
    // Mark this as a project-group so context menu + tests can detect it.
    group->setData(0, Qt::UserRole + 6, QString(QStringLiteral("group:") + key));
    m_projectGroups.insert(key, group);
    return group;
}

void SessionManagerPanel::showLoadingState()
{
    m_emptyStateLabel->setText(i18n("Loading sessions..."));
    m_emptyStateLabel->setVisible(true);
    m_loadingBar->setVisible(true);
    m_treeWidget->setVisible(false);
}

void SessionManagerPanel::showReadyState()
{
    m_loadingBar->setVisible(false);
    m_emptyStateLabel->setText(i18n("No Claude sessions yet\nOpen a Claude tab to get started"));
    // Tree visibility will be managed by updateTreeWidget — just ensure it's shown
    m_treeWidget->setVisible(true);
}

void SessionManagerPanel::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) {
        return;
    }

    m_collapsed = collapsed;

    if (collapsed) {
        m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-expand-left")));
        setMaximumWidth(32);
        m_treeWidget->hide();
        m_filterEdit->hide();
        m_newSessionButton->hide();
    } else {
        m_collapseButton->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-collapse-left")));
        setMaximumWidth(QWIDGETSIZE_MAX);
        m_treeWidget->show();
        m_filterEdit->show();
        m_newSessionButton->show();
    }

    Q_EMIT collapsedChanged(collapsed);
}

void SessionManagerPanel::ensureHooksConfigured(ClaudeSession *session)
{
    if (!session || session->isRemote()) {
        return; // Skip for null or remote sessions
    }

    QString workDir = session->workingDirectory();
    if (workDir.isEmpty()) {
        return;
    }

    QString settingsPath = workDir + QStringLiteral("/.claude/settings.local.json");
    QString sessionId = session->sessionId();
    QString socketPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
    QString handlerPath = ClaudeHookHandler::hookHandlerPath();

    if (handlerPath.isEmpty()) {
        return; // No hook handler available
    }

    // Check if hooks are already configured for this session
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (doc.isObject()) {
            QJsonObject settings = doc.object();
            if (settings.contains(QStringLiteral("hooks"))) {
                QJsonObject hooks = settings[QStringLiteral("hooks")].toObject();
                // Check if any hook points to our socket
                QByteArray hooksBytes = QJsonDocument(hooks).toJson();
                if (hooksBytes.contains(socketPath.toUtf8())) {
                    return; // Hooks already configured correctly
                }
            }
        }
    }

    // Hooks missing or pointing to wrong socket - repair them
    qDebug() << "SessionManagerPanel: Repairing missing/stale hooks for session" << sessionId;

    // Read existing settings
    QJsonObject settings;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            settings = doc.object();
        }
        file.close();
    }

    // Generate hooks config
    auto makeHookEntry = [&](const QString &eventType) -> QJsonArray {
        QString cmdStr = QStringLiteral("'%1' --socket '%2' --event '%3'").arg(handlerPath, socketPath, eventType);
        QJsonObject hookDef;
        hookDef[QStringLiteral("type")] = QStringLiteral("command");
        hookDef[QStringLiteral("command")] = cmdStr;

        QJsonObject entry;
        entry[QStringLiteral("matcher")] = QStringLiteral("*");
        entry[QStringLiteral("hooks")] = QJsonArray{hookDef};
        return QJsonArray{entry};
    };

    QJsonObject hooks;
    hooks[QStringLiteral("Notification")] = makeHookEntry(QStringLiteral("Notification"));
    hooks[QStringLiteral("Stop")] = makeHookEntry(QStringLiteral("Stop"));
    hooks[QStringLiteral("PreToolUse")] = makeHookEntry(QStringLiteral("PreToolUse"));
    hooks[QStringLiteral("PostToolUse")] = makeHookEntry(QStringLiteral("PostToolUse"));
    hooks[QStringLiteral("PermissionRequest")] = makeHookEntry(QStringLiteral("PermissionRequest"));
    hooks[QStringLiteral("SubagentStart")] = makeHookEntry(QStringLiteral("SubagentStart"));
    hooks[QStringLiteral("SubagentStop")] = makeHookEntry(QStringLiteral("SubagentStop"));
    hooks[QStringLiteral("TeammateIdle")] = makeHookEntry(QStringLiteral("TeammateIdle"));
    hooks[QStringLiteral("TaskCompleted")] = makeHookEntry(QStringLiteral("TaskCompleted"));

    settings[QStringLiteral("hooks")] = hooks;

    // Ensure .claude directory exists
    QDir().mkpath(workDir + QStringLiteral("/.claude"));

    // Write settings
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "SessionManagerPanel: Repaired hooks for session" << sessionId;
    }
}

void SessionManagerPanel::registerSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    // Queue sessions arriving before deferred init completes
    if (!m_initialized) {
        m_pendingRegistrations.append(session);
        return;
    }

    QString sessionId = session->sessionId();

    // Fast path: session already registered (tab switch, not new registration).
    // Update lastAccessed in memory only — no save or tree rebuild needed.
    // The timestamp will be persisted on the next natural save (state change, approval, etc.).
    if (m_activeSessions.contains(sessionId) && m_activeSessions[sessionId] == session) {
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
        }
        return;
    }

    m_activeSessions[sessionId] = session;

    // Ensure hooks are configured for this session's project
    ensureHooksConfigured(session);

    // Update or create metadata
    if (!m_metadata.contains(sessionId)) {
        SessionMetadata meta;
        meta.sessionId = sessionId;
        meta.sessionName = session->sessionName();
        meta.profileName = session->profileName();
        meta.workingDirectory = session->workingDirectory();
        meta.createdAt = QDateTime::currentDateTime();
        meta.lastAccessed = meta.createdAt;
        meta.isArchived = false;
        // New session - capture initial yolo mode from session (which comes from global settings)
        meta.yoloMode = session->yoloMode();
        meta.doubleYoloMode = session->doubleYoloMode();
        // Capture remote SSH fields so session can be restored after restart
        meta.isRemote = session->isRemote();
        meta.sshHost = session->sshHost();
        meta.sshUsername = session->sshUsername();
        meta.sshPort = session->sshPort();
        m_metadata[sessionId] = meta;
    } else {
        // Session was archived/expired/closed, now reopened - clear stale flags
        m_metadata[sessionId].isArchived = false;
        m_metadata[sessionId].isExpired = false;
        m_metadata[sessionId].isDismissed = false;
        m_metadata[sessionId].sessionName = session->sessionName();
        m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
        m_explicitlyClosed.remove(sessionId);

        // Restore per-session yolo mode settings from saved metadata
        session->setYoloMode(m_metadata[sessionId].yoloMode);
        session->setDoubleYoloMode(m_metadata[sessionId].doubleYoloMode);

        // Restore approval counts and log from saved metadata
        const auto &meta = m_metadata[sessionId];
        if (meta.yoloApprovalCount > 0 || meta.doubleYoloApprovalCount > 0) {
            session->restoreApprovalState(meta.yoloApprovalCount, meta.doubleYoloApprovalCount, meta.approvalLog);
        }

        qDebug() << "SessionManagerPanel: Restored yolo mode for" << sessionId << "- yolo:" << meta.yoloMode << "double:" << meta.doubleYoloMode
                 << "approvals:" << (meta.yoloApprovalCount + meta.doubleYoloApprovalCount);
    }

    scheduleMetadataSave();

    // Invalidate caches for this session's working directory and refresh in background
    const QString &workDir = session->workingDirectory();
    if (!workDir.isEmpty()) {
        m_conversationCache.remove(workDir);
        m_gitBranchCache.remove(workDir);
        m_gsdBadgeCache.remove(workDir);
    }
    m_discoveredCacheValid = false;
    refreshCachesAsync();

    scheduleTreeUpdate();

    // Signal connections below are idempotent — previous connections for this
    // session pointer are disconnected first (handles session ID reuse after unarchive).
    disconnect(session, nullptr, this, nullptr);

    // Connect to session finished (PTY died, e.g. tab closed) — fires immediately
    connect(session, &Konsole::Session::finished, this, [this, sessionId]() {
        if (m_activeSessions.contains(sessionId)) {
            m_activeSessions.remove(sessionId);
            scheduleTreeUpdate();
        }
    });

    // Connect to session destruction (backup, fires later via deleteLater)
    connect(session, &QObject::destroyed, this, [this, sessionId]() {
        if (m_activeSessions.contains(sessionId)) {
            m_activeSessions.remove(sessionId);
            scheduleTreeUpdate();
        }
    });

    // Connect to working directory changes (after run() gets real path from tmux)
    QPointer<ClaudeSession> safeSession(session);
    connect(session, &ClaudeSession::workingDirectoryChanged, this, [this, safeSession, sessionId](const QString &newPath) {
        if (!safeSession || !m_metadata.contains(sessionId) || newPath.isEmpty()) {
            return;
        }
        // Invalidate caches for old and new working directory
        const QString oldPath = m_metadata[sessionId].workingDirectory;
        if (!oldPath.isEmpty()) {
            m_conversationCache.remove(oldPath);
            m_gitBranchCache.remove(oldPath);
            m_gsdBadgeCache.remove(oldPath);
        }
        m_conversationCache.remove(newPath);
        m_gitBranchCache.remove(newPath);
        m_gsdBadgeCache.remove(newPath);

        m_metadata[sessionId].workingDirectory = newPath;
        // Re-run hook setup now that we have a valid working directory
        // (hooks require workDir and skip if empty at registerSession time)
        ensureHooksConfigured(safeSession);
        scheduleMetadataSave();
        updateTreeWidget();
        qDebug() << "SessionManagerPanel: Updated working directory for" << sessionId << "to" << newPath;
    });

    // Connect to approval count changes — lightweight label refresh only
    connect(session, &ClaudeSession::approvalCountChanged, this, [this, sessionId]() {
        ClaudeSession *s = m_activeSessions.value(sessionId);
        int newCount = s ? s->totalApprovalCount() : 0;
        if (m_lastKnownApprovalCount.value(sessionId, -1) == newCount) {
            return; // No visible change
        }
        m_lastKnownApprovalCount[sessionId] = newCount;
        refreshSessionItemLabel(sessionId);
    });

    // Persist approval state on each new approval (debounced to avoid excessive I/O)
    connect(session, &ClaudeSession::approvalLogged, this, [this, sessionId](const ApprovalLogEntry &entry) {
        Q_UNUSED(entry);
        if (m_metadata.contains(sessionId)) {
            if (ClaudeSession *s = m_activeSessions.value(sessionId)) {
                m_metadata[sessionId].yoloApprovalCount = s->yoloApprovalCount();
                m_metadata[sessionId].doubleYoloApprovalCount = s->doubleYoloApprovalCount();
                m_metadata[sessionId].approvalLog = s->approvalLog();
            }
        }
        scheduleMetadataSave();
    });

    // Connect to yolo mode changes — lightweight label refresh + persist
    connect(session, &ClaudeSession::yoloModeChanged, this, [this, sessionId](bool enabled) {
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].yoloMode = enabled;
            scheduleMetadataSave();
        }
        refreshSessionItemLabel(sessionId);
    });
    connect(session, &ClaudeSession::doubleYoloModeChanged, this, [this, sessionId](bool enabled) {
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].doubleYoloMode = enabled;
            scheduleMetadataSave();
        }
        refreshSessionItemLabel(sessionId);
    });
    // Connect to state changes (Working/Idle/etc.) to update live activity line (with smart filtering)
    connect(session, &ClaudeSession::stateChanged, this, [this, sessionId](ClaudeProcess::State newState) {
        if (m_lastKnownState.value(sessionId, static_cast<ClaudeProcess::State>(-1)) == newState) {
            return; // No visible change, skip rebuild
        }
        m_lastKnownState[sessionId] = newState;
        scheduleTreeUpdate();
    });

    // Connect to task description changes to update display
    connect(session, &ClaudeSession::taskDescriptionChanged, this, [this]() {
        scheduleTreeUpdate();
    });

    // Connect to subagent/team events to update tree with nested agents
    connect(session, &ClaudeSession::subagentStarted, this, [this]() {
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::subagentStopped, this, [this]() {
        scheduleTreeUpdate();
    });
    connect(session, &ClaudeSession::teamInfoChanged, this, [this]() {
        scheduleTreeUpdate();
    });

    // Connect to subprocess changes to update tree with running commands
    connect(session, &ClaudeSession::subprocessChanged, this, [this]() {
        scheduleTreeUpdate();
    });
}

void SessionManagerPanel::unregisterSession(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    QString sessionId = session->sessionId();

    // Guard: skip if already unregistered (e.g., archiveSession already removed it)
    if (!m_activeSessions.contains(sessionId)) {
        return;
    }

    // Save yolo mode settings, approval state, resume ID, and subagent/subprocess snapshots
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].yoloMode = session->yoloMode();
        m_metadata[sessionId].doubleYoloMode = session->doubleYoloMode();
        m_metadata[sessionId].yoloApprovalCount = session->yoloApprovalCount();
        m_metadata[sessionId].doubleYoloApprovalCount = session->doubleYoloApprovalCount();
        m_metadata[sessionId].approvalLog = session->approvalLog();
        if (!session->resumeSessionId().isEmpty()) {
            m_metadata[sessionId].lastResumeSessionId = session->resumeSessionId();
        }
        if (!session->taskDescription().isEmpty()) {
            m_metadata[sessionId].description = session->taskDescription();
        }
        m_metadata[sessionId].subagents = session->subagents().values().toVector();
        m_metadata[sessionId].subprocesses = session->subprocesses().values().toVector();
        m_metadata[sessionId].promptGroupLabels = session->promptGroupLabels();
        m_metadata[sessionId].currentPromptRound = session->currentPromptRound();
        m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
        scheduleMetadataSave();
    }

    m_activeSessions.remove(sessionId);

    // Clean up signal-filtering state maps
    m_lastKnownState.remove(sessionId);
    m_lastKnownApprovalCount.remove(sessionId);
    m_discoveredCacheValid = false;

    updateTreeWidget();
}

QList<SessionMetadata> SessionManagerPanel::allSessions() const
{
    return m_metadata.values();
}

const SessionMetadata *SessionManagerPanel::sessionMetadata(const QString &sessionId) const
{
    auto it = m_metadata.constFind(sessionId);
    return it != m_metadata.constEnd() ? &(*it) : nullptr;
}

bool SessionManagerPanel::isSessionActive(const QString &sessionId) const
{
    return m_activeSessions.contains(sessionId) && m_activeSessions[sessionId];
}

QList<SessionMetadata> SessionManagerPanel::pinnedSessions() const
{
    QList<SessionMetadata> result;
    for (const auto &meta : m_metadata) {
        if (meta.isPinned && !meta.isArchived) {
            result.append(meta);
        }
    }
    return result;
}

QList<SessionMetadata> SessionManagerPanel::archivedSessions() const
{
    QList<SessionMetadata> result;
    for (const auto &meta : m_metadata) {
        if (meta.isArchived) {
            result.append(meta);
        }
    }
    return result;
}

void SessionManagerPanel::cleanupStaleSockets()
{
    // Remove stale socket and yolo files from sessions that no longer have live tmux sessions.
    // Uses async tmux query to avoid blocking the GUI thread.
    QString sessionsDir = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions");
    QDir dir(sessionsDir);
    if (!dir.exists()) {
        return;
    }

    // Collect socket files before the async call (filesystem reads are fast)
    QStringList sockFiles = dir.entryList({QStringLiteral("*.sock")}, QDir::Files);
    if (sockFiles.isEmpty()) {
        return;
    }

    auto *tmux = new TmuxManager(nullptr);
    QPointer<SessionManagerPanel> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux, sessionsDir, sockFiles](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();

        if (!guard) {
            return;
        }

        QSet<QString> liveIds;
        for (const auto &info : liveSessions) {
            // Extract ID from session name: konsolai-{profile}-{id}
            QStringList parts = info.name.split(QLatin1Char('-'));
            if (parts.size() >= 3) {
                liveIds.insert(parts.last());
            }
        }

        // Clean up socket and yolo files for dead sessions
        QDir sessDir(sessionsDir);
        int cleaned = 0;
        for (const QString &sockFile : sockFiles) {
            QString id = sockFile.left(sockFile.length() - 5); // Remove .sock
            if (!liveIds.contains(id)) {
                QFile::remove(sessDir.filePath(sockFile));
                QString yoloFile = id + QStringLiteral(".yolo");
                if (sessDir.exists(yoloFile)) {
                    QFile::remove(sessDir.filePath(yoloFile));
                }
                QString teamYoloFile = id + QStringLiteral(".yolo-team");
                if (sessDir.exists(teamYoloFile)) {
                    QFile::remove(sessDir.filePath(teamYoloFile));
                }
                cleaned++;
            }
        }

        if (cleaned > 0) {
            qDebug() << "SessionManagerPanel: Cleaned up" << cleaned << "stale socket/yolo files";
        }
    });
}

void SessionManagerPanel::refresh()
{
    // Discover tmux sessions that aren't tracked, using async tmux query
    // to avoid blocking the GUI thread.
    if (!m_registry) {
        updateTreeWidget();
        return;
    }

    auto *tmux = new TmuxManager(nullptr);
    QPointer<SessionManagerPanel> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();

        if (!guard) {
            return;
        }

        // Feed pre-fetched tmux data to registry (non-blocking)
        if (guard->m_registry) {
            guard->m_registry->refreshOrphanedSessions(liveSessions);
            for (const auto &state : guard->m_registry->orphanedSessions()) {
                if (!guard->m_metadata.contains(state.sessionId)) {
                    SessionMetadata meta;
                    meta.sessionId = state.sessionId;
                    meta.sessionName = state.sessionName;
                    meta.profileName = state.profileName;
                    meta.workingDirectory = state.workingDirectory;
                    meta.lastAccessed = state.lastAccessed;
                    meta.createdAt = state.created;
                    meta.isArchived = false;
                    meta.isPinned = false;
                    guard->m_metadata[state.sessionId] = meta;
                }
            }
        }

        // Repair stale createdAt timestamps for existing metadata entries.
        // A previous bug recorded the discovery time instead of tmux creation time,
        // causing multiple sessions to share the same timestamp and display name.
        static const QRegularExpression idPattern(QStringLiteral("^konsolai-.+-([a-f0-9]{8})$"));
        for (const TmuxManager::SessionInfo &info : liveSessions) {
            QRegularExpressionMatch idMatch = idPattern.match(info.name);
            if (!idMatch.hasMatch()) {
                continue;
            }
            QString sessionId = idMatch.captured(1);
            if (!guard->m_metadata.contains(sessionId)) {
                continue;
            }
            bool ok = false;
            qint64 epoch = info.created.toLongLong(&ok);
            if (!ok || epoch <= 0) {
                continue;
            }
            QDateTime tmuxCreated = QDateTime::fromSecsSinceEpoch(epoch);
            auto &meta = guard->m_metadata[sessionId];
            // If metadata createdAt is later than tmux creation, it was set from
            // discovery time — replace with the real tmux creation time.
            if (meta.createdAt > tmuxCreated) {
                meta.createdAt = tmuxCreated;
            }
        }

        guard->updateTreeWidget();
    });
}

void SessionManagerPanel::pinSession(const QString &sessionId)
{
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].isPinned = true;
        scheduleMetadataSave();
        rebuildTreeSync(); // Immediate sync rebuild — user explicitly requested this
    }
}

void SessionManagerPanel::unpinSession(const QString &sessionId)
{
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].isPinned = false;
        scheduleMetadataSave();
        rebuildTreeSync(); // Immediate sync rebuild — user explicitly requested this
    }
}

void SessionManagerPanel::archiveSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    QString sessionName = m_metadata[sessionId].sessionName;

    // Snapshot live session data into metadata BEFORE removing from active map
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            if (!session->resumeSessionId().isEmpty()) {
                m_metadata[sessionId].lastResumeSessionId = session->resumeSessionId();
            }
            m_metadata[sessionId].subagents = session->subagents().values().toVector();
            m_metadata[sessionId].subprocesses = session->subprocesses().values().toVector();
            m_metadata[sessionId].promptGroupLabels = session->promptGroupLabels();
            m_metadata[sessionId].currentPromptRound = session->currentPromptRound();
            disconnect(session, nullptr, this, nullptr);
        }
    }

    // Remove from active sessions
    m_activeSessions.remove(sessionId);

    // Mark as archived
    m_metadata[sessionId].isArchived = true;
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    scheduleMetadataSave();

    // Clean up stale socket, yolo, and yolo-team files
    QString socketPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
    if (QFile::exists(socketPath)) {
        QFile::remove(socketPath);
    }
    QString yoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo");
    if (QFile::exists(yoloPath)) {
        QFile::remove(yoloPath);
    }
    QString teamYoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo-team");
    if (QFile::exists(teamYoloPath)) {
        QFile::remove(teamYoloPath);
    }

    // Kill the tmux session asynchronously, then update tree after kill completes
    if (sessionName.isEmpty()) {
        qDebug() << "SessionManagerPanel::archiveSession - no session name, skipping tmux kill";
        updateTreeWidget();
    } else {
        if (m_pendingAsyncKills++ == 0) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
        }
        auto *tmux = new TmuxManager(nullptr);
        QPointer<SessionManagerPanel> guard(this);
        tmux->sessionExistsAsync(sessionName, [tmux, sessionName, guard](bool exists) {
            if (exists) {
                tmux->killSessionAsync(sessionName, [tmux, guard](bool) {
                    tmux->deleteLater();
                    if (guard) {
                        if (--guard->m_pendingAsyncKills == 0) {
                            QApplication::restoreOverrideCursor();
                        }
                        guard->updateTreeWidget();
                    } else {
                        QApplication::restoreOverrideCursor();
                    }
                });
            } else {
                tmux->deleteLater();
                if (guard) {
                    if (--guard->m_pendingAsyncKills == 0) {
                        QApplication::restoreOverrideCursor();
                    }
                    guard->updateTreeWidget();
                } else {
                    QApplication::restoreOverrideCursor();
                }
            }
        });
    }
}

void SessionManagerPanel::closeSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    QString sessionName = m_metadata[sessionId].sessionName;

    // Snapshot live session data into metadata BEFORE removing from active map
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            if (!session->resumeSessionId().isEmpty()) {
                m_metadata[sessionId].lastResumeSessionId = session->resumeSessionId();
            }
            m_metadata[sessionId].subagents = session->subagents().values().toVector();
            m_metadata[sessionId].subprocesses = session->subprocesses().values().toVector();
            m_metadata[sessionId].promptGroupLabels = session->promptGroupLabels();
            m_metadata[sessionId].currentPromptRound = session->currentPromptRound();
            disconnect(session, nullptr, this, nullptr);
        }
    }

    // Remove from active sessions
    m_activeSessions.remove(sessionId);

    // Mark as explicitly closed so tree categorization skips the tmux-alive check
    // (tmux kill is async and may not have finished by tree update time)
    m_explicitlyClosed.insert(sessionId);

    // Update last accessed but do NOT mark as archived — it will appear in Closed
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    scheduleMetadataSave();

    // Clean up stale socket, yolo, and yolo-team files
    QString socketPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".sock");
    if (QFile::exists(socketPath)) {
        QFile::remove(socketPath);
    }
    QString yoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo");
    if (QFile::exists(yoloPath)) {
        QFile::remove(yoloPath);
    }
    QString teamYoloPath = ClaudeHookHandler::sessionDataDir() + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".yolo-team");
    if (QFile::exists(teamYoloPath)) {
        QFile::remove(teamYoloPath);
    }

    // Kill the tmux session asynchronously, then update tree AFTER kill completes.
    // This avoids a race where the tree queries tmux before the kill finishes,
    // causing the session to appear "Detached" instead of "Closed".
    if (sessionName.isEmpty()) {
        qDebug() << "SessionManagerPanel::closeSession - no session name, skipping tmux kill";
        updateTreeWidget();
    } else {
        if (m_pendingAsyncKills++ == 0) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
        }
        auto *tmux = new TmuxManager(nullptr);
        QPointer<SessionManagerPanel> guard(this);
        tmux->sessionExistsAsync(sessionName, [tmux, sessionName, guard](bool exists) {
            if (exists) {
                tmux->killSessionAsync(sessionName, [tmux, guard](bool) {
                    tmux->deleteLater();
                    if (guard) {
                        if (--guard->m_pendingAsyncKills == 0) {
                            QApplication::restoreOverrideCursor();
                        }
                        guard->updateTreeWidget();
                    } else {
                        QApplication::restoreOverrideCursor();
                    }
                });
            } else {
                tmux->deleteLater();
                if (guard) {
                    if (--guard->m_pendingAsyncKills == 0) {
                        QApplication::restoreOverrideCursor();
                    }
                    guard->updateTreeWidget();
                } else {
                    QApplication::restoreOverrideCursor();
                }
            }
        });
    }
}

void SessionManagerPanel::unarchiveSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    const auto &meta = m_metadata[sessionId];

    // Emit signal to create new session with same ID (including remote SSH fields)
    Q_EMIT unarchiveRequested(sessionId, meta.workingDirectory, meta.isRemote, meta.sshHost, meta.sshUsername, meta.sshPort);
}

void SessionManagerPanel::markExpired(const QString &sessionName)
{
    // Find metadata by session name, falling back to sessionId lookup
    auto it = m_metadata.end();

    // Try direct sessionId lookup first (caller may pass sessionId)
    if (m_metadata.contains(sessionName)) {
        it = m_metadata.find(sessionName);
    } else {
        // Linear search by sessionName
        for (auto search = m_metadata.begin(); search != m_metadata.end(); ++search) {
            if (search->sessionName == sessionName) {
                it = search;
                break;
            }
        }
    }

    if (it != m_metadata.end()) {
        // Mark as expired but NOT archived - let it go to Closed category
        // User must explicitly archive if they don't want to see it
        it->isExpired = true;
        it->lastAccessed = QDateTime::currentDateTime();
        m_activeSessions.remove(it->sessionId);
        scheduleMetadataSave();
        updateTreeWidget();
        qDebug() << "SessionManagerPanel: Marked session as expired (tmux dead):" << sessionName;
    } else {
        qDebug() << "SessionManagerPanel: Could not find session to mark expired:" << sessionName;
    }
}

void SessionManagerPanel::autoArchiveOldClosedSessions()
{
    // Auto-archive sessions in "Closed" category (isExpired && !isArchived) that
    // have been closed for more than 7 days. Never touches Active, Detached, or Pinned.
    const int thresholdDays = 7;
    const QDateTime now = QDateTime::currentDateTime();
    int archived = 0;

    for (auto &meta : m_metadata) {
        // Only target closed (expired) sessions that aren't already archived/dismissed/pinned
        if (!meta.isExpired || meta.isArchived || meta.isDismissed || meta.isPinned) {
            continue;
        }
        // Don't touch sessions with active ClaudeSession objects
        if (m_activeSessions.contains(meta.sessionId)) {
            continue;
        }
        // Check age: lastAccessed must be > threshold days ago
        if (meta.lastAccessed.isValid() && meta.lastAccessed.daysTo(now) > thresholdDays) {
            meta.isArchived = true;
            ++archived;
            qDebug() << "SessionManagerPanel: Auto-archived closed session:" << meta.sessionId << "last accessed:" << meta.lastAccessed.toString(Qt::ISODate);
        }
    }

    if (archived > 0) {
        scheduleMetadataSave();
        scheduleTreeUpdate();
        qDebug() << "SessionManagerPanel: Auto-archived" << archived << "closed sessions older than" << thresholdDays << "days";
    }
}

QString SessionManagerPanel::sessionIdForName(const QString &sessionName) const
{
    // Try direct sessionId lookup first (caller may pass sessionId)
    if (m_metadata.contains(sessionName)) {
        return sessionName;
    }
    // Linear search by sessionName
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        if (it->sessionName == sessionName) {
            return it->sessionId;
        }
    }
    return {};
}

void SessionManagerPanel::dismissSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    m_metadata[sessionId].isDismissed = true;
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    scheduleMetadataSave();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Dismissed session:" << sessionId;
}

void SessionManagerPanel::restoreSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    m_metadata[sessionId].isDismissed = false;
    m_metadata[sessionId].isArchived = true; // Restore to Archived state
    m_metadata[sessionId].lastAccessed = QDateTime::currentDateTime();
    scheduleMetadataSave();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Restored dismissed session:" << sessionId;
}

void SessionManagerPanel::purgeSession(const QString &sessionId)
{
    if (!m_metadata.contains(sessionId)) {
        return;
    }

    // Also remove from the registry
    QString sessionName = m_metadata[sessionId].sessionName;
    if (m_registry && !sessionName.isEmpty()) {
        m_registry->removeSessionState(sessionName);
    }

    m_metadata.remove(sessionId);
    scheduleMetadataSave();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Purged session metadata:" << sessionId;
}

void SessionManagerPanel::purgeDismissed()
{
    QStringList toRemove;
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        if (it->isDismissed) {
            toRemove.append(it.key());
        }
    }

    for (const QString &sessionId : toRemove) {
        QString sessionName = m_metadata[sessionId].sessionName;
        if (m_registry && !sessionName.isEmpty()) {
            m_registry->removeSessionState(sessionName);
        }
        m_metadata.remove(sessionId);
    }

    if (!toRemove.isEmpty()) {
        scheduleMetadataSave();
        updateTreeWidget();
        qDebug() << "SessionManagerPanel: Purged" << toRemove.size() << "dismissed sessions";
    }
}

bool SessionManagerPanel::mergeSessions(const QStringList &sessionIds, const QString &primarySessionId, const MergeFieldChoices &choices)
{
    // Validation: at least two distinct, known sessions; primary must be present.
    if (sessionIds.size() < 2) {
        qWarning() << "SessionManagerPanel::mergeSessions: refusing — fewer than 2 sessions in selection";
        return false;
    }
    if (!sessionIds.contains(primarySessionId)) {
        qWarning() << "SessionManagerPanel::mergeSessions: refusing — primary not in selection:" << primarySessionId;
        return false;
    }
    for (const QString &sid : sessionIds) {
        if (!m_metadata.contains(sid)) {
            qWarning() << "SessionManagerPanel::mergeSessions: refusing — unknown session id:" << sid;
            return false;
        }
    }

    const SessionMetadata &primaryMeta = m_metadata[primarySessionId];
    const QString workdir = primaryMeta.workingDirectory;

    QList<SessionMetadata> others;
    QStringList otherIds;
    for (const QString &sid : sessionIds) {
        if (sid == primarySessionId) {
            continue;
        }
        const SessionMetadata &m = m_metadata[sid];
        if (m.workingDirectory != workdir) {
            qWarning() << "SessionManagerPanel::mergeSessions: refusing — cross-project merge (" << workdir << "vs" << m.workingDirectory << ")";
            return false;
        }
        others.append(m);
        otherIds.append(sid);
    }

    // Build the jsonl size map for resume-id selection.
    QStringList resumeCandidates;
    resumeCandidates.append(primaryMeta.lastResumeSessionId);
    for (const auto &o : others) {
        resumeCandidates.append(o.lastResumeSessionId);
    }
    const QHash<QString, qint64> jsonlSizes = jsonlSizesForResumeIds(workdir, resumeCandidates);

    // Compute and apply.
    const SessionMetadata merged = applyMerge(primaryMeta, others, choices, jsonlSizes);
    m_metadata[primarySessionId] = merged;

    // Dismiss the others, stamping mergedInto.
    for (const QString &sid : otherIds) {
        m_metadata[sid].isDismissed = true;
        m_metadata[sid].mergedInto = primarySessionId;
        m_metadata[sid].lastAccessed = QDateTime::currentDateTime();
    }

    scheduleMetadataSave();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Merged" << otherIds.size() << "sessions into" << primarySessionId;
    return true;
}

bool SessionManagerPanel::canOfferMergeForSelection(const QStringList &sessionIds) const
{
    if (sessionIds.size() < 2) {
        return false;
    }
    QString sharedWorkdir;
    int countEligible = 0;
    for (const QString &sid : sessionIds) {
        auto it = m_metadata.constFind(sid);
        if (it == m_metadata.constEnd()) {
            return false;
        }
        if (it->isDismissed) {
            // Dismissed sessions are not eligible to be merged into a primary.
            return false;
        }
        if (sharedWorkdir.isEmpty()) {
            sharedWorkdir = it->workingDirectory;
        } else if (it->workingDirectory != sharedWorkdir) {
            return false;
        }
        ++countEligible;
    }
    return countEligible >= 2;
}

bool SessionManagerPanel::canOfferUnmergeForSession(const QString &sessionId) const
{
    auto it = m_metadata.constFind(sessionId);
    if (it == m_metadata.constEnd()) {
        return false;
    }
    return !it->mergedInto.isEmpty();
}

bool SessionManagerPanel::canOfferBroadcastForSelection(const QStringList &sessionIds) const
{
    for (const QString &id : sessionIds) {
        auto it = m_activeSessions.constFind(id);
        if (it == m_activeSessions.constEnd()) {
            continue;
        }
        if (it.value().isNull()) {
            continue;
        }
        return true;
    }
    return false;
}

int SessionManagerPanel::broadcastMessage(const QStringList &sessionIds, const QString &templateText, bool pressEnterAfterEach)
{
    // Build the list of valid active recipients first so the per-recipient
    // index/count vars reflect what actually goes out, not what was requested.
    QList<QString> validIds;
    validIds.reserve(sessionIds.size());
    for (const QString &id : sessionIds) {
        if (!m_activeSessions.contains(id)) {
            qWarning() << "SessionManagerPanel::broadcastMessage: skipping unknown/inactive session" << id;
            continue;
        }
        QPointer<ClaudeSession> session = m_activeSessions.value(id);
        if (session.isNull()) {
            qWarning() << "SessionManagerPanel::broadcastMessage: skipping null session pointer for" << id;
            continue;
        }
        validIds.append(id);
    }

    const int total = validIds.size();
    int sent = 0;
    for (int i = 0; i < total; ++i) {
        const QString &id = validIds[i];
        QPointer<ClaudeSession> session = m_activeSessions.value(id);
        if (session.isNull()) {
            qWarning() << "SessionManagerPanel::broadcastMessage: session went away mid-send for" << id;
            continue;
        }
        const SessionMetadata *meta = sessionMetadata(id);
        if (!meta) {
            qWarning() << "SessionManagerPanel::broadcastMessage: no metadata for active session" << id;
            continue;
        }

        QString displayName = meta->description.trimmed();
        if (displayName.isEmpty()) {
            displayName = QDir(meta->workingDirectory).dirName();
        }
        if (displayName.isEmpty()) {
            displayName = meta->sessionName;
        }
        const QString category = m_categoryMap.value(meta->workingDirectory, QDir(meta->workingDirectory).dirName());
        const BroadcastVars vars = buildVars(*meta, displayName, category, /*index=*/i + 1, /*count=*/total);
        const QString substituted = substituteTemplate(templateText, vars);

        session->sendText(substituted);
        if (pressEnterAfterEach) {
            // Mirror sendPrompt's behaviour: Enter via the typed newline so tmux
            // submits the input. sendText uses send-keys -l which means literal
            // input — \n is interpreted as Enter by Claude Code's Ink UI.
            session->sendText(QStringLiteral("\n"));
        }
        ++sent;
    }

    qInfo() << "SessionManagerPanel::broadcastMessage: sent to" << sent << "of" << sessionIds.size() << "requested";
    return sent;
}

void SessionManagerPanel::openBroadcastDialog(const QStringList &activeIds)
{
    QList<BroadcastRecipient> recipients;
    for (const QString &id : activeIds) {
        const SessionMetadata *m = sessionMetadata(id);
        if (!m) {
            continue;
        }
        BroadcastRecipient r;
        r.sessionId = id;
        r.workingDirectory = m->workingDirectory;
        r.tmuxSession = m->sessionName;
        r.displayName = !m->description.trimmed().isEmpty() ? m->description.trimmed() : QDir(m->workingDirectory).dirName();
        if (r.displayName.isEmpty()) {
            r.displayName = m->sessionName;
        }
        r.category = m_categoryMap.value(m->workingDirectory, QDir(m->workingDirectory).dirName());
        recipients.append(r);
    }
    if (recipients.isEmpty()) {
        return;
    }

    BroadcastDialog dlg(recipients, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QStringList chosen = dlg.selectedSessionIds();
    const QString tmpl = dlg.messageTemplate();
    const bool pressEnter = dlg.pressEnterAfterEach();
    broadcastMessage(chosen, tmpl, pressEnter);
}

bool SessionManagerPanel::unmergeSession(const QString &dismissedSessionId)
{
    if (!m_metadata.contains(dismissedSessionId)) {
        qWarning() << "SessionManagerPanel::unmergeSession: unknown session id:" << dismissedSessionId;
        return false;
    }
    SessionMetadata &meta = m_metadata[dismissedSessionId];
    if (meta.mergedInto.isEmpty()) {
        qWarning() << "SessionManagerPanel::unmergeSession: session was not merged:" << dismissedSessionId;
        return false;
    }
    meta.isDismissed = false;
    meta.mergedInto.clear();
    meta.lastAccessed = QDateTime::currentDateTime();
    scheduleMetadataSave();
    updateTreeWidget();
    qDebug() << "SessionManagerPanel: Unmerged session:" << dismissedSessionId;
    return true;
}

void SessionManagerPanel::pauseBackgroundTimers()
{
    if (m_timersPaused) {
        return;
    }
    m_timersPaused = true;

    // Stop periodic timers
    if (m_remoteTmuxTimer) {
        m_remoteTmuxTimer->stop();
    }
    if (m_gitCacheTimer) {
        m_gitCacheTimer->stop();
    }
    if (m_convCacheTimer) {
        m_convCacheTimer->stop();
    }
    if (m_durationTimer) {
        m_durationTimer->stop();
    }
    if (m_autoArchiveTimer) {
        m_autoArchiveTimer->stop();
    }

    // Stop debounce timers — no point rebuilding tree or saving metadata
    // while nobody is looking. Deferred work flushes on resume.
    if (m_updateDebounce && m_updateDebounce->isActive()) {
        m_updateDebounce->stop();
        m_pendingUpdate = true;
    }
    if (m_saveDebounce && m_saveDebounce->isActive()) {
        m_saveDebounce->stop();
        m_pendingSave = true;
    }

    // Pause display timers on each active session
    for (auto it = m_activeSessions.constBegin(); it != m_activeSessions.constEnd(); ++it) {
        if (auto *session = it.value().data()) {
            session->pauseDisplayTimers();
        }
    }

    qDebug() << "SessionManagerPanel: Paused background timers (window inactive)";
}

void SessionManagerPanel::resumeBackgroundTimers()
{
    if (!m_timersPaused) {
        return;
    }
    m_timersPaused = false;

    // Restart periodic timers
    if (m_remoteTmuxTimer) {
        m_remoteTmuxTimer->start();
    }
    if (m_gitCacheTimer) {
        m_gitCacheTimer->start();
    }
    if (m_convCacheTimer) {
        m_convCacheTimer->start();
    }
    if (m_autoArchiveTimer) {
        m_autoArchiveTimer->start();
    }
    // m_durationTimer is started/stopped by updateTreeWidget based on active subagents,
    // so just let the tree rebuild handle it.

    // Resume display timers on each active session
    for (auto it = m_activeSessions.constBegin(); it != m_activeSessions.constEnd(); ++it) {
        if (auto *session = it.value().data()) {
            session->resumeDisplayTimers();
        }
    }

    // Flush deferred metadata save first (so tree sees current data)
    if (m_pendingSave) {
        m_pendingSave = false;
        scheduleMetadataSave();
    }

    // Schedule a gentle tree refresh to pick up changes that occurred while paused
    scheduleTreeUpdate();

    qDebug() << "SessionManagerPanel: Resumed background timers (window active)";
}

void SessionManagerPanel::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)

    if (!item) {
        return;
    }

    // Project-group items — toggle expand, not clickable as sessions.
    QString compositeKey = item->data(0, Qt::UserRole + 6).toString();
    if (compositeKey.startsWith(QStringLiteral("group:")) || compositeKey.startsWith(QStringLiteral("category:"))) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    // Subagent/subprocess child items live under session items (parent is a session item).
    // Sessions live directly under project groups, so the parent of a "deep" item
    // is a session item — its parent in turn is the project group.
    QTreeWidgetItem *parentItem = item->parent();
    bool parentIsGroup = !parentItem || parentItem->data(0, Qt::UserRole + 6).toString().startsWith(QStringLiteral("group:"));
    if (parentItem && !parentIsGroup) {
        // Check if this is a prompt group item (toggle expand/collapse)
        QVariant promptGroupVar = item->data(0, Qt::UserRole + 3);
        if (promptGroupVar.isValid() && !promptGroupVar.isNull()) {
            item->setExpanded(!item->isExpanded());
            return;
        }

        // Check if this is a task group item (toggle expand/collapse)
        if (!item->data(0, Qt::UserRole + 2).toString().isEmpty()) {
            item->setExpanded(!item->isExpanded());
            return;
        }

        // Check if this is a subprocess item
        QString subprocessId = item->data(0, Qt::UserRole + 4).toString();
        if (!subprocessId.isEmpty()) {
            QString parentSessionId = item->data(0, Qt::UserRole + 1).toString();
            if (m_activeSessions.contains(parentSessionId)) {
                ClaudeSession *session = m_activeSessions[parentSessionId];
                if (session) {
                    const auto &procs = session->subprocesses();
                    if (procs.contains(subprocessId)) {
                        showSubprocessOutput(procs[subprocessId]);
                    }
                }
            }
            return;
        }

        // Subagent child item (depth 2 or 3)
        QString agentId = item->data(0, Qt::UserRole).toString();
        QString parentSessionId = item->data(0, Qt::UserRole + 1).toString();
        if (!agentId.isEmpty() && m_activeSessions.contains(parentSessionId)) {
            ClaudeSession *session = m_activeSessions[parentSessionId];
            if (session) {
                const auto &agents = session->subagents();
                if (agents.contains(agentId)) {
                    const auto &info = agents[agentId];
                    if (!info.transcriptPath.isEmpty() && QFile::exists(info.transcriptPath)) {
                        showSubagentTranscript(info);
                    } else {
                        showSubagentDetails(info);
                    }
                }
            }
        }
        return;
    }

    QString sessionId = item->data(0, Qt::UserRole).toString();
    if (sessionId.isEmpty()) {
        return;
    }

    // Discovered sessions are tagged with state token "discovered" at UserRole+5.
    if (item->data(0, Qt::UserRole + 5).toString() == QLatin1String("discovered")) {
        QString workDir = item->data(0, Qt::UserRole + 1).toString();
        if (workDir.isEmpty()) {
            return;
        }
        // Check if this is a remote discovered item
        QString remoteHost = item->data(0, Qt::UserRole + 2).toString();
        bool isRemoteItem = !remoteHost.isEmpty();
        QString remoteUser = isRemoteItem ? item->data(0, Qt::UserRole + 3).toString() : QString();
        int remotePort = isRemoteItem ? item->data(0, Qt::UserRole + 4).toInt() : 22;
        if (remotePort <= 0) {
            remotePort = 22;
        }

        // Check for existing conversations — offer resume before creating new
        if (!isRemoteItem) {
            if (!m_conversationCache.contains(workDir)) {
                m_conversationCache.insert(workDir, ClaudeSessionRegistry::readClaudeConversations(workDir));
            }
            const auto &conversations = m_conversationCache[workDir];
            if (!conversations.isEmpty()) {
                QString id = ClaudeConversationPicker::pick(conversations, this);
                if (!id.isEmpty()) {
                    Q_EMIT resumeConversationRequested(workDir, id, QString(), QString(), 22);
                    return;
                }
                // User chose "Start Fresh" — fall through to create new
            }
        }

        if (isRemoteItem) {
            Q_EMIT remoteSessionRequested(remoteHost, remoteUser, remotePort, workDir);
        } else {
            Q_EMIT unarchiveRequested(sessionId, workDir, false, QString(), QString(), 22);
        }
        return;
    }

    if (!m_metadata.contains(sessionId)) {
        return;
    }

    const auto &meta = m_metadata[sessionId];

    // "Closed" state: dead tmux, recreate like unarchive
    const bool isLive = m_activeSessions.contains(meta.sessionId) || m_cachedLiveNames.contains(meta.sessionName)
        || (meta.isRemote && m_cachedRemoteLiveNames.contains(meta.sessionName));
    const QString stateToken = stateTokenFor(meta, isLive);

    if (meta.isArchived) {
        // Unarchive and attach
        unarchiveSession(sessionId);
    } else if (m_activeSessions.contains(sessionId)) {
        // Active session — focus its tab
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            Q_EMIT focusSessionRequested(session);
        }
    } else if (stateToken == QLatin1String("closed")) {
        // Closed session — tmux is dead, recreate like unarchive
        unarchiveSession(sessionId);
    } else {
        // Detached session — reattach (remote or local)
        if (meta.isRemote) {
            Q_EMIT remoteAttachRequested(meta.sshHost, meta.sshUsername, meta.sshPort, meta.workingDirectory, meta.sessionName);
        } else {
            Q_EMIT attachRequested(meta.sessionName);
        }
    }
}

void SessionManagerPanel::onContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    qWarning() << "RIGHT-CLICK:" << pos << "item:" << (item ? item->text(0) : QStringLiteral("NULL"))
               << "parent:" << (item && item->parent() ? item->parent()->text(0) : QStringLiteral("none"));
    if (!item) {
        // Empty-space menu — offer "New Category…" so the user can create an
        // empty bucket without having to hunt for the toolbar button.
        QMenu menu(this);
        QAction *newCatAction = menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), i18n("New Category…"));
        newCatAction->setObjectName(QStringLiteral("newCategoryAction"));
        connect(newCatAction, &QAction::triggered, this, &SessionManagerPanel::createUserCategory);
        menu.addSeparator();
        QAction *reorgAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-tree")), i18n("Reorganize Tree with AI…"));
        reorgAction->setObjectName(QStringLiteral("reorganizeTreeAction"));
        connect(reorgAction, &QAction::triggered, this, &SessionManagerPanel::openReorganizeTreeDialog);
        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    // Category items — wrap multiple projects sharing a token prefix.
    // Recursively expand/collapse all nested project groups + their sessions.
    QString compositeKey = item->data(0, Qt::UserRole + 6).toString();
    if (compositeKey.startsWith(QStringLiteral("category:"))) {
        QMenu menu(this);
        QAction *expandAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-tree")), i18n("Expand All"));
        connect(expandAll, &QAction::triggered, this, [item]() {
            std::function<void(QTreeWidgetItem *)> expandRec = [&](QTreeWidgetItem *node) {
                node->setExpanded(true);
                for (int i = 0; i < node->childCount(); ++i) {
                    expandRec(node->child(i));
                }
            };
            expandRec(item);
        });
        QAction *collapseAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-text")), i18n("Collapse All"));
        connect(collapseAll, &QAction::triggered, this, [item]() {
            std::function<void(QTreeWidgetItem *)> collapseRec = [&](QTreeWidgetItem *node) {
                for (int i = 0; i < node->childCount(); ++i) {
                    collapseRec(node->child(i));
                }
                node->setExpanded(false);
            };
            collapseRec(item);
        });

        // Broadcast Message — walks all nested project groups, collects active
        // session IDs, and offers the broadcast dialog when at least one exists.
        {
            QStringList activeIds;
            std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *node) {
                for (int i = 0; i < node->childCount(); ++i) {
                    auto *child = node->child(i);
                    const QString id = child->data(0, Qt::UserRole).toString();
                    if (!id.isEmpty() && m_activeSessions.contains(id) && !m_activeSessions.value(id).isNull()) {
                        activeIds.append(id);
                    }
                    walk(child);
                }
            };
            walk(item);
            if (!activeIds.isEmpty()) {
                menu.addSeparator();
                QAction *broadcastAction = menu.addAction(QIcon::fromTheme(QStringLiteral("mail-send-symbolic"), QIcon::fromTheme(QStringLiteral("mail-send"))),
                                                          i18n("Broadcast Message... (%1)", activeIds.size()));
                broadcastAction->setObjectName(QStringLiteral("broadcastAction"));
                connect(broadcastAction, &QAction::triggered, this, [this, activeIds]() {
                    openBroadcastDialog(activeIds);
                });
            }
        }

        // Ungroup category — dissolve alias/override or add to suppress list.
        {
            const QString catKey = compositeKey.mid(QStringLiteral("category:").size());
            menu.addSeparator();
            QAction *ungroupAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")), i18n("Ungroup (Promote to Top Level)"));
            ungroupAction->setObjectName(QStringLiteral("ungroupCategoryAction"));
            connect(ungroupAction, &QAction::triggered, this, [this, catKey]() {
                ungroupCategory(catKey);
            });

            // Rename category — persists a CategoryAliases entry so any
            // reference to catKey (LCP-derived, user-created, or dropped)
            // reroutes into the new bucket everywhere.
            QAction *renameAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Rename Category…"));
            renameAction->setObjectName(QStringLiteral("renameCategoryAction"));
            connect(renameAction, &QAction::triggered, this, [this, catKey]() {
                renameCategory(catKey);
            });

            // Suggest Name — ask Claude for a short name based on this category's projects.
            QAction *suggestAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-find")), i18n("Suggest Name for This Category…"));
            suggestAction->setObjectName(QStringLiteral("suggestCategoryNameAction"));
            connect(suggestAction, &QAction::triggered, this, [this, catKey]() {
                QStringList workdirs;
                for (auto it = m_categoryMap.cbegin(); it != m_categoryMap.cend(); ++it) {
                    if (it.value() == catKey) {
                        workdirs.append(it.key());
                    }
                }
                suggestCategoryName(workdirs);
            });

            menu.addSeparator();
            QAction *reorgAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-tree")), i18n("Reorganize Tree with AI…"));
            reorgAction->setObjectName(QStringLiteral("reorganizeTreeAction"));
            connect(reorgAction, &QAction::triggered, this, &SessionManagerPanel::openReorganizeTreeDialog);
        }

        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    // Project-group items — expand/collapse plus bulk actions filtered by child state.
    if (compositeKey.startsWith(QStringLiteral("group:"))) {
        QMenu menu(this);
        QAction *expandAction = menu.addAction(i18n("Expand All"));
        connect(expandAction, &QAction::triggered, this, [item]() {
            item->setExpanded(true);
            for (int i = 0; i < item->childCount(); ++i) {
                item->child(i)->setExpanded(true);
            }
        });
        QAction *collapseAction = menu.addAction(i18n("Collapse All"));
        connect(collapseAction, &QAction::triggered, this, [item]() {
            item->setExpanded(false);
        });

        // Collect child session IDs bucketed by state token.
        // Group children are direct sessions in the new architecture.
        auto childIdsByState = [this](QTreeWidgetItem *group) -> QHash<QString, QStringList> {
            QHash<QString, QStringList> bucket;
            for (int i = 0; i < group->childCount(); ++i) {
                auto *child = group->child(i);
                const QString id = child->data(0, Qt::UserRole).toString();
                if (id.isEmpty()) {
                    continue;
                }
                const SessionMetadata *meta = sessionMetadata(id);
                if (!meta) {
                    // Discovered session item (no SessionMetadata) — tag from UserRole+5.
                    const QString tag = child->data(0, Qt::UserRole + 5).toString();
                    if (!tag.isEmpty()) {
                        bucket[tag].append(id);
                    }
                    continue;
                }
                const bool isLive = m_activeSessions.contains(meta->sessionId) || m_cachedLiveNames.contains(meta->sessionName)
                    || (meta->isRemote && m_cachedRemoteLiveNames.contains(meta->sessionName));
                bucket[stateTokenFor(*meta, isLive)].append(id);
            }
            return bucket;
        };

        const QHash<QString, QStringList> buckets = childIdsByState(item);
        const QStringList activeIds = buckets.value(QStringLiteral("active"));
        const QStringList detachedIds = buckets.value(QStringLiteral("detached"));
        const QStringList closedIds = buckets.value(QStringLiteral("closed"));
        const QStringList pinnedIds = buckets.value(QStringLiteral("pinned"));
        const QStringList archivedIds = buckets.value(QStringLiteral("archived"));
        const QStringList dismissedIds = buckets.value(QStringLiteral("dismissed"));

        // Restart all active sessions in this group
        if (!activeIds.isEmpty()) {
            menu.addSeparator();
            QAction *restartAllAction =
                menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Restart All Active in Group (%1)", activeIds.size()));
            restartAllAction->setToolTip(i18n("Restart all active Claude sessions in this group to pick up CLI/MCP/plugin updates"));
            QString groupLabel = item->text(0);
            connect(restartAllAction, &QAction::triggered, this, [this, activeIds, groupLabel]() {
                int ret = QMessageBox::question(this,
                                                i18n("Restart Sessions in Group"),
                                                i18n("Restart all %1 active Claude session(s) in \"%2\"?\n\n"
                                                     "Each session's conversation will be preserved (resumed by ID), but the Claude CLI process "
                                                     "will be replaced — picking up new CLI/MCP/plugin versions.",
                                                     activeIds.size(),
                                                     groupLabel),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
                if (ret != QMessageBox::Yes) {
                    return;
                }
                for (const QString &id : activeIds) {
                    QPointer<ClaudeSession> session = m_activeSessions.value(id);
                    if (session) {
                        session->restart();
                    }
                }
            });

            // Broadcast Message — open the broadcast dialog pre-populated with
            // this group's active sessions.
            QAction *broadcastAction = menu.addAction(QIcon::fromTheme(QStringLiteral("mail-send-symbolic"), QIcon::fromTheme(QStringLiteral("mail-send"))),
                                                      i18n("Broadcast Message... (%1)", activeIds.size()));
            broadcastAction->setObjectName(QStringLiteral("broadcastAction"));
            connect(broadcastAction, &QAction::triggered, this, [this, activeIds]() {
                openBroadcastDialog(activeIds);
            });
        }

        // Attach All Detached
        if (!detachedIds.isEmpty()) {
            menu.addSeparator();
            QAction *attachAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Attach All Detached (%1)", detachedIds.size()));
            connect(attachAll, &QAction::triggered, this, [this, detachedIds]() {
                int ret = QMessageBox::question(this,
                                                i18n("Attach All Detached"),
                                                i18n("Attach all %1 detached sessions in this group?", detachedIds.size()),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
                if (ret != QMessageBox::Yes) {
                    return;
                }
                for (const QString &id : detachedIds) {
                    auto *meta = findMetadata(id);
                    if (!meta) {
                        continue;
                    }
                    if (meta->isRemote) {
                        Q_EMIT remoteAttachRequested(meta->sshHost, meta->sshUsername, meta->sshPort, meta->workingDirectory, meta->sessionName);
                    } else {
                        Q_EMIT attachRequested(meta->sessionName);
                    }
                }
            });
        }

        // Close All Active
        if (!activeIds.isEmpty() || !detachedIds.isEmpty()) {
            QStringList closableIds = activeIds;
            closableIds.append(detachedIds);
            if (!closableIds.isEmpty()) {
                QAction *closeAll =
                    menu.addAction(QIcon::fromTheme(QStringLiteral("window-close")), i18n("Close All Active/Detached (%1)", closableIds.size()));
                connect(closeAll, &QAction::triggered, this, [this, closableIds]() {
                    int ret = QMessageBox::question(this,
                                                    i18n("Close All"),
                                                    i18n("Close all %1 sessions in this group? This kills their tmux backends.", closableIds.size()),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                    if (ret != QMessageBox::Yes) {
                        return;
                    }
                    for (const QString &id : closableIds) {
                        closeSession(id);
                    }
                });
            }
        }

        // Archive everything that can be archived (active/detached/closed/pinned)
        QStringList archivableIds = activeIds;
        archivableIds.append(detachedIds);
        archivableIds.append(closedIds);
        archivableIds.append(pinnedIds);
        if (!archivableIds.isEmpty()) {
            menu.addSeparator();
            QAction *archiveAll = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-insert")), i18n("Archive All in Group (%1)", archivableIds.size()));
            connect(archiveAll, &QAction::triggered, this, [this, archivableIds]() {
                int ret = QMessageBox::question(this,
                                                i18n("Archive All"),
                                                i18n("Archive all %1 sessions in this group?", archivableIds.size()),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
                if (ret != QMessageBox::Yes) {
                    return;
                }
                for (const QString &id : archivableIds) {
                    archiveSession(id);
                }
            });
        }

        // Dismiss All Archived
        if (!archivedIds.isEmpty()) {
            QAction *dismissAll = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Dismiss All Archived (%1)", archivedIds.size()));
            connect(dismissAll, &QAction::triggered, this, [this, archivedIds]() {
                int ret = QMessageBox::question(this,
                                                i18n("Dismiss All Archived"),
                                                i18n("Dismiss all %1 archived sessions in this group? They can be restored later.", archivedIds.size()),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
                if (ret != QMessageBox::Yes) {
                    return;
                }
                for (const QString &id : archivedIds) {
                    dismissSession(id);
                }
            });
        }

        // Purge All Dismissed (in this group)
        if (!dismissedIds.isEmpty()) {
            QAction *purgeAll = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete-remove")), i18n("Purge All Dismissed (%1)", dismissedIds.size()));
            connect(purgeAll, &QAction::triggered, this, [this, dismissedIds]() {
                int ret = QMessageBox::question(
                    this,
                    i18n("Purge All Dismissed"),
                    i18n("Permanently delete metadata for %1 dismissed sessions in this group? This cannot be undone.", dismissedIds.size()),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (ret != QMessageBox::Yes) {
                    return;
                }
                for (const QString &id : dismissedIds) {
                    purgeSession(id);
                }
            });
        }

        // Consolidate Duplicates — surface when the underlying project has
        // ≥2 known sessions (any state). Reuses MergeSessionsDialog machinery.
        const QString projectKey = compositeKey.mid(QStringLiteral("group:").size());
        if (canOfferConsolidateForProject(projectKey)) {
            int projSessionCount = 0;
            for (auto it = m_metadata.cbegin(); it != m_metadata.cend(); ++it) {
                if (it.value().workingDirectory == projectKey) {
                    ++projSessionCount;
                }
            }
            menu.addSeparator();
            QAction *consolidateAction =
                menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Consolidate Duplicates... (%1 sessions)", projSessionCount));
            consolidateAction->setObjectName(QStringLiteral("consolidateDuplicatesAction"));
            connect(consolidateAction, &QAction::triggered, this, [this, projectKey]() {
                openConsolidateDialog(projectKey);
            });
        }

        // Reset auto-grouping — if this workdir's LCP category is suppressed,
        // offer to un-suppress. Only visible when the suppression actually
        // applies to this workdir (the workdir has an LCP-derived category
        // in the SuppressCategories list). This is the reverse of the
        // "Ungroup" action on a category node.
        {
            auto *settings = KonsolaiSettings::instance();
            if (settings) {
                const QStringList suppressList = settings->suppressedCategories();
                if (!suppressList.isEmpty()) {
                    // Rebuild the raw LCP map (ignoring user rules) to discover
                    // this workdir's native category. If it's currently in the
                    // suppress list, offer the reset action.
                    QSet<QString> allWorkdirs;
                    for (auto it = m_metadata.cbegin(); it != m_metadata.cend(); ++it) {
                        const QString &wd = it.value().workingDirectory;
                        if (!wd.isEmpty()) {
                            allWorkdirs.insert(wd);
                        }
                    }
                    const QHash<QString, QString> rawMap = buildCategoryMap(QList<QString>(allWorkdirs.cbegin(), allWorkdirs.cend()));
                    const QString nativeCat = rawMap.value(projectKey);
                    if (!nativeCat.isEmpty() && suppressList.contains(nativeCat)) {
                        menu.addSeparator();
                        QAction *resetAction =
                            menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reset auto-grouping (regroup under \"%1\")", nativeCat));
                        resetAction->setObjectName(QStringLiteral("resetAutoGroupingAction"));
                        connect(resetAction, &QAction::triggered, this, [this, nativeCat]() {
                            auto *s = KonsolaiSettings::instance();
                            if (s) {
                                s->removeSuppressedCategory(nativeCat);
                                scheduleTreeUpdate();
                            }
                        });
                    }
                }
            }
        }

        // Suggest Name — walk selected group items (or fall back to just this one)
        // and ask Claude for a short category name. Also renders as "Selected"
        // when the user has multi-selected groups.
        {
            const QList<QTreeWidgetItem *> selectedGroupItems = [this, item]() {
                QList<QTreeWidgetItem *> out;
                const auto sel = m_treeWidget->selectedItems();
                for (auto *s : sel) {
                    if (s->data(0, Qt::UserRole + 6).toString().startsWith(QStringLiteral("group:"))) {
                        out.append(s);
                    }
                }
                if (out.isEmpty()) {
                    out.append(item);
                }
                return out;
            }();

            QStringList workdirs;
            for (auto *g : selectedGroupItems) {
                const QString ck = g->data(0, Qt::UserRole + 6).toString();
                if (ck.startsWith(QStringLiteral("group:"))) {
                    workdirs.append(ck.mid(QStringLiteral("group:").size()));
                }
            }
            if (!workdirs.isEmpty()) {
                menu.addSeparator();
                const QString label =
                    selectedGroupItems.size() > 1 ? i18n("Suggest Name for Selected… (%1)", selectedGroupItems.size()) : i18n("Suggest Name for This Project…");
                QAction *suggestAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-find")), label);
                suggestAction->setObjectName(QStringLiteral("suggestCategoryNameAction"));
                connect(suggestAction, &QAction::triggered, this, [this, workdirs]() {
                    suggestCategoryName(workdirs);
                });
            }
        }

        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    // Handle items deeper than direct children of categories (subagents, subprocesses, task/prompt groups)
    // Skip session items that happen to be under a group (depth 2 but still sessions):
    // sessions have UserRole + 6 = "s:..." — let them fall through to the session menu below.
    QTreeWidgetItem *parentItem = item->parent();
    QString compositeKeyForItem = item->data(0, Qt::UserRole + 6).toString();
    bool isSessionItem = compositeKeyForItem.startsWith(QStringLiteral("s:"));
    if (!isSessionItem && parentItem && parentItem->parent() != nullptr) {
        // Check if this is a prompt group item (UserRole + 3)
        QVariant promptGroupVar = item->data(0, Qt::UserRole + 3);
        if (promptGroupVar.isValid() && !promptGroupVar.isNull()) {
            QMenu menu(this);
            QAction *expandAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-tree")), i18n("Expand All"));
            connect(expandAll, &QAction::triggered, this, [item]() {
                item->setExpanded(true);
                for (int i = 0; i < item->childCount(); ++i) {
                    item->child(i)->setExpanded(true);
                }
            });
            QAction *collapseAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-text")), i18n("Collapse"));
            connect(collapseAll, &QAction::triggered, this, [item]() {
                item->setExpanded(false);
            });
            menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
            return;
        }

        // Check if this is a task group item (UserRole + 2)
        QString taskGroupDesc = item->data(0, Qt::UserRole + 2).toString();
        if (!taskGroupDesc.isEmpty()) {
            QMenu menu(this);
            QAction *expandAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-tree")), i18n("Expand All"));
            connect(expandAll, &QAction::triggered, this, [item]() {
                item->setExpanded(true);
                for (int i = 0; i < item->childCount(); ++i) {
                    item->child(i)->setExpanded(true);
                }
            });
            QAction *collapseAll = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-text")), i18n("Collapse"));
            connect(collapseAll, &QAction::triggered, this, [item]() {
                item->setExpanded(false);
            });
            menu.addSeparator();
            QAction *copyDesc = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy Task Description"));
            connect(copyDesc, &QAction::triggered, this, [taskGroupDesc]() {
                QApplication::clipboard()->setText(taskGroupDesc);
            });
            menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
            return;
        }

        // Check if this is a subprocess item (UserRole + 4)
        QString subprocessId = item->data(0, Qt::UserRole + 4).toString();
        if (!subprocessId.isEmpty()) {
            QString parentSessionId = item->data(0, Qt::UserRole + 1).toString();
            if (!m_activeSessions.contains(parentSessionId)) {
                return;
            }
            QPointer<ClaudeSession> session = m_activeSessions[parentSessionId];
            if (!session) {
                return;
            }
            const auto &procs = session->subprocesses();
            if (!procs.contains(subprocessId)) {
                return;
            }
            const auto &procInfo = procs[subprocessId];

            QMenu menu(this);

            QAction *viewOutput = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("View Output"));
            viewOutput->setEnabled(!procInfo.output.isEmpty() || procInfo.status != SubprocessInfo::Running);
            connect(viewOutput, &QAction::triggered, this, [this, procInfo]() {
                showSubprocessOutput(procInfo);
            });

            if (procInfo.status == SubprocessInfo::Running) {
                menu.addSeparator();
                QAction *killAction = menu.addAction(QIcon::fromTheme(QStringLiteral("process-stop")), i18n("Kill (SIGTERM)"));
                connect(killAction, &QAction::triggered, this, [session, subprocessId]() {
                    if (session) {
                        session->killSubprocess(subprocessId, 15); // SIGTERM
                    }
                });
                QAction *forceKillAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Force Kill (SIGKILL)"));
                connect(forceKillAction, &QAction::triggered, this, [session, subprocessId]() {
                    if (session) {
                        session->killSubprocess(subprocessId, 9); // SIGKILL
                    }
                });
            }

            menu.addSeparator();
            QAction *copyCmd = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy Command"));
            connect(copyCmd, &QAction::triggered, this, [procInfo]() {
                QApplication::clipboard()->setText(procInfo.fullCommand);
            });

            menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
            return;
        }

        // Subagent child item
        QString agentId = item->data(0, Qt::UserRole).toString();
        QString parentSessionId = item->data(0, Qt::UserRole + 1).toString();
        if (agentId.isEmpty() || !m_activeSessions.contains(parentSessionId)) {
            return;
        }
        ClaudeSession *session = m_activeSessions[parentSessionId];
        if (!session) {
            return;
        }
        const auto &agents = session->subagents();
        if (!agents.contains(agentId)) {
            return;
        }
        const auto &info = agents[agentId];

        QMenu menu(this);

        bool hasTranscript = !info.transcriptPath.isEmpty() && QFile::exists(info.transcriptPath);

        QAction *viewTranscript = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("View Transcript"));
        viewTranscript->setEnabled(hasTranscript);
        connect(viewTranscript, &QAction::triggered, this, [this, info]() {
            showSubagentTranscript(info);
        });

        QAction *openExternal = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open Transcript in Editor"));
        openExternal->setEnabled(hasTranscript);
        connect(openExternal, &QAction::triggered, this, [info]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.transcriptPath));
        });

        menu.addSeparator();

        QAction *copyId = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy Agent ID"));
        connect(copyId, &QAction::triggered, this, [info]() {
            QApplication::clipboard()->setText(info.agentId);
        });

        QAction *details = menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), i18n("Show Details"));
        connect(details, &QAction::triggered, this, [this, info]() {
            showSubagentDetails(info);
        });

        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    // (Category-header right-click branches removed — bulk actions live on the
    // project-group context menu now, filtered by child state.)

    QString sessionId = item->data(0, Qt::UserRole).toString();
    if (sessionId.isEmpty()) {
        return;
    }

    // Handle discovered sessions (tagged with state token at UserRole+5).
    if (item->data(0, Qt::UserRole + 5).toString() == QLatin1String("discovered")) {
        QString workDir = item->data(0, Qt::UserRole + 1).toString();
        if (workDir.isEmpty()) {
            return;
        }

        QString remoteHost = item->data(0, Qt::UserRole + 2).toString();
        bool isRemoteItem = !remoteHost.isEmpty();

        QMenu menu(this);

        // Resume Conversation action (for items with conversations)
        if (!isRemoteItem) {
            if (!m_conversationCache.contains(workDir)) {
                m_conversationCache.insert(workDir, ClaudeSessionRegistry::readClaudeConversations(workDir));
            }
            const auto &conversations = m_conversationCache[workDir];
            if (!conversations.isEmpty()) {
                QAction *resumeAction = menu.addAction(
                    QIcon::fromTheme(QStringLiteral("media-playback-start")),
                    i18n("Resume Conversation (%1)...", conversations.size()));
                connect(resumeAction, &QAction::triggered, this, [this, workDir, conversations]() {
                    QString id = ClaudeConversationPicker::pick(conversations, this);
                    if (!id.isEmpty()) {
                        Q_EMIT resumeConversationRequested(workDir, id, QString(), QString(), 22);
                    }
                });
                menu.addSeparator();
            }
        }

        QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Open New Session Here"));
        connect(openAction, &QAction::triggered, this, [this, sessionId, workDir, isRemoteItem, item]() {
            if (isRemoteItem) {
                QString host = item->data(0, Qt::UserRole + 2).toString();
                QString user = item->data(0, Qt::UserRole + 3).toString();
                int port = item->data(0, Qt::UserRole + 4).toInt();
                if (port <= 0) {
                    port = 22;
                }
                Q_EMIT remoteSessionRequested(host, user, port, workDir);
            } else {
                Q_EMIT unarchiveRequested(sessionId, workDir, false, QString(), QString(), 22);
            }
        });

        menu.addSeparator();

        QAction *trashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), i18n("Move to Trash..."));
        connect(trashAction, &QAction::triggered, this, [this, workDir]() {
            auto answer = QMessageBox::question(this,
                                                i18n("Move to Trash"),
                                                i18n("Move this project folder to the trash?\n\n%1", workDir),
                                                QMessageBox::Yes | QMessageBox::No,
                                                QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                QFile dir(workDir);
                if (dir.moveToTrash()) {
                    updateTreeWidget();
                } else {
                    QMessageBox::warning(this, i18n("Trash Failed"), i18n("Could not move folder to trash:\n%1", workDir));
                }
            }
        });

        menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
        return;
    }

    if (!m_metadata.contains(sessionId)) {
        return;
    }

    const auto &meta = m_metadata[sessionId];

    QMenu menu(this);

    if (meta.isDismissed) {
        // Collect all selected dismissed session IDs for batch operations.
        // Any selected item whose metadata is dismissed qualifies, regardless of group.
        QStringList selectedDismissed;
        const auto selectedItemsDismissed = m_treeWidget->selectedItems();
        for (auto *sel : selectedItemsDismissed) {
            QString sid = sel->data(0, Qt::UserRole).toString();
            if (!sid.isEmpty() && m_metadata.contains(sid) && m_metadata[sid].isDismissed) {
                selectedDismissed.append(sid);
            }
        }
        if (!selectedDismissed.contains(sessionId)) {
            selectedDismissed.append(sessionId);
        }

        int selectedCount = selectedDismissed.size();

        if (selectedCount > 1) {
            // Multi-selection batch menu
            QAction *restoreAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Restore Selected (%1)", selectedCount));
            connect(restoreAction, &QAction::triggered, this, [this, selectedDismissed]() {
                for (const auto &sid : selectedDismissed) {
                    restoreSession(sid);
                }
            });

            menu.addSeparator();

            QAction *purgeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Purge Selected (%1)", selectedCount));
            connect(purgeAction, &QAction::triggered, this, [this, selectedDismissed, selectedCount]() {
                auto answer = QMessageBox::question(this,
                                                    i18n("Purge Sessions"),
                                                    i18n("Permanently remove metadata for %1 dismissed session(s)?\n\n"
                                                         "Project folders will NOT be affected.",
                                                         selectedCount),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                if (answer == QMessageBox::Yes) {
                    for (const auto &sid : selectedDismissed) {
                        purgeSession(sid);
                    }
                }
            });
        } else {
            // Single item menu
            // Unmerge entry — only when this dismissed session was merged into another.
            if (canOfferUnmergeForSession(sessionId)) {
                QString primaryLabel;
                if (m_metadata.contains(meta.mergedInto)) {
                    const auto &primaryMeta = m_metadata[meta.mergedInto];
                    primaryLabel = !primaryMeta.description.trimmed().isEmpty() ? primaryMeta.description.trimmed()
                        : !primaryMeta.workingDirectory.isEmpty()               ? QDir(primaryMeta.workingDirectory).dirName()
                                                                                : primaryMeta.sessionName;
                } else {
                    primaryLabel = meta.mergedInto;
                }
                QAction *unmergeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Unmerge (was merged into %1)", primaryLabel));
                connect(unmergeAction, &QAction::triggered, this, [this, sessionId]() {
                    unmergeSession(sessionId);
                });
                menu.addSeparator();
            }

            QAction *restoreAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Restore"));
            connect(restoreAction, &QAction::triggered, this, [this, sessionId]() {
                restoreSession(sessionId);
            });

            menu.addSeparator();

            QAction *purgeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Purge"));
            connect(purgeAction, &QAction::triggered, this, [this, sessionId]() {
                auto answer = QMessageBox::question(this,
                                                    i18n("Purge Session"),
                                                    i18n("Permanently remove this session's metadata?\n\n"
                                                         "Project folder will NOT be affected."),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                if (answer == QMessageBox::Yes) {
                    purgeSession(sessionId);
                }
            });
        }
    } else if (meta.isArchived) {
        // Collect all selected archived session IDs for batch operations
        QStringList selectedArchived;
        const auto selectedItems = m_treeWidget->selectedItems();
        for (auto *sel : selectedItems) {
            QString sid = sel->data(0, Qt::UserRole).toString();
            if (!sid.isEmpty() && m_metadata.contains(sid) && m_metadata[sid].isArchived) {
                selectedArchived.append(sid);
            }
        }
        // Ensure the right-clicked item is included
        if (!selectedArchived.contains(sessionId)) {
            selectedArchived.append(sessionId);
        }

        int selectedCount = selectedArchived.size();

        if (selectedCount > 1) {
            // Multi-selection batch menu
            QAction *unarchiveAction =
                menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")), i18n("Unarchive && Start Selected (%1)", selectedCount));
            connect(unarchiveAction, &QAction::triggered, this, [this, selectedArchived]() {
                for (const auto &sid : selectedArchived) {
                    unarchiveSession(sid);
                }
            });

            menu.addSeparator();

            QAction *dismissAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Dismiss Selected (%1)", selectedCount));
            connect(dismissAction, &QAction::triggered, this, [this, selectedArchived, selectedCount]() {
                auto answer = QMessageBox::question(this,
                                                    i18n("Dismiss Sessions"),
                                                    i18n("Dismiss %1 archived session(s)?", selectedCount),
                                                    QMessageBox::Yes | QMessageBox::No,
                                                    QMessageBox::No);
                if (answer == QMessageBox::Yes) {
                    for (const auto &sid : selectedArchived) {
                        dismissSession(sid);
                    }
                }
            });
        } else {
            // Single item menu
            QAction *unarchiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")), i18n("Unarchive && Start"));
            connect(unarchiveAction, &QAction::triggered, this, [this, sessionId]() {
                unarchiveSession(sessionId);
            });

            menu.addSeparator();

            QAction *dismissAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Dismiss"));
            connect(dismissAction, &QAction::triggered, this, [this, sessionId]() {
                dismissSession(sessionId);
            });

            if (!meta.workingDirectory.isEmpty() && QDir(meta.workingDirectory).exists()) {
                QAction *trashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), i18n("Move to Trash..."));
                connect(trashAction, &QAction::triggered, this, [this, sessionId, meta]() {
                    auto answer = QMessageBox::question(this,
                                                        i18n("Move to Trash"),
                                                        i18n("Move this project folder to the trash?\n\n%1", meta.workingDirectory),
                                                        QMessageBox::Yes | QMessageBox::No,
                                                        QMessageBox::No);
                    if (answer == QMessageBox::Yes) {
                        QFile dir(meta.workingDirectory);
                        if (dir.moveToTrash()) {
                            m_metadata.remove(sessionId);
                            scheduleMetadataSave();
                            updateTreeWidget();
                        } else {
                            QMessageBox::warning(this, i18n("Trash Failed"), i18n("Could not move folder to trash:\n%1", meta.workingDirectory));
                        }
                    }
                });
            }
        }
    } else {
        // Active, detached, or closed session
        bool isActive = m_activeSessions.contains(sessionId);
        const bool isLive = isActive || m_cachedLiveNames.contains(meta.sessionName) || (meta.isRemote && m_cachedRemoteLiveNames.contains(meta.sessionName));
        const bool isClosed = stateTokenFor(meta, isLive) == QLatin1String("closed");
        QPointer<ClaudeSession> activeSession = isActive ? m_activeSessions[sessionId] : nullptr;

        if (isActive && activeSession) {
            QAction *focusAction = menu.addAction(QIcon::fromTheme(QStringLiteral("go-jump")), i18n("Focus Tab"));
            connect(focusAction, &QAction::triggered, this, [this, activeSession]() {
                if (activeSession) {
                    Q_EMIT focusSessionRequested(activeSession);
                }
            });
        } else if (isClosed) {
            // Closed session — tmux is dead, offer restart (fresh tmux, optionally resume conversation)
            QAction *restartAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Restart Session"));
            connect(restartAction, &QAction::triggered, this, [this, sessionId]() {
                unarchiveSession(sessionId);
            });
        } else if (!isActive) {
            if (meta.isRemote) {
                // Remote detached session — reattach via SSH
                QAction *attachAction = menu.addAction(QIcon::fromTheme(QStringLiteral("network-connect")), i18n("Attach (SSH)"));
                connect(attachAction, &QAction::triggered, this, [this, meta]() {
                    Q_EMIT remoteAttachRequested(meta.sshHost, meta.sshUsername, meta.sshPort, meta.workingDirectory, meta.sessionName);
                });
            } else {
                QAction *attachAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Attach"));
                connect(attachAction, &QAction::triggered, this, [this, meta]() {
                    Q_EMIT attachRequested(meta.sessionName);
                });
            }
        }

        menu.addSeparator();

        if (meta.isPinned) {
            QAction *unpinAction = menu.addAction(QIcon::fromTheme(QStringLiteral("window-unpin")), i18n("Unpin"));
            connect(unpinAction, &QAction::triggered, this, [this, sessionId]() {
                unpinSession(sessionId);
            });
        } else {
            QAction *pinAction = menu.addAction(QIcon::fromTheme(QStringLiteral("pin")), i18n("Pin to Top"));
            connect(pinAction, &QAction::triggered, this, [this, sessionId]() {
                pinSession(sessionId);
            });
        }

        // Set/edit task description
        QAction *descAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Set Description..."));
        connect(descAction, &QAction::triggered, this, [this, sessionId]() {
            editSessionDescription(sessionId);
        });

        // View Session Activity — show parsed conversation transcript
        // Defer the expensive .jsonl lookup to when user actually clicks the action
        if (!meta.workingDirectory.isEmpty()) {
            QString convId = meta.lastResumeSessionId;
            if (convId.isEmpty() && isActive && activeSession) {
                convId = activeSession->resumeSessionId();
            }
            QAction *activityAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-text")), i18n("View Session Activity"));
            connect(activityAction, &QAction::triggered, this, [this, convId, meta]() {
                // Use cached conversations to avoid disk I/O
                if (!m_conversationCache.contains(meta.workingDirectory)) {
                    m_conversationCache.insert(meta.workingDirectory, ClaudeSessionRegistry::readClaudeConversations(meta.workingDirectory));
                }
                const auto &conversations = m_conversationCache[meta.workingDirectory];

                // Find the .jsonl path for this conversation
                auto findJsonlPath = [](const QString &targetId) -> QString {
                    if (targetId.isEmpty())
                        return {};
                    QString projectsDir = QDir::homePath() + QStringLiteral("/.claude/projects");
                    QDirIterator it(projectsDir, QDir::Dirs | QDir::NoDotAndDotDot);
                    while (it.hasNext()) {
                        QString dir = it.next();
                        QString candidate = dir + QStringLiteral("/") + targetId + QStringLiteral(".jsonl");
                        if (QFile::exists(candidate)) {
                            return candidate;
                        }
                    }
                    return {};
                };

                QString jsonlPath;
                if (!convId.isEmpty()) {
                    for (const auto &conv : conversations) {
                        if (conv.sessionId == convId) {
                            jsonlPath = findJsonlPath(convId);
                            break;
                        }
                    }
                }
                // Fallback: most recent conversation
                if (jsonlPath.isEmpty() && !conversations.isEmpty()) {
                    jsonlPath = findJsonlPath(conversations.first().sessionId);
                }
                if (!jsonlPath.isEmpty()) {
                    showSessionActivity(jsonlPath, meta.workingDirectory);
                }
            });
        }

        // Show session structure (subagent/subprocess tree) — only when there's something to show
        bool hasStructure = false;
        if (isActive && activeSession) {
            hasStructure = !activeSession->subagents().isEmpty() || !activeSession->subprocesses().isEmpty();
        }
        if (!hasStructure && m_metadata.contains(sessionId)) {
            const auto &m = m_metadata[sessionId];
            hasStructure = !m.subagents.isEmpty() || !m.subprocesses.isEmpty();
        }
        if (hasStructure) {
            QAction *structureAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-tree")), i18n("Show Session Structure..."));
            connect(structureAction, &QAction::triggered, this, [this, sessionId]() {
                showSessionStructure(sessionId);
            });
        }

        // Show approval log for active sessions with approvals
        if (isActive && activeSession && activeSession->totalApprovalCount() > 0) {
            QAction *logAction =
                menu.addAction(QIcon::fromTheme(QStringLiteral("view-list-details")), i18n("View Approval Log (%1)", activeSession->totalApprovalCount()));
            connect(logAction, &QAction::triggered, this, [this, activeSession]() {
                if (activeSession) {
                    showApprovalLog(activeSession);
                }
            });
        }

        // Yolo mode toggles for active sessions
        if (isActive && activeSession) {
            menu.addSeparator();

            QAction *yoloAction = menu.addAction(i18n("Yolo Mode"));
            yoloAction->setCheckable(true);
            yoloAction->setChecked(activeSession->yoloMode());
            connect(yoloAction, &QAction::triggered, this, [this, activeSession](bool checked) {
                if (activeSession) {
                    activeSession->setYoloMode(checked);
                    if (checked) {
                        ensureHooksConfigured(activeSession);
                    }
                }
            });

            QAction *doubleYoloAction = menu.addAction(i18n("Double Yolo"));
            doubleYoloAction->setCheckable(true);
            doubleYoloAction->setChecked(activeSession->doubleYoloMode());
            connect(doubleYoloAction, &QAction::triggered, this, [this, activeSession](bool checked) {
                if (activeSession) {
                    activeSession->setDoubleYoloMode(checked);
                    if (checked) {
                        ensureHooksConfigured(activeSession);
                    }
                }
            });

            // Reset Hooks — force-clear all hooks and reset yolo state
            QAction *resetHooksAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear")), i18n("Reset Hooks"));
            connect(resetHooksAction, &QAction::triggered, this, [this, sessionId, activeSession]() {
                auto *meta = findMetadata(sessionId);
                if (!meta) {
                    return;
                }

                // 1. Clear all hooks from settings file
                ClaudeSession::removeHooksForWorkDir(meta->workingDirectory);

                // 2. Reset yolo state if session is active
                if (activeSession) {
                    activeSession->setYoloMode(false);
                    activeSession->setDoubleYoloMode(false);
                }

                // 3. Re-install fresh hooks if session is still active
                if (activeSession && m_activeSessions.contains(sessionId)) {
                    ensureHooksConfigured(activeSession);
                }

                qDebug() << "SessionManagerPanel: Reset hooks for session" << sessionId;
            });
        }

        // Edit budget controls for active sessions
        if (isActive && activeSession) {
            QAction *budgetAction = menu.addAction(QIcon::fromTheme(QStringLiteral("budget")), i18n("Edit Budget..."));
            connect(budgetAction, &QAction::triggered, this, [this, activeSession, sessionId]() {
                if (activeSession) {
                    editSessionBudget(activeSession, sessionId);
                }
            });
        }

        // Toggle completed agents visibility for sessions with subagents
        if (isActive && activeSession && !activeSession->subagents().isEmpty()) {
            bool hiding = m_hideCompletedAgents.contains(sessionId);
            QAction *toggleAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-visible")), i18n("Show Completed Agents"));
            toggleAction->setCheckable(true);
            toggleAction->setChecked(!hiding);
            connect(toggleAction, &QAction::triggered, this, [this, sessionId](bool checked) {
                if (checked) {
                    m_hideCompletedAgents.remove(sessionId);
                } else {
                    m_hideCompletedAgents.insert(sessionId);
                }
                scheduleTreeUpdate();
            });
        }

        // Mute auto-expand toggle for sessions with subagents
        if (isActive && activeSession && !activeSession->subagents().isEmpty()) {
            bool muted = m_mutedSessions.contains(sessionId);
            QAction *muteAction = menu.addAction(QIcon::fromTheme(muted ? QStringLiteral("audio-volume-high") : QStringLiteral("audio-volume-muted")),
                                                 muted ? i18n("Unmute Auto-Expand") : i18n("Mute Auto-Expand"));
            muteAction->setCheckable(true);
            muteAction->setChecked(muted);
            connect(muteAction, &QAction::triggered, this, [this, sessionId](bool checked) {
                if (checked) {
                    m_mutedSessions.insert(sessionId);
                    // Immediately collapse the session in the tree
                    if (QTreeWidgetItem *sessionItem = findTreeItem(sessionId)) {
                        sessionItem->setExpanded(false);
                    }
                } else {
                    m_mutedSessions.remove(sessionId);
                }
                scheduleTreeUpdate();
            });
        }

        // Restart option for active sessions
        if (isActive && activeSession) {
            QAction *restartAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Restart Claude"));
            connect(restartAction, &QAction::triggered, this, [activeSession]() {
                if (activeSession) {
                    activeSession->restart();
                }
            });
        }

        // Create worktree session from this session's project
        if (!meta.workingDirectory.isEmpty()) {
            QAction *worktreeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-branch")), i18n("New Worktree Session..."));
            connect(worktreeAction, &QAction::triggered, this, [this, meta]() {
                Q_EMIT worktreeSessionRequested(meta.workingDirectory);
            });
        }

        // Show Agent — navigate to agent panel for agent-originated sessions
        if (!meta.agentId.isEmpty()) {
            QAction *showAgentAction = menu.addAction(QIcon::fromTheme(QStringLiteral("system-run")), i18n("Show Agent"));
            QString agentIdCopy = meta.agentId;
            connect(showAgentAction, &QAction::triggered, this, [this, agentIdCopy]() {
                Q_EMIT showAgentRequested(agentIdCopy);
            });
        }

        menu.addSeparator();

        QAction *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("process-stop")), i18n("Close"));
        connect(closeAction, &QAction::triggered, this, [this, sessionId]() {
            closeSession(sessionId);
        });

        QAction *archiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive"));
        connect(archiveAction, &QAction::triggered, this, [this, sessionId]() {
            archiveSession(sessionId);
        });

        if (!isActive) {
            QAction *dismissAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Dismiss"));
            connect(dismissAction, &QAction::triggered, this, [this, sessionId]() {
                dismissSession(sessionId);
            });
        }

        // Merge Selected (N)... — only when ≥2 non-dismissed session items are
        // selected and they all share the same workingDirectory. Cross-project
        // merges are out of scope for v1.
        {
            const auto selectedItems = m_treeWidget->selectedItems();
            QStringList selectedSessionIds;
            for (auto *sel : selectedItems) {
                const QString sid = sel->data(0, Qt::UserRole).toString();
                if (sid.isEmpty() || !m_metadata.contains(sid)) {
                    continue;
                }
                if (m_metadata[sid].isDismissed) {
                    continue;
                }
                selectedSessionIds.append(sid);
            }
            if (canOfferMergeForSelection(selectedSessionIds) && selectedSessionIds.contains(sessionId)) {
                menu.addSeparator();
                QAction *mergeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("merge")), i18n("Merge Selected (%1)...", selectedSessionIds.size()));
                connect(mergeAction, &QAction::triggered, this, [this, selectedSessionIds]() {
                    QList<SessionMetadata> candidates;
                    QStringList resumeIds;
                    QString workdir;
                    for (const QString &sid : selectedSessionIds) {
                        if (!m_metadata.contains(sid)) {
                            continue;
                        }
                        const auto &m = m_metadata[sid];
                        candidates.append(m);
                        resumeIds.append(m.lastResumeSessionId);
                        if (workdir.isEmpty()) {
                            workdir = m.workingDirectory;
                        }
                    }
                    const QHash<QString, qint64> jsonlSizes = jsonlSizesForResumeIds(workdir, resumeIds);
                    MergeSessionsDialog dlg(candidates, jsonlSizes, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        mergeSessions(selectedSessionIds, dlg.primarySessionId(), dlg.choices());
                    }
                });
            }
        }
    }

    menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
}

void SessionManagerPanel::onNewSessionClicked()
{
    Q_EMIT newSessionRequested();
}

void SessionManagerPanel::scheduleTreeUpdate()
{
    if (!m_initialized) {
        return;
    }

    // When window is inactive, skip tree rebuilds entirely — nobody is looking.
    // resumeBackgroundTimers() will trigger one rebuild when the window reactivates.
    if (m_timersPaused) {
        m_pendingUpdate = true;
        return;
    }

    // Debounce: coalesce rapid-fire signals (e.g. approvalCountChanged fires
    // many times per minute during yolo mode) into a single deferred update.
    if (!m_updateDebounce) {
        m_updateDebounce = new QTimer(this);
        m_updateDebounce->setSingleShot(true);
        connect(m_updateDebounce, &QTimer::timeout, this, &SessionManagerPanel::updateTreeWidget);
    }

    // Defer rebuild while user is interacting with the tree (hover or focus)
    if (isTreeInteractionActive()) {
        m_pendingUpdate = true;
        if (!m_deferRetryTimer) {
            m_deferRetryTimer = new QTimer(this);
            m_deferRetryTimer->setSingleShot(true);
            connect(m_deferRetryTimer, &QTimer::timeout, this, [this]() {
                if (!m_pendingUpdate) {
                    return;
                }
                if (isTreeInteractionActive()) {
                    m_deferRetryTimer->start(1000); // Still interacting, retry
                } else {
                    m_pendingUpdate = false;
                    m_updateDebounce->start(100); // Short debounce for deferred flush
                }
            });
        }
        if (!m_deferRetryTimer->isActive()) {
            m_deferRetryTimer->start(1000);
        }
        return;
    }

    m_pendingUpdate = false;
    // Restart the 500ms window on each call — only the last one fires.
    m_updateDebounce->start(500);
}

void SessionManagerPanel::updateDurationLabels()
{
    if (!m_treeWidget) {
        return;
    }

    // Walk tree items and update only the duration QLabel widgets in column 1,
    // avoiding a full teardown/rebuild just to refresh elapsed time strings.
    // Also check if any active items remain — stop timer if not.
    bool anyActive = false;

    // Helper: update labels for child items of a given tree item
    std::function<void(QTreeWidgetItem *)> walkChildren = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            auto *child = parent->child(i);

            // Check if this item has a duration label widget in column 1
            auto *widget = m_treeWidget->itemWidget(child, 1);
            auto *label = qobject_cast<QLabel *>(widget);
            if (label) {
                // Look up the session and find the matching subagent/subprocess by ID
                QString parentSessionId = child->data(0, Qt::UserRole + 1).toString();
                ClaudeSession *session = m_activeSessions.value(parentSessionId);
                if (session) {
                    // Try as subagent (agentId in UserRole)
                    QString agentId = child->data(0, Qt::UserRole).toString();
                    if (!agentId.isEmpty() && session->subagents().contains(agentId)) {
                        const auto &info = session->subagents()[agentId];
                        QString elapsed = formatElapsed(info.startedAt);
                        if (label->text() != elapsed) {
                            label->setText(elapsed);
                        }
                        if (info.state == ClaudeProcess::State::Working || info.state == ClaudeProcess::State::Idle) {
                            anyActive = true;
                        }
                    }
                    // Try as subprocess (id in UserRole + 4)
                    QString procId = child->data(0, Qt::UserRole + 4).toString();
                    if (!procId.isEmpty() && session->subprocesses().contains(procId)) {
                        const auto &info = session->subprocesses()[procId];
                        QString col1;
                        QString elapsed = formatElapsed(info.startedAt);
                        if (!elapsed.isEmpty()) {
                            col1 = elapsed;
                        }
                        if (info.resourceUsage.rssBytes > 0 || info.resourceUsage.cpuPercent > 0.0) {
                            if (!col1.isEmpty())
                                col1 += QStringLiteral(" ");
                            col1 += info.resourceUsage.formatCompact();
                        }
                        if (label->text() != col1) {
                            label->setText(col1);
                        }
                        if (info.status == SubprocessInfo::Running) {
                            anyActive = true;
                        }
                    }
                }
            }

            // Recurse into children (prompt groups, subtasks containers, task groups)
            if (child->childCount() > 0) {
                walkChildren(child);
            }
        }
    };

    // Walk all top-level project groups; each contains session items as children.
    for (int g = 0; g < m_treeWidget->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_treeWidget->topLevelItem(g);
        if (!group) {
            continue;
        }
        for (int i = 0; i < group->childCount(); ++i) {
            walkChildren(group->child(i));
        }
    }

    // Stop duration timer if no active items remain
    if (!anyActive && m_durationTimer) {
        m_durationTimer->stop();
    }
}

void SessionManagerPanel::scheduleMetadataSave()
{
    // When window is inactive, defer saves — they'll flush on resume.
    if (m_timersPaused) {
        m_pendingSave = true;
        return;
    }

    if (!m_saveDebounce) {
        m_saveDebounce = new QTimer(this);
        m_saveDebounce->setSingleShot(true);
        connect(m_saveDebounce, &QTimer::timeout, this, [this]() {
            saveMetadata();
        });
    }
    m_saveDebounce->start(1000);
}

void SessionManagerPanel::updateTreeWidget()
{
    if (!m_initialized) {
        return;
    }

    // Guard against overlapping async tmux queries - if one is already running,
    // mark that we need another update after it finishes
    if (m_asyncQueryInFlight) {
        m_asyncQueryPending = true;
        return;
    }

    m_asyncQueryInFlight = true;
    m_asyncQueryPending = false;

    // Async pre-pass: query tmux for live sessions without blocking the GUI,
    // then call updateTreeWidgetWithLiveSessions() with the result.
    // TmuxManager is NOT parented to this, so it survives panel destruction.
    auto *tmux = new TmuxManager(nullptr);

    // Use QPointer to guard against the panel being destroyed before callback fires
    QPointer<SessionManagerPanel> guard(this);

    tmux->listKonsolaiSessionsAsync([guard, tmux](const QList<TmuxManager::SessionInfo> &liveSessions) {
        tmux->deleteLater();

        // Check if the panel was destroyed while waiting for async result
        if (!guard) {
            qDebug() << "SessionManagerPanel: Panel destroyed during async tmux query, skipping update";
            return;
        }

        guard->m_asyncQueryInFlight = false;

        QSet<QString> liveNames;
        for (const auto &info : liveSessions) {
            liveNames.insert(info.name);
        }
        guard->m_cachedLiveNames = liveNames;
        guard->updateTreeWidgetWithLiveSessions(liveNames);

        // If another update was requested while we were querying, run it now
        if (guard->m_asyncQueryPending) {
            guard->updateTreeWidget();
        }
    });
}

void SessionManagerPanel::refreshRemoteTmuxSessions()
{
    // Collect unique SSH hosts from metadata
    QMap<QString, QPair<QString, int>> hostCredentials; // host → (username, port)
    for (const auto &meta : std::as_const(m_metadata)) {
        if (meta.isRemote && !meta.sshHost.isEmpty()) {
            if (!hostCredentials.contains(meta.sshHost)) {
                hostCredentials[meta.sshHost] = {meta.sshUsername, meta.sshPort};
            }
        }
    }

    if (hostCredentials.isEmpty()) {
        return;
    }

    // Use a shared counter to track when all queries complete,
    // accumulating into a temporary set to avoid clearing the cache
    // while queries are still in flight.
    auto pendingCount = std::make_shared<int>(hostCredentials.size());
    auto accumulated = std::make_shared<QSet<QString>>();

    // Query each unique host asynchronously
    for (auto it = hostCredentials.constBegin(); it != hostCredentials.constEnd(); ++it) {
        const QString host = it.key();
        const QString user = it.value().first;
        const int port = it.value().second;

        auto *proc = new QProcess(this);
        QStringList args = {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5")};
        if (port != 22) {
            args << QStringLiteral("-p") << QString::number(port);
        }
        QString userHost = user.isEmpty() ? host : QStringLiteral("%1@%2").arg(user, host);
        args << userHost << QStringLiteral("tmux list-sessions -F '#{session_name}' 2>/dev/null | grep ^konsolai-");

        QPointer<SessionManagerPanel> guard(this);
        connect(proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [guard, proc, pendingCount, accumulated](int exitCode, QProcess::ExitStatus) {
                    proc->deleteLater();
                    if (!guard) {
                        return;
                    }
                    if (exitCode == 0) {
                        QString output = QString::fromUtf8(proc->readAllStandardOutput());
                        const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                        for (const auto &line : lines) {
                            QString name = line.trimmed();
                            if (!name.isEmpty()) {
                                accumulated->insert(name);
                            }
                        }
                    }
                    // Only update cache and tree when all host queries have completed
                    --(*pendingCount);
                    if (*pendingCount <= 0) {
                        guard->m_cachedRemoteLiveNames = *accumulated;
                        guard->scheduleTreeUpdate();
                    }
                });

        // Kill SSH process after 15s to prevent leak on network hang
        QPointer<QProcess> safeProc(proc);
        QTimer::singleShot(15000, proc, [safeProc, pendingCount, accumulated, guard]() {
            if (safeProc && safeProc->state() != QProcess::NotRunning) {
                safeProc->kill();
                // finished() signal will fire after kill, delivering deleteLater and
                // decrementing pendingCount via the connection above.
            }
        });

        proc->start(QStringLiteral("ssh"), args);
    }
}

void SessionManagerPanel::refreshCachesAsync()
{
    if (m_cacheRefreshInFlight) {
        return;
    }
    m_cacheRefreshInFlight = true;

    // Snapshot search root
    QString searchRoot;
    auto *settings = KonsolaiSettings::instance();
    if (settings) {
        searchRoot = settings->projectRoot();
    }
    if (searchRoot.isEmpty()) {
        searchRoot = QDir::homePath() + QStringLiteral("/projects");
    }

    // Snapshot known working directories from registry (discover filter)
    QSet<QString> knownDirs;
    if (m_registry) {
        for (const auto &state : m_registry->allSessionStates()) {
            knownDirs.insert(state.workingDirectory);
        }
    }

    // Collect all directories that need conversation refresh
    QSet<QString> convDirs;
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        if (!it.value().workingDirectory.isEmpty()) {
            convDirs.insert(it.value().workingDirectory);
        }
    }
    for (const auto &state : std::as_const(m_cachedDiscoveredSessions)) {
        convDirs.insert(state.workingDirectory);
    }

    QPointer<SessionManagerPanel> guard(this);
    auto future = QtConcurrent::run([searchRoot, knownDirs, convDirs]() -> CacheRefreshResult {
        CacheRefreshResult result;

        // Discover sessions (directory scan + settings.local.json reads)
        if (!searchRoot.isEmpty() && QDir(searchRoot).exists()) {
            QDir rootDir(searchRoot);
            const auto entries = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &dirName : entries) {
                QString projectPath = rootDir.filePath(dirName);
                QString claudeDir = QDir(projectPath).filePath(QStringLiteral(".claude"));
                if (!QDir(claudeDir).exists()) {
                    continue;
                }
                if (knownDirs.contains(projectPath)) {
                    continue;
                }

                ClaudeSessionState state;
                state.sessionName = QStringLiteral("discovered-%1").arg(dirName);
                state.sessionId = dirName.left(8);
                state.workingDirectory = projectPath;
                state.isAttached = false;

                QString settingsPath = QDir(claudeDir).filePath(QStringLiteral("settings.local.json"));
                if (QFile::exists(settingsPath)) {
                    QFile f(settingsPath);
                    if (f.open(QIODevice::ReadOnly)) {
                        QJsonParseError err;
                        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
                        if (err.error == QJsonParseError::NoError && doc.isObject()) {
                            QString content = QString::fromUtf8(doc.toJson());
                            state.profileName = content.contains(QStringLiteral("konsolai")) ? QStringLiteral("Claude") : QStringLiteral("External");
                        }
                        f.close();
                    }
                } else {
                    state.profileName = QStringLiteral("External");
                }

                QFileInfo claudeDirInfo(claudeDir);
                state.created = claudeDirInfo.birthTime().isValid() ? claudeDirInfo.birthTime() : claudeDirInfo.lastModified();
                state.lastAccessed = claudeDirInfo.lastModified();

                result.discovered.append(state);
            }
        }

        // Read conversations for all directories (the expensive part)
        QSet<QString> allDirs = convDirs;
        for (const auto &state : std::as_const(result.discovered)) {
            allDirs.insert(state.workingDirectory);
        }
        for (const QString &dir : std::as_const(allDirs)) {
            result.conversations[dir] = ClaudeSessionRegistry::readClaudeConversations(dir);
        }

        return result;
    });

    auto *watcher = new QFutureWatcher<CacheRefreshResult>(this);
    connect(watcher, &QFutureWatcher<CacheRefreshResult>::finished, this, [this, guard, watcher]() {
        if (!guard) {
            watcher->deleteLater();
            return;
        }
        auto result = watcher->result();
        m_cachedDiscoveredSessions = result.discovered;
        m_discoveredCacheValid = true;
        for (auto it = result.conversations.constBegin(); it != result.conversations.constEnd(); ++it) {
            m_conversationCache[it.key()] = it.value();
        }
        m_cacheRefreshInFlight = false;
        watcher->deleteLater();
        scheduleTreeUpdate();
    });
    watcher->setFuture(future);
}

void SessionManagerPanel::updateTreeWidgetWithLiveSessions(const QSet<QString> &liveNames)
{
    // Purge stale null QPointer entries — sessions destroyed via deleteLater()
    // leave null entries in the map that would otherwise be treated as "active".
    for (auto it = m_activeSessions.begin(); it != m_activeSessions.end();) {
        if (it.value().isNull()) {
            it = m_activeSessions.erase(it);
        } else {
            ++it;
        }
    }

    // Caches are now TTL-based (git 60s, conversations/discovered/GSD 120s)
    // and invalidated on targeted events (register, unregister, workDir change).
    // No unconditional clear here — this was the main perf bottleneck.

    // Prune stale expansion keys if the set has grown large
    if (m_knownItems.size() > 500) {
        pruneStaleKeys();
    }

    // Save expansion state, scroll position, and selection before destroying items
    saveTreeState();

    // Suppress repaints during rebuild to eliminate flicker
    m_treeWidget->setUpdatesEnabled(false);

    // Clear everything — project groups are recreated lazily via ensureProjectGroup().
    // Cached group pointers are now stale, so flush them.
    m_treeWidget->clear();
    m_projectGroups.clear();
    m_categoryGroups.clear();

    // Build the category map from the full set of distinct workdirs we know about
    // (m_metadata plus discovered sessions, if visible). We do this BEFORE rendering
    // so ensureProjectGroup() can route each project to its category bucket.
    {
        QSet<QString> workdirSet;
        // resolveProjectKey is defined below; inline an equivalent here to avoid
        // forward-reference plumbing. Worktrees collapse to their parent project.
        auto resolveKey = [](const QString &workDir) -> QString {
            const int idx = workDir.indexOf(QStringLiteral("/.claude/worktrees/"));
            return idx > 0 ? workDir.left(idx) : workDir;
        };
        for (auto it = m_metadata.cbegin(); it != m_metadata.cend(); ++it) {
            const QString &wd = it.value().workingDirectory;
            if (!wd.isEmpty()) {
                workdirSet.insert(resolveKey(wd));
            }
        }
        // Include discovered workdirs when either the Discovered chip or
        // the Subagents chip is on — subagent items are surfaced via the
        // same discovered pipeline.
        if (m_registry && (m_visibleStates.contains(QStringLiteral("discovered")) || m_visibleStates.contains(QStringLiteral("subagent")))) {
            for (const auto &state : std::as_const(m_cachedDiscoveredSessions)) {
                if (!state.workingDirectory.isEmpty()) {
                    workdirSet.insert(resolveKey(state.workingDirectory));
                }
            }
        }
        m_categoryMap = buildCategoryMap(QList<QString>(workdirSet.cbegin(), workdirSet.cend()));

        // Apply user rules on top of LCP grouping (order matters):
        //   1. Per-workdir overrides — most specific, always win
        //   2. Category aliases — rename bucket keys (cowardly-irregular → cowir)
        //   3. Suppress list — dissolve unwanted LCP-created buckets
        // Aliases/overrides beat suppression by design: an explicit user rule
        // always trumps a bulk suppression.
        auto *settings = KonsolaiSettings::instance();
        if (settings) {
            const QHash<QString, QString> workdirOverrides = settings->workdirCategoryOverrides();
            const QHash<QString, QString> aliases = settings->categoryAliases();
            const QStringList suppressList = settings->suppressedCategories();
            const QSet<QString> suppressed(suppressList.cbegin(), suppressList.cend());

            // Per-workdir overrides (highest priority)
            for (auto it = m_categoryMap.begin(); it != m_categoryMap.end(); ++it) {
                const QString &wd = it.key();
                auto ov = workdirOverrides.constFind(wd);
                if (ov != workdirOverrides.constEnd() && !ov.value().isEmpty()) {
                    it.value() = ov.value();
                }
            }
            // Category aliases
            for (auto it = m_categoryMap.begin(); it != m_categoryMap.end(); ++it) {
                auto aliasIt = aliases.constFind(it.value());
                if (aliasIt != aliases.constEnd() && !aliasIt.value().isEmpty()) {
                    it.value() = aliasIt.value();
                }
            }
            // Suppress LCP categories the user has dissolved: fall back to
            // standalone (basename), so the workdir renders top-level.
            for (auto it = m_categoryMap.begin(); it != m_categoryMap.end(); ++it) {
                if (suppressed.contains(it.value())) {
                    it.value() = QDir(it.key()).dirName();
                }
            }
        }
    }

    // User-defined empty categories: pre-create the top-level nodes so that
    // (a) ensureProjectGroup can find and reuse them even when only one
    // project routes here (the LCP >=2 rule would normally standalone it),
    // and (b) categories with zero projects still surface for the user to
    // drop things into.  The label starts as "name (empty)" but the label-
    // annotation pass later replaces it with "name (N)" when N > 0.
    if (auto *settingsForUserCats = KonsolaiSettings::instance()) {
        const QStringList userCats = settingsForUserCats->userCategories();
        for (const QString &name : userCats) {
            if (name.isEmpty() || m_categoryGroups.contains(name)) {
                continue;
            }
            auto *cat = new QTreeWidgetItem(m_treeWidget);
            cat->setText(0, name + QLatin1Char(' ') + i18n("(empty)"));
            cat->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-symbolic"), QIcon::fromTheme(QStringLiteral("folder"))));
            cat->setToolTip(0, i18n("User category: %1", name));
            cat->setFlags(Qt::ItemIsEnabled);
            cat->setExpanded(false);
            cat->setData(0, Qt::UserRole + 6, QString(QStringLiteral("category:") + name));
            m_categoryGroups.insert(name, cat);
        }
    }

    // Sort sessions by last accessed (most recent first)
    QList<SessionMetadata> sortedMeta = m_metadata.values();
    std::sort(sortedMeta.begin(), sortedMeta.end(), [](const SessionMetadata &a, const SessionMetadata &b) {
        return a.lastAccessed > b.lastAccessed;
    });

    // Helper: collapse to a single working-directory key for grouping.
    // Worktree paths (.../.claude/worktrees/...) collapse to the parent project,
    // so all worktree variants share a single group with the main repo.
    auto resolveProjectKey = [](const QString &workDir) -> QString {
        int idx = workDir.indexOf(QStringLiteral("/.claude/worktrees/"));
        if (idx > 0) {
            return workDir.left(idx);
        }
        return workDir;
    };

    // Pre-pass: determine state token and project key for each session, applying
    // the visible-state filter chip set so we only render what the user wants.
    struct SessionEntry {
        SessionMetadata meta;
        QString stateToken;
        QString projectKey;
        bool isLive = false;
    };
    QList<SessionEntry> entries;
    entries.reserve(sortedMeta.size());

    for (const auto &meta : sortedMeta) {
        bool isAct = m_activeSessions.contains(meta.sessionId);
        bool alive = liveNames.contains(meta.sessionName) || (meta.isRemote && m_cachedRemoteLiveNames.contains(meta.sessionName));
        bool wasClosed = m_explicitlyClosed.contains(meta.sessionId);
        if (wasClosed && !alive) {
            m_explicitlyClosed.remove(meta.sessionId);
            wasClosed = false;
        }
        bool isLive = isAct || (alive && !wasClosed);

        SessionEntry e;
        e.meta = meta;
        e.isLive = isLive;
        e.stateToken = stateTokenFor(meta, isLive);
        e.projectKey = resolveProjectKey(meta.workingDirectory);

        // Filter by visible state-token chips
        if (!m_visibleStates.contains(e.stateToken)) {
            continue;
        }
        entries.append(e);
    }

    // Sibling-disambiguation count: how many entries share the same workingDirectory?
    QHash<QString, int> dirCount;
    for (const auto &e : entries) {
        dirCount[e.meta.workingDirectory]++;
    }

    // Track per-project: max lastAccessed (for group sort) and session count (for label).
    QHash<QString, QDateTime> groupMaxAccess;
    QHash<QString, int> groupSessionCount;

    // Add each session under its project group (created on demand).
    for (const auto &e : entries) {
        QTreeWidgetItem *group = ensureProjectGroup(e.projectKey);
        bool hasSiblings = dirCount.value(e.meta.workingDirectory, 0) > 1;
        addSessionToTree(e.meta, group, hasSiblings);

        const QString gKey = projectGroupKey(e.projectKey);
        if (e.meta.lastAccessed.isValid()) {
            auto it = groupMaxAccess.find(gKey);
            if (it == groupMaxAccess.end() || e.meta.lastAccessed > it.value()) {
                groupMaxAccess[gKey] = e.meta.lastAccessed;
            }
        }
        groupSessionCount[gKey]++;
    }

    // Discovered sessions: only render if the Discovered chip is on, OR the
    // Subagents chip is on (subagent jsonls surface via discovery too).
    // Group them under their own project key alongside regular sessions.
    const bool showDiscovered = m_visibleStates.contains(QStringLiteral("discovered"));
    const bool showSubagentsFromDiscovery = m_visibleStates.contains(QStringLiteral("subagent"));
    static const QRegularExpression discoveredSubagentPattern(QStringLiteral("agent-[a-f0-9]{16}"));
    if (m_registry && (showDiscovered || showSubagentsFromDiscovery)) {
        for (const auto &state : std::as_const(m_cachedDiscoveredSessions)) {
            // Classify: subagent if its sessionName matches the agent-fleet
            // pattern OR if the newest jsonl lives under a /subagents/ dir.
            // Otherwise it's a plain "discovered" session.
            bool isSubagentDiscovered = discoveredSubagentPattern.match(state.sessionName).hasMatch();
            if (!isSubagentDiscovered && !state.workingDirectory.isEmpty()) {
                const QString hashed = ClaudeSessionRegistry::hashedProjectPath(state.workingDirectory);
                const QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashed;
                QDir dir(projectDir);
                if (dir.exists()) {
                    QDirIterator dit(dir.absolutePath(), {QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
                    while (dit.hasNext()) {
                        dit.next();
                        if (dit.fileInfo().absoluteFilePath().contains(QStringLiteral("/subagents/"))) {
                            isSubagentDiscovered = true;
                            break;
                        }
                    }
                }
            }

            const QString effectiveState = isSubagentDiscovered ? QStringLiteral("subagent") : QStringLiteral("discovered");
            // Respect the chip filter: skip if this item's effective state
            // isn't in the visible set (avoids showing a subagent when only
            // Discovered is enabled, and vice-versa).
            if (!m_visibleStates.contains(effectiveState)) {
                continue;
            }

            QString pKey = resolveProjectKey(state.workingDirectory);
            QTreeWidgetItem *group = ensureProjectGroup(pKey);
            auto *item = new QTreeWidgetItem(group);
            QString displayName = QDir(state.workingDirectory).dirName();
            item->setText(0, displayName);
            item->setData(0, Qt::UserRole, state.sessionId);
            item->setData(0, Qt::UserRole + 1, state.workingDirectory);
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-cloud")));
            // State token marker — kept as "discovered" for double-click
            // routing (unarchive path) even when bucketed as "subagent" in
            // the chip filter.  The runtime action is identical.
            item->setData(0, Qt::UserRole + 5, QStringLiteral("discovered"));

            // Per-session state icon in column 1 reflects the effective state
            // so the user visually distinguishes a subagent from a plain
            // discovered project.
            item->setIcon(1, stateIcon(effectiveState));
            item->setToolTip(1, stateLabel(effectiveState));

            // Show conversation count from cache (populated by refreshCachesAsync)
            const auto &conversations = m_conversationCache[state.workingDirectory];
            if (!conversations.isEmpty()) {
                QString existing = item->text(1);
                if (!existing.isEmpty()) {
                    existing += QStringLiteral(" ");
                }
                existing += QStringLiteral("%1 conv").arg(conversations.size());
                item->setText(1, existing);
            }

            item->setToolTip(0, QStringLiteral("%1\n%2\nLast modified: %3").arg(state.profileName, state.workingDirectory, state.lastAccessed.toString()));

            const QString gKey = projectGroupKey(pKey);
            if (state.lastAccessed.isValid()) {
                auto it = groupMaxAccess.find(gKey);
                if (it == groupMaxAccess.end() || state.lastAccessed > it.value()) {
                    groupMaxAccess[gKey] = state.lastAccessed;
                }
            }
            groupSessionCount[gKey]++;
        }
    }

    // Sort each project group's children: pinned first, then by lastAccessed desc.
    auto childSessionId = [](QTreeWidgetItem *child) -> QString {
        return child->data(0, Qt::UserRole).toString();
    };

    for (auto git = m_projectGroups.constBegin(); git != m_projectGroups.constEnd(); ++git) {
        QTreeWidgetItem *group = git.value();
        QList<QTreeWidgetItem *> children;
        while (group->childCount() > 0) {
            children.append(group->takeChild(0));
        }
        std::sort(children.begin(), children.end(), [&](QTreeWidgetItem *a, QTreeWidgetItem *b) {
            const QString aId = childSessionId(a);
            const QString bId = childSessionId(b);
            const SessionMetadata *aMeta = aId.isEmpty() ? nullptr : sessionMetadata(aId);
            const SessionMetadata *bMeta = bId.isEmpty() ? nullptr : sessionMetadata(bId);
            const bool aPinned = aMeta ? aMeta->isPinned : false;
            const bool bPinned = bMeta ? bMeta->isPinned : false;
            if (aPinned != bPinned) {
                return aPinned;
            }
            const QDateTime aWhen = aMeta ? aMeta->lastAccessed : QDateTime();
            const QDateTime bWhen = bMeta ? bMeta->lastAccessed : QDateTime();
            if (aWhen != bWhen) {
                return aWhen > bWhen;
            }
            return a->text(0) < b->text(0);
        });
        for (QTreeWidgetItem *c : std::as_const(children)) {
            group->addChild(c);
        }
        // Append (count) to the existing label set by ensureProjectGroup.
        // (For multi-project categories, the label is the prefix-stripped sub-label
        // — we must not regenerate it from the workdir basename here.)
        const int count = groupSessionCount.value(git.key(), 0);
        const QString existing = group->text(0);
        if (count > 0 && !existing.endsWith(QStringLiteral(")"))) {
            group->setText(0, QStringLiteral("%1 (%2)").arg(existing).arg(count));
        }
        const QString expandKey = QStringLiteral("group:") + git.key();
        // Default to expanded for new groups; preserve saved state for known groups.
        const bool expanded = m_expansionState.value(expandKey, true);
        group->setExpanded(expanded);
    }

    // Sort projects inside each multi-project category by max-lastAccessed of any descendant.
    // Also annotate category labels with total session count across all sub-projects.
    for (auto cit = m_categoryGroups.constBegin(); cit != m_categoryGroups.constEnd(); ++cit) {
        QTreeWidgetItem *cat = cit.value();
        QList<QTreeWidgetItem *> subProjects;
        while (cat->childCount() > 0) {
            subProjects.append(cat->takeChild(0));
        }
        std::sort(subProjects.begin(), subProjects.end(), [&](QTreeWidgetItem *a, QTreeWidgetItem *b) {
            QString aKey = a->data(0, Qt::UserRole + 6).toString();
            QString bKey = b->data(0, Qt::UserRole + 6).toString();
            if (aKey.startsWith(QStringLiteral("group:"))) {
                aKey = aKey.mid(6);
            }
            if (bKey.startsWith(QStringLiteral("group:"))) {
                bKey = bKey.mid(6);
            }
            const QDateTime aWhen = groupMaxAccess.value(aKey);
            const QDateTime bWhen = groupMaxAccess.value(bKey);
            if (aWhen != bWhen) {
                return aWhen > bWhen;
            }
            return a->text(0) < b->text(0);
        });
        int catTotalSessions = 0;
        for (QTreeWidgetItem *sp : std::as_const(subProjects)) {
            cat->addChild(sp);
            QString sKey = sp->data(0, Qt::UserRole + 6).toString();
            if (sKey.startsWith(QStringLiteral("group:"))) {
                sKey = sKey.mid(6);
            }
            catTotalSessions += groupSessionCount.value(sKey, 0);
            // Re-apply sub-project expansion state since takeChild/addChild clears it.
            sp->setExpanded(m_expansionState.value(QStringLiteral("group:") + sKey, true));
        }
        if (catTotalSessions > 0) {
            cat->setText(0, QStringLiteral("%1 (%2)").arg(cit.key()).arg(catTotalSessions));
        }
        cat->setExpanded(m_expansionState.value(QStringLiteral("category:") + cit.key(), true));
    }

    // Sort top-level items (standalone projects + multi-project category buckets)
    // by max-lastAccessed of any descendant session, desc.
    auto topLevelMaxAccess = [&](QTreeWidgetItem *t) -> QDateTime {
        const QString eKey = t->data(0, Qt::UserRole + 6).toString();
        if (eKey.startsWith(QStringLiteral("category:"))) {
            const QString cKey = eKey.mid(9);
            // Walk member projects; pick the max.
            QDateTime best;
            for (auto mit = m_categoryMap.cbegin(); mit != m_categoryMap.cend(); ++mit) {
                if (mit.value() != cKey) {
                    continue;
                }
                const QDateTime when = groupMaxAccess.value(projectGroupKey(mit.key()));
                if (when.isValid() && (!best.isValid() || when > best)) {
                    best = when;
                }
            }
            return best;
        }
        if (eKey.startsWith(QStringLiteral("group:"))) {
            return groupMaxAccess.value(eKey.mid(6));
        }
        return {};
    };

    if (m_treeWidget->topLevelItemCount() > 1) {
        QList<QTreeWidgetItem *> tops;
        while (m_treeWidget->topLevelItemCount() > 0) {
            tops.append(m_treeWidget->takeTopLevelItem(0));
        }
        std::sort(tops.begin(), tops.end(), [&](QTreeWidgetItem *a, QTreeWidgetItem *b) {
            const QDateTime aWhen = topLevelMaxAccess(a);
            const QDateTime bWhen = topLevelMaxAccess(b);
            if (aWhen != bWhen) {
                return aWhen > bWhen;
            }
            return a->text(0) < b->text(0);
        });
        for (QTreeWidgetItem *t : std::as_const(tops)) {
            m_treeWidget->addTopLevelItem(t);
            // Re-apply expansion state since insertion clears it.
            const QString eKey = t->data(0, Qt::UserRole + 6).toString();
            if (eKey.startsWith(QStringLiteral("group:"))) {
                t->setExpanded(m_expansionState.value(eKey, true));
            } else if (eKey.startsWith(QStringLiteral("category:"))) {
                t->setExpanded(m_expansionState.value(eKey, true));
            }
        }
    }

    // Stop duration timer if no sessions have active teams
    if (m_durationTimer && m_durationTimer->isActive()) {
        bool anyActiveTeam = false;
        for (auto it = m_activeSessions.constBegin(); it != m_activeSessions.constEnd(); ++it) {
            if (it.value() && it.value()->hasActiveTeam()) {
                anyActiveTeam = true;
                break;
            }
        }
        if (!anyActiveTeam) {
            m_durationTimer->stop();
        }
    }

    // Show empty state when no project groups exist (nothing to render)
    m_emptyStateLabel->setVisible(m_projectGroups.isEmpty());

    // Re-apply active filter after tree rebuild
    if (!m_filterEdit->text().isEmpty()) {
        applyFilter(m_filterEdit->text());
    }

    // Re-enable repaints after rebuild
    m_treeWidget->setUpdatesEnabled(true);

    // Restore scroll position and selection
    restoreTreeState();
}

void SessionManagerPanel::applyFilter(const QString &text)
{
    if (!m_treeWidget) {
        return;
    }

    // Helper: check if a leaf (session) item matches the filter text
    auto itemMatchesFilter = [&text](QTreeWidgetItem *item) -> bool {
        QString sessionId = item->data(0, Qt::UserRole).toString();
        return item->text(0).contains(text, Qt::CaseInsensitive) || item->toolTip(0).contains(text, Qt::CaseInsensitive)
            || sessionId.contains(text, Qt::CaseInsensitive);
    };

    // Tree shape is now 2-3 levels:
    //   category (optional) → project group → session
    //   project group (standalone)         → session
    // Recursively show/hide so a category bucket whose every session is filtered out hides too.
    std::function<int(QTreeWidgetItem *)> walkVisible;
    walkVisible = [&](QTreeWidgetItem *item) -> int {
        // Leaf (session) item — UserRole + 6 starts with "s:".
        const QString key = item->data(0, Qt::UserRole + 6).toString();
        const bool isLeaf = key.startsWith(QStringLiteral("s:")) || key.startsWith(QStringLiteral("discovered:"));
        if (isLeaf) {
            const bool matches = text.isEmpty() || itemMatchesFilter(item);
            item->setHidden(!matches);
            return matches ? 1 : 0;
        }
        // Group (project or category): aggregate from children.
        int visible = 0;
        for (int i = 0; i < item->childCount(); ++i) {
            visible += walkVisible(item->child(i));
        }
        item->setHidden(text.isEmpty() ? item->childCount() == 0 : visible == 0);
        return visible;
    };

    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_treeWidget->topLevelItem(i);
        if (!top) {
            continue;
        }
        walkVisible(top);
    }
}

void SessionManagerPanel::addSessionToTree(const SessionMetadata &meta, QTreeWidgetItem *parent, bool hasSiblings)
{
    auto *item = new QTreeWidgetItem(parent);

    // Display name: prefer user-set/auto-derived description as the PRIMARY label
    // (replacing the workdir basename). Falls back to dirname, then session name.
    // Branch/time/state suffixes get appended below regardless of source.
    QString displayName;
    if (!meta.description.trimmed().isEmpty()) {
        displayName = meta.description.trimmed();
    } else if (!meta.workingDirectory.isEmpty() && meta.workingDirectory != QStringLiteral(".") && meta.workingDirectory != QDir::homePath()) {
        displayName = QDir(meta.workingDirectory).dirName();
    } else {
        displayName = meta.sessionName;
    }
    // Fallback to session name if display name is empty or just "." or "build" (which is misleading)
    if (displayName.isEmpty() || displayName == QStringLiteral(".") || displayName == QStringLiteral("build")) {
        displayName = meta.sessionName;
    }

    // Trim displayName if it came from a long description so the tree stays compact.
    if (displayName.length() > 50) {
        displayName = displayName.left(47) + QStringLiteral("...");
    }

    // Snapshot the clean primary label for the tooltip's first line. The
    // displayName below gets decorated with branch/time/badges; the tooltip
    // wants the original.
    const QString primaryLabel = displayName;

    // Add task description for disambiguation only when we did NOT already
    // use meta.description as the primary label (avoids duplication).
    // Priority: active taskDescription > persisted description > Claude CLI conversation > nothing
    bool isActive = m_activeSessions.contains(meta.sessionId);
    QString description;
    const bool descriptionIsPrimary = !meta.description.trimmed().isEmpty();

    // 1. Live session task description
    if (isActive) {
        ClaudeSession *session = m_activeSessions[meta.sessionId];
        if (session && !session->taskDescription().isEmpty()) {
            description = session->taskDescription();
        }
    }

    // 2. Persisted description from previous run (suppressed if already used as primary label)
    if (description.isEmpty() && !meta.description.isEmpty() && !descriptionIsPrimary) {
        description = meta.description;
    }

    // 3. Claude CLI conversation data (only available for local sessions)
    // Use cached data only — never block tree rebuild with disk I/O.
    // refreshCachesAsync() keeps the cache populated in background.
    if (description.isEmpty() && !meta.workingDirectory.isEmpty() && m_registry && m_conversationCache.contains(meta.workingDirectory)) {
        const auto &conversations = m_conversationCache[meta.workingDirectory];

        // 1. Direct match by conversation UUID
        if (!meta.lastResumeSessionId.isEmpty()) {
            for (const auto &conv : conversations) {
                if (conv.sessionId == meta.lastResumeSessionId) {
                    description = conv.summary.isEmpty() ? conv.firstPrompt : conv.summary;
                    break;
                }
            }
        }
        // 2. Match by closest creation timestamp
        if (description.isEmpty() && meta.createdAt.isValid() && !conversations.isEmpty()) {
            qint64 bestDelta = std::numeric_limits<qint64>::max();
            const ClaudeConversation *bestMatch = nullptr;
            for (const auto &conv : conversations) {
                if (!conv.created.isValid()) {
                    continue;
                }
                qint64 delta = qAbs(meta.createdAt.secsTo(conv.created));
                if (delta < bestDelta) {
                    bestDelta = delta;
                    bestMatch = &conv;
                }
            }
            if (bestMatch) {
                description = bestMatch->summary.isEmpty() ? bestMatch->firstPrompt : bestMatch->summary;
            }
        }
        // 3. Last resort: most recent conversation
        if (description.isEmpty() && !conversations.isEmpty()) {
            const auto &first = conversations.first();
            description = first.summary.isEmpty() ? first.firstPrompt : first.summary;
        }
    }

    // Apply description or fall back to session ID
    if (!description.isEmpty()) {
        // Collapse newlines and trim for single-line display
        description = description.simplified();
        if (description.length() > 35) {
            description = description.left(32) + QStringLiteral("...");
        }
        displayName += QStringLiteral(" (%1)").arg(description);
    }
    // No hash fallback — directory name alone is clearer than a random hex string

    // When multiple sessions share the same directory in the same category,
    // append creation date to disambiguate visually-identical entries.
    if (hasSiblings && meta.createdAt.isValid()) {
        // Use "MMM d" for older sessions, "h:mm AP" for today's sessions
        if (meta.createdAt.date() == QDate::currentDate()) {
            displayName += QStringLiteral(" — %1").arg(meta.createdAt.toString(QStringLiteral("h:mm AP")));
        } else {
            displayName += QStringLiteral(" — %1").arg(meta.createdAt.toString(QStringLiteral("MMM d")));
        }
    }

    // Add team badge when subagents exist (active or completed/persisted)
    if (isActive) {
        ClaudeSession *activeSession = m_activeSessions[meta.sessionId];
        if (activeSession && !activeSession->subagents().isEmpty()) {
            int activeCount = 0;
            int totalCount = activeSession->subagents().size();
            const auto &agents = activeSession->subagents();
            for (auto it = agents.constBegin(); it != agents.constEnd(); ++it) {
                if (it->state == ClaudeProcess::State::Working || it->state == ClaudeProcess::State::Idle) {
                    activeCount++;
                }
            }
            QString badgeLabel = activeSession->teamName().isEmpty() ? QStringLiteral("team") : activeSession->teamName();
            if (activeCount == 0) {
                displayName += QStringLiteral(" [%1: done]").arg(badgeLabel);
            } else if (activeCount < totalCount) {
                displayName += QStringLiteral(" [%1: %2/%3]").arg(badgeLabel).arg(activeCount).arg(totalCount);
            } else {
                displayName += QStringLiteral(" [%1: %2]").arg(badgeLabel).arg(activeCount);
            }
        }
    } else if (!meta.subagents.isEmpty()) {
        // Persisted team badge
        displayName += QStringLiteral(" [team: done]");
    }

    // Agent linkage badge
    if (!meta.agentId.isEmpty()) {
        displayName += QStringLiteral(" [\U0001F916 %1]").arg(meta.agentId);
    }

    // Add GSD badge when .planning/ or ROADMAP.md exists in working directory (cached)
    if (!meta.workingDirectory.isEmpty()) {
        if (!m_gsdBadgeCache.contains(meta.workingDirectory)) {
            QDir workDir(meta.workingDirectory);
            m_gsdBadgeCache.insert(meta.workingDirectory, workDir.exists(QStringLiteral(".planning")) || workDir.exists(QStringLiteral("ROADMAP.md")));
        }
        if (m_gsdBadgeCache.value(meta.workingDirectory)) {
            displayName += QStringLiteral(" [GSD]");
        }
    }

    // Git branch badge (local sessions only, cached per working directory)
    if (!meta.workingDirectory.isEmpty() && !meta.isRemote) {
        if (!m_gitBranchCache.contains(meta.workingDirectory)) {
            // Insert placeholder to prevent duplicate async queries for the same dir
            m_gitBranchCache.insert(meta.workingDirectory, QString());

            // Async git query — updates cache and triggers tree refresh when done
            auto *git = new QProcess(this);
            git->setWorkingDirectory(meta.workingDirectory);
            QPointer<SessionManagerPanel> guard(this);
            QString workDir = meta.workingDirectory;
            connect(git, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [guard, git, workDir](int exitCode, QProcess::ExitStatus) {
                git->deleteLater();
                if (!guard) {
                    return;
                }
                if (exitCode == 0) {
                    QString branch = QString::fromUtf8(git->readAllStandardOutput()).trimmed();
                    guard->m_gitBranchCache[workDir] = branch;
                } else {
                    guard->m_gitBranchCache[workDir] = QString();
                }
                guard->scheduleTreeUpdate();
            });
            git->start(QStringLiteral("git"), {QStringLiteral("branch"), QStringLiteral("--show-current")});
        }
        const QString &branch = m_gitBranchCache[meta.workingDirectory];
        if (!branch.isEmpty() && branch != QStringLiteral("main") && branch != QStringLiteral("master")) {
            displayName += QStringLiteral(" [%1]").arg(branch);
        }
    }

    // Disambiguate when multiple sessions share the same directory in the same category
    if (hasSiblings && description.isEmpty() && meta.createdAt.isValid()) {
        displayName += QStringLiteral(" — %1").arg(meta.createdAt.toString(QStringLiteral("MMM d h:mmap")));
    }

    // Add @host suffix for remote sessions
    if (meta.isRemote && !meta.sshHost.isEmpty()) {
        displayName += QStringLiteral(" @%1").arg(meta.sshHost);
    }

    // Muted indicator
    if (m_mutedSessions.contains(meta.sessionId)) {
        displayName += QStringLiteral(" [muted]");
    }

    // Merged-away indicator: this session was collapsed into another via
    // mergeSessions(). Adds a small suffix and is annotated in the tooltip
    // (appended below).
    if (!meta.mergedInto.isEmpty()) {
        displayName += QStringLiteral(" \xE2\x86\x92 merged");
    }

    item->setText(0, displayName);
    item->setData(0, Qt::UserRole, meta.sessionId);

    // Composite key for expansion state preservation
    QString sessionKey = QStringLiteral("s:%1").arg(meta.sessionId);
    item->setData(0, Qt::UserRole + 6, sessionKey);

    // Muted visual styling
    if (m_mutedSessions.contains(meta.sessionId)) {
        QFont f = item->font(0);
        f.setItalic(true);
        item->setFont(0, f);
        item->setForeground(0, QBrush(QColor(140, 140, 140)));
    }

    // Enhanced tooltip — always preserves the original session/project identity
    // even when the displayName is a free-text description.
    QString tooltip;
    if (meta.isRemote) {
        QString userHost = meta.sshUsername.isEmpty() ? meta.sshHost : QStringLiteral("%1@%2").arg(meta.sshUsername, meta.sshHost);
        tooltip = i18n("%1\nProject: %2\nSession: %3\nRemote: %4\nLast accessed: %5",
                       primaryLabel,
                       meta.workingDirectory,
                       meta.sessionName,
                       userHost,
                       meta.lastAccessed.toString());
    } else {
        tooltip = i18n("%1\nProject: %2\nSession: %3\nLast accessed: %4", primaryLabel, meta.workingDirectory, meta.sessionName, meta.lastAccessed.toString());
    }
    // Append git branch to tooltip (always, including main/master)
    if (m_gitBranchCache.contains(meta.workingDirectory) && !m_gitBranchCache[meta.workingDirectory].isEmpty()) {
        tooltip += QStringLiteral("\nBranch: %1").arg(m_gitBranchCache[meta.workingDirectory]);
    }
    if (!meta.mergedInto.isEmpty()) {
        QString primaryLabel = meta.mergedInto;
        if (m_metadata.contains(meta.mergedInto)) {
            const auto &primaryMeta = m_metadata[meta.mergedInto];
            if (!primaryMeta.description.trimmed().isEmpty()) {
                primaryLabel = primaryMeta.description.trimmed();
            } else if (!primaryMeta.workingDirectory.isEmpty()) {
                primaryLabel = QDir(primaryMeta.workingDirectory).dirName();
            } else {
                primaryLabel = primaryMeta.sessionName;
            }
        }
        tooltip += QStringLiteral("\nMerged into: %1").arg(primaryLabel);
    }
    item->setToolTip(0, tooltip);

    // Add yolo mode and approval count indicators in column 1 (always visible).
    // Uses QLabel with rich HTML for colored bolts. Right-click is handled by
    // the viewport event filter intercepting QEvent::ContextMenu directly.
    if (isActive) {
        ClaudeSession *session = m_activeSessions[meta.sessionId];
        if (session) {
            QString boltsHtml;
            int yoloCount = session->yoloApprovalCount();
            int doubleCount = session->doubleYoloApprovalCount();

            if (session->yoloMode() || yoloCount > 0) {
                boltsHtml += QStringLiteral("<span style='color:#FFB300'>⚡</span>");
                if (yoloCount > 0) {
                    boltsHtml += QStringLiteral("<span style='color:#FFB300'>[%1]</span>").arg(yoloCount);
                }
            }
            if (session->doubleYoloMode() || doubleCount > 0) {
                if (!boltsHtml.isEmpty()) {
                    boltsHtml += QStringLiteral(" ");
                }
                boltsHtml += QStringLiteral("<span style='color:#42A5F5'>⚡</span>");
                if (doubleCount > 0) {
                    boltsHtml += QStringLiteral("<span style='color:#42A5F5'>[%1]</span>").arg(doubleCount);
                }
            }
            if (auto *bc = session->budgetController()) {
                if (bc->budget().hasAnyLimit() && bc->budget().timeLimitMinutes > 0) {
                    int elapsed = bc->budget().elapsedMinutes();
                    boltsHtml += QStringLiteral(" <span style='color:gray; font-size:10px'>%1/%2m</span>").arg(elapsed).arg(bc->budget().timeLimitMinutes);
                }
            }
            if (auto *obs = session->sessionObserver()) {
                int severity = obs->composedSeverity();
                if (severity >= 5) {
                    boltsHtml += QStringLiteral(" <span style='color:#F44336'>\xe2\x9a\xa0 CRITICAL</span>");
                } else if (severity >= 3) {
                    boltsHtml += QStringLiteral(" <span style='color:#FF9800'>\xe2\x9a\xa0</span>");
                } else if (severity > 0) {
                    boltsHtml += QStringLiteral(" <span style='color:#FFC107'>\xe2\x9a\xa0</span>");
                }
            }
            // Use plain setText for column 1 — QLabel item widgets intercept
            // mouse events on Wayland, preventing right-click context menus.
            if (!boltsHtml.isEmpty()) {
                // Strip HTML to plain text for setText, preserve unicode symbols
                QString plain = boltsHtml;
                plain.replace(QRegularExpression(QStringLiteral("<[^>]*>")), QString());
                plain = plain.simplified();
                item->setText(1, plain);
            }
        }
    }

    // Set icon based on state (remote sessions use network icons)
    if (meta.isArchived && meta.isExpired) {
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
    } else if (meta.isArchived) {
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-grey")));
    } else if (meta.isRemote) {
        // Remote sessions use network icons
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("network-server"), QIcon::fromTheme(QStringLiteral("folder-remote"))));
    } else if (isActive) {
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-open")));
    } else {
        // Detached but not archived
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
    }

    // Add status indicator (green for local, cyan for remote)
    if (isActive) {
        if (meta.isRemote) {
            item->setForeground(0, QBrush(Qt::cyan));
        } else {
            item->setForeground(0, QBrush(Qt::darkGreen));
        }
    }

    // Stamp the session's state token at UserRole+5 and surface a small
    // state-icon in column 1 (alongside any yolo/budget text). Pinned sessions
    // additionally get the "📌" pin icon prepended to the name as a badge.
    {
        const bool isLiveForState =
            isActive || m_cachedLiveNames.contains(meta.sessionName) || (meta.isRemote && m_cachedRemoteLiveNames.contains(meta.sessionName));
        const QString stateToken = stateTokenFor(meta, isLiveForState);
        item->setData(0, Qt::UserRole + 5, stateToken);
        // For column 1, prefer the state icon when no text/bolts present.
        if (item->text(1).isEmpty()) {
            item->setIcon(1, stateIcon(stateToken));
            item->setToolTip(1, stateLabel(stateToken));
        }
        // Pin indicator for pinned sessions: bold font on the name column.
        // (Previously a "📌 " emoji prefix; replaced because some font/encoding
        // stacks render it as mojibake.) A pin theme icon is only set when no
        // column-0 icon is already in place — folder/remote/archived icons win.
        if (meta.isPinned) {
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
            if (item->icon(0).isNull()) {
                item->setIcon(0, QIcon::fromTheme(QStringLiteral("pin")));
            }
        }
    }
}

void SessionManagerPanel::loadMetadata()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    QString filePath = dataPath + QStringLiteral("/sessions.json");

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "SessionManagerPanel::loadMetadata: JSON parse error at offset" << parseError.offset << ":" << parseError.errorString() << "in"
                   << filePath;
        return;
    }
    if (!doc.isArray()) {
        qWarning() << "SessionManagerPanel::loadMetadata: Expected JSON array in" << filePath;
        return;
    }

    QJsonArray array = doc.array();
    for (const auto &value : array) {
        QJsonObject obj = value.toObject();
        SessionMetadata meta;
        meta.sessionId = obj[QStringLiteral("sessionId")].toString();
        meta.sessionName = obj[QStringLiteral("sessionName")].toString();
        meta.profileName = obj[QStringLiteral("profileName")].toString();
        meta.workingDirectory = obj[QStringLiteral("workingDirectory")].toString();
        meta.isPinned = obj[QStringLiteral("isPinned")].toBool();
        meta.isArchived = obj[QStringLiteral("isArchived")].toBool();
        meta.isExpired = obj[QStringLiteral("isExpired")].toBool();
        meta.isDismissed = obj[QStringLiteral("isDismissed")].toBool();
        // Optional flag — false by default; writer only emits it when true.
        meta.isSubagent = obj[QStringLiteral("isSubagent")].toBool(false);
        meta.lastAccessed = QDateTime::fromString(obj[QStringLiteral("lastAccessed")].toString(), Qt::ISODate);
        meta.createdAt = QDateTime::fromString(obj[QStringLiteral("createdAt")].toString(), Qt::ISODate);

        // SSH remote session fields
        meta.isRemote = obj[QStringLiteral("isRemote")].toBool();
        meta.sshHost = obj[QStringLiteral("sshHost")].toString();
        meta.sshUsername = obj[QStringLiteral("sshUsername")].toString();
        meta.sshPort = obj[QStringLiteral("sshPort")].toInt(22);

        // Per-session yolo mode settings
        meta.yoloMode = obj[QStringLiteral("yoloMode")].toBool();
        meta.doubleYoloMode = obj[QStringLiteral("doubleYoloMode")].toBool();

        // Approval counts
        meta.yoloApprovalCount = obj[QStringLiteral("yoloApprovalCount")].toInt();
        meta.doubleYoloApprovalCount = obj[QStringLiteral("doubleYoloApprovalCount")].toInt();

        // Approval log
        const QJsonArray logArray = obj[QStringLiteral("approvalLog")].toArray();
        for (const auto &logVal : logArray) {
            QJsonObject logObj = logVal.toObject();
            ApprovalLogEntry entry;
            entry.timestamp = QDateTime::fromString(logObj[QStringLiteral("time")].toString(), Qt::ISODate);
            entry.toolName = logObj[QStringLiteral("tool")].toString();
            entry.action = logObj[QStringLiteral("action")].toString();
            entry.yoloLevel = logObj[QStringLiteral("level")].toInt();
            entry.totalTokens = static_cast<quint64>(logObj[QStringLiteral("tokens")].toInteger(0));
            entry.estimatedCostUSD = logObj[QStringLiteral("cost")].toDouble();
            entry.toolInput = logObj[QStringLiteral("input")].toString();
            entry.toolOutput = logObj[QStringLiteral("output")].toString();
            meta.approvalLog.append(entry);
        }

        // Resume session ID, description, and agent linkage
        meta.lastResumeSessionId = obj[QStringLiteral("lastResumeSessionId")].toString();
        meta.description = obj[QStringLiteral("description")].toString();
        meta.agentId = obj[QStringLiteral("agentId")].toString();
        meta.mergedInto = obj[QStringLiteral("mergedInto")].toString();

        // Budget settings
        meta.budgetTimeLimitMinutes = obj[QStringLiteral("budgetTimeLimitMinutes")].toInt();
        meta.budgetCostCeilingUSD = obj[QStringLiteral("budgetCostCeilingUSD")].toDouble();
        meta.budgetTokenCeiling = static_cast<quint64>(obj[QStringLiteral("budgetTokenCeiling")].toInteger(0));

        // Subagent/subprocess snapshots
        if (obj.contains(QStringLiteral("subagents"))) {
            const QJsonArray agentArray = obj[QStringLiteral("subagents")].toArray();
            for (const auto &val : agentArray) {
                meta.subagents.append(SubagentInfo::fromJson(val.toObject()));
            }
        }
        if (obj.contains(QStringLiteral("subprocesses"))) {
            const QJsonArray procArray = obj[QStringLiteral("subprocesses")].toArray();
            for (const auto &val : procArray) {
                meta.subprocesses.append(SubprocessInfo::fromJson(val.toObject()));
            }
        }
        if (obj.contains(QStringLiteral("promptLabels"))) {
            const QJsonObject labelsObj = obj[QStringLiteral("promptLabels")].toObject();
            for (auto it = labelsObj.constBegin(); it != labelsObj.constEnd(); ++it) {
                meta.promptGroupLabels[it.key().toInt()] = it.value().toString();
            }
        }
        meta.currentPromptRound = obj[QStringLiteral("promptRound")].toInt(0);

        if (!meta.sessionId.isEmpty() && !meta.sessionName.isEmpty()) {
            m_metadata[meta.sessionId] = meta;
        }
    }
}

void SessionManagerPanel::saveMetadata(bool sync)
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    QString filePath = dataPath + QStringLiteral("/sessions.json");

    QJsonArray array;
    for (auto &meta : m_metadata) {
        // Snapshot live session data into metadata before serializing
        // Use QPointer to safely detect if the session was deleted between
        // the map lookup and the dereference (e.g., during archiveSession).
        if (m_activeSessions.contains(meta.sessionId)) {
            QPointer<ClaudeSession> session = m_activeSessions[meta.sessionId];
            if (session) {
                meta.subagents = session->subagents().values().toVector();
                meta.subprocesses = session->subprocesses().values().toVector();
                meta.promptGroupLabels = session->promptGroupLabels();
                meta.currentPromptRound = session->currentPromptRound();
            }
        }

        QJsonObject obj;
        obj[QStringLiteral("sessionId")] = meta.sessionId;
        obj[QStringLiteral("sessionName")] = meta.sessionName;
        obj[QStringLiteral("profileName")] = meta.profileName;
        obj[QStringLiteral("workingDirectory")] = meta.workingDirectory;
        obj[QStringLiteral("isPinned")] = meta.isPinned;
        obj[QStringLiteral("isArchived")] = meta.isArchived;
        obj[QStringLiteral("isExpired")] = meta.isExpired;
        obj[QStringLiteral("lastAccessed")] = meta.lastAccessed.toString(Qt::ISODate);
        obj[QStringLiteral("createdAt")] = meta.createdAt.toString(Qt::ISODate);

        // SSH remote session fields
        if (meta.isRemote) {
            obj[QStringLiteral("isRemote")] = true;
            obj[QStringLiteral("sshHost")] = meta.sshHost;
            obj[QStringLiteral("sshUsername")] = meta.sshUsername;
            obj[QStringLiteral("sshPort")] = meta.sshPort;
        }

        // Per-session yolo mode settings (only save if enabled to keep JSON clean)
        if (meta.yoloMode) {
            obj[QStringLiteral("yoloMode")] = true;
        }
        if (meta.doubleYoloMode) {
            obj[QStringLiteral("doubleYoloMode")] = true;
        }
        if (meta.isDismissed) {
            obj[QStringLiteral("isDismissed")] = true;
        }
        // Same optional-write pattern as isDismissed / mergedInto — only
        // emit when true so the JSON stays clean for the common case.
        if (meta.isSubagent) {
            obj[QStringLiteral("isSubagent")] = true;
        }

        // Approval counts (only save if non-zero)
        int totalApprovals = meta.yoloApprovalCount + meta.doubleYoloApprovalCount;
        if (totalApprovals > 0) {
            obj[QStringLiteral("yoloApprovalCount")] = meta.yoloApprovalCount;
            obj[QStringLiteral("doubleYoloApprovalCount")] = meta.doubleYoloApprovalCount;

            // Approval log (cap at 500 most recent entries to keep JSON manageable)
            if (!meta.approvalLog.isEmpty()) {
                QJsonArray logArray;
                int startIdx = qMax(0, meta.approvalLog.size() - 500);
                for (int i = startIdx; i < meta.approvalLog.size(); ++i) {
                    const auto &entry = meta.approvalLog[i];
                    QJsonObject logObj;
                    logObj[QStringLiteral("time")] = entry.timestamp.toString(Qt::ISODate);
                    logObj[QStringLiteral("tool")] = entry.toolName;
                    logObj[QStringLiteral("action")] = entry.action;
                    logObj[QStringLiteral("level")] = entry.yoloLevel;
                    if (entry.totalTokens > 0) {
                        logObj[QStringLiteral("tokens")] = static_cast<double>(entry.totalTokens);
                        logObj[QStringLiteral("cost")] = entry.estimatedCostUSD;
                    }
                    if (!entry.toolInput.isEmpty()) {
                        logObj[QStringLiteral("input")] = entry.toolInput;
                    }
                    if (!entry.toolOutput.isEmpty()) {
                        logObj[QStringLiteral("output")] = entry.toolOutput;
                    }
                    logArray.append(logObj);
                }
                obj[QStringLiteral("approvalLog")] = logArray;
            }
        }

        // Resume session ID and description (only save if non-empty)
        if (!meta.lastResumeSessionId.isEmpty()) {
            obj[QStringLiteral("lastResumeSessionId")] = meta.lastResumeSessionId;
        }
        if (!meta.description.isEmpty()) {
            obj[QStringLiteral("description")] = meta.description;
        }
        if (!meta.agentId.isEmpty()) {
            obj[QStringLiteral("agentId")] = meta.agentId;
        }
        if (!meta.mergedInto.isEmpty()) {
            obj[QStringLiteral("mergedInto")] = meta.mergedInto;
        }

        // Budget settings (only save if any limit is set)
        if (meta.budgetTimeLimitMinutes > 0) {
            obj[QStringLiteral("budgetTimeLimitMinutes")] = meta.budgetTimeLimitMinutes;
        }
        if (meta.budgetCostCeilingUSD > 0.0) {
            obj[QStringLiteral("budgetCostCeilingUSD")] = meta.budgetCostCeilingUSD;
        }
        if (meta.budgetTokenCeiling > 0) {
            obj[QStringLiteral("budgetTokenCeiling")] = static_cast<double>(meta.budgetTokenCeiling);
        }

        // Subagent/subprocess snapshots (only write if non-empty)
        if (!meta.subagents.isEmpty()) {
            QJsonArray agentArray;
            for (const auto &agent : meta.subagents) {
                agentArray.append(agent.toJson());
            }
            obj[QStringLiteral("subagents")] = agentArray;
        }
        if (!meta.subprocesses.isEmpty()) {
            QJsonArray procArray;
            for (const auto &proc : meta.subprocesses) {
                procArray.append(proc.toJson());
            }
            obj[QStringLiteral("subprocesses")] = procArray;
        }
        if (!meta.promptGroupLabels.isEmpty()) {
            QJsonObject labelsObj;
            for (auto it = meta.promptGroupLabels.constBegin(); it != meta.promptGroupLabels.constEnd(); ++it) {
                labelsObj[QString::number(it.key())] = it.value();
            }
            obj[QStringLiteral("promptLabels")] = labelsObj;
        }
        if (meta.currentPromptRound > 0) {
            obj[QStringLiteral("promptRound")] = meta.currentPromptRound;
        }

        array.append(obj);
    }

    // Serialize + write: async by default (avoids blocking UI for 1MB+ files),
    // but synchronous during destruction to prevent races with the next operation.
    QJsonDocument doc(array);
    auto writeFile = [filePath, doc]() {
        QByteArray json = doc.toJson();
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            qint64 written = file.write(json);
            if (written != json.size()) {
                qWarning() << "SessionManagerPanel::saveMetadata: Incomplete write —" << written << "of" << json.size() << "bytes to" << filePath;
            }
            file.close();
        } else {
            qWarning() << "SessionManagerPanel::saveMetadata: Failed to open" << filePath << "for writing:" << file.errorString();
        }
    };
    if (sync) {
        writeFile();
    } else {
        (void)QtConcurrent::run(writeFile);
    }

    Q_EMIT usageAggregateChanged();
}

// Compute incremental cost within a time window from cumulative approval log entries.
// Each entry's estimatedCostUSD is cumulative for the session, so we need the delta
// between the last entry in the window and the last entry before the window.
static double periodCostFromLog(const QVector<ApprovalLogEntry> &log, const QDateTime &windowStart)
{
    if (log.isEmpty()) {
        return 0.0;
    }

    // Find the last entry in the window and the last entry before the window
    double lastInWindow = -1.0;
    double lastBeforeWindow = 0.0;
    bool hasEntryInWindow = false;

    for (const auto &entry : log) {
        if (entry.timestamp < windowStart) {
            lastBeforeWindow = entry.estimatedCostUSD;
        } else {
            lastInWindow = entry.estimatedCostUSD;
            hasEntryInWindow = true;
        }
    }

    if (!hasEntryInWindow) {
        return 0.0;
    }

    return lastInWindow - lastBeforeWindow;
}

double SessionManagerPanel::weeklySpentUSD() const
{
    QDateTime now = QDateTime::currentDateTime();
    QDate today = now.date();
    // dayOfWeek: 1=Mon .. 7=Sun
    QDate weekStart = today.addDays(-(today.dayOfWeek() - 1));
    QDateTime windowStart(weekStart, QTime(0, 0, 0));

    double total = 0.0;
    for (const auto &meta : m_metadata) {
        total += periodCostFromLog(meta.approvalLog, windowStart);
    }
    return total;
}

double SessionManagerPanel::monthlySpentUSD() const
{
    QDateTime now = QDateTime::currentDateTime();
    QDate monthStart(now.date().year(), now.date().month(), 1);
    QDateTime windowStart(monthStart, QTime(0, 0, 0));

    double total = 0.0;
    for (const auto &meta : m_metadata) {
        total += periodCostFromLog(meta.approvalLog, windowStart);
    }
    return total;
}

SessionMetadata *SessionManagerPanel::findMetadata(const QString &sessionId)
{
    if (m_metadata.contains(sessionId)) {
        return &m_metadata[sessionId];
    }
    return nullptr;
}

QTreeWidgetItem *SessionManagerPanel::findTreeItem(const QString &sessionId)
{
    // Tree layout: top-level = project group, children = session items.
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_treeWidget->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *item = group->child(j);
            if (item->data(0, Qt::UserRole).toString() == sessionId) {
                return item;
            }
        }
    }
    return nullptr;
}

void SessionManagerPanel::refreshSessionItemLabel(const QString &sessionId)
{
    QTreeWidgetItem *item = findTreeItem(sessionId);
    if (!item) {
        return;
    }

    ClaudeSession *session = m_activeSessions.value(sessionId);
    if (!session) {
        return;
    }

    // Rebuild column-1 QLabel in-place (bolts, budget, observer badges).
    // Right-click is handled by viewport event filter, not customContextMenuRequested.
    QString boltsHtml;
    int yoloCount = session->yoloApprovalCount();
    int doubleCount = session->doubleYoloApprovalCount();

    if (session->yoloMode() || yoloCount > 0) {
        boltsHtml += QStringLiteral("<span style='color:#FFB300'>⚡</span>");
        if (yoloCount > 0) {
            boltsHtml += QStringLiteral("<span style='color:#FFB300'>[%1]</span>").arg(yoloCount);
        }
    }
    if (session->doubleYoloMode() || doubleCount > 0) {
        if (!boltsHtml.isEmpty()) {
            boltsHtml += QStringLiteral(" ");
        }
        boltsHtml += QStringLiteral("<span style='color:#42A5F5'>⚡</span>");
        if (doubleCount > 0) {
            boltsHtml += QStringLiteral("<span style='color:#42A5F5'>[%1]</span>").arg(doubleCount);
        }
    }
    if (auto *bc = session->budgetController()) {
        if (bc->budget().hasAnyLimit() && bc->budget().timeLimitMinutes > 0) {
            int elapsed = bc->budget().elapsedMinutes();
            boltsHtml += QStringLiteral(" <span style='color:gray; font-size:10px'>%1/%2m</span>").arg(elapsed).arg(bc->budget().timeLimitMinutes);
        }
    }
    if (auto *obs = session->sessionObserver()) {
        int severity = obs->composedSeverity();
        if (severity >= 5) {
            boltsHtml += QStringLiteral(" <span style='color:#F44336'>\xe2\x9a\xa0 CRITICAL</span>");
        } else if (severity >= 3) {
            boltsHtml += QStringLiteral(" <span style='color:#FF9800'>\xe2\x9a\xa0</span>");
        } else if (severity > 0) {
            boltsHtml += QStringLiteral(" <span style='color:#FFC107'>\xe2\x9a\xa0</span>");
        }
    }
    // Use plain setText — QLabel item widgets block right-click on Wayland
    if (!boltsHtml.isEmpty()) {
        QString plain = boltsHtml;
        plain.replace(QRegularExpression(QStringLiteral("<[^>]*>")), QString());
        plain = plain.simplified();
        item->setText(1, plain);
    } else {
        item->setText(1, QString());
    }
}

void SessionManagerPanel::showApprovalLog(ClaudeSession *session)
{
    if (!session) {
        return;
    }

    const auto &log = session->approvalLog();

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Approval Log - %1", QDir(session->workingDirectory()).dirName()));
    auto *layout = new QVBoxLayout(&dialog);

    auto *summary = new QLabel(i18n("Total auto-approvals: %1 (Yolo: %2, Double: %3)",
                                    session->totalApprovalCount(),
                                    session->yoloApprovalCount(),
                                    session->doubleYoloApprovalCount()),
                               &dialog);
    layout->addWidget(summary);

    auto *splitter = new QSplitter(Qt::Vertical, &dialog);

    auto *tree = new QTreeWidget(splitter);
    tree->setHeaderLabels({i18n("Time"), i18n("Tool"), i18n("Action"), i18n("Level"), i18n("Tokens"), i18n("Cost")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setSortingEnabled(true);

    // Show most recent first
    for (int i = log.size() - 1; i >= 0; --i) {
        const auto &entry = log[i];
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, entry.timestamp.toString(QStringLiteral("hh:mm:ss")));
        item->setText(1, entry.toolName);
        item->setText(2, entry.action);
        QString levelStr;
        if (entry.yoloLevel == 1) {
            levelStr = QStringLiteral("Yolo");
        } else if (entry.yoloLevel == 2) {
            levelStr = QStringLiteral("Double");
        } else if (entry.yoloLevel == 3) {
            levelStr = QStringLiteral("Triple");
        }
        item->setText(3, levelStr);
        if (entry.totalTokens > 0) {
            // Format tokens compactly: "12.3K", "1.2M"
            double t = static_cast<double>(entry.totalTokens);
            QString tokenStr = t >= 1000000.0 ? QStringLiteral("%1M").arg(t / 1000000.0, 0, 'f', 1)
                : t >= 1000.0                 ? QStringLiteral("%1K").arg(t / 1000.0, 0, 'f', 1)
                                              : QString::number(entry.totalTokens);
            item->setText(4, tokenStr);
            item->setText(5, QStringLiteral("$%1").arg(entry.estimatedCostUSD, 0, 'f', 4));
            item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
            item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
            // Store raw values for correct numeric sorting
            item->setData(4, Qt::UserRole, static_cast<qulonglong>(entry.totalTokens));
            item->setData(5, Qt::UserRole, entry.estimatedCostUSD);
        }
        // Store original log index for detail lookup
        item->setData(0, Qt::UserRole + 1, i);
        // Store yolo level for numeric sorting
        item->setData(3, Qt::UserRole, entry.yoloLevel);
    }

    // Default sort by time descending (most recent first)
    tree->sortByColumn(0, Qt::DescendingOrder);

    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    auto *detailEdit = new QPlainTextEdit(splitter);
    detailEdit->setReadOnly(true);
    detailEdit->setPlaceholderText(i18n("Select an entry above to view tool input/output"));

    splitter->addWidget(tree);
    splitter->addWidget(detailEdit);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter);

    // Show tool input/output when an entry is selected.
    // Capture log by value — it's a const ref to caller's data that may go out of scope.
    const auto logCopy = log;
    QObject::connect(tree, &QTreeWidget::currentItemChanged, &dialog, [logCopy, detailEdit](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (!current) {
            detailEdit->clear();
            return;
        }
        int idx = current->data(0, Qt::UserRole + 1).toInt();
        if (idx < 0 || idx >= logCopy.size()) {
            detailEdit->clear();
            return;
        }
        const auto &entry = logCopy[idx];
        QString detail;

        // For double yolo (suggestion acceptance), show the action
        if (entry.yoloLevel == 2) {
            detail += QStringLiteral("--- Action ---\nAccepted inline suggestion (Tab + Enter)\n");
        }

        if (!entry.toolInput.isEmpty()) {
            if (!detail.isEmpty()) {
                detail += QStringLiteral("\n");
            }
            detail += QStringLiteral("--- Input ---\n") + entry.toolInput;
        }
        if (!entry.toolOutput.isEmpty()) {
            if (!detail.isEmpty()) {
                detail += QStringLiteral("\n");
            }
            detail += QStringLiteral("--- Output ---\n") + entry.toolOutput;
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("(no tool input/output recorded)");
        }
        detailEdit->setPlainText(detail);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(800, 500);
    dialog.exec();
}

void SessionManagerPanel::showSubagentTranscript(const SubagentInfo &info)
{
    if (info.transcriptPath.isEmpty() || !QFile::exists(info.transcriptPath)) {
        QMessageBox::information(this, i18n("No Transcript"), i18n("Transcript file is not available yet.\nIt will be available after the agent completes."));
        return;
    }

    QFile file(info.transcriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n("Read Error"), i18n("Could not open transcript file:\n%1", info.transcriptPath));
        return;
    }

    // Parse JSONL and extract readable content
    // Cap at 5000 lines to prevent UI freeze on large transcripts
    static constexpr int MAX_LINES = 5000;
    QString readable;
    int lineCount = 0;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        if (++lineCount > MAX_LINES) {
            readable += i18n("\n\n(Truncated at %1 lines — open externally for full transcript)\n", MAX_LINES);
            break;
        }
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonDocument lineDoc = QJsonDocument::fromJson(line.toUtf8());
        if (!lineDoc.isObject()) {
            continue;
        }
        QJsonObject obj = lineDoc.object();
        QString type = obj[QStringLiteral("type")].toString();

        if (type == QStringLiteral("assistant")) {
            // Extract assistant message content
            QJsonArray content = obj[QStringLiteral("message")].toObject()[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                    readable += QStringLiteral("[Assistant]\n%1\n\n").arg(b[QStringLiteral("text")].toString());
                } else if (b[QStringLiteral("type")].toString() == QStringLiteral("tool_use")) {
                    readable += QStringLiteral("[Tool: %1]\n").arg(b[QStringLiteral("name")].toString());
                    QString inputStr = QString::fromUtf8(QJsonDocument(b[QStringLiteral("input")].toObject()).toJson(QJsonDocument::Compact));
                    if (inputStr.length() > 200) {
                        inputStr = inputStr.left(197) + QStringLiteral("...");
                    }
                    readable += inputStr + QStringLiteral("\n\n");
                }
            }
        } else if (type == QStringLiteral("tool_result") || type == QStringLiteral("result")) {
            QJsonArray content = obj[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                    QString text = b[QStringLiteral("text")].toString();
                    if (text.length() > 500) {
                        text = text.left(497) + QStringLiteral("...");
                    }
                    readable += QStringLiteral("[Result]\n%1\n\n").arg(text);
                }
            }
        } else if (type == QStringLiteral("human") || type == QStringLiteral("user")) {
            QJsonArray content = obj[QStringLiteral("message")].toObject()[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                    readable += QStringLiteral("[User]\n%1\n\n").arg(b[QStringLiteral("text")].toString());
                }
            }
        }
    }
    file.close();

    if (readable.isEmpty()) {
        // Fallback: show first 5000 lines of raw JSONL (capped to avoid UI freeze)
        QFile raw(info.transcriptPath);
        if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream rawStream(&raw);
            int rawLines = 0;
            while (!rawStream.atEnd() && rawLines < MAX_LINES) {
                readable += rawStream.readLine() + QStringLiteral("\n");
                rawLines++;
            }
            if (!rawStream.atEnd()) {
                readable += i18n("\n(Truncated at %1 lines)\n", MAX_LINES);
            }
            raw.close();
        }
    }

    // Build viewer dialog
    QString title = i18n("Transcript \u2014 %1", info.agentType);
    if (!info.teammateName.isEmpty()) {
        title = i18n("Transcript \u2014 %1 (%2)", info.agentType, info.teammateName);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);

    auto *toolbar = new QToolBar(&dialog);
    QAction *openExternalAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open in External Editor"));
    connect(openExternalAction, &QAction::triggered, &dialog, [info]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.transcriptPath));
    });
    layout->addWidget(toolbar);

    auto *textEdit = new QPlainTextEdit(&dialog);
    textEdit->setReadOnly(true);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setStyleHint(QFont::TypeWriter);
    textEdit->setFont(monoFont);
    textEdit->setPlainText(readable);
    layout->addWidget(textEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(700, 500);
    dialog.exec();
}

void SessionManagerPanel::showSessionActivity(const QString &jsonlPath, const QString &workDir)
{
    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n("Read Error"), i18n("Could not open conversation file:\n%1", jsonlPath));
        return;
    }

    // Parse JSONL: build structured activity and collect file paths from tool_use
    // Cap at 5000 lines to prevent UI freeze on large conversations (10K+ lines → 500ms+)
    static constexpr int MAX_LINES = 5000;
    QString readable;
    QSet<QString> filesModified; // unique file paths from Write/Edit tool calls
    int toolCallCount = 0;
    int userMessageCount = 0;
    int lineCount = 0;
    bool truncated = false;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        if (++lineCount > MAX_LINES) {
            truncated = true;
            break;
        }
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonDocument lineDoc = QJsonDocument::fromJson(line.toUtf8());
        if (!lineDoc.isObject()) {
            continue;
        }
        QJsonObject obj = lineDoc.object();
        QString type = obj[QStringLiteral("type")].toString();

        if (type == QStringLiteral("assistant")) {
            QJsonArray content = obj[QStringLiteral("message")].toObject()[QStringLiteral("content")].toArray();
            for (const auto &block : content) {
                QJsonObject b = block.toObject();
                QString blockType = b[QStringLiteral("type")].toString();
                if (blockType == QStringLiteral("text")) {
                    readable += QStringLiteral("[Assistant]\n%1\n\n").arg(b[QStringLiteral("text")].toString());
                } else if (blockType == QStringLiteral("tool_use")) {
                    toolCallCount++;
                    QString toolName = b[QStringLiteral("name")].toString();
                    QJsonObject input = b[QStringLiteral("input")].toObject();

                    // Extract file path from Write/Edit/Read tool calls
                    QString filePath = input[QStringLiteral("file_path")].toString();
                    if (!filePath.isEmpty() && (toolName == QStringLiteral("Write") || toolName == QStringLiteral("Edit"))) {
                        filesModified.insert(filePath);
                    }

                    readable += QStringLiteral("[Tool: %1]").arg(toolName);
                    if (!filePath.isEmpty()) {
                        readable += QStringLiteral("  %1").arg(filePath);
                    }
                    readable += QStringLiteral("\n");

                    // Show compact input for non-file tools
                    if (filePath.isEmpty()) {
                        QString inputStr = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
                        if (inputStr.length() > 200) {
                            inputStr = inputStr.left(197) + QStringLiteral("...");
                        }
                        readable += inputStr;
                    }
                    readable += QStringLiteral("\n\n");
                }
            }
        } else if (type == QStringLiteral("human") || type == QStringLiteral("user")) {
            userMessageCount++;
            QJsonValue content = obj[QStringLiteral("message")].toObject()[QStringLiteral("content")];
            if (content.isString()) {
                readable += QStringLiteral("[User]\n%1\n\n").arg(content.toString());
            } else if (content.isArray()) {
                for (const auto &block : content.toArray()) {
                    QJsonObject b = block.toObject();
                    if (b[QStringLiteral("type")].toString() == QStringLiteral("text")) {
                        readable += QStringLiteral("[User]\n%1\n\n").arg(b[QStringLiteral("text")].toString());
                    }
                }
            }
        }
        // Skip tool_result for activity view — focus on actions, not outputs
    }
    file.close();

    // Build summary header
    QString summary;
    summary += i18n("Project: %1\n", QDir(workDir).dirName());
    summary += i18n("Messages: %1 user, Tool calls: %2\n", userMessageCount, toolCallCount);
    if (truncated) {
        summary += i18n("(Showing first %1 lines — open externally for full transcript)\n", MAX_LINES);
    }
    if (!filesModified.isEmpty()) {
        QStringList sortedFiles = filesModified.values();
        sortedFiles.sort();
        summary += i18n("Files modified (%1):\n", sortedFiles.size());
        for (const auto &f : std::as_const(sortedFiles)) {
            summary += QStringLiteral("  %1\n").arg(f);
        }
    }
    summary += QStringLiteral("\n") + QString(60, QChar(0x2500)) + QStringLiteral("\n\n");

    QString fullText = summary + readable;

    // Build viewer dialog
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Session Activity \u2014 %1", QDir(workDir).dirName()));
    auto *layout = new QVBoxLayout(&dialog);

    auto *toolbar = new QToolBar(&dialog);
    QAction *openExternalAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open in External Editor"));
    connect(openExternalAction, &QAction::triggered, &dialog, [jsonlPath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(jsonlPath));
    });
    layout->addWidget(toolbar);

    auto *textEdit = new QPlainTextEdit(&dialog);
    textEdit->setReadOnly(true);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setStyleHint(QFont::TypeWriter);
    textEdit->setFont(monoFont);
    textEdit->setPlainText(fullText);
    layout->addWidget(textEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(800, 600);
    dialog.exec();
}

void SessionManagerPanel::showSessionStructure(const QString &sessionId)
{
    // Pull subagent + subprocess snapshots from either the live session or persisted metadata.
    QMap<QString, SubagentInfo> subagentsMap;
    QMap<QString, SubprocessInfo> subprocessesMap;
    QMap<int, QString> promptLabels;
    QString sessionLabel;

    if (m_activeSessions.contains(sessionId)) {
        QPointer<ClaudeSession> session = m_activeSessions[sessionId];
        if (session) {
            subagentsMap = session->subagents();
            subprocessesMap = session->subprocesses();
            promptLabels = session->promptGroupLabels();
            sessionLabel = session->sessionName();
        }
    }
    if (subagentsMap.isEmpty() && subprocessesMap.isEmpty() && m_metadata.contains(sessionId)) {
        const auto &meta = m_metadata[sessionId];
        for (const auto &agent : meta.subagents) {
            SubagentInfo a = agent;
            a.state = ClaudeProcess::State::NotRunning;
            subagentsMap[a.agentId] = a;
        }
        for (const auto &proc : meta.subprocesses) {
            subprocessesMap[proc.id] = proc;
        }
        promptLabels = meta.promptGroupLabels;
        if (sessionLabel.isEmpty()) {
            sessionLabel = meta.sessionName;
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Session Structure — %1", sessionLabel.isEmpty() ? sessionId : sessionLabel));
    dialog.resize(700, 500);
    auto *layout = new QVBoxLayout(&dialog);

    auto *tree = new QTreeWidget(&dialog);
    tree->setHeaderLabels({i18n("Item"), i18n("Status")});
    tree->setRootIsDecorated(true);
    layout->addWidget(tree, 1);

    if (subagentsMap.isEmpty() && subprocessesMap.isEmpty()) {
        auto *empty = new QLabel(i18n("This session has no subagents or subprocesses to inspect."), &dialog);
        empty->setStyleSheet(QStringLiteral("color: gray; font-style: italic; padding: 12px;"));
        layout->insertWidget(0, empty);
    } else {
        auto stateText = [](ClaudeProcess::State s) -> QString {
            switch (s) {
            case ClaudeProcess::State::Working:
                return i18n("Working");
            case ClaudeProcess::State::Idle:
                return i18n("Idle");
            case ClaudeProcess::State::WaitingInput:
                return i18n("Waiting");
            case ClaudeProcess::State::Starting:
                return i18n("Starting");
            case ClaudeProcess::State::Error:
                return i18n("Error");
            case ClaudeProcess::State::NotRunning:
            default:
                return i18n("Done");
            }
        };

        // Collect prompt rounds
        QSet<int> rounds;
        for (auto it = subagentsMap.constBegin(); it != subagentsMap.constEnd(); ++it) {
            rounds.insert(it->promptGroupId);
        }
        for (auto it = subprocessesMap.constBegin(); it != subprocessesMap.constEnd(); ++it) {
            rounds.insert(it->promptGroupId);
        }
        QList<int> sortedRounds = rounds.values();
        std::sort(sortedRounds.begin(), sortedRounds.end());
        bool multipleRounds = sortedRounds.size() > 1;

        for (int round : sortedRounds) {
            QTreeWidgetItem *roundItem = nullptr;
            if (multipleRounds) {
                roundItem = new QTreeWidgetItem(tree);
                QString label = promptLabels.value(round, i18n("Prompt #%1", round + 1));
                roundItem->setText(0, label);
                roundItem->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-question")));
                roundItem->setExpanded(true);
            }

            for (auto it = subagentsMap.constBegin(); it != subagentsMap.constEnd(); ++it) {
                if (it->promptGroupId != round) {
                    continue;
                }
                QTreeWidgetItem *parent = roundItem ? roundItem : tree->invisibleRootItem();
                auto *agentItem = new QTreeWidgetItem(parent);
                QString name = it->agentType;
                if (!it->teammateName.isEmpty()) {
                    name = QStringLiteral("%1 (%2)").arg(it->agentType, it->teammateName);
                }
                if (!it->currentTaskSubject.isEmpty()) {
                    name += QStringLiteral(" — %1").arg(it->currentTaskSubject);
                }
                agentItem->setText(0, name);
                agentItem->setText(1, stateText(it->state));
                agentItem->setIcon(0, QIcon::fromTheme(QStringLiteral("system-run")));
                if (!it->transcriptPath.isEmpty()) {
                    agentItem->setToolTip(0, i18n("Transcript: %1", it->transcriptPath));
                }
            }

            for (auto it = subprocessesMap.constBegin(); it != subprocessesMap.constEnd(); ++it) {
                if (it->promptGroupId != round) {
                    continue;
                }
                QTreeWidgetItem *parent = roundItem ? roundItem : tree->invisibleRootItem();
                auto *procItem = new QTreeWidgetItem(parent);
                QString cmd = it->fullCommand;
                if (cmd.length() > 80) {
                    cmd = cmd.left(77) + QStringLiteral("...");
                }
                procItem->setText(0, cmd);
                QString status;
                switch (it->status) {
                case SubprocessInfo::Running:
                    status = i18n("Running");
                    break;
                case SubprocessInfo::Completed:
                    status = i18n("Exit %1", it->exitCode);
                    break;
                case SubprocessInfo::Failed:
                    status = i18n("Failed (%1)", it->exitCode);
                    break;
                default:
                    status = i18n("Unknown");
                    break;
                }
                procItem->setText(1, status);
                procItem->setIcon(0, QIcon::fromTheme(QStringLiteral("utilities-terminal")));
                procItem->setToolTip(0, it->fullCommand);
            }
        }
        tree->expandAll();
        tree->resizeColumnToContents(0);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void SessionManagerPanel::showSubagentDetails(const SubagentInfo &info)
{
    QString title = i18n("Agent Details \u2014 %1", info.agentType);
    if (!info.teammateName.isEmpty()) {
        title = i18n("Agent Details \u2014 %1 (%2)", info.agentType, info.teammateName);
    }

    QString stateStr;
    switch (info.state) {
    case ClaudeProcess::State::Working:
        stateStr = i18n("Working");
        break;
    case ClaudeProcess::State::Idle:
        stateStr = i18n("Idle");
        break;
    case ClaudeProcess::State::NotRunning:
        stateStr = i18n("Not Running");
        break;
    default:
        stateStr = i18n("Unknown");
        break;
    }

    QString details;
    details += QStringLiteral("<b>Agent Type:</b> %1<br>").arg(info.agentType.toHtmlEscaped());
    details += QStringLiteral("<b>Agent ID:</b> %1<br>").arg(info.agentId.toHtmlEscaped());
    if (!info.teammateName.isEmpty()) {
        details += QStringLiteral("<b>Teammate Name:</b> %1<br>").arg(info.teammateName.toHtmlEscaped());
    }
    details += QStringLiteral("<b>State:</b> %1<br>").arg(stateStr);
    if (info.startedAt.isValid()) {
        details += QStringLiteral("<b>Started:</b> %1<br>").arg(info.startedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        details += QStringLiteral("<b>Elapsed:</b> %1<br>").arg(formatElapsed(info.startedAt));
    }
    if (!info.currentTaskSubject.isEmpty()) {
        details += QStringLiteral("<b>Task:</b> %1<br>").arg(info.currentTaskSubject.toHtmlEscaped());
    }
    if (!info.transcriptPath.isEmpty()) {
        details += QStringLiteral("<b>Transcript:</b> %1<br>").arg(info.transcriptPath.toHtmlEscaped());
    }

    QMessageBox::information(this, title, details);
}

void SessionManagerPanel::showSubprocessOutput(const SubprocessInfo &info)
{
    QString title = i18n("Subprocess Output");

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(700, 500);

    auto *layout = new QVBoxLayout(dialog);

    // Header with command and status info
    QString statusStr;
    switch (info.status) {
    case SubprocessInfo::Running:
        statusStr = i18n("Running");
        break;
    case SubprocessInfo::Completed:
        statusStr = i18n("Completed (exit %1)", info.exitCode);
        break;
    case SubprocessInfo::Failed:
        statusStr = i18n("Failed (exit %1)", info.exitCode);
        break;
    }

    QString header;
    header += QStringLiteral("<b>Command:</b> %1<br>").arg(info.fullCommand.toHtmlEscaped());
    header += QStringLiteral("<b>Status:</b> %1<br>").arg(statusStr);
    if (info.pid > 0) {
        header += QStringLiteral("<b>PID:</b> %1<br>").arg(info.pid);
    }
    if (info.startedAt.isValid()) {
        header += QStringLiteral("<b>Started:</b> %1<br>").arg(info.startedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        header += QStringLiteral("<b>Elapsed:</b> %1<br>").arg(formatElapsed(info.startedAt));
    }
    if (info.resourceUsage.rssBytes > 0) {
        header += QStringLiteral("<b>Resources:</b> %1<br>").arg(info.resourceUsage.formatCompact());
    }

    auto *headerLabel = new QLabel(header);
    headerLabel->setTextFormat(Qt::RichText);
    headerLabel->setWordWrap(true);
    layout->addWidget(headerLabel);

    // Output text
    auto *outputEdit = new QPlainTextEdit();
    outputEdit->setReadOnly(true);
    outputEdit->setFont(QFont(QStringLiteral("monospace"), 9));
    outputEdit->setPlainText(info.output.isEmpty() ? i18n("(no output captured)") : info.output);
    layout->addWidget(outputEdit);

    // Copy button
    auto *copyButton = new QPushButton(i18n("Copy Output"));
    connect(copyButton, &QPushButton::clicked, dialog, [outputEdit]() {
        QApplication::clipboard()->setText(outputEdit->toPlainText());
    });
    layout->addWidget(copyButton);

    dialog->show();
}

void SessionManagerPanel::editSessionDescription(const QString &sessionId)
{
    // Get current description (from active session or empty)
    QString currentDesc;
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            currentDesc = session->taskDescription();
        }
    }

    // Show input dialog
    bool ok = false;
    QString newDesc =
        QInputDialog::getText(this, i18n("Set Session Description"), i18n("Description (shown in tabs and panel):"), QLineEdit::Normal, currentDesc, &ok);

    if (!ok) {
        return; // User cancelled
    }

    // Update active session if exists
    if (m_activeSessions.contains(sessionId)) {
        ClaudeSession *session = m_activeSessions[sessionId];
        if (session) {
            session->setTaskDescription(newDesc);
        }
    }

    // Update metadata for inactive sessions so the description persists
    if (m_metadata.contains(sessionId)) {
        m_metadata[sessionId].description = newDesc;
        scheduleMetadataSave();
    }

    // Refresh display
    scheduleTreeUpdate();
}

void SessionManagerPanel::editSessionBudget(ClaudeSession *session, const QString &sessionId)
{
    auto *bc = session->budgetController();
    if (!bc) {
        return;
    }

    auto &budget = bc->budget();
    auto &gate = bc->resourceGate();

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(i18n("Edit Budget — %1", session->sessionName()));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    auto *mainLayout = new QVBoxLayout(dlg);

    // --- Budget limits ---
    auto *budgetGroup = new QGroupBox(i18n("Budget Limits"), dlg);
    auto *grid = new QGridLayout(budgetGroup);

    grid->addWidget(new QLabel(i18n("Time limit (min):"), dlg), 0, 0);
    auto *timeSpin = new QSpinBox(dlg);
    timeSpin->setRange(0, 1440);
    timeSpin->setSpecialValueText(i18n("Unlimited"));
    timeSpin->setSuffix(i18n(" min"));
    timeSpin->setValue(budget.timeLimitMinutes);
    grid->addWidget(timeSpin, 0, 1);

    grid->addWidget(new QLabel(i18n("Cost ceiling ($):"), dlg), 1, 0);
    auto *costSpin = new QDoubleSpinBox(dlg);
    costSpin->setRange(0.0, 1000.0);
    costSpin->setDecimals(2);
    costSpin->setSingleStep(0.50);
    costSpin->setSpecialValueText(i18n("Unlimited"));
    costSpin->setPrefix(QStringLiteral("$"));
    costSpin->setValue(budget.costCeilingUSD);
    grid->addWidget(costSpin, 1, 1);

    grid->addWidget(new QLabel(i18n("Token ceiling (K):"), dlg), 2, 0);
    auto *tokenSpin = new QSpinBox(dlg);
    tokenSpin->setRange(0, 100000);
    tokenSpin->setSingleStep(100);
    tokenSpin->setSpecialValueText(i18n("Unlimited"));
    tokenSpin->setSuffix(QStringLiteral("K"));
    tokenSpin->setValue(static_cast<int>(budget.tokenCeiling / 1000));
    grid->addWidget(tokenSpin, 2, 1);

    mainLayout->addWidget(budgetGroup);

    // --- Resource gate ---
    auto *gateGroup = new QGroupBox(i18n("Resource Gate"), dlg);
    auto *gateGrid = new QGridLayout(gateGroup);

    gateGrid->addWidget(new QLabel(i18n("CPU threshold (%):"), dlg), 0, 0);
    auto *cpuSpin = new QDoubleSpinBox(dlg);
    cpuSpin->setRange(50.0, 100.0);
    cpuSpin->setDecimals(0);
    cpuSpin->setSingleStep(5.0);
    cpuSpin->setSuffix(QStringLiteral("%"));
    cpuSpin->setValue(gate.cpuThresholdPercent);
    gateGrid->addWidget(cpuSpin, 0, 1);

    gateGrid->addWidget(new QLabel(i18n("RAM threshold (GB):"), dlg), 1, 0);
    auto *ramSpin = new QDoubleSpinBox(dlg);
    ramSpin->setRange(0.0, 128.0);
    ramSpin->setDecimals(1);
    ramSpin->setSingleStep(1.0);
    ramSpin->setSpecialValueText(i18n("Auto (80%%)"));
    double ramGB = gate.rssThresholdBytes > 0 ? static_cast<double>(gate.rssThresholdBytes) / (1024.0 * 1024.0 * 1024.0) : 0.0;
    ramSpin->setValue(ramGB);
    gateGrid->addWidget(ramSpin, 1, 1);

    gateGrid->addWidget(new QLabel(i18n("Gate action:"), dlg), 2, 0);
    auto *actionCombo = new QComboBox(dlg);
    actionCombo->addItem(i18n("Pause Yolo"));
    actionCombo->addItem(i18n("Reduce Yolo"));
    actionCombo->addItem(i18n("Notify Only"));
    actionCombo->setCurrentIndex(static_cast<int>(gate.action));
    gateGrid->addWidget(actionCombo, 2, 1);

    mainLayout->addWidget(gateGroup);

    // --- Current usage summary ---
    const auto &usage = session->tokenUsage();
    QString usageSummary = QStringLiteral("Current: %1 tokens, $%2").arg(usage.formatCompact()).arg(usage.estimatedCostUSD(), 0, 'f', 2);
    if (budget.startedAt.isValid()) {
        usageSummary += QStringLiteral(", %1 min elapsed").arg(budget.elapsedMinutes());
    }
    auto *usageLabel = new QLabel(usageSummary, dlg);
    usageLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    mainLayout->addWidget(usageLabel);

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    QPointer<ClaudeSession> safeSession(session);
    connect(buttons, &QDialogButtonBox::accepted, dlg, [=]() {
        if (!safeSession) {
            dlg->reject();
            return;
        }
        auto *ctrl = safeSession->budgetController();

        // Apply budget limits
        SessionBudget newBudget = ctrl->budget();
        newBudget.timeLimitMinutes = timeSpin->value();
        newBudget.costCeilingUSD = costSpin->value();
        newBudget.tokenCeiling = static_cast<quint64>(tokenSpin->value()) * 1000;

        // Start the clock if a time limit is newly set
        if (newBudget.timeLimitMinutes > 0 && !newBudget.startedAt.isValid()) {
            newBudget.startedAt = QDateTime::currentDateTime();
        }

        // Clear exceeded flags when limits are raised/removed
        if (newBudget.costCeilingUSD == 0.0 || newBudget.costCeilingUSD > ctrl->budget().costCeilingUSD) {
            newBudget.costExceeded = false;
        }
        if (newBudget.tokenCeiling == 0 || newBudget.tokenCeiling > ctrl->budget().tokenCeiling) {
            newBudget.tokenExceeded = false;
        }
        if (newBudget.timeLimitMinutes == 0 || newBudget.timeLimitMinutes > ctrl->budget().timeLimitMinutes) {
            newBudget.timeExceeded = false;
        }

        ctrl->setBudget(newBudget);

        // Apply resource gate
        auto &g = ctrl->resourceGate();
        g.cpuThresholdPercent = cpuSpin->value();
        g.rssThresholdBytes = ramSpin->value() > 0.0 ? static_cast<quint64>(ramSpin->value() * 1024.0 * 1024.0 * 1024.0) : 0;
        g.action = static_cast<ResourceGate::Action>(actionCombo->currentIndex());

        // Persist budget limits in metadata
        if (m_metadata.contains(sessionId)) {
            m_metadata[sessionId].budgetTimeLimitMinutes = newBudget.timeLimitMinutes;
            m_metadata[sessionId].budgetCostCeilingUSD = newBudget.costCeilingUSD;
            m_metadata[sessionId].budgetTokenCeiling = newBudget.tokenCeiling;
            scheduleMetadataSave();
        }

        scheduleTreeUpdate();
        dlg->accept();
    });

    mainLayout->addWidget(buttons);
    dlg->resize(380, 360);
    dlg->show();
}

void SessionManagerPanel::updateSessionDescription(const QString &sessionId, const QString &desc)
{
    auto *meta = findMetadata(sessionId);
    if (!meta) {
        qWarning() << "SessionManagerPanel::updateSessionDescription: unknown sessionId" << sessionId;
        return;
    }
    meta->description = desc;
    scheduleMetadataSave();
    scheduleTreeUpdate();
}

void SessionManagerPanel::setSessionAgentId(const QString &sessionId, const QString &agentId)
{
    auto *meta = findMetadata(sessionId);
    if (!meta) {
        qWarning() << "SessionManagerPanel::setSessionAgentId: unknown sessionId" << sessionId;
        return;
    }
    meta->agentId = agentId;
    scheduleMetadataSave();
    scheduleTreeUpdate();
}

// --- Tree expansion state preservation ---

QString SessionManagerPanel::compositeKeyForItem(QTreeWidgetItem *item) const
{
    if (!item) {
        return {};
    }
    return item->data(0, Qt::UserRole + 6).toString();
}

void SessionManagerPanel::saveTreeState()
{
    m_expansionState.clear();
    m_savedSelectedKey.clear();
    m_savedScrollPosition = 0;

    if (!m_treeWidget) {
        return;
    }

    // Save scroll position
    if (m_treeWidget->verticalScrollBar()) {
        m_savedScrollPosition = m_treeWidget->verticalScrollBar()->value();
    }

    // Save selected item
    if (QTreeWidgetItem *sel = m_treeWidget->currentItem()) {
        m_savedSelectedKey = compositeKeyForItem(sel);
    }

    // Walk every tree item (including top-level project/category groups)
    // and capture its expansion state.
    auto walkItem = [this](QTreeWidgetItem *item, auto &&self) -> void {
        QString key = compositeKeyForItem(item);
        if (!key.isEmpty()) {
            m_expansionState[key] = item->isExpanded();
        }
        for (int i = 0; i < item->childCount(); ++i) {
            self(item->child(i), self);
        }
    };

    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        walkItem(m_treeWidget->topLevelItem(i), walkItem);
    }
}

void SessionManagerPanel::restoreTreeState()
{
    if (!m_treeWidget) {
        return;
    }

    // Restore selection
    if (!m_savedSelectedKey.isEmpty()) {
        bool found = false;
        auto walkRestore = [this, &found](QTreeWidgetItem *item, auto &&self) -> void {
            if (found) {
                return;
            }
            if (compositeKeyForItem(item) == m_savedSelectedKey) {
                m_treeWidget->setCurrentItem(item);
                found = true;
                return;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                self(item->child(i), self);
            }
        };
        for (int i = 0; i < m_treeWidget->topLevelItemCount() && !found; ++i) {
            walkRestore(m_treeWidget->topLevelItem(i), walkRestore);
        }
    }

    // Restore scroll position after layout has settled
    int savedScroll = m_savedScrollPosition;
    QTimer::singleShot(0, this, [this, savedScroll]() {
        if (m_treeWidget && m_treeWidget->verticalScrollBar()) {
            m_treeWidget->verticalScrollBar()->setValue(savedScroll);
        }
    });
}

bool SessionManagerPanel::shouldAutoExpand(const QString &key, const QString &sessionId, bool hasActiveChildren) const
{
    // Muted sessions are always collapsed
    if (m_mutedSessions.contains(sessionId)) {
        return false;
    }

    // Known item → restore saved state
    if (m_knownItems.contains(key)) {
        auto it = m_expansionState.constFind(key);
        if (it != m_expansionState.constEnd()) {
            return it.value();
        }
        // Known but somehow missing from expansion state — default collapsed
        return false;
    }

    // New item → auto-expand if it has active children
    return hasActiveChildren;
}

void SessionManagerPanel::pruneStaleKeys()
{
    // Collect valid session IDs
    QSet<QString> validIds;
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        validIds.insert(it.key());
    }

    // Helper: extract session ID from composite key "prefix:sessionId" or "prefix:sessionId:extra"
    auto extractSessionId = [](const QString &key) -> QString {
        int first = key.indexOf(QLatin1Char(':'));
        if (first < 0) {
            return {};
        }
        int second = key.indexOf(QLatin1Char(':'), first + 1);
        if (second > 0) {
            return key.mid(first + 1, second - first - 1);
        }
        return key.mid(first + 1);
    };

    auto pruneSet = [&](QSet<QString> &set) {
        auto it = set.begin();
        while (it != set.end()) {
            QString sid = extractSessionId(*it);
            if (!sid.isEmpty() && !validIds.contains(sid)) {
                it = set.erase(it);
            } else {
                ++it;
            }
        }
    };

    auto pruneHash = [&](QHash<QString, bool> &hash) {
        auto it = hash.begin();
        while (it != hash.end()) {
            QString sid = extractSessionId(it.key());
            if (!sid.isEmpty() && !validIds.contains(sid)) {
                it = hash.erase(it);
            } else {
                ++it;
            }
        }
    };

    pruneSet(m_knownItems);
    pruneHash(m_expansionState);
    m_mutedSessions.intersect(validIds);
}

bool SessionManagerPanel::isTreeInteractionActive() const
{
    if (!m_treeWidget || !m_treeWidget->isVisible()) {
        return false;
    }
    return m_treeWidget->underMouse() || m_treeWidget->hasFocus();
}

bool SessionManagerPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Filter box Esc: clear text and shift focus back to the tree.  Without
    // this, Esc in the QLineEdit is silently swallowed and the user has to
    // reach for the mouse to clear a stale filter.
    if (m_filterEdit && watched == m_filterEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (!m_filterEdit->text().isEmpty()) {
                m_filterEdit->clear();
            }
            if (m_treeWidget) {
                m_treeWidget->setFocus();
            }
            return true;
        }
    }

    if (m_treeWidget && (watched == m_treeWidget || watched == m_treeWidget->viewport())) {
        if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut) {
            if (m_pendingUpdate && !isTreeInteractionActive()) {
                m_pendingUpdate = false;
                scheduleTreeUpdate();
            }
        }

        // Handle right-click directly via MouseButtonRelease on viewport.
        // customContextMenuRequested doesn't fire reliably for items under
        // group headers (depth > 1) on Qt6/Wayland.
        if (watched == m_treeWidget->viewport() && event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::RightButton) {
                onContextMenu(me->pos());
                return true;
            }
        }

        // F2 renames the currently-selected category header. We intentionally
        // only handle F2 for category items — session/project-group items keep
        // whatever default behavior QTreeWidget applies (currently none, but
        // future features may bind F2 there).
        if (watched == m_treeWidget && event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_F2 && !ke->modifiers().testFlag(Qt::ControlModifier)) {
                QTreeWidgetItem *sel = m_treeWidget->currentItem();
                if (sel) {
                    const QString key = sel->data(0, Qt::UserRole + 6).toString();
                    if (key.startsWith(QStringLiteral("category:"))) {
                        const QString catKey = key.mid(QStringLiteral("category:").size());
                        renameCategory(catKey);
                        return true;
                    }
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

// ============================================================
// Feature 3 — Consolidate Duplicates dialog
// ============================================================

void SessionManagerPanel::openConsolidateDialog(const QString &projectKey)
{
    // Gather ALL sessions in this project (any state).
    QList<SessionMetadata> candidates;
    QStringList sessionIds;
    QStringList resumeIds;
    for (auto it = m_metadata.cbegin(); it != m_metadata.cend(); ++it) {
        if (it.value().workingDirectory == projectKey) {
            candidates.append(it.value());
            sessionIds.append(it.key());
            if (!it.value().lastResumeSessionId.isEmpty()) {
                resumeIds.append(it.value().lastResumeSessionId);
            }
        }
    }
    if (candidates.size() < 2) {
        return;
    }

    const QHash<QString, qint64> jsonlSizes = jsonlSizesForResumeIds(projectKey, resumeIds);
    MergeSessionsDialog dlg(candidates, jsonlSizes, this);
    if (dlg.exec() == QDialog::Accepted) {
        mergeSessions(sessionIds, dlg.primarySessionId(), dlg.choices());
    }
}

bool SessionManagerPanel::canOfferConsolidateForProject(const QString &workingDirectory) const
{
    if (workingDirectory.isEmpty()) {
        return false;
    }
    int count = 0;
    for (auto it = m_metadata.cbegin(); it != m_metadata.cend(); ++it) {
        if (it.value().workingDirectory == workingDirectory) {
            ++count;
            if (count >= 2) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================
// Feature 2 — Ungroup a category
// ============================================================

void SessionManagerPanel::ungroupCategory(const QString &categoryKey)
{
    if (categoryKey.isEmpty()) {
        return;
    }
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    // 1) If any alias targets this category, remove those aliases.
    //    (e.g. cowardly-irregular=cowir → removing cowardly-irregular alias
    //    dissolves the "cowir" bucket back to its LCP-native "cowardly-irregular".)
    const QHash<QString, QString> aliases = settings->categoryAliases();
    bool aliasRemoved = false;
    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        if (it.value() == categoryKey) {
            settings->removeCategoryAlias(it.key());
            aliasRemoved = true;
        }
    }
    if (aliasRemoved) {
        scheduleTreeUpdate();
        return;
    }

    // 2) Otherwise, if any workdir-override points at this category, remove those.
    const QHash<QString, QString> overrides = settings->workdirCategoryOverrides();
    bool overrideRemoved = false;
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.value() == categoryKey) {
            settings->removeWorkdirCategoryOverride(it.key());
            overrideRemoved = true;
        }
    }
    if (overrideRemoved) {
        scheduleTreeUpdate();
        return;
    }

    // 3) Otherwise it was an LCP-derived category — suppress it.
    settings->addSuppressedCategory(categoryKey);
    scheduleTreeUpdate();
}

// ============================================================
// Feature 1 — Drop handler
// ============================================================

namespace
{

// Build a human-readable label for a source composite key. Categories show
// their raw name; groups show the workdir basename (falling back to full path).
QString labelForSourceKey(const QString &sourceKey)
{
    if (sourceKey.startsWith(QStringLiteral("category:"))) {
        return sourceKey.mid(QStringLiteral("category:").size());
    }
    if (sourceKey.startsWith(QStringLiteral("group:"))) {
        const QString wd = sourceKey.mid(QStringLiteral("group:").size());
        const QString base = QDir(wd).dirName();
        return base.isEmpty() ? wd : base;
    }
    return sourceKey;
}

} // namespace

void SessionManagerPanel::handleDropRequest(const QStringList &sourceKeys, const QString &targetCategoryKey)
{
    if (sourceKeys.isEmpty() || !targetCategoryKey.startsWith(QStringLiteral("category:"))) {
        return;
    }
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    const QString targetCat = targetCategoryKey.mid(QStringLiteral("category:").size());
    if (targetCat.isEmpty()) {
        return;
    }

    // Pre-filter — drop keys that would collide with the target itself (a
    // category renaming itself into its own bucket is a no-op). Also drop
    // unknown source-type strings so a corrupt payload can't crash us.
    QStringList validSources;
    validSources.reserve(sourceKeys.size());
    for (const QString &k : sourceKeys) {
        if (!k.startsWith(QStringLiteral("group:")) && !k.startsWith(QStringLiteral("category:"))) {
            continue;
        }
        if (k == targetCategoryKey) {
            continue;
        }
        // A category source that IS the target category is a self-drop.
        if (k.startsWith(QStringLiteral("category:")) && k.mid(QStringLiteral("category:").size()) == targetCat) {
            continue;
        }
        validSources << k;
    }
    if (validSources.isEmpty()) {
        return;
    }

    // Build the confirmation text: single-source uses the labeled form; multi
    // uses the counted form. Users still see one prompt for the whole batch.
    const QString targetLabel = targetCat;
    const QString confirmText = validSources.size() == 1
        ? i18n("Move \"%1\" under \"%2\"?\n\nFuture sessions matching this rule will land here too.", labelForSourceKey(validSources.first()), targetLabel)
        : i18n("Move %1 items under \"%2\"?\n\nFuture sessions matching these rules will land here too.", validSources.size(), targetLabel);
    const QString titleText =
        validSources.size() == 1 ? i18n("Move %1?", labelForSourceKey(validSources.first())) : i18n("Move %1 items?", validSources.size());

    const int ret = QMessageBox::question(this, titleText, confirmText, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    // Block signalsChanged storms during the batch — KonsolaiSettings emits
    // settingsChanged() from each mutator, and downstream slots trigger a
    // tree rebuild each time. We want a single rebuild at the end.
    settings->blockSignals(true);
    for (const QString &k : validSources) {
        if (k.startsWith(QStringLiteral("category:"))) {
            const QString sourceCat = k.mid(QStringLiteral("category:").size());
            settings->addCategoryAlias(sourceCat, targetCat);
        } else if (k.startsWith(QStringLiteral("group:"))) {
            const QString workdir = k.mid(QStringLiteral("group:").size());
            settings->addWorkdirCategoryOverride(workdir, targetCat);
        }
    }
    // Clear any pre-existing suppression that would fight the new rule.
    settings->removeSuppressedCategory(targetCat);
    settings->blockSignals(false);

    scheduleTreeUpdate();
}

// ============================================================
// Feature — Vim hotkey dispatch + subagent classification
// ============================================================

void SessionManagerPanel::handleTreeAction(const QString &action)
{
    if (!m_treeWidget) {
        return;
    }
    QTreeWidgetItem *current = m_treeWidget->currentItem();
    if (!current) {
        return;
    }
    const QString sessionId = current->data(0, Qt::UserRole).toString();
    const QString compositeKey = current->data(0, Qt::UserRole + 6).toString();
    const bool isSession = compositeKey.startsWith(QStringLiteral("s:"));
    const bool isCategory = compositeKey.startsWith(QStringLiteral("category:"));

    if (action == QLatin1String("attach")) {
        // Same effect as double-click on a session leaf.  For categories/
        // groups we no-op (Enter-on-category is handled by the widget's own
        // expand toggle before it emits actionRequested).
        if (isSession && !sessionId.isEmpty()) {
            onItemDoubleClicked(current, 0);
        }
        return;
    }
    if (action == QLatin1String("archive")) {
        if (isSession && !sessionId.isEmpty()) {
            archiveSession(sessionId);
        }
        return;
    }
    if (action == QLatin1String("pin")) {
        if (isSession && !sessionId.isEmpty()) {
            const SessionMetadata *m = findMetadata(sessionId);
            if (m && m->isPinned) {
                unpinSession(sessionId);
            } else if (m) {
                pinSession(sessionId);
            }
        }
        return;
    }
    if (action == QLatin1String("close")) {
        if (isSession && !sessionId.isEmpty()) {
            closeSession(sessionId);
        }
        return;
    }
    if (action == QLatin1String("dismiss")) {
        if (isSession && !sessionId.isEmpty()) {
            dismissSession(sessionId);
        }
        return;
    }
    if (action == QLatin1String("rename")) {
        if (isCategory) {
            renameCategory(compositeKey.mid(QStringLiteral("category:").size()));
        } else if (isSession && !sessionId.isEmpty()) {
            editSessionDescription(sessionId);
        }
        return;
    }
    if (action == QLatin1String("new-category")) {
        createUserCategory();
        return;
    }
    // Unknown action: no-op (defensive — future actions might arrive from
    // a newer SessionTreeWidget subclass).
}

bool SessionManagerPanel::isSubagentSession(const SessionMetadata &meta) const
{
    // 1. Explicit metadata flag — most reliable (future producer will set it
    // during subagent-spawn plumbing).  No cache consult; the flag is
    // authoritative.
    if (meta.isSubagent) {
        return true;
    }

    // 2. sessionName heuristic — cheap in-memory check.  Konsolai's own
    // agent-fleet-launched sessions use "konsolai-...-agent-<16-hex>".
    // Use a static QRegularExpression to avoid recompiling per call.
    static const QRegularExpression agentNamePattern(QStringLiteral("agent-[a-f0-9]{16}"));
    if (agentNamePattern.match(meta.sessionName).hasMatch()) {
        return true;
    }

    // 3. jsonl-path heuristic — hits disk; cache by sessionId so repeated
    // filter checks stay cheap.  Cache is invalidated implicitly when
    // metadata changes via metadata-mutating slots (scheduleMetadataSave).
    if (meta.sessionId.isEmpty()) {
        return false;
    }
    auto cachedIt = m_subagentClassificationCache.constFind(meta.sessionId);
    if (cachedIt != m_subagentClassificationCache.constEnd()) {
        return cachedIt.value();
    }

    bool result = false;
    if (!meta.workingDirectory.isEmpty()) {
        const QString hashed = ClaudeSessionRegistry::hashedProjectPath(meta.workingDirectory);
        const QString projectDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashed;
        QDir dir(projectDir);
        if (dir.exists()) {
            // Find newest .jsonl by mtime and inspect its PATH for
            // /subagents/ — Claude Code stores subagent transcripts under
            // .../subagents/agent-<id>.jsonl beneath the parent.
            QFileInfoList jsonls;
            QDirIterator it(dir.absolutePath(), {QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                jsonls.append(it.fileInfo());
            }
            if (!jsonls.isEmpty()) {
                std::sort(jsonls.begin(), jsonls.end(), [](const QFileInfo &a, const QFileInfo &b) {
                    return a.lastModified() > b.lastModified();
                });
                const QString newestPath = jsonls.first().absoluteFilePath();
                if (newestPath.contains(QStringLiteral("/subagents/"))) {
                    result = true;
                }
            }
        }
    }
    m_subagentClassificationCache.insert(meta.sessionId, result);
    return result;
}

// ============================================================
// Feature — Create a new user-defined empty category
// ============================================================

void SessionManagerPanel::createUserCategory()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n("New Category"), i18n("Category name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }

    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    // Reject if the name collides with an existing category bucket in the
    // current tree — the user would be confused if their "new" category
    // silently merged into an existing one.
    if (m_categoryGroups.contains(name)) {
        QMessageBox::information(this, i18n("Category exists"), i18n("A category named \"%1\" already exists.", name));
        return;
    }
    settings->addUserCategory(name);
    scheduleTreeUpdate();
}

// ============================================================
// Feature — Rename a category (uses CategoryAliases)
// ============================================================

void SessionManagerPanel::renameCategory(const QString &oldKey)
{
    if (oldKey.isEmpty()) {
        return;
    }
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, i18n("Rename Category"), i18n("New name for \"%1\":", oldKey), QLineEdit::Normal, oldKey, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldKey) {
        return;
    }

    // If the new name collides with an existing category bucket, prompt the
    // user — proceeding will silently merge the two.
    if (m_categoryGroups.contains(newName)) {
        const int ret = QMessageBox::question(this,
                                              i18n("Category exists"),
                                              i18n("A category named \"%1\" already exists — projects would merge into it. Rename anyway?", newName),
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }

    settings->addCategoryAlias(oldKey, newName);
    // If the rename source is itself a user-defined empty category, also drop
    // that entry so the two categories don't coexist in the settings state.
    // (The alias handles projects; removeUserCategory prevents a leftover
    // empty bucket at top level.)
    settings->removeUserCategory(oldKey);
    // If the OLD name was in the suppress list, the alias may not fire; wipe
    // any suppression on both sides so the tree reflects the intent.
    settings->removeSuppressedCategory(oldKey);
    scheduleTreeUpdate();
}

// ============================================================
// Feature — LLM-assisted tree reorganization
// ============================================================

TreeInventory SessionManagerPanel::buildTreeInventory() const
{
    TreeInventory inv;

    // 1) Group metadata by working directory to compute per-project session counts.
    QHash<QString, int> countsByWorkdir;
    QHash<QString, QString> descriptionByWorkdir;
    for (auto it = m_metadata.cbegin(); it != m_metadata.cend(); ++it) {
        const SessionMetadata &m = it.value();
        if (m.isDismissed) {
            continue;
        }
        if (m.workingDirectory.isEmpty()) {
            continue;
        }
        countsByWorkdir[m.workingDirectory] += 1;
        // Prefer the first non-empty description we come across for that workdir.
        if (!descriptionByWorkdir.contains(m.workingDirectory) && !m.description.trimmed().isEmpty()) {
            descriptionByWorkdir.insert(m.workingDirectory, m.description.trimmed());
        }
    }

    // 2) Project list — sorted for prompt stability.
    QStringList workdirs = countsByWorkdir.keys();
    std::sort(workdirs.begin(), workdirs.end());
    for (const QString &wd : workdirs) {
        TreeInventory::Project p;
        p.workingDirectory = wd;
        p.basename = QDir(wd).dirName();
        p.description = descriptionByWorkdir.value(wd);
        p.sessionCount = countsByWorkdir.value(wd);
        inv.projects.append(p);
    }

    // 3) Categories — invert m_categoryMap (workdir → catKey) into catKey → [workdirs].
    QHash<QString, QStringList> byCategory;
    for (auto it = m_categoryMap.cbegin(); it != m_categoryMap.cend(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
        byCategory[it.value()].append(it.key());
    }
    QStringList catKeys = byCategory.keys();
    std::sort(catKeys.begin(), catKeys.end());
    for (const QString &k : catKeys) {
        TreeInventory::Category c;
        c.key = k;
        c.projectWorkdirs = byCategory.value(k);
        std::sort(c.projectWorkdirs.begin(), c.projectWorkdirs.end());
        inv.categories.append(c);
    }

    // 4) Settings-backed metadata.
    if (auto *settings = KonsolaiSettings::instance()) {
        inv.userCategories = settings->userCategories();
        inv.existingAliases = settings->categoryAliases();
        inv.existingWorkdirOverrides = settings->workdirCategoryOverrides();
        inv.existingSuppressedCategories = settings->suppressedCategories();
    }

    return inv;
}

void SessionManagerPanel::applyReorganizeProposal(const ReorganizeProposal &proposal)
{
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }
    if (proposal.isEmpty()) {
        return;
    }

    // Coalesce settingsChanged emissions across the batch so the tree rebuild
    // fires once at the end.
    settings->blockSignals(true);
    for (auto it = proposal.categoryAliases.cbegin(); it != proposal.categoryAliases.cend(); ++it) {
        settings->addCategoryAlias(it.key(), it.value());
    }
    for (auto it = proposal.workdirOverrides.cbegin(); it != proposal.workdirOverrides.cend(); ++it) {
        settings->addWorkdirCategoryOverride(it.key(), it.value());
    }
    for (const QString &s : proposal.suppressedCategories) {
        settings->addSuppressedCategory(s);
    }
    for (const QString &u : proposal.userCategories) {
        settings->addUserCategory(u);
    }
    settings->blockSignals(false);

    scheduleTreeUpdate();
}

void SessionManagerPanel::openReorganizeTreeDialog()
{
    if (!ClaudeAssistant::claudeExecutablePath().isEmpty()) {
        // OK — CLI available.
    } else {
        QMessageBox::warning(this,
                             i18n("Claude CLI not found"),
                             i18n("The 'claude' command-line tool is not on your PATH. Install it to use "
                                  "LLM-assisted tree reorganization."));
        return;
    }

    const TreeInventory inv = buildTreeInventory();
    ReorganizeTreeDialog dlg(inv, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    applyReorganizeProposal(dlg.proposal());
}

void SessionManagerPanel::suggestCategoryName(const QStringList &workdirs)
{
    if (workdirs.isEmpty()) {
        return;
    }
    if (ClaudeAssistant::claudeExecutablePath().isEmpty()) {
        QMessageBox::warning(this,
                             i18n("Claude CLI not found"),
                             i18n("The 'claude' command-line tool is not on your PATH. Install it to use "
                                  "LLM-assisted name suggestions."));
        return;
    }

    // Collect basenames + descriptions for the prompt.
    QStringList basenames;
    QStringList descriptions;
    QSet<QString> seenWd;
    QString existingCategoryKey;
    bool sharedCategory = true;
    for (const QString &wd : workdirs) {
        if (wd.isEmpty() || seenWd.contains(wd)) {
            continue;
        }
        seenWd.insert(wd);
        basenames << QDir(wd).dirName();

        // Pick the first non-empty description across sessions for this workdir.
        QString desc;
        for (auto mit = m_metadata.cbegin(); mit != m_metadata.cend(); ++mit) {
            if (mit->workingDirectory == wd && !mit->description.trimmed().isEmpty()) {
                desc = mit->description.trimmed();
                break;
            }
        }
        descriptions << desc;

        // Track shared category (used to decide alias vs. workdir-overrides).
        const QString cat = m_categoryMap.value(wd);
        if (existingCategoryKey.isEmpty()) {
            existingCategoryKey = cat;
        } else if (cat != existingCategoryKey) {
            sharedCategory = false;
        }
    }
    if (basenames.isEmpty()) {
        return;
    }

    const QString prompt = buildSuggestNamePrompt(basenames, descriptions);

    // Set up a modal wait dialog around the async assistant call.
    QProgressDialog progress(i18n("Asking Claude for a name suggestion…"), i18n("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    ClaudeAssistant assistant(this);
    QString suggested;
    QString failureMsg;
    bool doneFlag = false;

    connect(&assistant, &ClaudeAssistant::finished, this, [&](const QString &output, int exitCode) {
        if (exitCode == 0) {
            suggested = parseSuggestNameResponse(output);
        } else {
            failureMsg = i18n("Claude returned exit code %1", exitCode);
        }
        doneFlag = true;
        progress.reset();
    });
    connect(&assistant, &ClaudeAssistant::failed, this, [&](const QString &err) {
        failureMsg = err;
        doneFlag = true;
        progress.reset();
    });
    connect(&progress, &QProgressDialog::canceled, this, [&]() {
        assistant.cancel();
        doneFlag = true;
    });

    assistant.ask(prompt, /*jsonOutput=*/false);
    while (!doneFlag && !progress.wasCanceled()) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    if (progress.wasCanceled() && suggested.isEmpty()) {
        return;
    }
    if (!failureMsg.isEmpty()) {
        QMessageBox::warning(this, i18n("Claude request failed"), i18n("Claude could not suggest a name: %1", failureMsg));
        return;
    }
    if (suggested.isEmpty()) {
        QMessageBox::information(this, i18n("No suggestion"), i18n("Claude did not return a usable name."));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n("Suggested Category Name"), i18n("Claude suggests:"), QLineEdit::Normal, suggested, &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }

    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    settings->blockSignals(true);
    if (sharedCategory && !existingCategoryKey.isEmpty() && existingCategoryKey != name) {
        // All workdirs share a category — treat as a rename.
        settings->addCategoryAlias(existingCategoryKey, name);
        settings->removeUserCategory(existingCategoryKey);
        settings->removeSuppressedCategory(existingCategoryKey);
    } else {
        // Route each workdir explicitly into the new category.
        for (const QString &wd : seenWd) {
            settings->addWorkdirCategoryOverride(wd, name);
        }
        settings->addUserCategory(name);
    }
    settings->blockSignals(false);

    scheduleTreeUpdate();
}

} // namespace Konsolai

#include "moc_SessionManagerPanel.cpp"
