/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_SETTINGS_H
#define KONSOLAI_SETTINGS_H

#include <QObject>
#include <QString>

#include "konsoleprivate_export.h"
#include <KSharedConfig>

namespace Konsolai
{

/**
 * KonsolaiSettings manages application-wide settings.
 *
 * Settings include:
 * - Default project root directory
 * - Git remote root URL
 * - GitHub API key (stored securely)
 * - Default Claude model
 */
class KONSOLEPRIVATE_EXPORT KonsolaiSettings : public QObject
{
    Q_OBJECT

public:
    static KonsolaiSettings *instance();

    explicit KonsolaiSettings(QObject *parent = nullptr);
    ~KonsolaiSettings() override;

    /**
     * Default project root directory (e.g., ~/projects)
     */
    QString projectRoot() const;
    void setProjectRoot(const QString &path);

    /**
     * Git remote root URL (e.g., git@github.com:username/)
     */
    QString gitRemoteRoot() const;
    void setGitRemoteRoot(const QString &url);

    /**
     * GitHub API token
     */
    QString githubApiToken() const;
    void setGithubApiToken(const QString &token);

    /**
     * Default Claude model
     */
    QString defaultModel() const;
    void setDefaultModel(const QString &model);

    /**
     * Whether to auto-init git for new projects
     */
    bool autoInitGit() const;
    void setAutoInitGit(bool enabled);

    /**
     * Source repository for creating worktrees.
     * When set, new sessions create worktrees from this repo.
     * Path to the main git repository (e.g., ~/projects/main-repo)
     */
    QString worktreeSourceRepo() const;
    void setWorktreeSourceRepo(const QString &path);

    /**
     * Whether to use worktrees by default for new sessions
     */
    bool useWorktrees() const;
    void setUseWorktrees(bool enabled);

    /**
     * Save settings to disk
     */
    void save();

Q_SIGNALS:
    void settingsChanged();

private:
    KSharedConfig::Ptr m_config;
};

} // namespace Konsolai

#endif // KONSOLAI_SETTINGS_H
