/*
    SPDX-FileCopyrightText: 2025 Konsolai Authors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDE_SESSION_WIZARD_H
#define CLAUDE_SESSION_WIZARD_H

#include "konsoleprivate_export.h"

#include <QDialog>
#include <QDir>
#include <QStringList>

#include "profile/Profile.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QGroupBox;
class QRadioButton;
class QButtonGroup;
class QSpinBox;
class QDoubleSpinBox;

namespace Konsolai
{

/**
 * Single-screen dialog for creating new Claude sessions.
 *
 * Layout (top to bottom):
 * - Workspace root (top, not focused)
 * - Task prompt (center, focused on open)
 * - Folder name (auto-inferred from prompt, with browse)
 * - Git (Optional) panel: combo box for git mode
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

    // SSH remote session support
    bool isRemoteSession() const;
    QString sshHost() const;
    QString sshUsername() const;
    int sshPort() const;
    QString sshConfigEntry() const;

    // Budget controls
    int budgetTimeLimitMinutes() const;
    double budgetCostCeilingUSD() const;
    quint64 budgetTokenCeiling() const;
    int budgetPolicy() const; // 0 = Soft, 1 = Hard

private Q_SLOTS:
    void onPromptChanged();
    void onFolderNameChanged(const QString &name);
    void onProjectRootChanged(const QString &path);
    void onCreatePressed();
    void onLocationChanged();
    void onTestConnectionClicked();

private:
    void setupUi();
    void updateFolderCompleter();
    QString generateFolderName(const QString &prompt) const;
    QString generateWorktreeName(const QString &prompt) const;
    void detectGitState(const QString &path);
    QStringList getWorktrees(const QString &repoRoot);
    void updatePreview();
    void updateSshVisibility();
    void loadSshConfigHosts();

    // Git mode enum
    enum GitMode {
        GitInit = 0,
        GitWorktree = 1,
        GitCurrentBranch = 2,
        GitNone = 3
    };

    void updateGitSubFields();

    // Widgets
    QButtonGroup *m_locationGroup = nullptr;
    QRadioButton *m_localRadio = nullptr;
    QRadioButton *m_remoteRadio = nullptr;

    // SSH Connection widgets
    QGroupBox *m_sshGroup = nullptr;
    QLineEdit *m_sshHostEdit = nullptr;
    QLineEdit *m_sshUsernameEdit = nullptr;
    QLineEdit *m_sshPortEdit = nullptr;
    QCheckBox *m_useSshConfigCheck = nullptr;
    QComboBox *m_sshConfigCombo = nullptr;
    QPushButton *m_testConnectionButton = nullptr;
    QLabel *m_connectionStatusLabel = nullptr;

    // Local/shared widgets
    QLabel *m_pathLabel = nullptr;
    QLineEdit *m_projectRootEdit = nullptr;
    QPushButton *m_browseRootButton = nullptr;
    QLineEdit *m_promptEdit = nullptr;
    QLineEdit *m_folderNameEdit = nullptr;
    QPushButton *m_browseFolderButton = nullptr;
    QGroupBox *m_gitGroup = nullptr;
    QComboBox *m_gitModeCombo = nullptr;
    QLabel *m_remotePrefixLabel = nullptr;
    QLineEdit *m_gitRemoteEdit = nullptr;
    QLabel *m_sourceRepoLabel = nullptr;
    QLineEdit *m_sourceRepoEdit = nullptr;
    QPushButton *m_browseRepoButton = nullptr;
    QLabel *m_branchNameLabel = nullptr;
    QLineEdit *m_worktreeNameEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QCheckBox *m_autoApproveReadCheck = nullptr;
    QLabel *m_previewLabel = nullptr;

    // Budget Controls widgets
    QGroupBox *m_budgetGroup = nullptr;
    QSpinBox *m_timeLimitSpin = nullptr;
    QDoubleSpinBox *m_costCeilingSpin = nullptr;
    QSpinBox *m_tokenCeilingSpin = nullptr;
    QComboBox *m_budgetPolicyCombo = nullptr;

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
