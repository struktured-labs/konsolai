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

#include "konsoleprivate_export.h"
#include "profile/Profile.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QFileSystemModel;
class QTreeView;
class QPlainTextEdit;

namespace Konsolai
{

// Forward declarations
class PromptPage;
class ConfirmPage;

/**
 * Wizard for creating new Claude sessions.
 *
 * Prompt-centric design:
 * 1. User enters a task description (prompt)
 * 2. Folder and worktree names are auto-generated from prompt
 * 3. Settings are pre-populated with sensible defaults
 *
 * Flow:
 * - Enter prompt/task description
 * - Folder name auto-generated (truncated, kebab-case)
 * - Worktree name auto-generated (fuller version)
 * - Project root defaults to ~/projects (configurable)
 * - Git init is automatic
 */
class KONSOLEPRIVATE_EXPORT ClaudeSessionWizard : public QWizard
{
    Q_OBJECT

    friend class PromptPage;
    friend class ConfirmPage;

public:
    enum PageId {
        PromptPageId,
        ConfirmPageId
    };

    explicit ClaudeSessionWizard(QWidget *parent = nullptr);
    ~ClaudeSessionWizard() override;

    /**
     * Set the profile for this session
     */
    void setProfile(const Konsole::Profile::Ptr &profile);

    /**
     * Set the default directory to show (overrides project root)
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

    /**
     * Get the task prompt
     */
    QString taskPrompt() const;

private Q_SLOTS:
    void onPromptChanged();
    void onFolderNameChanged(const QString &name);
    void onProjectRootChanged(const QString &path);

private:
    void setupPromptPage();
    void setupConfirmPage();
    QString generateFolderName(const QString &prompt) const;
    QString generateWorktreeName(const QString &prompt) const;
    void detectGitState(const QString &path);
    QStringList getWorktrees(const QString &repoRoot);
    void updatePreview();

    // Prompt page widgets
    QPlainTextEdit *m_promptEdit = nullptr;
    QLineEdit *m_folderNameEdit = nullptr;
    QLineEdit *m_worktreeNameEdit = nullptr;
    QLineEdit *m_projectRootEdit = nullptr;
    QPushButton *m_browseRootButton = nullptr;
    QCheckBox *m_createWorktreeCheck = nullptr;
    QLineEdit *m_sourceRepoEdit = nullptr;
    QPushButton *m_browseRepoButton = nullptr;
    QLabel *m_previewLabel = nullptr;

    // Confirm page widgets
    QComboBox *m_modelCombo = nullptr;
    QCheckBox *m_autoApproveReadCheck = nullptr;
    QCheckBox *m_initGitCheck = nullptr;
    QLineEdit *m_gitRemoteEdit = nullptr;
    QLabel *m_summaryLabel = nullptr;

    // State
    Konsole::Profile::Ptr m_profile;
    QString m_defaultDirectory;
    QString m_selectedDirectory;
    QString m_repoRoot;
    QString m_taskPrompt;
    bool m_isGitRepo = false;
    bool m_useExistingDir = false;
};

/**
 * Prompt input wizard page - main page where user enters task
 */
class PromptPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit PromptPage(ClaudeSessionWizard *wizard);

    bool isComplete() const override;
    bool validatePage() override;

private:
    ClaudeSessionWizard *m_wizard;
};

/**
 * Confirmation wizard page - review settings before creating
 */
class ConfirmPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit ConfirmPage(ClaudeSessionWizard *wizard);

    void initializePage() override;
    bool validatePage() override;

private:
    ClaudeSessionWizard *m_wizard;
};

} // namespace Konsolai

#endif // CLAUDE_SESSION_WIZARD_H
