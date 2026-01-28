/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionWizard.h"
#include "KonsolaiSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace Konsolai
{

ClaudeSessionWizard::ClaudeSessionWizard(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(i18n("New Claude Session"));
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(650, 550);
    resize(700, 600);

    // Setup widgets before creating pages
    setupPromptPage();
    setupConfirmPage();

    // Add pages
    setPage(PromptPageId, new PromptPage(this));
    setPage(ConfirmPageId, new ConfirmPage(this));

    // Load defaults from settings
    if (KonsolaiSettings *settings = KonsolaiSettings::instance()) {
        m_projectRootEdit->setText(settings->projectRoot());
        m_gitRemoteEdit->setText(settings->gitRemoteRoot());
        m_initGitCheck->setChecked(settings->autoInitGit());

        // Worktree settings
        QString sourceRepo = settings->worktreeSourceRepo();
        if (!sourceRepo.isEmpty()) {
            m_sourceRepoEdit->setText(sourceRepo);
        }
        bool useWorktrees = settings->useWorktrees();
        m_createWorktreeCheck->setChecked(useWorktrees);
        // Manually trigger the toggle to update enabled states
        m_sourceRepoEdit->setEnabled(useWorktrees);
        m_browseRepoButton->setEnabled(useWorktrees);
        m_worktreeNameEdit->setEnabled(useWorktrees);
        if (useWorktrees) {
            m_initGitCheck->setEnabled(false);
            m_initGitCheck->setChecked(false);
        }

        QString model = settings->defaultModel();
        int idx = m_modelCombo->findText(model);
        if (idx >= 0) {
            m_modelCombo->setCurrentIndex(idx);
        }
    }
}

ClaudeSessionWizard::~ClaudeSessionWizard() = default;

void ClaudeSessionWizard::setProfile(const Konsole::Profile::Ptr &profile)
{
    m_profile = profile;
}

void ClaudeSessionWizard::setDefaultDirectory(const QString &path)
{
    // For the prompt-centric wizard, we don't use the default directory
    // to lock fields - the user enters a prompt and folder name is auto-generated.
    // We just store it in case it's needed later.
    m_defaultDirectory = path;
    // Don't set m_useExistingDir = true - we want the prompt-centric flow
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
    // Don't init git if creating a worktree (it's already a git repo)
    if (m_createWorktreeCheck && m_createWorktreeCheck->isChecked()) {
        return false;
    }
    return m_initGitCheck && m_initGitCheck->isChecked() && !m_isGitRepo;
}

QString ClaudeSessionWizard::worktreeBranch() const
{
    // Only return worktree branch if worktree mode is enabled
    if (!m_createWorktreeCheck || !m_createWorktreeCheck->isChecked()) {
        return QString();
    }
    if (!m_worktreeNameEdit || m_worktreeNameEdit->text().isEmpty()) {
        return QString();
    }
    return m_worktreeNameEdit->text();
}

QString ClaudeSessionWizard::repoRoot() const
{
    // Return the source repo path when worktree mode is enabled
    if (m_createWorktreeCheck && m_createWorktreeCheck->isChecked() && m_sourceRepoEdit) {
        return m_sourceRepoEdit->text();
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

void ClaudeSessionWizard::setupPromptPage()
{
    // Prompt input
    m_promptEdit = new QPlainTextEdit(this);
    m_promptEdit->setPlaceholderText(i18n("Describe what you want to build...\n\nExample: A REST API for managing todo items with user authentication"));
    m_promptEdit->setMaximumHeight(100);
    connect(m_promptEdit, &QPlainTextEdit::textChanged, this, &ClaudeSessionWizard::onPromptChanged);

    // Project root (workspace directory, e.g., ~/projects)
    m_projectRootEdit = new QLineEdit(this);
    m_projectRootEdit->setPlaceholderText(i18n("~/projects"));
    connect(m_projectRootEdit, &QLineEdit::textChanged, this, &ClaudeSessionWizard::onProjectRootChanged);

    // Browse button for project root
    m_browseRootButton = new QPushButton(i18n("Browse..."), this);
    connect(m_browseRootButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Workspace Root"), m_projectRootEdit->text());
        if (!dir.isEmpty()) {
            m_projectRootEdit->setText(dir);
        }
    });

    // Folder name (auto-generated from prompt)
    m_folderNameEdit = new QLineEdit(this);
    m_folderNameEdit->setPlaceholderText(i18n("my-project-name"));
    connect(m_folderNameEdit, &QLineEdit::textChanged, this, &ClaudeSessionWizard::onFolderNameChanged);

    // Create as worktree checkbox
    m_createWorktreeCheck = new QCheckBox(i18n("Create as git worktree"), this);
    connect(m_createWorktreeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_sourceRepoEdit->setEnabled(checked);
        m_browseRepoButton->setEnabled(checked);
        m_worktreeNameEdit->setEnabled(checked);
        // When worktree is enabled, disable git init (worktree is already a git repo)
        m_initGitCheck->setEnabled(!checked);
        if (checked) {
            m_initGitCheck->setChecked(false);
        } else {
            // Re-enable git init when worktree is unchecked
            m_initGitCheck->setChecked(true);
        }
        updatePreview();
    });

    // Source repo for worktree (the main repo to create worktree from)
    m_sourceRepoEdit = new QLineEdit(this);
    m_sourceRepoEdit->setPlaceholderText(i18n("/path/to/main/repo"));
    m_sourceRepoEdit->setEnabled(false);
    connect(m_sourceRepoEdit, &QLineEdit::textChanged, this, [this]() {
        updatePreview();
    });

    // Browse button for source repo
    m_browseRepoButton = new QPushButton(i18n("Browse..."), this);
    m_browseRepoButton->setEnabled(false);
    connect(m_browseRepoButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Source Repository"), m_sourceRepoEdit->text());
        if (!dir.isEmpty()) {
            m_sourceRepoEdit->setText(dir);
        }
    });

    // Worktree/branch name (auto-generated, fuller version)
    m_worktreeNameEdit = new QLineEdit(this);
    m_worktreeNameEdit->setPlaceholderText(i18n("feature/project-name-description"));
    m_worktreeNameEdit->setEnabled(false);

    // Preview label
    m_previewLabel = new QLabel(this);
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
}

void ClaudeSessionWizard::setupConfirmPage()
{
    // Model selection
    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem(QStringLiteral("claude-sonnet-4"));
    m_modelCombo->addItem(QStringLiteral("claude-opus-4"));
    m_modelCombo->addItem(QStringLiteral("claude-haiku"));

    // Auto-approve read
    m_autoApproveReadCheck = new QCheckBox(i18n("Auto-approve Read permissions"), this);

    // Git init checkbox
    m_initGitCheck = new QCheckBox(i18n("Initialize git repository"), this);
    m_initGitCheck->setChecked(true);

    // Git remote
    m_gitRemoteEdit = new QLineEdit(this);
    m_gitRemoteEdit->setPlaceholderText(i18n("git@github.com:username/"));

    // Summary label
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
}

void ClaudeSessionWizard::onPromptChanged()
{
    QString prompt = m_promptEdit->toPlainText();
    m_taskPrompt = prompt;

    if (!m_useExistingDir) {
        // Generate folder name from prompt
        QString folderName = generateFolderName(prompt);
        m_folderNameEdit->setText(folderName);

        // Generate worktree name (fuller version)
        QString worktreeName = generateWorktreeName(prompt);
        m_worktreeNameEdit->setText(worktreeName);
    }

    updatePreview();
}

void ClaudeSessionWizard::onFolderNameChanged(const QString &name)
{
    Q_UNUSED(name);
    updatePreview();
}

void ClaudeSessionWizard::onProjectRootChanged(const QString &path)
{
    Q_UNUSED(path);
    updatePreview();
}

QString ClaudeSessionWizard::generateFolderName(const QString &prompt) const
{
    if (prompt.isEmpty()) {
        return QString();
    }

    // Take first few words and convert to kebab-case
    static QRegularExpression wordSplit(QStringLiteral("[\\s_]+"));
    static QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));

    QString lower = prompt.toLower();
    QStringList words = lower.split(wordSplit, Qt::SkipEmptyParts);

    // Take first 3-4 words
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

    // Limit length
    if (result.length() > 30) {
        result = result.left(30);
        // Remove trailing dash
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

    // Generate a fuller branch-style name
    static QRegularExpression wordSplit(QStringLiteral("[\\s_]+"));
    static QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));

    QString lower = prompt.toLower();
    QStringList words = lower.split(wordSplit, Qt::SkipEmptyParts);

    // Take more words for the branch name
    int maxWords = qMin(8, words.size());
    QString result = QStringLiteral("feature/");
    for (int i = 0; i < maxWords; ++i) {
        QString word = words[i];
        word.remove(nonAlnum);
        if (!word.isEmpty()) {
            if (result.length() > 8) { // After "feature/"
                result += QLatin1Char('-');
            }
            result += word;
        }
    }

    // Limit length
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

void ClaudeSessionWizard::updatePreview()
{
    QString dir = selectedDirectory();
    if (dir.isEmpty()) {
        m_previewLabel->setText(i18n("Enter a task description to generate project name"));
        return;
    }

    QString preview;
    if (m_createWorktreeCheck && m_createWorktreeCheck->isChecked()) {
        QString branch = m_worktreeNameEdit ? m_worktreeNameEdit->text() : QString();
        QString repo = m_sourceRepoEdit ? m_sourceRepoEdit->text() : QString();
        if (!branch.isEmpty() && !repo.isEmpty()) {
            preview = i18n("Will create worktree: %1\nFrom repo: %2\nBranch: %3", dir, repo, branch);
        } else {
            preview = i18n("Will create worktree: %1\n(Select source repo and branch)", dir);
        }
    } else {
        preview = i18n("Will create folder: %1", dir);
        if (m_initGitCheck && m_initGitCheck->isChecked()) {
            preview += i18n("\nWith git repository initialized");
        }
    }
    m_previewLabel->setText(preview);
}

// ============================================================================
// PromptPage
// ============================================================================

PromptPage::PromptPage(ClaudeSessionWizard *wizard)
    : QWizardPage(wizard)
    , m_wizard(wizard)
{
    setTitle(i18n("What do you want to build?"));
    setSubTitle(i18n("Describe your project and we'll set everything up."));

    auto *layout = new QVBoxLayout(this);

    // Prompt section
    auto *promptGroup = new QGroupBox(i18n("Task Description"), this);
    auto *promptLayout = new QVBoxLayout(promptGroup);
    promptLayout->addWidget(wizard->m_promptEdit);
    layout->addWidget(promptGroup);

    // Project location section
    auto *locationGroup = new QGroupBox(i18n("Project Location"), this);
    auto *locationLayout = new QGridLayout(locationGroup);

    locationLayout->addWidget(new QLabel(i18n("Workspace root:"), this), 0, 0);
    auto *rootLayout = new QHBoxLayout();
    rootLayout->addWidget(wizard->m_projectRootEdit);
    rootLayout->addWidget(wizard->m_browseRootButton);
    locationLayout->addLayout(rootLayout, 0, 1);

    locationLayout->addWidget(new QLabel(i18n("Folder name:"), this), 1, 0);
    locationLayout->addWidget(wizard->m_folderNameEdit, 1, 1);

    layout->addWidget(locationGroup);

    // Git worktree section
    auto *worktreeGroup = new QGroupBox(i18n("Git Worktree (Optional)"), this);
    auto *worktreeLayout = new QGridLayout(worktreeGroup);

    worktreeLayout->addWidget(wizard->m_createWorktreeCheck, 0, 0, 1, 2);

    worktreeLayout->addWidget(new QLabel(i18n("Source repo:"), this), 1, 0);
    auto *repoLayout = new QHBoxLayout();
    repoLayout->addWidget(wizard->m_sourceRepoEdit);
    repoLayout->addWidget(wizard->m_browseRepoButton);
    worktreeLayout->addLayout(repoLayout, 1, 1);

    worktreeLayout->addWidget(new QLabel(i18n("Branch name:"), this), 2, 0);
    worktreeLayout->addWidget(wizard->m_worktreeNameEdit, 2, 1);

    layout->addWidget(worktreeGroup);

    // Preview
    layout->addWidget(wizard->m_previewLabel);
    layout->addStretch();

    // Register field for validation
    registerField(QStringLiteral("folderName*"), wizard->m_folderNameEdit);
}

bool PromptPage::isComplete() const
{
    return !m_wizard->m_folderNameEdit->text().isEmpty() && !m_wizard->m_projectRootEdit->text().isEmpty();
}

bool PromptPage::validatePage()
{
    QString dir = m_wizard->selectedDirectory();
    qDebug() << "PromptPage::validatePage() - dir:" << dir;

    if (dir.isEmpty()) {
        QMessageBox::warning(const_cast<PromptPage *>(this), i18n("Error"), i18n("Please enter a project name."));
        return false;
    }

    // Check if directory already exists
    if (QDir(dir).exists() && !m_wizard->m_useExistingDir) {
        auto result = QMessageBox::question(const_cast<PromptPage *>(this),
                                            i18n("Directory Exists"),
                                            i18n("The directory '%1' already exists. Use it anyway?", dir),
                                            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return false;
        }
        m_wizard->m_useExistingDir = true;
    }

    // Check if parent directory exists
    QString parentDir = m_wizard->m_projectRootEdit->text();
    if (!QDir(parentDir).exists()) {
        auto result = QMessageBox::question(const_cast<PromptPage *>(this),
                                            i18n("Create Directory"),
                                            i18n("The project root '%1' does not exist. Create it?", parentDir),
                                            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return false;
        }
        QDir().mkpath(parentDir);
    }

    m_wizard->m_selectedDirectory = dir;
    return true;
}

// ============================================================================
// ConfirmPage
// ============================================================================

ConfirmPage::ConfirmPage(ClaudeSessionWizard *wizard)
    : QWizardPage(wizard)
    , m_wizard(wizard)
{
    setTitle(i18n("Confirm Settings"));
    setSubTitle(i18n("Review and adjust settings before creating the session."));

    auto *layout = new QVBoxLayout(this);

    // Summary
    layout->addWidget(wizard->m_summaryLabel);

    // Git settings
    auto *gitGroup = new QGroupBox(i18n("Git Settings"), this);
    auto *gitLayout = new QVBoxLayout(gitGroup);
    gitLayout->addWidget(wizard->m_initGitCheck);

    auto *remoteLayout = new QHBoxLayout();
    remoteLayout->addWidget(new QLabel(i18n("Remote URL prefix:"), this));
    remoteLayout->addWidget(wizard->m_gitRemoteEdit);
    gitLayout->addLayout(remoteLayout);

    layout->addWidget(gitGroup);

    // Claude settings
    auto *claudeGroup = new QGroupBox(i18n("Claude Settings"), this);
    auto *claudeLayout = new QGridLayout(claudeGroup);

    claudeLayout->addWidget(new QLabel(i18n("Model:"), this), 0, 0);
    claudeLayout->addWidget(wizard->m_modelCombo, 0, 1);
    claudeLayout->addWidget(wizard->m_autoApproveReadCheck, 1, 0, 1, 2);

    layout->addWidget(claudeGroup);

    layout->addStretch();
}

void ConfirmPage::initializePage()
{
    QString dir = m_wizard->selectedDirectory();
    QString prompt = m_wizard->m_taskPrompt;

    QString summary;
    if (!prompt.isEmpty()) {
        summary = i18n("<b>Task:</b> %1<br><br>", prompt.left(100) + (prompt.length() > 100 ? QStringLiteral("...") : QString()));
    }
    summary += i18n("<b>Directory:</b> %1<br>", dir);
    summary += i18n("<b>Branch:</b> %1", m_wizard->m_worktreeNameEdit->text());

    m_wizard->m_summaryLabel->setText(summary);

    // Check if target directory is already a git repo
    m_wizard->detectGitState(dir);
    m_wizard->m_initGitCheck->setEnabled(!m_wizard->m_isGitRepo);
    if (m_wizard->m_isGitRepo) {
        m_wizard->m_initGitCheck->setChecked(false);
        m_wizard->m_initGitCheck->setText(i18n("Already a git repository"));
    }
}

bool ConfirmPage::validatePage()
{
    QString dir = m_wizard->selectedDirectory();

    // Create directory if needed
    if (!QDir(dir).exists()) {
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(const_cast<ConfirmPage *>(this), i18n("Error"), i18n("Failed to create directory: %1", dir));
            return false;
        }
        qDebug() << "ConfirmPage: Created directory:" << dir;
    }

    // Init git if requested
    if (m_wizard->shouldInitGit()) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start(QStringLiteral("git"), {QStringLiteral("init")});
        git.waitForFinished(5000);

        if (git.exitCode() == 0) {
            qDebug() << "ConfirmPage: Initialized git repo in:" << dir;

            // Set up remote if configured
            QString remoteRoot = m_wizard->m_gitRemoteEdit->text();
            if (!remoteRoot.isEmpty()) {
                QString repoName = QDir(dir).dirName();
                QString remoteUrl = remoteRoot + repoName + QStringLiteral(".git");

                QProcess gitRemote;
                gitRemote.setWorkingDirectory(dir);
                gitRemote.start(QStringLiteral("git"), {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remoteUrl});
                gitRemote.waitForFinished(5000);

                if (gitRemote.exitCode() == 0) {
                    qDebug() << "ConfirmPage: Added remote origin:" << remoteUrl;
                }
            }
        } else {
            qWarning() << "ConfirmPage: Failed to init git:" << git.readAllStandardError();
        }
    }

    qDebug() << "ConfirmPage: Session will use directory:" << dir;
    return true;
}

} // namespace Konsolai

// Include MOC
#include "moc_ClaudeSessionWizard.cpp"
