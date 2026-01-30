/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    Based on Bobcat's SessionManager by İsmail Yılmaz
*/

#ifndef TMUXMANAGER_H
#define TMUXMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace Konsolai
{

/**
 * TmuxManager provides tmux session management for Claude sessions.
 *
 * Each Claude session runs inside a tmux session for persistence.
 * If Konsolai is closed, the tmux session continues running and can
 * be reattached later.
 */
class TmuxManager : public QObject
{
    Q_OBJECT

public:
    /**
     * Information about a tmux session
     */
    struct SessionInfo {
        QString name;       // Session name (e.g., "konsolai-default-a1b2c3d4")
        QString id;         // Session ID
        bool attached;      // Whether session is currently attached
        int windows;        // Number of windows in session
        QString created;    // Creation timestamp
    };

    explicit TmuxManager(QObject *parent = nullptr);
    ~TmuxManager() override;

    /**
     * Check if tmux is available on the system
     */
    static bool isAvailable();

    /**
     * Get tmux version string
     */
    static QString version();

    /**
     * Generate a unique session ID (8 hex characters)
     */
    static QString generateSessionId();

    /**
     * Build session name from template
     *
     * Template variables:
     *   {profile} - Profile name
     *   {id} - Unique session ID
     *
     * Default template: "konsolai-{profile}-{id}"
     */
    static QString buildSessionName(const QString &profileName,
                                    const QString &sessionId,
                                    const QString &templateFormat = QString());

    /**
     * Build command to create or attach to a tmux session
     *
     * @param sessionName Name for the tmux session
     * @param command Command to run inside the session (e.g., "claude")
     * @param attachExisting If true, attach to existing session with same name
     * @param workingDir Working directory for the session
     */
    QString buildNewSessionCommand(const QString &sessionName,
                                   const QString &command,
                                   bool attachExisting = true,
                                   const QString &workingDir = QString()) const;

    /**
     * Build command to attach to an existing tmux session
     */
    QString buildAttachCommand(const QString &sessionName) const;

    /**
     * Build command to kill a tmux session
     */
    QString buildKillCommand(const QString &sessionName) const;

    /**
     * Build command to detach a client from a tmux session
     */
    QString buildDetachCommand(const QString &sessionName) const;

    /**
     * List all tmux sessions
     */
    QList<SessionInfo> listSessions() const;

    /**
     * List only Konsolai-managed sessions (those matching our naming pattern)
     */
    QList<SessionInfo> listKonsolaiSessions() const;

    /**
     * Check if a session with the given name exists
     */
    bool sessionExists(const QString &sessionName) const;

    /**
     * Kill a specific session
     */
    bool killSession(const QString &sessionName);

    /**
     * Detach the client from a session (session keeps running in background)
     */
    bool detachSession(const QString &sessionName);

    /**
     * Send keys to a tmux session
     */
    bool sendKeys(const QString &sessionName, const QString &keys);

    /**
     * Capture pane content from a tmux session
     */
    QString capturePane(const QString &sessionName, int startLine = -100, int endLine = 100);

    /**
     * Get the current working directory of a tmux session's pane
     */
    QString getPaneWorkingDirectory(const QString &sessionName) const;

Q_SIGNALS:
    /**
     * Emitted when an error occurs during tmux operations
     */
    void errorOccurred(const QString &message);

private:
    /**
     * Execute a tmux command and return the output
     */
    QString executeCommand(const QStringList &args, bool *ok = nullptr) const;

    /**
     * Parse tmux list-sessions output
     */
    QList<SessionInfo> parseSessionList(const QString &output) const;
};

} // namespace Konsolai

#endif // TMUXMANAGER_H
