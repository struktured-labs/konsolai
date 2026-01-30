/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeSessionWizard.h"
#include "KonsolaiSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
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
        m_initGitCheck->setChecked(settings->autoInitGit());

        QString sourceRepo = settings->worktreeSourceRepo();
        if (!sourceRepo.isEmpty()) {
            m_sourceRepoEdit->setText(sourceRepo);
        }
        bool useWorktrees = settings->useWorktrees();
        m_createWorktreeCheck->setChecked(useWorktrees);
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
    if (m_createWorktreeCheck && m_createWorktreeCheck->isChecked()) {
        return false;
    }
    return m_initGitCheck && m_initGitCheck->isChecked() && !m_isGitRepo;
}

QString ClaudeSessionWizard::worktreeBranch() const
{
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

void ClaudeSessionWizard::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Workspace root (top) ---
    auto *rootRow = new QHBoxLayout();
    rootRow->addWidget(new QLabel(i18n("Workspace root:"), this));
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
            m_useExistingDir = true;
        }
    });
    folderRow->addWidget(m_browseFolderButton);
    mainLayout->addLayout(folderRow);

    mainLayout->addSpacing(4);

    // --- Git (Optional) panel ---
    m_gitGroup = new QGroupBox(i18n("Git (Optional)"), this);
    auto *gitLayout = new QGridLayout(m_gitGroup);

    // Git init
    m_initGitCheck = new QCheckBox(i18n("Initialize git repository"), this);
    m_initGitCheck->setChecked(true);
    gitLayout->addWidget(m_initGitCheck, 0, 0, 1, 2);

    // Remote URL
    gitLayout->addWidget(new QLabel(i18n("Remote prefix:"), this), 1, 0);
    m_gitRemoteEdit = new QLineEdit(this);
    m_gitRemoteEdit->setPlaceholderText(i18n("git@github.com:username/"));
    gitLayout->addWidget(m_gitRemoteEdit, 1, 1);

    // Separator line via spacing
    auto *worktreeSep = new QLabel(this);
    worktreeSep->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    gitLayout->addWidget(worktreeSep, 2, 0, 1, 2);

    // Worktree option
    m_createWorktreeCheck = new QCheckBox(i18n("Create as git worktree"), this);
    connect(m_createWorktreeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_sourceRepoEdit->setEnabled(checked);
        m_browseRepoButton->setEnabled(checked);
        m_worktreeNameEdit->setEnabled(checked);
        m_initGitCheck->setEnabled(!checked);
        if (checked) {
            m_initGitCheck->setChecked(false);
        } else {
            m_initGitCheck->setChecked(true);
        }
        updatePreview();
    });
    gitLayout->addWidget(m_createWorktreeCheck, 3, 0, 1, 2);

    // Source repo
    gitLayout->addWidget(new QLabel(i18n("Source repo:"), this), 4, 0);
    auto *repoRow = new QHBoxLayout();
    m_sourceRepoEdit = new QLineEdit(this);
    m_sourceRepoEdit->setPlaceholderText(i18n("/path/to/main/repo"));
    m_sourceRepoEdit->setEnabled(false);
    connect(m_sourceRepoEdit, &QLineEdit::textChanged, this, [this]() {
        updatePreview();
    });
    repoRow->addWidget(m_sourceRepoEdit);
    m_browseRepoButton = new QPushButton(i18n("Browse..."), this);
    m_browseRepoButton->setEnabled(false);
    connect(m_browseRepoButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Source Repository"), m_sourceRepoEdit->text());
        if (!dir.isEmpty()) {
            m_sourceRepoEdit->setText(dir);
        }
    });
    repoRow->addWidget(m_browseRepoButton);
    gitLayout->addLayout(repoRow, 4, 1);

    // Branch name
    gitLayout->addWidget(new QLabel(i18n("Branch name:"), this), 5, 0);
    m_worktreeNameEdit = new QLineEdit(this);
    m_worktreeNameEdit->setPlaceholderText(i18n("feature/project-name"));
    m_worktreeNameEdit->setEnabled(false);
    gitLayout->addWidget(m_worktreeNameEdit, 5, 1);

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
    setTabOrder(m_browseFolderButton, m_initGitCheck);
    setTabOrder(m_initGitCheck, m_gitRemoteEdit);
    setTabOrder(m_gitRemoteEdit, m_createWorktreeCheck);
    setTabOrder(m_createWorktreeCheck, m_sourceRepoEdit);
    setTabOrder(m_sourceRepoEdit, m_worktreeNameEdit);
    setTabOrder(m_worktreeNameEdit, m_modelCombo);
    setTabOrder(m_modelCombo, m_autoApproveReadCheck);
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
    updatePreview();
}

void ClaudeSessionWizard::onProjectRootChanged(const QString &path)
{
    Q_UNUSED(path);
    updatePreview();
}

void ClaudeSessionWizard::onCreatePressed()
{
    QString dir = selectedDirectory();

    if (dir.isEmpty()) {
        QMessageBox::warning(this, i18n("Error"), i18n("Please enter a project name."));
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

    // Init git if requested
    if (shouldInitGit()) {
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
        preview = i18n("Will create: %1", dir);
        if (m_initGitCheck && m_initGitCheck->isChecked()) {
            preview += i18n(" (git init)");
        }
    }
    m_previewLabel->setText(preview);
}

} // namespace Konsolai

#include "moc_ClaudeSessionWizard.cpp"
