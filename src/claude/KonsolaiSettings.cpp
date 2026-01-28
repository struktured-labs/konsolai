/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "KonsolaiSettings.h"

#include <KConfigGroup>
#include <QDir>
#include <QStandardPaths>

namespace Konsolai
{

KonsolaiSettings *KonsolaiSettings::s_instance = nullptr;

KonsolaiSettings *KonsolaiSettings::instance()
{
    return s_instance;
}

KonsolaiSettings::KonsolaiSettings(QObject *parent)
    : QObject(parent)
{
    if (!s_instance) {
        s_instance = this;
    }

    // Load config from ~/.config/konsolai/konsolairc
    m_config = KSharedConfig::openConfig(QStringLiteral("konsolairc"));
}

KonsolaiSettings::~KonsolaiSettings()
{
    save();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

QString KonsolaiSettings::projectRoot() const
{
    KConfigGroup group(m_config, QStringLiteral("General"));
    QString defaultRoot = QDir::homePath() + QStringLiteral("/projects");
    return group.readEntry("ProjectRoot", defaultRoot);
}

void KonsolaiSettings::setProjectRoot(const QString &path)
{
    KConfigGroup group(m_config, QStringLiteral("General"));
    group.writeEntry("ProjectRoot", path);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::gitRemoteRoot() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    // Default to git@github.com:<username>/
    QString defaultRemote = QStringLiteral("git@github.com:%1/").arg(qEnvironmentVariable("USER"));
    return group.readEntry("RemoteRoot", defaultRemote);
}

void KonsolaiSettings::setGitRemoteRoot(const QString &url)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("RemoteRoot", url);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::githubApiToken() const
{
    KConfigGroup group(m_config, QStringLiteral("GitHub"));
    return group.readEntry("ApiToken", QString());
}

void KonsolaiSettings::setGithubApiToken(const QString &token)
{
    KConfigGroup group(m_config, QStringLiteral("GitHub"));
    group.writeEntry("ApiToken", token);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::defaultModel() const
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    return group.readEntry("DefaultModel", QStringLiteral("claude-sonnet-4"));
}

void KonsolaiSettings::setDefaultModel(const QString &model)
{
    KConfigGroup group(m_config, QStringLiteral("Claude"));
    group.writeEntry("DefaultModel", model);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::autoInitGit() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    return group.readEntry("AutoInit", true);
}

void KonsolaiSettings::setAutoInitGit(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("AutoInit", enabled);
    Q_EMIT settingsChanged();
}

QString KonsolaiSettings::worktreeSourceRepo() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    return group.readEntry("WorktreeSourceRepo", QString());
}

void KonsolaiSettings::setWorktreeSourceRepo(const QString &path)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("WorktreeSourceRepo", path);
    Q_EMIT settingsChanged();
}

bool KonsolaiSettings::useWorktrees() const
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    return group.readEntry("UseWorktrees", true); // Default to true for worktree workflow
}

void KonsolaiSettings::setUseWorktrees(bool enabled)
{
    KConfigGroup group(m_config, QStringLiteral("Git"));
    group.writeEntry("UseWorktrees", enabled);
    Q_EMIT settingsChanged();
}

void KonsolaiSettings::save()
{
    m_config->sync();
}

} // namespace Konsolai

#include "moc_KonsolaiSettings.cpp"
