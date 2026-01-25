/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionWizard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeView>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace Konsolai
{

ClaudeSessionWizard::ClaudeSessionWizard(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(i18n("New Claude Session"));
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(600, 500);

    // Setup pages
    setupDirectoryPage();
    setupGitOptionsPage();
    setupSessionOptionsPage();

    // Set page order
    setPage(DirectoryPageId, new DirectorySelectionPage(this));
    setPage(GitOptionsPageId, new GitOptionsPage(this));
    setPage(SessionOptionsPageId, new SessionOptionsPage(this));
}

ClaudeSessionWizard::~ClaudeSessionWizard() = default;

void ClaudeSessionWizard::setProfile(const Konsole::Profile::Ptr &profile)
{
    m_profile = profile;

    // Pre-populate from profile if available
    if (m_profile) {
        QString model = m_profile->property<QString>(Konsole::Profile::ClaudeModel);
        if (!model.isEmpty() && m_modelCombo) {
            int idx = m_modelCombo->findText(model);
            if (idx >= 0) {
                m_modelCombo->setCurrentIndex(idx);
            }
        }

        if (m_autoApproveReadCheck) {
            m_autoApproveReadCheck->setChecked(m_profile->property<bool>(Konsole::Profile::ClaudeAutoApproveRead));
        }

        QString args = m_profile->property<QString>(Konsole::Profile::ClaudeArgs);
        if (!args.isEmpty() && m_claudeArgsEdit) {
            m_claudeArgsEdit->setText(args);
        }
    }
}

void ClaudeSessionWizard::setDefaultDirectory(const QString &path)
{
    m_defaultDirectory = path.isEmpty() ? QDir::homePath() : path;
    m_selectedDirectory = m_defaultDirectory;

    if (m_directoryEdit) {
        m_directoryEdit->setText(m_defaultDirectory);
    }

    detectGitState(m_defaultDirectory);
}

QString ClaudeSessionWizard::selectedDirectory() const
{
    return m_selectedDirectory;
}

bool ClaudeSessionWizard::shouldInitGit() const
{
    return m_initGitCheck && m_initGitCheck->isChecked() && !m_isGitRepo;
}

QString ClaudeSessionWizard::worktreeBranch() const
{
    if (m_createNewWorktree && m_newWorktreeBranch) {
        return m_newWorktreeBranch->text();
    }
    return QString();
}

QString ClaudeSessionWizard::repoRoot() const
{
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
    if (m_claudeArgsEdit) {
        return m_claudeArgsEdit->text();
    }
    return QString();
}

void ClaudeSessionWizard::setupDirectoryPage()
{
    // Directory edit
    m_directoryEdit = new QLineEdit(this);
    m_directoryEdit->setPlaceholderText(i18n("Enter directory path..."));
    connect(m_directoryEdit, &QLineEdit::textChanged, this, &ClaudeSessionWizard::onDirectoryChanged);

    // Browse button
    m_browseButton = new QPushButton(i18n("Browse..."), this);
    connect(m_browseButton, &QPushButton::clicked, this, &ClaudeSessionWizard::onBrowseClicked);

    // Create folder button
    m_createFolderButton = new QPushButton(i18n("Create New Folder"), this);
    connect(m_createFolderButton, &QPushButton::clicked, this, &ClaudeSessionWizard::onCreateFolderClicked);

    // Recent directories combo
    m_recentDirsCombo = new QComboBox(this);
    m_recentDirsCombo->setPlaceholderText(i18n("Recent directories..."));
    m_recentDirsCombo->addItems(getRecentDirectories());
    connect(m_recentDirsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            QString dir = m_recentDirsCombo->itemText(index);
            m_directoryEdit->setText(dir);
        }
    });

    // File system model for tree view
    m_fileSystemModel = new QFileSystemModel(this);
    m_fileSystemModel->setRootPath(QDir::homePath());
    m_fileSystemModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

    // Directory tree view
    m_dirTreeView = new QTreeView(this);
    m_dirTreeView->setModel(m_fileSystemModel);
    m_dirTreeView->setRootIndex(m_fileSystemModel->index(QDir::homePath()));
    m_dirTreeView->hideColumn(1); // Size
    m_dirTreeView->hideColumn(2); // Type
    m_dirTreeView->hideColumn(3); // Date
    m_dirTreeView->setHeaderHidden(true);

    connect(m_dirTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        QString path = m_fileSystemModel->filePath(current);
        m_directoryEdit->setText(path);
    });
}

void ClaudeSessionWizard::setupGitOptionsPage()
{
    // Git status label
    m_gitStatusLabel = new QLabel(this);

    // Current branch label
    m_currentBranchLabel = new QLabel(this);

    // Init git checkbox (for non-git directories)
    m_initGitCheck = new QCheckBox(i18n("Initialize git repository"), this);
    m_initGitCheck->setToolTip(i18n("Create a new git repository in this directory"));

    // Worktree combo (for git directories)
    m_worktreeCombo = new QComboBox(this);
    m_worktreeCombo->setToolTip(i18n("Select an existing worktree or create a new one"));
    connect(m_worktreeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ClaudeSessionWizard::onWorktreeSelectionChanged);

    // New worktree branch name
    m_newWorktreeBranch = new QLineEdit(this);
    m_newWorktreeBranch->setPlaceholderText(i18n("New branch name..."));
    m_newWorktreeBranch->setVisible(false);
}

void ClaudeSessionWizard::setupSessionOptionsPage()
{
    // Model selection
    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem(QStringLiteral("claude-sonnet-4"));
    m_modelCombo->addItem(QStringLiteral("claude-opus-4"));
    m_modelCombo->addItem(QStringLiteral("claude-haiku"));

    // Auto-approve read
    m_autoApproveReadCheck = new QCheckBox(i18n("Auto-approve Read permissions"), this);
    m_autoApproveReadCheck->setToolTip(i18n("Automatically approve file read operations without prompting"));

    // Claude args
    m_claudeArgsEdit = new QLineEdit(this);
    m_claudeArgsEdit->setPlaceholderText(i18n("Additional Claude CLI arguments..."));
}

void ClaudeSessionWizard::onDirectoryChanged(const QString &path)
{
    m_selectedDirectory = path;
    detectGitState(path);
}

void ClaudeSessionWizard::onBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    i18n("Select Working Directory"),
                                                    m_selectedDirectory.isEmpty() ? QDir::homePath() : m_selectedDirectory,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        m_directoryEdit->setText(dir);
    }
}

void ClaudeSessionWizard::onCreateFolderClicked()
{
    QString basePath = m_selectedDirectory.isEmpty() ? QDir::homePath() : m_selectedDirectory;
    QString folderName = QInputDialog::getText(this, i18n("Create New Folder"), i18n("Folder name:"), QLineEdit::Normal, QString());

    if (!folderName.isEmpty()) {
        QDir dir(basePath);
        if (dir.mkdir(folderName)) {
            QString newPath = dir.filePath(folderName);
            m_directoryEdit->setText(newPath);
        } else {
            QMessageBox::warning(this, i18n("Error"), i18n("Failed to create folder: %1", folderName));
        }
    }
}

void ClaudeSessionWizard::onWorktreeSelectionChanged(int index)
{
    if (index < 0) {
        return;
    }

    QString selection = m_worktreeCombo->itemText(index);

    if (selection == i18n("Create new worktree...")) {
        m_createNewWorktree = true;
        m_newWorktreeBranch->setVisible(true);
    } else {
        m_createNewWorktree = false;
        m_newWorktreeBranch->setVisible(false);

        // If selecting an existing worktree, update the directory
        QString worktreePath = m_worktreeCombo->itemData(index).toString();
        if (!worktreePath.isEmpty()) {
            m_directoryEdit->setText(worktreePath);
        }
    }
}

void ClaudeSessionWizard::detectGitState(const QString &path)
{
    if (path.isEmpty() || !QDir(path).exists()) {
        m_isGitRepo = false;
        m_repoRoot.clear();
        return;
    }

    // Check if inside a git repository
    QProcess git;
    git.setWorkingDirectory(path);
    git.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    git.waitForFinished(3000);

    if (git.exitCode() == 0) {
        m_isGitRepo = true;
        m_repoRoot = QString::fromUtf8(git.readAllStandardOutput()).trimmed();

        // Get current branch
        QProcess branchProcess;
        branchProcess.setWorkingDirectory(path);
        branchProcess.start(QStringLiteral("git"), {QStringLiteral("branch"), QStringLiteral("--show-current")});
        branchProcess.waitForFinished(3000);
        QString currentBranch = QString::fromUtf8(branchProcess.readAllStandardOutput()).trimmed();

        if (m_currentBranchLabel) {
            m_currentBranchLabel->setText(i18n("Current branch: %1", currentBranch));
        }

        if (m_gitStatusLabel) {
            m_gitStatusLabel->setText(i18n("Git repository detected"));
        }

        // Populate worktrees
        if (m_worktreeCombo) {
            m_worktreeCombo->clear();
            m_worktreeCombo->addItem(i18n("(stay in current directory)"));

            QStringList worktrees = getWorktrees(m_repoRoot);
            for (const QString &wt : worktrees) {
                QStringList parts = wt.split(QLatin1Char('\t'));
                if (parts.size() >= 2) {
                    QString wtPath = parts[0];
                    QString wtBranch = parts[1];
                    m_worktreeCombo->addItem(QStringLiteral("%1 (%2)").arg(wtBranch, wtPath), wtPath);
                }
            }

            m_worktreeCombo->addItem(i18n("Create new worktree..."));
            m_worktreeCombo->setEnabled(true);
        }

        if (m_initGitCheck) {
            m_initGitCheck->setEnabled(false);
            m_initGitCheck->setChecked(false);
        }

    } else {
        m_isGitRepo = false;
        m_repoRoot.clear();

        if (m_gitStatusLabel) {
            m_gitStatusLabel->setText(i18n("Not a git repository"));
        }

        if (m_currentBranchLabel) {
            m_currentBranchLabel->clear();
        }

        if (m_worktreeCombo) {
            m_worktreeCombo->clear();
            m_worktreeCombo->setEnabled(false);
        }

        if (m_initGitCheck) {
            m_initGitCheck->setEnabled(true);
        }
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
            currentPath = line.mid(9); // "worktree " is 9 chars
            currentBranch.clear();
        } else if (line.startsWith(QLatin1String("branch "))) {
            currentBranch = line.mid(7); // "branch " is 7 chars
            // Remove refs/heads/ prefix
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

QStringList ClaudeSessionWizard::getRecentDirectories() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ClaudeSession"));
    QStringList dirs = settings.value(QStringLiteral("RecentDirectories")).toStringList();
    settings.endGroup();

    // Filter out non-existent directories
    QStringList existing;
    for (const QString &dir : dirs) {
        if (QDir(dir).exists()) {
            existing << dir;
        }
    }

    return existing;
}

void ClaudeSessionWizard::saveRecentDirectory(const QString &path)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ClaudeSession"));

    QStringList dirs = settings.value(QStringLiteral("RecentDirectories")).toStringList();
    dirs.removeAll(path);
    dirs.prepend(path);

    // Keep only last 10
    while (dirs.size() > 10) {
        dirs.removeLast();
    }

    settings.setValue(QStringLiteral("RecentDirectories"), dirs);
    settings.endGroup();
}

// ============================================================================
// DirectorySelectionPage
// ============================================================================

DirectorySelectionPage::DirectorySelectionPage(ClaudeSessionWizard *wizard)
    : QWizardPage(wizard)
    , m_wizard(wizard)
{
    setTitle(i18n("Select Working Directory"));
    setSubTitle(i18n("Choose the directory where your Claude session will run."));

    auto *layout = new QVBoxLayout(this);

    // Path input area
    auto *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(wizard->m_directoryEdit);
    pathLayout->addWidget(wizard->m_browseButton);
    layout->addLayout(pathLayout);

    // Buttons
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(wizard->m_createFolderButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    // Recent directories
    auto *recentGroup = new QGroupBox(i18n("Recent Directories"), this);
    auto *recentLayout = new QVBoxLayout(recentGroup);
    recentLayout->addWidget(wizard->m_recentDirsCombo);
    layout->addWidget(recentGroup);

    // Directory tree
    auto *browseGroup = new QGroupBox(i18n("Browse"), this);
    auto *browseLayout = new QVBoxLayout(browseGroup);
    browseLayout->addWidget(wizard->m_dirTreeView);
    layout->addWidget(browseGroup, 1);

    // Register field for validation
    registerField(QStringLiteral("selectedDirectory*"), wizard->m_directoryEdit);
}

bool DirectorySelectionPage::isComplete() const
{
    QString path = m_wizard->m_directoryEdit->text();
    return !path.isEmpty() && QDir(path).exists();
}

bool DirectorySelectionPage::validatePage()
{
    QString path = m_wizard->m_directoryEdit->text();

    if (!QDir(path).exists()) {
        QMessageBox::warning(const_cast<DirectorySelectionPage *>(this), i18n("Invalid Directory"), i18n("The selected directory does not exist."));
        return false;
    }

    // Save to recent directories
    m_wizard->saveRecentDirectory(path);
    return true;
}

// ============================================================================
// GitOptionsPage
// ============================================================================

GitOptionsPage::GitOptionsPage(ClaudeSessionWizard *wizard)
    : QWizardPage(wizard)
    , m_wizard(wizard)
{
    setTitle(i18n("Git Options"));
    setSubTitle(i18n("Configure git repository settings for your session."));

    auto *layout = new QVBoxLayout(this);

    // Git status
    layout->addWidget(wizard->m_gitStatusLabel);
    layout->addWidget(wizard->m_currentBranchLabel);

    // Non-git options
    auto *initGroup = new QGroupBox(i18n("New Repository"), this);
    auto *initLayout = new QVBoxLayout(initGroup);
    initLayout->addWidget(wizard->m_initGitCheck);
    layout->addWidget(initGroup);

    // Worktree options
    auto *worktreeGroup = new QGroupBox(i18n("Worktree"), this);
    auto *worktreeLayout = new QVBoxLayout(worktreeGroup);
    worktreeLayout->addWidget(new QLabel(i18n("Select worktree:"), this));
    worktreeLayout->addWidget(wizard->m_worktreeCombo);
    worktreeLayout->addWidget(wizard->m_newWorktreeBranch);
    layout->addWidget(worktreeGroup);

    layout->addStretch();
}

void GitOptionsPage::initializePage()
{
    // Refresh git state when entering page
    m_wizard->detectGitState(m_wizard->selectedDirectory());
}

bool GitOptionsPage::isComplete() const
{
    // If creating new worktree, need branch name
    if (m_wizard->m_createNewWorktree) {
        return !m_wizard->m_newWorktreeBranch->text().isEmpty();
    }
    return true;
}

// ============================================================================
// SessionOptionsPage
// ============================================================================

SessionOptionsPage::SessionOptionsPage(ClaudeSessionWizard *wizard)
    : QWizardPage(wizard)
    , m_wizard(wizard)
{
    setTitle(i18n("Session Options"));
    setSubTitle(i18n("Configure Claude session settings."));

    auto *layout = new QGridLayout(this);

    // Model selection
    layout->addWidget(new QLabel(i18n("Claude Model:"), this), 0, 0);
    layout->addWidget(wizard->m_modelCombo, 0, 1);

    // Auto-approve read
    layout->addWidget(wizard->m_autoApproveReadCheck, 1, 0, 1, 2);

    // Additional args
    layout->addWidget(new QLabel(i18n("Additional Arguments:"), this), 2, 0);
    layout->addWidget(wizard->m_claudeArgsEdit, 2, 1);

    layout->setRowStretch(3, 1);
}

void SessionOptionsPage::initializePage()
{
    // Nothing special to do
}

} // namespace Konsolai

// Include MOC
#include "moc_ClaudeSessionWizard.cpp"
