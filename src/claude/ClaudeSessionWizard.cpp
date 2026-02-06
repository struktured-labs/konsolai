/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionWizard.h"
#include "KonsolaiSettings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStringListModel>
#include <QTextStream>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace Konsolai
{

ClaudeSessionWizard::ClaudeSessionWizard(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("New Claude Session"));
    setMinimumSize(600, 480);
    resize(650, 520);

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
    return QStringLiteral("claude-sonnet-4");
}

bool ClaudeSessionWizard::autoApproveRead() const
{
    return m_autoApproveReadCheck && m_autoApproveReadCheck->isChecked();
}

QString ClaudeSessionWizard::claudeArgs() const
{
    return QString();
}

QString ClaudeSessionWizard::taskPrompt() const
{
    return m_taskPrompt;
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

    // Host
    sshLayout->addWidget(new QLabel(i18n("Host:"), this), 0, 0);
    m_sshHostEdit = new QLineEdit(this);
    m_sshHostEdit->setPlaceholderText(i18n("hostname or IP"));
    connect(m_sshHostEdit, &QLineEdit::textChanged, this, [this]() {
        updatePreview();
    });
    sshLayout->addWidget(m_sshHostEdit, 0, 1, 1, 2);

    // Username and Port on same row
    sshLayout->addWidget(new QLabel(i18n("Username:"), this), 1, 0);
    m_sshUsernameEdit = new QLineEdit(this);
    m_sshUsernameEdit->setPlaceholderText(QString::fromLocal8Bit(qgetenv("USER")));
    sshLayout->addWidget(m_sshUsernameEdit, 1, 1);

    auto *portLayout = new QHBoxLayout();
    portLayout->addWidget(new QLabel(i18n("Port:"), this));
    m_sshPortEdit = new QLineEdit(this);
    m_sshPortEdit->setPlaceholderText(QStringLiteral("22"));
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
    m_gitModeCombo->addItem(i18n("Initialize new repository"));
    m_gitModeCombo->addItem(i18n("Create as worktree"));
    m_gitModeCombo->addItem(i18n("Use current branch"));
    m_gitModeCombo->addItem(i18n("None"));
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
    optionsRow->addWidget(new QLabel(i18n("Model:"), this));
    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem(QStringLiteral("claude-sonnet-4"));
    m_modelCombo->addItem(QStringLiteral("claude-opus-4"));
    m_modelCombo->addItem(QStringLiteral("claude-haiku"));
    optionsRow->addWidget(m_modelCombo);
    optionsRow->addSpacing(16);
    m_autoApproveReadCheck = new QCheckBox(i18n("Auto-approve Read"), this);
    m_autoApproveReadCheck->setChecked(true);
    optionsRow->addWidget(m_autoApproveReadCheck);
    optionsRow->addStretch();
    mainLayout->addLayout(optionsRow);

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

    if (!m_useExistingDir) {
        m_folderNameEdit->setText(generateFolderName(prompt));
        m_worktreeNameEdit->setText(generateWorktreeName(prompt));
    }

    updatePreview();
}

void ClaudeSessionWizard::onFolderNameChanged(const QString &name)
{
    Q_UNUSED(name);
    QString dir = selectedDirectory();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        detectGitState(dir);
        if (m_isGitRepo) {
            m_gitModeCombo->setCurrentIndex(GitWorktree);
            m_sourceRepoEdit->setText(m_repoRoot);
        } else {
            m_gitModeCombo->setCurrentIndex(GitInit);
            m_sourceRepoEdit->clear();
        }
    }
    updatePreview();
}

void ClaudeSessionWizard::onProjectRootChanged(const QString &path)
{
    Q_UNUSED(path);
    updateFolderCompleter();
    QString dir = selectedDirectory();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        detectGitState(dir);
        if (m_isGitRepo) {
            m_gitModeCombo->setCurrentIndex(GitWorktree);
            m_sourceRepoEdit->setText(m_repoRoot);
        } else {
            m_gitModeCombo->setCurrentIndex(GitInit);
            m_sourceRepoEdit->clear();
        }
    }
    updatePreview();
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
        return;
    }

    QProcess git;
    git.setWorkingDirectory(path);
    git.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    git.waitForFinished(3000);

    if (git.exitCode() == 0) {
        m_isGitRepo = true;
        m_repoRoot = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
    } else {
        m_isGitRepo = false;
        m_repoRoot.clear();
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

    // Update path label
    if (remote) {
        m_pathLabel->setText(i18n("Remote path:"));
        m_projectRootEdit->setPlaceholderText(i18n("~/projects"));
        m_browseRootButton->setEnabled(false); // Can't browse remote filesystem
    } else {
        m_pathLabel->setText(i18n("Workspace root:"));
        m_projectRootEdit->setPlaceholderText(i18n("~/projects"));
        m_browseRootButton->setEnabled(true);
    }
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
    args << QStringLiteral("echo") << QStringLiteral("ok");

    auto *process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus) {
        process->deleteLater();
        m_testConnectionButton->setEnabled(true);

        if (exitCode == 0) {
            m_connectionStatusLabel->setText(i18n("Connected"));
            m_connectionStatusLabel->setStyleSheet(QStringLiteral("color: green;"));
        } else {
            QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
            if (error.isEmpty()) {
                error = i18n("Connection failed (exit %1)", exitCode);
            }
            m_connectionStatusLabel->setText(error.left(50));
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
                const QStringList hostList = hosts.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                for (const QString &h : hostList) {
                    if (!h.contains(QLatin1Char('*')) && !h.contains(QLatin1Char('?'))) {
                        m_sshConfigCombo->addItem(h);
                    }
                }
            }
        }
    }
}

} // namespace Konsolai

#include "moc_ClaudeSessionWizard.cpp"
