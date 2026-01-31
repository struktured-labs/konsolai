/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_SETTINGS_H
#define KONSOLAI_SETTINGS_H

#include "konsoleprivate_export.h"

#include <QObject>
#include <QString>

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
     * Git mode for new sessions:
     * 0 = Initialize new repository
     * 1 = Create as worktree
     * 2 = Use current branch (existing repo)
     * 3 = None
     */
    int gitMode() const;
    void setGitMode(int mode);

    /**
     * Source repository for creating worktrees.
     * When set, new sessions create worktrees from this repo.
     * Path to the main git repository (e.g., ~/projects/main-repo)
     */
    QString worktreeSourceRepo() const;
    void setWorktreeSourceRepo(const QString &path);

    /**
     * Yolo mode (auto-approve) default
     */
    bool yoloMode() const;
    void setYoloMode(bool enabled);

    /**
     * Double yolo mode (auto-complete) default
     */
    bool doubleYoloMode() const;
    void setDoubleYoloMode(bool enabled);

    /**
     * Triple yolo mode (auto-continue) default
     */
    bool tripleYoloMode() const;
    void setTripleYoloMode(bool enabled);

    /**
     * Auto-continue prompt for triple yolo mode
     */
    QString autoContinuePrompt() const;
    void setAutoContinuePrompt(const QString &prompt);

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
