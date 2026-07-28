/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionWizard.h"
#include "ClaudeConversationPicker.h"
#include "ClaudeSessionRegistry.h"
#include "KonsolaiSettings.h"
#include "TmuxManager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStringListModel>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace Konsolai
{

ClaudeSessionWizard::ClaudeSessionWizard(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("New Claude Session"));
    setMinimumSize(600, 620);
    resize(650, 680);

    setupUi();

    // Load defaults from settings
    if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
        m_projectRootEdit->setText(settings->projectRoot());
        m_gitRemoteEdit->setText(settings->gitRemoteRoot());

        int gitMode = settings->gitMode();
        if (gitMode >= 0 && gitMode <= GitNone) {
            m_gitModeCombo->setCurrentIndex(gitMode);
        }
        updateGitSubFields();

        QString sourceRepo = settings->worktreeSourceRepo();
        if (!sourceRepo.isEmpty()) {
            m_sourceRepoEdit->setText(sourceRepo);
        }

        QString model = settings->defaultModel();
        int idx = m_modelCombo->findText(model);
        if (idx >= 0) {
            m_modelCombo->setCurrentIndex(idx);
        }

        // Load budget defaults
        m_timeLimitSpin->setValue(settings->defaultTimeLimitMinutes());
        m_costCeilingSpin->setValue(settings->defaultCostCeilingUSD());
        m_tokenCeilingSpin->setValue(static_cast<int>(settings->defaultTokenCeiling() / 1000));
        int budgetPol = settings->defaultBudgetPolicy();
        if (budgetPol >= 0 && budgetPol <= 1) {
            m_budgetPolicyCombo->setCurrentIndex(budgetPol);
        }
        // Show budget group as checked if any limit is set
        if (settings->defaultTimeLimitMinutes() > 0 || settings->defaultCostCeilingUSD() > 0.0 || settings->defaultTokenCeiling() > 0) {
            m_budgetGroup->setChecked(true);
        }
    }

    // Populate folder completer from workspace root
    updateFolderCompleter();

    // Load SSH config hosts
    loadSshConfigHosts();

    // Focus the prompt field
    m_promptEdit->setFocus();
}

ClaudeSessionWizard::~ClaudeSessionWizard() = default;

void ClaudeSessionWizard::setProfile(const Konsole::Profile::Ptr &profile)
{
    m_profile = profile;
}

void ClaudeSessionWizard::setDefaultDirectory(const QString &path)
{
    m_defaultDirectory = path;
}

void ClaudeSessionWizard::setWorktreeSource(const QString &sourceWorkingDir)
{
    // Set the project root to the parent of the source session's working dir
    m_defaultDirectory = QFileInfo(sourceWorkingDir).absolutePath();
    if (m_projectRootEdit) {
        m_projectRootEdit->setText(m_defaultDirectory);
    }

    // Switch git mode to Worktree
    if (m_gitModeCombo) {
        m_gitModeCombo->setCurrentIndex(GitWorktree);
    }

    // Fill the source repo field
    if (m_sourceRepoEdit) {
        m_sourceRepoEdit->setText(sourceWorkingDir);
    }

    // Detect git state for the source dir
    detectGitState(sourceWorkingDir);

    // Focus the prompt field — user types what they want to do
    if (m_promptEdit) {
        m_promptEdit->setFocus();
        m_promptEdit->setPlaceholderText(i18n("Describe the task for this worktree branch..."));
    }
}

QString ClaudeSessionWizard::selectedDirectory() const
{
    if (m_useExistingDir) {
        return m_selectedDirectory;
    }

    QString root = m_projectRootEdit->text();
    QString folder = m_folderNameEdit->text();

    if (root.isEmpty() || folder.isEmpty()) {
        return QString();
    }

    return QDir(root).filePath(folder);
}

bool ClaudeSessionWizard::shouldInitGit() const
{
    return m_gitModeCombo && m_gitModeCombo->currentIndex() == GitInit;
}

QString ClaudeSessionWizard::worktreeBranch() const
{
    // Worktrees are local-only — never for remote SSH sessions
    if (isRemoteSession()) {
        return QString();
    }
    if (!m_gitModeCombo || m_gitModeCombo->currentIndex() != GitWorktree) {
        return QString();
    }
    if (!m_worktreeNameEdit || m_worktreeNameEdit->text().isEmpty()) {
        return QString();
    }
    return m_worktreeNameEdit->text();
}

QString ClaudeSessionWizard::repoRoot() const
{
    if (m_gitModeCombo && m_gitModeCombo->currentIndex() == GitWorktree && m_sourceRepoEdit) {
        QString repo = m_sourceRepoEdit->text();
        // Fall back to workspace root if source repo is empty
        if (repo.isEmpty() && m_projectRootEdit) {
            return m_projectRootEdit->text();
        }
        return repo;
    }
    return m_repoRoot;
}

QString ClaudeSessionWizard::claudeModel() const
{
    if (m_modelCombo) {
        return m_modelCombo->currentText();
    }
    return QStringLiteral("claude-opus-5[1m]");
}

void ClaudeSessionWizard::populateModelCombo()
{
    if (!m_modelCombo) {
        return;
    }

    // Rebuilding fires currentIndexChanged; block it so refilling the list
    // can't recurse back through the agent-change handler.
    QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();

    if (agentKind() == ClaudeSession::AgentKind::Codex) {
        // Codex slugs, newest first. gpt-5.6-sol is the configured default.
        m_modelCombo->addItem(QStringLiteral("gpt-5.6-sol"));
        m_modelCombo->addItem(QStringLiteral("gpt-5.6-terra"));
        m_modelCombo->addItem(QStringLiteral("gpt-5.6-luna"));
        m_modelCombo->addItem(QStringLiteral("gpt-5.5"));
        m_modelCombo->addItem(QStringLiteral("gpt-5.4"));
        if (auto *settings = KonsolaiSettings::instance()) {
            const int idx = m_modelCombo->findText(settings->codexModel());
            if (idx >= 0) {
                m_modelCombo->setCurrentIndex(idx);
            }
        }
        return;
    }

    m_modelCombo->addItem(QStringLiteral("claude-opus-5[1m]"));
    m_modelCombo->addItem(QStringLiteral("claude-sonnet-4"));
    m_modelCombo->addItem(QStringLiteral("claude-opus-4"));
    m_modelCombo->addItem(QStringLiteral("claude-haiku"));
    m_modelCombo->addItem(QStringLiteral("claude-fable-5"));
    if (auto *settings = KonsolaiSettings::instance()) {
        const int idx = m_modelCombo->findText(settings->defaultModel());
        if (idx >= 0) {
            m_modelCombo->setCurrentIndex(idx);
        }
    }
}

ClaudeSession::AgentKind ClaudeSessionWizard::agentKind() const
{
    if (m_agentCombo) {
        return static_cast<ClaudeSession::AgentKind>(m_agentCombo->currentData().toInt());
    }
    return ClaudeSession::AgentKind::Claude;
}

bool ClaudeSessionWizard::autoApproveRead() const
{
    return m_autoApproveReadCheck && m_autoApproveReadCheck->isChecked();
}

QString ClaudeSessionWizard::claudeArgs() const
{
    return m_extraArgsEdit ? m_extraArgsEdit->text().trimmed() : QString();
}

QString ClaudeSessionWizard::taskPrompt() const
{
    return m_taskPrompt;
}

QString ClaudeSessionWizard::resumeSessionId() const
{
    return m_resumeSessionId;
}

QString ClaudeSessionWizard::selectedTmuxSession() const
{
    return m_selectedTmuxSession;
}

bool ClaudeSessionWizard::isRemoteSession() const
{
    return m_remoteRadio && m_remoteRadio->isChecked();
}

QString ClaudeSessionWizard::sshHost() const
{
    if (!isRemoteSession()) {
        return QString();
    }
    if (m_useSshConfigCheck && m_useSshConfigCheck->isChecked() && m_sshConfigCombo) {
        return m_sshConfigCombo->currentText();
    }
    return m_sshHostEdit ? m_sshHostEdit->text() : QString();
}

QString ClaudeSessionWizard::sshUsername() const
{
    if (!isRemoteSession() || !m_sshUsernameEdit) {
        return QString();
    }
    return m_sshUsernameEdit->text();
}

int ClaudeSessionWizard::sshPort() const
{
    if (!isRemoteSession() || !m_sshPortEdit) {
        return 22;
    }
    bool ok = false;
    int port = m_sshPortEdit->text().toInt(&ok);
    return ok ? port : 22;
}

QString ClaudeSessionWizard::sshConfigEntry() const
{
    if (!isRemoteSession() || !m_useSshConfigCheck || !m_useSshConfigCheck->isChecked()) {
        return QString();
    }
    return m_sshConfigCombo ? m_sshConfigCombo->currentText() : QString();
}

int ClaudeSessionWizard::budgetTimeLimitMinutes() const
{
    if (!m_budgetGroup || !m_budgetGroup->isChecked()) {
        return 0;
    }
    return m_timeLimitSpin ? m_timeLimitSpin->value() : 0;
}

double ClaudeSessionWizard::budgetCostCeilingUSD() const
{
    if (!m_budgetGroup || !m_budgetGroup->isChecked()) {
        return 0.0;
    }
    return m_costCeilingSpin ? m_costCeilingSpin->value() : 0.0;
}

quint64 ClaudeSessionWizard::budgetTokenCeiling() const
{
    if (!m_budgetGroup || !m_budgetGroup->isChecked()) {
        return 0;
    }
    // Value is in K (thousands), convert to raw tokens
    return m_tokenCeilingSpin ? static_cast<quint64>(m_tokenCeilingSpin->value()) * 1000 : 0;
}

int ClaudeSessionWizard::budgetPolicy() const
{
    if (!m_budgetGroup || !m_budgetGroup->isChecked()) {
        return 0; // Soft
    }
    return m_budgetPolicyCombo ? m_budgetPolicyCombo->currentIndex() : 0;
}

void ClaudeSessionWizard::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Location: Local / Remote (SSH) ---
    auto *locationRow = new QHBoxLayout();
    locationRow->addWidget(new QLabel(i18n("Location:"), this));
    m_locationGroup = new QButtonGroup(this);
    m_localRadio = new QRadioButton(i18n("Local"), this);
    m_remoteRadio = new QRadioButton(i18n("Remote (SSH)"), this);
    m_localRadio->setChecked(true);
    m_locationGroup->addButton(m_localRadio, 0);
    m_locationGroup->addButton(m_remoteRadio, 1);
    connect(m_locationGroup, &QButtonGroup::idClicked, this, &ClaudeSessionWizard::onLocationChanged);
    locationRow->addWidget(m_localRadio);
    locationRow->addWidget(m_remoteRadio);
    locationRow->addStretch();
    mainLayout->addLayout(locationRow);

    mainLayout->addSpacing(4);

    // --- SSH Connection group (visible when Remote is selected) ---
    m_sshGroup = new QGroupBox(i18n("SSH Connection"), this);
    auto *sshLayout = new QGridLayout(m_sshGroup);

    // Pre-fill from last-used SSH settings
    QString lastHost, lastUser;
    int lastPort = 22;
    if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
        lastHost = settings->lastSshHost();
        lastUser = settings->lastSshUsername();
        lastPort = settings->lastSshPort();
    }

    // Host
    sshLayout->addWidget(new QLabel(i18n("Host:"), this), 0, 0);
    m_sshHostEdit = new QLineEdit(this);
    m_sshHostEdit->setPlaceholderText(i18n("hostname or IP"));
    if (!lastHost.isEmpty()) {
        m_sshHostEdit->setText(lastHost);
    }
    connect(m_sshHostEdit, &QLineEdit::textChanged, this, [this]() {
        updatePreview();
    });
    sshLayout->addWidget(m_sshHostEdit, 0, 1, 1, 2);

    // Username and Port on same row
    sshLayout->addWidget(new QLabel(i18n("Username:"), this), 1, 0);
    m_sshUsernameEdit = new QLineEdit(this);
    m_sshUsernameEdit->setPlaceholderText(QString::fromLocal8Bit(qgetenv("USER")));
    if (!lastUser.isEmpty()) {
        m_sshUsernameEdit->setText(lastUser);
    }
    connect(m_sshUsernameEdit, &QLineEdit::textChanged, this, [this]() {
        updateRemoteProjectRoot();
        updatePreview();
    });
    sshLayout->addWidget(m_sshUsernameEdit, 1, 1);

    auto *portLayout = new QHBoxLayout();
    portLayout->addWidget(new QLabel(i18n("Port:"), this));
    m_sshPortEdit = new QLineEdit(this);
    m_sshPortEdit->setPlaceholderText(QStringLiteral("22"));
    if (lastPort != 22 && lastPort > 0) {
        m_sshPortEdit->setText(QString::number(lastPort));
    }
    m_sshPortEdit->setMaximumWidth(60);
    portLayout->addWidget(m_sshPortEdit);
    sshLayout->addLayout(portLayout, 1, 2);

    // SSH Config checkbox and dropdown
    m_useSshConfigCheck = new QCheckBox(i18n("Use ~/.ssh/config entry:"), this);
    connect(m_useSshConfigCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_sshConfigCombo->setEnabled(checked);
        m_sshHostEdit->setEnabled(!checked);
        m_sshUsernameEdit->setEnabled(!checked);
        m_sshPortEdit->setEnabled(!checked);
        updatePreview();
    });
    sshLayout->addWidget(m_useSshConfigCheck, 2, 0, 1, 2);

    m_sshConfigCombo = new QComboBox(this);
    m_sshConfigCombo->setEnabled(false);
    connect(m_sshConfigCombo, &QComboBox::currentTextChanged, this, [this]() {
        updatePreview();
    });
    sshLayout->addWidget(m_sshConfigCombo, 2, 2);

    // Test Connection button and status
    m_testConnectionButton = new QPushButton(i18n("Test Connection"), this);
    connect(m_testConnectionButton, &QPushButton::clicked, this, &ClaudeSessionWizard::onTestConnectionClicked);
    sshLayout->addWidget(m_testConnectionButton, 3, 0);

    m_connectionStatusLabel = new QLabel(this);
    m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    sshLayout->addWidget(m_connectionStatusLabel, 3, 1, 1, 2);

    // Discover remote tmux sessions button
    m_discoverRemoteTmuxButton = new QPushButton(i18n("Browse Conversations..."), this);
    m_discoverRemoteTmuxButton->setToolTip(i18n("Find all Claude conversations on the remote host"));
    connect(m_discoverRemoteTmuxButton, &QPushButton::clicked,
            this, &ClaudeSessionWizard::onDiscoverRemoteTmuxClicked);
    sshLayout->addWidget(m_discoverRemoteTmuxButton, 4, 0);

    m_remoteTmuxLabel = new QLabel(this);
    m_remoteTmuxLabel->setStyleSheet(QStringLiteral("color: gray;"));
    sshLayout->addWidget(m_remoteTmuxLabel, 4, 1, 1, 2);

    // Attach to an existing running tmux session on the remote (vs. "Browse
    // Conversations" above, which resumes a Claude *conversation* in a fresh tmux).
    m_browseLiveSessionsButton = new QPushButton(i18n("Browse Live Sessions..."), this);
    m_browseLiveSessionsButton->setToolTip(
        i18n("Attach to a running tmux session on the remote host"));
    connect(m_browseLiveSessionsButton, &QPushButton::clicked,
            this, &ClaudeSessionWizard::onBrowseLiveRemoteSessionsClicked);
    sshLayout->addWidget(m_browseLiveSessionsButton, 5, 0);

    m_browseLiveSessionsLabel = new QLabel(this);
    m_browseLiveSessionsLabel->setStyleSheet(QStringLiteral("color: gray;"));
    sshLayout->addWidget(m_browseLiveSessionsLabel, 5, 1, 1, 2);

    m_sshGroup->setVisible(false); // Hidden by default (Local selected)
    mainLayout->addWidget(m_sshGroup);

    // --- Workspace root / Remote path ---
    auto *rootRow = new QHBoxLayout();
    m_pathLabel = new QLabel(i18n("Workspace root:"), this);
    rootRow->addWidget(m_pathLabel);
    m_projectRootEdit = new QLineEdit(this);
    m_projectRootEdit->setPlaceholderText(i18n("~/projects"));
    connect(m_projectRootEdit, &QLineEdit::textChanged, this, &ClaudeSessionWizard::onProjectRootChanged);
    rootRow->addWidget(m_projectRootEdit);
    m_browseRootButton = new QPushButton(i18n("Browse..."), this);
    connect(m_browseRootButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Workspace Root"), m_projectRootEdit->text());
        if (!dir.isEmpty()) {
            m_projectRootEdit->setText(dir);
        }
    });
    rootRow->addWidget(m_browseRootButton);
    mainLayout->addLayout(rootRow);

    mainLayout->addSpacing(8);

    // --- Task prompt (center, gets focus) ---
    auto *promptGroup = new QGroupBox(i18n("Task Description"), this);
    auto *promptLayout = new QVBoxLayout(promptGroup);
    m_promptEdit = new QLineEdit(this);
    m_promptEdit->setPlaceholderText(i18n("Describe what you want to build..."));
    connect(m_promptEdit, &QLineEdit::textChanged, this, [this]() {
        onPromptChanged();
    });
    promptLayout->addWidget(m_promptEdit);
    mainLayout->addWidget(promptGroup);

    // --- Resume previous session ---
    auto *resumeRow = new QHBoxLayout();
    m_resumeButton = new QPushButton(i18n("Resume Previous..."), this);
    m_resumeButton->setEnabled(false);
    m_resumeButton->setToolTip(i18n("Resume a previous Claude conversation in this project"));
    connect(m_resumeButton, &QPushButton::clicked, this, &ClaudeSessionWizard::onResumeClicked);
    resumeRow->addWidget(m_resumeButton);
    m_resumeLabel = new QLabel(this);
    m_resumeLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    resumeRow->addWidget(m_resumeLabel);
    resumeRow->addStretch();
    mainLayout->addLayout(resumeRow);

    // --- Folder name (right after prompt) ---
    auto *folderRow = new QHBoxLayout();
    folderRow->addWidget(new QLabel(i18n("Folder name:"), this));
    m_folderNameEdit = new QLineEdit(this);
    m_folderNameEdit->setPlaceholderText(i18n("my-project-name"));
    connect(m_folderNameEdit, &QLineEdit::textChanged, this, &ClaudeSessionWizard::onFolderNameChanged);

    // Directory completion relative to workspace root
    auto *folderCompleter = new QCompleter(this);
    folderCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    folderCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_folderNameEdit->setCompleter(folderCompleter);

    folderRow->addWidget(m_folderNameEdit);
    m_browseFolderButton = new QPushButton(i18n("Browse..."), this);
    connect(m_browseFolderButton, &QPushButton::clicked, this, [this]() {
        // Open file dialog starting at workspace root
        QString startDir = m_projectRootEdit->text();
        if (startDir.isEmpty() || !QDir(startDir).exists()) {
            startDir = QDir::homePath();
        }
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Project Folder"), startDir);
        if (!dir.isEmpty()) {
            // If selected dir is under workspace root, just use the relative name
            QString root = m_projectRootEdit->text();
            if (!root.isEmpty() && dir.startsWith(root)) {
                QString relative = dir.mid(root.length());
                if (relative.startsWith(QLatin1Char('/'))) {
                    relative = relative.mid(1);
                }
                m_folderNameEdit->setText(relative);
            } else {
                // Selected outside workspace root - update both
                QDir d(dir);
                m_folderNameEdit->setText(d.dirName());
                m_projectRootEdit->setText(QDir(dir).filePath(QStringLiteral("..")));
            }
            m_selectedDirectory = dir;
            m_useExistingDir = true;
            checkForConversations(dir);
        }
    });
    folderRow->addWidget(m_browseFolderButton);
    mainLayout->addLayout(folderRow);

    mainLayout->addSpacing(4);

    // --- Git (Optional) panel ---
    m_gitGroup = new QGroupBox(i18n("Git (Optional)"), this);
    auto *gitLayout = new QGridLayout(m_gitGroup);

    // Git mode combo
    gitLayout->addWidget(new QLabel(i18n("Git mode:"), this), 0, 0);
    m_gitModeCombo = new QComboBox(this);
    m_gitModeCombo->setObjectName(QStringLiteral("wizardGitModeCombo"));
    m_gitModeCombo->addItem(i18n("Initialize new repository"));
    m_gitModeCombo->addItem(i18n("Create as worktree"));
    m_gitModeCombo->addItem(i18n("Nothing — use the directory as-is"));
    // Default to doing nothing even before settings load, so the wizard never
    // silently pre-creates a repo or worktree.
    m_gitModeCombo->setCurrentIndex(GitCurrentBranch);
    connect(m_gitModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateGitSubFields();
        updatePreview();
    });
    gitLayout->addWidget(m_gitModeCombo, 0, 1);

    // Remote prefix
    m_remotePrefixLabel = new QLabel(i18n("Remote prefix:"), this);
    gitLayout->addWidget(m_remotePrefixLabel, 1, 0);
    m_gitRemoteEdit = new QLineEdit(this);
    m_gitRemoteEdit->setPlaceholderText(i18n("git@github.com:username/"));
    gitLayout->addWidget(m_gitRemoteEdit, 1, 1);

    // Source repo
    m_sourceRepoLabel = new QLabel(i18n("Source repo:"), this);
    gitLayout->addWidget(m_sourceRepoLabel, 2, 0);
    auto *repoRow = new QHBoxLayout();
    m_sourceRepoEdit = new QLineEdit(this);
    m_sourceRepoEdit->setPlaceholderText(i18n("(defaults to workspace root)"));
    connect(m_sourceRepoEdit, &QLineEdit::textChanged, this, [this]() {
        updatePreview();
    });
    repoRow->addWidget(m_sourceRepoEdit);
    m_browseRepoButton = new QPushButton(i18n("Browse..."), this);
    connect(m_browseRepoButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Source Repository"), m_sourceRepoEdit->text());
        if (!dir.isEmpty()) {
            m_sourceRepoEdit->setText(dir);
        }
    });
    repoRow->addWidget(m_browseRepoButton);
    gitLayout->addLayout(repoRow, 2, 1);

    // Branch name
    m_branchNameLabel = new QLabel(i18n("Branch name:"), this);
    gitLayout->addWidget(m_branchNameLabel, 3, 0);
    m_worktreeNameEdit = new QLineEdit(this);
    m_worktreeNameEdit->setPlaceholderText(i18n("feature/project-name"));
    gitLayout->addWidget(m_worktreeNameEdit, 3, 1);

    mainLayout->addWidget(m_gitGroup);

    // --- Model + options row ---
    auto *optionsRow = new QHBoxLayout();
    optionsRow->addWidget(new QLabel(i18n("Agent:"), this));
    m_agentCombo = new QComboBox(this);
    m_agentCombo->setObjectName(QStringLiteral("wizardAgentCombo"));
    m_agentCombo->addItem(i18n("Claude"), static_cast<int>(ClaudeSession::AgentKind::Claude));
    m_agentCombo->addItem(i18n("Codex"), static_cast<int>(ClaudeSession::AgentKind::Codex));
    // Codex is only offered when its binary is actually resolvable, so the
    // picker can't hand back a kind that would fail to launch.
    if (!CodexProcess::isAvailable()) {
        m_agentCombo->setItemData(1, false, Qt::UserRole - 1);
        m_agentCombo->setToolTip(i18n("Codex CLI not found — install it to enable Codex sessions"));
    }
    // Switching agents changes both the model vocabulary and which transcript
    // store the resume affordance reads, so refresh both rather than leaving
    // Claude model names (or a stale count) in front of a Codex session.
    connect(m_agentCombo, &QComboBox::currentIndexChanged, this, [this]() {
        populateModelCombo();
        const QString dir = selectedDirectory();
        if (!dir.isEmpty()) {
            checkForConversations(dir);
        }
    });
    optionsRow->addWidget(m_agentCombo);
    optionsRow->addSpacing(16);
    optionsRow->addWidget(new QLabel(i18n("Model:"), this));
    m_modelCombo = new QComboBox(this);
    m_modelCombo->setObjectName(QStringLiteral("wizardModelCombo"));
    populateModelCombo();
    optionsRow->addWidget(m_modelCombo);
    optionsRow->addSpacing(16);
    m_autoApproveReadCheck = new QCheckBox(i18n("Auto-approve Read"), this);
    m_autoApproveReadCheck->setChecked(true);
    optionsRow->addWidget(m_autoApproveReadCheck);
    optionsRow->addStretch();
    mainLayout->addLayout(optionsRow);

    // --- Extra Claude args row ---
    auto *argsRow = new QHBoxLayout();
    argsRow->addWidget(new QLabel(i18n("Extra args:"), this));
    m_extraArgsEdit = new QLineEdit(this);
    m_extraArgsEdit->setObjectName(QStringLiteral("wizardExtraArgsEdit"));
    {
        QString globalDefault;
        if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
            globalDefault = settings->extraClaudeArgs();
        }
        m_extraArgsEdit->setPlaceholderText(globalDefault.isEmpty() ? i18n("Extra claude CLI arguments (e.g. --debug)") : globalDefault);
        m_extraArgsEdit->setToolTip(
            i18n("Override the default extra arguments passed to the claude CLI. Leave blank to use the global default shown as placeholder."));
    }
    argsRow->addWidget(m_extraArgsEdit, 1);
    mainLayout->addLayout(argsRow);

    mainLayout->addSpacing(4);

    // --- Budget Controls (collapsible) ---
    m_budgetGroup = new QGroupBox(i18n("Budget Controls"), this);
    m_budgetGroup->setCheckable(true);
    m_budgetGroup->setChecked(false); // Collapsed by default
    auto *budgetLayout = new QGridLayout(m_budgetGroup);

    budgetLayout->addWidget(new QLabel(i18n("Time limit (min):"), this), 0, 0);
    m_timeLimitSpin = new QSpinBox(this);
    m_timeLimitSpin->setRange(0, 1440);
    m_timeLimitSpin->setSpecialValueText(i18n("Unlimited"));
    m_timeLimitSpin->setSuffix(i18n(" min"));
    budgetLayout->addWidget(m_timeLimitSpin, 0, 1);

    budgetLayout->addWidget(new QLabel(i18n("Cost ceiling ($):"), this), 1, 0);
    m_costCeilingSpin = new QDoubleSpinBox(this);
    m_costCeilingSpin->setRange(0.0, 1000.0);
    m_costCeilingSpin->setDecimals(2);
    m_costCeilingSpin->setSingleStep(0.50);
    m_costCeilingSpin->setSpecialValueText(i18n("Unlimited"));
    m_costCeilingSpin->setPrefix(QStringLiteral("$"));
    budgetLayout->addWidget(m_costCeilingSpin, 1, 1);

    budgetLayout->addWidget(new QLabel(i18n("Token ceiling (K):"), this), 2, 0);
    m_tokenCeilingSpin = new QSpinBox(this);
    m_tokenCeilingSpin->setRange(0, 100000);
    m_tokenCeilingSpin->setSingleStep(100);
    m_tokenCeilingSpin->setSpecialValueText(i18n("Unlimited"));
    m_tokenCeilingSpin->setSuffix(QStringLiteral("K"));
    budgetLayout->addWidget(m_tokenCeilingSpin, 2, 1);

    budgetLayout->addWidget(new QLabel(i18n("Policy:"), this), 3, 0);
    m_budgetPolicyCombo = new QComboBox(this);
    m_budgetPolicyCombo->addItem(i18n("Soft (warn only)"));
    m_budgetPolicyCombo->addItem(i18n("Hard (block yolo)"));
    budgetLayout->addWidget(m_budgetPolicyCombo, 3, 1);

    mainLayout->addWidget(m_budgetGroup);

    mainLayout->addSpacing(4);

    // --- Preview ---
    m_previewLabel = new QLabel(this);
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    mainLayout->addWidget(m_previewLabel);

    mainLayout->addStretch();

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *createButton = buttons->addButton(i18n("Create Session"), QDialogButtonBox::AcceptRole);
    createButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &ClaudeSessionWizard::onCreatePressed);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // --- Tab order: prompt -> folder name -> git options -> model ---
    setTabOrder(m_promptEdit, m_folderNameEdit);
    setTabOrder(m_folderNameEdit, m_browseFolderButton);
    setTabOrder(m_browseFolderButton, m_gitModeCombo);
    setTabOrder(m_gitModeCombo, m_gitRemoteEdit);
    setTabOrder(m_gitRemoteEdit, m_sourceRepoEdit);
    setTabOrder(m_sourceRepoEdit, m_worktreeNameEdit);
    setTabOrder(m_worktreeNameEdit, m_modelCombo);
    setTabOrder(m_modelCombo, m_autoApproveReadCheck);
}

void ClaudeSessionWizard::updateFolderCompleter()
{
    QString root = m_projectRootEdit->text();
    if (root.isEmpty() || !QDir(root).exists()) {
        return;
    }

    QStringList dirs;
    QDir rootDir(root);
    const auto entries = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        dirs << entry;
    }

    auto *completer = m_folderNameEdit->completer();
    if (completer) {
        auto *model = qobject_cast<QStringListModel *>(completer->model());
        if (!model) {
            model = new QStringListModel(completer);
            completer->setModel(model);
        }
        model->setStringList(dirs);
    }
}

void ClaudeSessionWizard::onPromptChanged()
{
    QString prompt = m_promptEdit->text();
    m_taskPrompt = prompt;

    // Reset browse state so folder auto-generates from prompt again
    m_useExistingDir = false;
    m_resumeSessionId.clear();

    m_folderNameEdit->setText(generateFolderName(prompt));
    m_worktreeNameEdit->setText(generateWorktreeName(prompt));

    updatePreview();
}

void ClaudeSessionWizard::onFolderNameChanged(const QString &name)
{
    Q_UNUSED(name);
    updatePreview();
    debouncedDetectGitAndConversations();
}

void ClaudeSessionWizard::onProjectRootChanged(const QString &path)
{
    Q_UNUSED(path);
    updateFolderCompleter();
    updatePreview();
    debouncedDetectGitAndConversations();
}

void ClaudeSessionWizard::debouncedDetectGitAndConversations()
{
    if (!m_gitDebounce) {
        m_gitDebounce = new QTimer(this);
        m_gitDebounce->setSingleShot(true);
        m_gitDebounce->setInterval(300);
        connect(m_gitDebounce, &QTimer::timeout, this, [this]() {
            QString dir = selectedDirectory();
            if (!dir.isEmpty() && QDir(dir).exists()) {
                // detectGitState is async — onGitStateDetected() handles the result
                detectGitState(dir);
                checkForConversations(dir);
            } else {
                m_resumeButton->setEnabled(false);
                m_resumeLabel->clear();
                m_resumeSessionId.clear();
            }
        });
    }
    m_gitDebounce->start();
}

void ClaudeSessionWizard::onCreatePressed()
{
    QString dir = selectedDirectory();

    if (dir.isEmpty()) {
        if (m_folderNameEdit->text().isEmpty()) {
            QMessageBox::warning(this, i18n("Missing Folder Name"), i18n("Enter a task description or type a folder name."));
            m_promptEdit->setFocus();
        } else {
            QMessageBox::warning(this, i18n("Missing Workspace Root"), i18n("Set the workspace root directory."));
            m_projectRootEdit->setFocus();
        }
        return;
    }

    // For remote sessions, skip all local filesystem checks — remote dirs
    // are created via SSH in buildRemoteSshArgs (mkdir -p)
    if (isRemoteSession()) {
        // Just validate SSH connection fields
        if (sshHost().isEmpty()) {
            QMessageBox::warning(this, i18n("Missing SSH Host"), i18n("Enter an SSH host or select a config entry."));
            m_sshHostEdit->setFocus();
            return;
        }

        // Save settings (model + SSH fields for next time)
        if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
            settings->setDefaultModel(m_modelCombo->currentText());
            settings->setLastSshHost(sshHost());
            settings->setLastSshUsername(sshUsername());
            settings->setLastSshPort(sshPort());
            settings->save();
        }

        m_selectedDirectory = dir;
        accept();
        return;
    }

    // --- Local session validation below ---

    // Check if directory already exists
    if (QDir(dir).exists() && !m_useExistingDir) {
        auto result = QMessageBox::question(this,
                                            i18n("Directory Exists"),
                                            i18n("The directory '%1' already exists. Use it anyway?", dir),
                                            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
        m_useExistingDir = true;
    }

    // Check if parent directory exists
    QString parentDir = m_projectRootEdit->text();
    if (!QDir(parentDir).exists()) {
        auto result = QMessageBox::question(this,
                                            i18n("Create Directory"),
                                            i18n("The project root '%1' does not exist. Create it?", parentDir),
                                            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
        QDir().mkpath(parentDir);
    }

    // Create project directory if needed
    if (!QDir(dir).exists()) {
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(this, i18n("Error"), i18n("Failed to create directory: %1", dir));
            return;
        }
    }

    // Validate worktree fields
    int gitMode = m_gitModeCombo->currentIndex();
    if (gitMode == GitWorktree) {
        if (m_worktreeNameEdit->text().isEmpty()) {
            QMessageBox::warning(this, i18n("Missing Branch Name"), i18n("Enter a branch name for the worktree."));
            m_worktreeNameEdit->setFocus();
            return;
        }
        QString repo = m_sourceRepoEdit->text();
        if (repo.isEmpty()) {
            repo = m_projectRootEdit->text();
        }
        if (repo.isEmpty()) {
            QMessageBox::warning(this, i18n("Missing Source Repository"), i18n("Enter a source repository for the worktree."));
            m_sourceRepoEdit->setFocus();
            return;
        }
    }

    // Init git if requested
    if (gitMode == GitInit) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start(QStringLiteral("git"), {QStringLiteral("init")});
        git.waitForFinished(5000);

        if (git.exitCode() == 0) {
            qDebug() << "ClaudeSessionWizard: Initialized git repo in:" << dir;

            // Set up remote if configured
            QString remoteRoot = m_gitRemoteEdit->text();
            if (!remoteRoot.isEmpty()) {
                QString repoName = QDir(dir).dirName();
                QString remoteUrl = remoteRoot + repoName + QStringLiteral(".git");

                QProcess gitRemote;
                gitRemote.setWorkingDirectory(dir);
                gitRemote.start(QStringLiteral("git"), {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remoteUrl});
                gitRemote.waitForFinished(5000);
            }
        }
    }

    // Save settings
    if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
        settings->setProjectRoot(m_projectRootEdit->text());
        settings->setGitRemoteRoot(m_gitRemoteEdit->text());
        settings->setGitMode(gitMode);
        settings->setWorktreeSourceRepo(m_sourceRepoEdit->text());
        settings->setDefaultModel(m_modelCombo->currentText());
        settings->save();
    }

    m_selectedDirectory = dir;
    accept();
}

QString ClaudeSessionWizard::generateFolderName(const QString &prompt) const
{
    if (prompt.isEmpty()) {
        return QString();
    }

    static QRegularExpression wordSplit(QStringLiteral("[\\s_]+"));
    static QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));

    QString lower = prompt.toLower();
    QStringList words = lower.split(wordSplit, Qt::SkipEmptyParts);

    int maxWords = qMin(4, words.size());
    QString result;
    for (int i = 0; i < maxWords; ++i) {
        QString word = words[i];
        word.remove(nonAlnum);
        if (!word.isEmpty()) {
            if (!result.isEmpty()) {
                result += QLatin1Char('-');
            }
            result += word;
        }
    }

    if (result.length() > 30) {
        result = result.left(30);
        while (result.endsWith(QLatin1Char('-'))) {
            result.chop(1);
        }
    }

    return result.isEmpty() ? QStringLiteral("new-project") : result;
}

QString ClaudeSessionWizard::generateWorktreeName(const QString &prompt) const
{
    if (prompt.isEmpty()) {
        return QString();
    }

    static QRegularExpression wordSplit(QStringLiteral("[\\s_]+"));
    static QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));

    QString lower = prompt.toLower();
    QStringList words = lower.split(wordSplit, Qt::SkipEmptyParts);

    int maxWords = qMin(8, words.size());
    QString result = QStringLiteral("feature/");
    for (int i = 0; i < maxWords; ++i) {
        QString word = words[i];
        word.remove(nonAlnum);
        if (!word.isEmpty()) {
            if (result.length() > 8) {
                result += QLatin1Char('-');
            }
            result += word;
        }
    }

    if (result.length() > 50) {
        result = result.left(50);
        while (result.endsWith(QLatin1Char('-'))) {
            result.chop(1);
        }
    }

    return result;
}

void ClaudeSessionWizard::detectGitState(const QString &path)
{
    if (path.isEmpty() || !QDir(path).exists()) {
        m_isGitRepo = false;
        m_repoRoot.clear();
        onGitStateDetected();
        return;
    }

    auto *git = new QProcess(this);
    git->setWorkingDirectory(path);
    connect(git, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, git](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            m_isGitRepo = true;
            m_repoRoot = QString::fromUtf8(git->readAllStandardOutput()).trimmed();
        } else {
            m_isGitRepo = false;
            m_repoRoot.clear();
        }
        git->deleteLater();
        onGitStateDetected();
    });
    connect(git, &QProcess::errorOccurred, this, [this, git](QProcess::ProcessError) {
        m_isGitRepo = false;
        m_repoRoot.clear();
        git->deleteLater();
        onGitStateDetected();
    });
    git->start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
}

void ClaudeSessionWizard::onGitStateDetected()
{
    if (m_isGitRepo) {
        m_gitModeCombo->setCurrentIndex(GitWorktree);
        m_sourceRepoEdit->setText(m_repoRoot);
    } else {
        m_gitModeCombo->setCurrentIndex(GitInit);
        m_sourceRepoEdit->clear();
    }
}

QStringList ClaudeSessionWizard::getWorktrees(const QString &repoRoot)
{
    QStringList result;

    QProcess git;
    git.setWorkingDirectory(repoRoot);
    git.start(QStringLiteral("git"), {QStringLiteral("worktree"), QStringLiteral("list"), QStringLiteral("--porcelain")});
    git.waitForFinished(3000);

    if (git.exitCode() != 0) {
        return result;
    }

    QString output = QString::fromUtf8(git.readAllStandardOutput());
    QStringList lines = output.split(QLatin1Char('\n'));

    QString currentPath;
    QString currentBranch;

    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("worktree "))) {
            if (!currentPath.isEmpty()) {
                result << QStringLiteral("%1\t%2").arg(currentPath, currentBranch);
            }
            currentPath = line.mid(9);
            currentBranch.clear();
        } else if (line.startsWith(QLatin1String("branch "))) {
            currentBranch = line.mid(7);
            if (currentBranch.startsWith(QLatin1String("refs/heads/"))) {
                currentBranch = currentBranch.mid(11);
            }
        }
    }

    if (!currentPath.isEmpty()) {
        result << QStringLiteral("%1\t%2").arg(currentPath, currentBranch);
    }

    return result;
}

void ClaudeSessionWizard::updateGitSubFields()
{
    int mode = m_gitModeCombo->currentIndex();

    bool remoteOn = (mode == GitInit);
    bool worktreeOn = (mode == GitWorktree);

    m_remotePrefixLabel->setEnabled(remoteOn);
    m_gitRemoteEdit->setEnabled(remoteOn);

    m_sourceRepoLabel->setEnabled(worktreeOn);
    m_sourceRepoEdit->setEnabled(worktreeOn);
    m_browseRepoButton->setEnabled(worktreeOn);
    m_branchNameLabel->setEnabled(worktreeOn);
    m_worktreeNameEdit->setEnabled(worktreeOn);
}

void ClaudeSessionWizard::updatePreview()
{
    QString dir = selectedDirectory();
    if (dir.isEmpty()) {
        m_previewLabel->setText(i18n("Enter a task description to generate project name"));
        return;
    }

    QString preview;

    // SSH remote session preview
    if (isRemoteSession()) {
        QString host = sshHost();
        if (host.isEmpty()) {
            preview = i18n("Configure SSH connection above");
        } else {
            QString user = sshUsername();
            QString target = user.isEmpty() ? host : QStringLiteral("%1@%2").arg(user, host);
            preview = i18n("Will connect to: %1\nRemote path: %2\nCommand: ssh -t %3 tmux ... claude", host, dir, target);
        }
        m_previewLabel->setText(preview);
        return;
    }

    // Local session preview
    switch (m_gitModeCombo->currentIndex()) {
    case GitInit:
        preview = i18n("Will create: %1 (git init)", dir);
        break;
    case GitWorktree: {
        QString branch = m_worktreeNameEdit ? m_worktreeNameEdit->text() : QString();
        QString repo = m_sourceRepoEdit ? m_sourceRepoEdit->text() : QString();
        if (!branch.isEmpty() && !repo.isEmpty()) {
            preview = i18n("Will create worktree: %1\nFrom repo: %2\nBranch: %3", dir, repo, branch);
        } else {
            preview = i18n("Will create worktree: %1\n(Select source repo and branch)", dir);
        }
        break;
    }
    case GitCurrentBranch:
        preview = i18n("Will use: %1 (existing repo, current branch)", dir);
        break;
    case GitNone:
    default:
        preview = i18n("Will create: %1", dir);
        break;
    }
    m_previewLabel->setText(preview);
}

void ClaudeSessionWizard::onLocationChanged()
{
    updateSshVisibility();
    updatePreview();
}

void ClaudeSessionWizard::updateSshVisibility()
{
    bool remote = isRemoteSession();
    m_sshGroup->setVisible(remote);

    // Update path label and defaults
    if (remote) {
        m_pathLabel->setText(i18n("Remote path:"));
        m_browseRootButton->setEnabled(false); // Can't browse remote filesystem
        m_browseFolderButton->setEnabled(false);

        // Hide git group for remote sessions (git ops are local-only)
        m_gitGroup->setVisible(false);

        // Set default remote project root using SSH username
        updateRemoteProjectRoot();
    } else {
        m_pathLabel->setText(i18n("Workspace root:"));
        m_projectRootEdit->setPlaceholderText(i18n("~/projects"));
        m_browseRootButton->setEnabled(true);
        m_browseFolderButton->setEnabled(true);
        m_gitGroup->setVisible(true);

        // Restore local project root from settings
        if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
            m_projectRootEdit->setText(settings->projectRoot());
        }
    }

    // Adjust dialog size for SSH group visibility
    adjustSize();
}

void ClaudeSessionWizard::updateRemoteProjectRoot()
{
    if (!isRemoteSession()) {
        return;
    }

    QString user = m_sshUsernameEdit->text();
    if (user.isEmpty()) {
        user = QString::fromLocal8Bit(qgetenv("USER"));
    }
    // Check common project directories on the remote host
    const QStringList candidates = {
        QStringLiteral("/home/%1/projects").arg(user),
        QStringLiteral("/home/%1/code").arg(user),
        QStringLiteral("/home/%1/workspace").arg(user),
        QStringLiteral("/home/%1/src").arg(user),
    };
    // Default to the first candidate (remote dirs can't be stat'd locally)
    m_projectRootEdit->setText(candidates.first());
}

void ClaudeSessionWizard::onTestConnectionClicked()
{
    QString host = sshHost();
    if (host.isEmpty()) {
        m_connectionStatusLabel->setText(i18n("Enter a host first"));
        m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: orange;"));
        return;
    }

    m_connectionStatusLabel->setText(i18n("Testing..."));
    m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_testConnectionButton->setEnabled(false);

    // Build SSH command
    QStringList args;
    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    args << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=5");

    if (m_useSshConfigCheck->isChecked()) {
        args << host;
    } else {
        QString user = sshUsername();
        int port = sshPort();
        if (!user.isEmpty()) {
            args << QStringLiteral("-l") << user;
        }
        if (port != 22) {
            args << QStringLiteral("-p") << QString::number(port);
        }
        args << host;
    }
    // Check connectivity, tmux availability, and claude availability.
    // Use semicolons (not &&) so missing tools don't cause a non-zero exit code.
    args << QStringLiteral("echo ok; which tmux 2>/dev/null; which claude 2>/dev/null; exit 0");

    auto *process = new QProcess(this);
    ClaudeSessionRegistry::ensureSshAuthSock(process);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus) {
        process->deleteLater();
        m_testConnectionButton->setEnabled(true);

        QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        bool connected = output.contains(QStringLiteral("ok"));

        if (connected) {
            // Check for tool paths in the output
            bool hasTmux = output.contains(QStringLiteral("/tmux"));
            bool hasClaude = output.contains(QStringLiteral("/claude"));

            QString status;
            if (hasTmux && hasClaude) {
                status = i18n("OK (tmux + claude found)");
                m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: green;"));
            } else {
                QStringList missing;
                if (!hasTmux) {
                    missing << QStringLiteral("tmux");
                }
                if (!hasClaude) {
                    missing << QStringLiteral("claude");
                }
                status = i18n("Connected, missing: %1", missing.join(QStringLiteral(", ")));
                m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: orange;"));
            }
            m_connectionStatusLabel->setText(status);
        } else {
            QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
            if (error.isEmpty()) {
                error = i18n("Connection failed (exit %1)", exitCode);
            }
            m_connectionStatusLabel->setText(error.left(80));
            m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: red;"));
        }
    });

    process->start(QStringLiteral("ssh"), args);
}

void ClaudeSessionWizard::loadSshConfigHosts()
{
    m_sshConfigCombo->clear();

    QString configPath = QDir::homePath() + QStringLiteral("/.ssh/config");
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    static QRegularExpression hostPattern(QStringLiteral("^\\s*Host\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);

    while (!in.atEnd()) {
        QString line = in.readLine();
        QRegularExpressionMatch match = hostPattern.match(line);
        if (match.hasMatch()) {
            QString hosts = match.captured(1).trimmed();
            // Skip wildcard entries
            if (!hosts.contains(QLatin1Char('*')) && !hosts.contains(QLatin1Char('?'))) {
                // May have multiple hosts on one line
                static const QRegularExpression whitespace(QStringLiteral("\\s+"));
                const QStringList hostList = hosts.split(whitespace, Qt::SkipEmptyParts);
                for (const QString &h : hostList) {
                    if (!h.contains(QLatin1Char('*')) && !h.contains(QLatin1Char('?'))) {
                        m_sshConfigCombo->addItem(h);
                    }
                }
            }
        }
    }
}

void ClaudeSessionWizard::checkForConversations(const QString &projectPath)
{
    m_resumeSessionId.clear();

    // Codex records its own transcripts in a separate tree, so the resume
    // affordance has to ask the CLI the user actually picked.
    if (agentKind() == ClaudeSession::AgentKind::Codex) {
        const int count = CodexProcess::discoverConversations(projectPath).size();
        m_resumeButton->setEnabled(count > 0);
        if (count > 0) {
            m_resumeButton->setText(i18n("Resume Previous (%1)...", count));
            m_resumeLabel->setText(i18n("%1 Codex session(s) in this directory", count));
        } else {
            m_resumeButton->setText(i18n("Resume Previous..."));
            m_resumeLabel->setText(i18n("No previous Codex sessions"));
        }
        return;
    }

    auto conversations = ClaudeSessionRegistry::readClaudeConversations(projectPath);
    if (conversations.isEmpty()) {
        m_resumeButton->setEnabled(false);
        m_resumeLabel->setText(i18n("No previous sessions"));
    } else {
        m_resumeButton->setEnabled(true);
        m_resumeButton->setText(i18n("Resume Previous (%1)...", conversations.size()));
        m_resumeLabel->clear();
    }
}

void ClaudeSessionWizard::onResumeClicked()
{
    QString dir = selectedDirectory();
    if (dir.isEmpty()) {
        return;
    }

    // Codex keeps its transcripts elsewhere. Map them onto ClaudeConversation
    // so the existing picker — and everything downstream of it — works
    // unchanged; only the source of the list differs.
    if (agentKind() == ClaudeSession::AgentKind::Codex) {
        const QList<CodexConversation> codexSessions = CodexProcess::discoverConversations(dir);
        if (codexSessions.isEmpty()) {
            m_resumeButton->setEnabled(false);
            m_resumeLabel->setText(i18n("No previous Codex sessions"));
            return;
        }
        QList<ClaudeConversation> conversations;
        conversations.reserve(codexSessions.size());
        for (const CodexConversation &c : codexSessions) {
            ClaudeConversation conv;
            conv.sessionId = c.sessionId;
            conv.summary = c.firstPrompt.isEmpty() ? c.model : c.firstPrompt;
            conv.firstPrompt = c.firstPrompt;
            conv.created = c.created;
            conv.modified = c.modified;
            conv.projectPath = c.workingDirectory;
            conversations.append(conv);
        }
        showConversationPicker(conversations);
        return;
    }

    if (isRemoteSession()) {
        // Remote: async conversation discovery via SSH
        QString host = sshHost();
        if (host.isEmpty()) {
            return;
        }
        QString target;
        QString user = sshUsername();
        if (!user.isEmpty()) {
            target = QStringLiteral("%1@%2").arg(user, host);
        } else {
            target = host;
        }

        m_resumeLabel->setText(i18n("Loading remote sessions..."));
        m_resumeButton->setEnabled(false);

        auto *registry = ClaudeSessionRegistry::instance();
        if (!registry) {
            return;
        }

        QPointer<ClaudeSessionWizard> guard(this);
        registry->readRemoteConversationsAsync(target, sshPort(), dir,
            [guard](const QList<ClaudeConversation> &conversations) {
                if (!guard) {
                    return;
                }
                guard->m_resumeButton->setEnabled(true);
                if (conversations.isEmpty()) {
                    guard->m_resumeLabel->setText(i18n("No previous sessions on remote"));
                    return;
                }
                guard->showConversationPicker(conversations);
            });
        return;
    }

    // Local: synchronous
    auto conversations = ClaudeSessionRegistry::readClaudeConversations(dir);
    if (conversations.isEmpty()) {
        m_resumeButton->setEnabled(false);
        m_resumeLabel->setText(i18n("No previous sessions"));
        return;
    }
    showConversationPicker(conversations);
}

void ClaudeSessionWizard::onDiscoverRemoteTmuxClicked()
{
    QString host = sshHost();
    if (host.isEmpty()) {
        m_remoteTmuxLabel->setText(i18n("Enter a host first"));
        m_remoteTmuxLabel->setStyleSheet(QStringLiteral("color: orange;"));
        return;
    }

    m_remoteTmuxLabel->setText(i18n("Scanning all projects..."));
    m_remoteTmuxLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_discoverRemoteTmuxButton->setEnabled(false);

    QString target;
    QString user = sshUsername();
    if (!user.isEmpty()) {
        target = QStringLiteral("%1@%2").arg(user, host);
    } else {
        target = host;
    }

    auto *registry = ClaudeSessionRegistry::instance();
    if (!registry) {
        m_discoverRemoteTmuxButton->setEnabled(true);
        return;
    }

    QPointer<ClaudeSessionWizard> guard(this);

    // Discover ALL Claude conversations across all projects on the remote
    registry->discoverAllRemoteConversationsAsync(target, sshPort(),
        [guard](const QList<ClaudeConversation> &conversations) {
            if (!guard) {
                return;
            }
            guard->m_discoverRemoteTmuxButton->setEnabled(true);
            if (conversations.isEmpty()) {
                guard->m_remoteTmuxLabel->setText(i18n("No conversations found"));
                return;
            }

            guard->m_remoteTmuxLabel->setText(
                i18n("%1 conversation(s) found", conversations.size()));

            // Show conversation picker with project paths
            ClaudeConversationPicker picker(conversations, guard);
            picker.setWindowTitle(QStringLiteral("Resume Remote Conversation"));
            if (picker.exec() != QDialog::Accepted) {
                return;
            }

            QString id = picker.selectedSessionId();
            QString projectPath = picker.selectedProjectPath();
            if (id.isEmpty()) {
                // User chose "Start Fresh"
                guard->m_remoteTmuxLabel->clear();
                return;
            }

            // Set working directory from the selected conversation.
            // Update folder name BEFORE setting resume ID because setText()
            // triggers onFolderNameChanged() → checkForConversations() which
            // clears m_resumeSessionId.
            if (projectPath.startsWith(QLatin1Char('/'))) {
                guard->m_selectedDirectory = projectPath;
                guard->m_useExistingDir = true;
            }

            QString dirName = QDir(projectPath).dirName();
            if (!dirName.isEmpty() && dirName != QStringLiteral(".")) {
                guard->m_folderNameEdit->setText(dirName);
            }

            // Set resume ID AFTER folder name change (which clears it via signal)
            guard->m_resumeSessionId = id;

            // Find the summary for display
            for (const auto &conv : conversations) {
                if (conv.sessionId == id) {
                    QString summary = conv.summary.isEmpty() ? conv.firstPrompt : conv.summary;
                    summary = summary.simplified();
                    if (summary.length() > 60) {
                        summary = summary.left(57) + QStringLiteral("...");
                    }
                    guard->m_remoteTmuxLabel->setText(
                        i18n("Resuming: %1", summary));
                    guard->m_remoteTmuxLabel->setStyleSheet(
                        QStringLiteral("color: green;"));
                    break;
                }
            }

            guard->accept();
        });
}

void ClaudeSessionWizard::onBrowseLiveRemoteSessionsClicked()
{
    QString host = sshHost();
    if (host.isEmpty()) {
        m_browseLiveSessionsLabel->setText(i18n("Enter a host first"));
        m_browseLiveSessionsLabel->setStyleSheet(QStringLiteral("color: orange;"));
        return;
    }

    m_browseLiveSessionsLabel->setText(i18n("Scanning remote tmux..."));
    m_browseLiveSessionsLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_browseLiveSessionsButton->setEnabled(false);

    const QString user = sshUsername();
    const QString target = user.isEmpty() ? host : QStringLiteral("%1@%2").arg(user, host);

    auto *registry = ClaudeSessionRegistry::instance();
    if (!registry) {
        m_browseLiveSessionsButton->setEnabled(true);
        return;
    }

    QPointer<ClaudeSessionWizard> guard(this);

    // konsolaiOnly=true: only offer sessions we recognize; foreign tmux sessions
    // would confuse yolo/hooks detection and the session panel.
    registry->discoverRemoteTmuxSessionsAsync(target, sshPort(), /*konsolaiOnly*/ true,
        [guard](const QList<TmuxManager::SessionInfo> &sessions) {
            if (!guard) {
                return;
            }
            guard->m_browseLiveSessionsButton->setEnabled(true);
            if (sessions.isEmpty()) {
                guard->m_browseLiveSessionsLabel->setText(i18n("No live sessions found"));
                return;
            }

            QStringList items;
            items.reserve(sessions.size());
            for (const auto &info : sessions) {
                items << TmuxManager::formatSessionForPicker(info);
            }

            bool ok = false;
            const QString chosen = QInputDialog::getItem(
                guard, i18n("Attach to Remote Session"),
                i18n("Pick a running tmux session on %1:", guard->sshHost()),
                items, 0, /*editable*/ false, &ok);
            if (!ok || chosen.isEmpty()) {
                guard->m_browseLiveSessionsLabel->clear();
                return;
            }

            // Recover the matching SessionInfo. The label starts with the exact
            // session name (formatSessionForPicker guarantees this).
            const TmuxManager::SessionInfo *picked = nullptr;
            for (const auto &info : sessions) {
                if (chosen.startsWith(info.name)) {
                    picked = &info;
                    break;
                }
            }
            if (!picked) {
                return;
            }

            // Wire everything up so wizard finish → attach path (not new-session).
            guard->m_selectedTmuxSession = picked->name;

            // Auto-populate the working directory from the remote pane so the
            // session panel and hooks match. Folder-name UI mirrors the
            // conversation picker (must set BEFORE folder name to avoid the
            // clearing side-effect of onFolderNameChanged).
            if (picked->paneCurrentPath.startsWith(QLatin1Char('/'))) {
                guard->m_selectedDirectory = picked->paneCurrentPath;
                guard->m_useExistingDir = true;
                const QString dirName = QDir(picked->paneCurrentPath).dirName();
                if (!dirName.isEmpty() && dirName != QStringLiteral(".")) {
                    guard->m_folderNameEdit->setText(dirName);
                }
            }

            guard->m_browseLiveSessionsLabel->setText(
                i18n("Will attach to: %1", picked->name));
            guard->m_browseLiveSessionsLabel->setStyleSheet(QStringLiteral("color: green;"));
        });
}

void ClaudeSessionWizard::showConversationPicker(const QList<ClaudeConversation> &conversations)
{
    QString id = ClaudeConversationPicker::pick(conversations, this);
    if (!id.isEmpty()) {
        m_resumeSessionId = id;
        // Find the selected conversation to show its summary
        for (const auto &conv : conversations) {
            if (conv.sessionId == id) {
                QString summary = conv.summary.isEmpty() ? conv.firstPrompt : conv.summary;
                if (summary.length() > 60) {
                    summary = summary.left(57) + QStringLiteral("...");
                }
                m_resumeLabel->setText(i18n("Resuming: %1", summary));
                m_resumeLabel->setStyleSheet(QStringLiteral("color: green; font-style: italic;"));
                break;
            }
        }
        accept();
    } else {
        // User picked "Start Fresh" — clear resume ID
        m_resumeSessionId.clear();
        m_resumeLabel->clear();
    }
}

} // namespace Konsolai

#include "moc_ClaudeSessionWizard.cpp"
