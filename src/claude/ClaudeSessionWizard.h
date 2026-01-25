/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDE_SESSION_WIZARD_H
#define CLAUDE_SESSION_WIZARD_H

#include <QDir>
#include <QStringList>
#include <QWizard>
#include <QWizardPage>

#include "profile/Profile.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QFileSystemModel;
class QTreeView;

namespace Konsolai
{

// Forward declarations
class DirectorySelectionPage;
class GitOptionsPage;
class SessionOptionsPage;

/**
 * Wizard for creating new Claude sessions.
 *
 * Pages:
 * 1. Directory Selection - Choose working directory
 * 2. Git Options - Git init, worktree selection
 * 3. Session Options - Model, auto-approve settings
 */
class ClaudeSessionWizard : public QWizard
{
    Q_OBJECT

    // Page classes need access to private widgets
    friend class DirectorySelectionPage;
    friend class GitOptionsPage;
    friend class SessionOptionsPage;

public:
    enum PageId {
        DirectoryPageId,
        GitOptionsPageId,
        SessionOptionsPageId
    };

    explicit ClaudeSessionWizard(QWidget *parent = nullptr);
    ~ClaudeSessionWizard() override;

    /**
     * Set the profile for this session
     */
    void setProfile(const Konsole::Profile::Ptr &profile);

    /**
     * Set the default directory to show
     */
    void setDefaultDirectory(const QString &path);

    /**
     * Get the selected working directory
     */
    QString selectedDirectory() const;

    /**
     * Whether to initialize a new git repository
     */
    bool shouldInitGit() const;

    /**
     * Get the worktree branch name (empty if not creating worktree)
     */
    QString worktreeBranch() const;

    /**
     * Get the git repository root (for worktree operations)
     */
    QString repoRoot() const;

    /**
     * Get selected Claude model
     */
    QString claudeModel() const;

    /**
     * Whether to auto-approve read permissions
     */
    bool autoApproveRead() const;

    /**
     * Get additional Claude CLI arguments
     */
    QString claudeArgs() const;

private Q_SLOTS:
    void onDirectoryChanged(const QString &path);
    void onBrowseClicked();
    void onCreateFolderClicked();
    void onWorktreeSelectionChanged(int index);

private:
    void setupDirectoryPage();
    void setupGitOptionsPage();
    void setupSessionOptionsPage();
    void detectGitState(const QString &path);
    QStringList getWorktrees(const QString &repoRoot);
    QStringList getRecentDirectories() const;
    void saveRecentDirectory(const QString &path);

    // Directory page widgets
    QLineEdit *m_directoryEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_createFolderButton = nullptr;
    QComboBox *m_recentDirsCombo = nullptr;
    QTreeView *m_dirTreeView = nullptr;
    QFileSystemModel *m_fileSystemModel = nullptr;

    // Git options page widgets
    QCheckBox *m_initGitCheck = nullptr;
    QComboBox *m_worktreeCombo = nullptr;
    QLineEdit *m_newWorktreeBranch = nullptr;
    QLabel *m_gitStatusLabel = nullptr;
    QLabel *m_currentBranchLabel = nullptr;

    // Session options page widgets
    QComboBox *m_modelCombo = nullptr;
    QCheckBox *m_autoApproveReadCheck = nullptr;
    QLineEdit *m_claudeArgsEdit = nullptr;

    // State
    Konsole::Profile::Ptr m_profile;
    QString m_defaultDirectory;
    QString m_selectedDirectory;
    QString m_repoRoot;
    bool m_isGitRepo = false;
    bool m_createNewWorktree = false;
};

/**
 * Directory selection wizard page
 */
class DirectorySelectionPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit DirectorySelectionPage(ClaudeSessionWizard *wizard);

    bool isComplete() const override;
    bool validatePage() override;

private:
    ClaudeSessionWizard *m_wizard;
};

/**
 * Git options wizard page
 */
class GitOptionsPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit GitOptionsPage(ClaudeSessionWizard *wizard);

    void initializePage() override;
    bool isComplete() const override;

private:
    ClaudeSessionWizard *m_wizard;
};

/**
 * Session options wizard page
 */
class SessionOptionsPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit SessionOptionsPage(ClaudeSessionWizard *wizard);

    void initializePage() override;

private:
    ClaudeSessionWizard *m_wizard;
};

} // namespace Konsolai

#endif // CLAUDE_SESSION_WIZARD_H
