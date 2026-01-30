/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDE_SESSION_WIZARD_H
#define CLAUDE_SESSION_WIZARD_H

#include <QDialog>
#include <QDir>
#include <QStringList>

#include "konsoleprivate_export.h"
#include "profile/Profile.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QGroupBox;

namespace Konsolai
{

/**
 * Single-screen dialog for creating new Claude sessions.
 *
 * Layout (top to bottom):
 * - Workspace root (top, not focused)
 * - Task prompt (center, focused on open)
 * - Folder name (auto-inferred from prompt, with browse)
 * - Git (Optional) panel: init + worktree combined
 * - Model and options
 * - Preview
 *
 * Tab order: prompt -> folder name -> git options -> model
 */
class KONSOLEPRIVATE_EXPORT ClaudeSessionWizard : public QDialog
{
    Q_OBJECT

public:
    explicit ClaudeSessionWizard(QWidget *parent = nullptr);
    ~ClaudeSessionWizard() override;

    void setProfile(const Konsole::Profile::Ptr &profile);
    void setDefaultDirectory(const QString &path);

    QString selectedDirectory() const;
    bool shouldInitGit() const;
    QString worktreeBranch() const;
    QString repoRoot() const;
    QString claudeModel() const;
    bool autoApproveRead() const;
    QString claudeArgs() const;
    QString taskPrompt() const;

private Q_SLOTS:
    void onPromptChanged();
    void onFolderNameChanged(const QString &name);
    void onProjectRootChanged(const QString &path);
    void onCreatePressed();

private:
    void setupUi();
    QString generateFolderName(const QString &prompt) const;
    QString generateWorktreeName(const QString &prompt) const;
    void detectGitState(const QString &path);
    QStringList getWorktrees(const QString &repoRoot);
    void updatePreview();

    // Widgets
    QLineEdit *m_projectRootEdit = nullptr;
    QPushButton *m_browseRootButton = nullptr;
    QLineEdit *m_promptEdit = nullptr;
    QLineEdit *m_folderNameEdit = nullptr;
    QPushButton *m_browseFolderButton = nullptr;
    QGroupBox *m_gitGroup = nullptr;
    QCheckBox *m_initGitCheck = nullptr;
    QLineEdit *m_gitRemoteEdit = nullptr;
    QCheckBox *m_createWorktreeCheck = nullptr;
    QLineEdit *m_sourceRepoEdit = nullptr;
    QPushButton *m_browseRepoButton = nullptr;
    QLineEdit *m_worktreeNameEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QCheckBox *m_autoApproveReadCheck = nullptr;
    QLabel *m_previewLabel = nullptr;

    // State
    Konsole::Profile::Ptr m_profile;
    QString m_defaultDirectory;
    QString m_selectedDirectory;
    QString m_repoRoot;
    QString m_taskPrompt;
    bool m_isGitRepo = false;
    bool m_useExistingDir = false;
};

} // namespace Konsolai

#endif // CLAUDE_SESSION_WIZARD_H
