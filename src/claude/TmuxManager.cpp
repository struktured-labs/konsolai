/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    Based on Bobcat's SessionManager by İsmail Yılmaz
*/

#include "TmuxManager.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Konsolai
{

TmuxManager::TmuxManager(QObject *parent)
    : QObject(parent)
{
}

TmuxManager::~TmuxManager() = default;

bool TmuxManager::isAvailable()
{
    const QString tmuxPath = QStandardPaths::findExecutable(QStringLiteral("tmux"));
    return !tmuxPath.isEmpty();
}

QString TmuxManager::version()
{
    QProcess process;
    process.start(QStringLiteral("tmux"), {QStringLiteral("-V")});
    if (!process.waitForFinished(5000)) {
        return QString();
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString TmuxManager::generateSessionId()
{
    QString id;
    id.reserve(8);
    for (int i = 0; i < 8; ++i) {
        int r = QRandomGenerator::global()->bounded(16);
        id.append(QLatin1Char(r < 10 ? '0' + r : 'a' + (r - 10)));
    }
    return id;
}

QString TmuxManager::buildSessionName(const QString &profileName,
                                       const QString &sessionId,
                                       const QString &templateFormat)
{
    QString tmpl = templateFormat;
    if (tmpl.isEmpty()) {
        tmpl = QStringLiteral("konsolai-{profile}-{id}");
    }

    QString result = tmpl;
    result.replace(QStringLiteral("{profile}"), profileName);
    result.replace(QStringLiteral("{id}"), sessionId);

    // Sanitize session name: tmux doesn't allow certain characters
    result.replace(QLatin1Char('.'), QLatin1Char('-'));
    result.replace(QLatin1Char(':'), QLatin1Char('-'));

    return result;
}

QString TmuxManager::buildNewSessionCommand(const QString &sessionName,
                                             const QString &command,
                                             bool attachExisting,
                                             const QString &workingDir) const
{
    // tmux new-session -A -s <session-name> [-c <dir>] -- <command>
    // -A: attach if exists, create if not

    QStringList args;
    args << QStringLiteral("tmux") << QStringLiteral("new-session");

    if (attachExisting) {
        args << QStringLiteral("-A");
    }

    args << QStringLiteral("-s") << sessionName;

    if (!workingDir.isEmpty()) {
        args << QStringLiteral("-c") << workingDir;
    }

    args << QStringLiteral("--");
    args << command;

    return args.join(QLatin1Char(' '));
}

QString TmuxManager::buildAttachCommand(const QString &sessionName) const
{
    // tmux attach-session -t <session-name>
    return QStringLiteral("tmux attach-session -t %1").arg(sessionName);
}

QString TmuxManager::buildKillCommand(const QString &sessionName) const
{
    // tmux kill-session -t <session-name>
    return QStringLiteral("tmux kill-session -t %1").arg(sessionName);
}

QString TmuxManager::buildDetachCommand(const QString &sessionName) const
{
    // tmux detach-client -s <session-name>
    return QStringLiteral("tmux detach-client -s %1").arg(sessionName);
}

QList<TmuxManager::SessionInfo> TmuxManager::listSessions() const
{
    bool ok = false;
    // Format: #{session_name}:#{session_windows}:#{session_created}:#{session_attached}
    QString output = executeCommand({
        QStringLiteral("list-sessions"),
        QStringLiteral("-F"),
        QStringLiteral("#{session_name}:#{session_windows}:#{session_created}:#{session_attached}")
    }, &ok);

    if (!ok) {
        return {};
    }

    return parseSessionList(output);
}

QList<TmuxManager::SessionInfo> TmuxManager::listKonsolaiSessions() const
{
    QList<SessionInfo> all = listSessions();
    QList<SessionInfo> konsolai;

    // Filter sessions that match our naming pattern: konsolai-*
    static const QRegularExpression pattern(QStringLiteral("^konsolai-"));

    for (const auto &session : all) {
        if (pattern.match(session.name).hasMatch()) {
            konsolai.append(session);
        }
    }

    return konsolai;
}

bool TmuxManager::sessionExists(const QString &sessionName) const
{
    bool ok = false;
    executeCommand({QStringLiteral("has-session"), QStringLiteral("-t"), sessionName}, &ok);
    return ok;
}

bool TmuxManager::killSession(const QString &sessionName)
{
    bool ok = false;
    executeCommand({QStringLiteral("kill-session"), QStringLiteral("-t"), sessionName}, &ok);
    return ok;
}

bool TmuxManager::detachSession(const QString &sessionName)
{
    bool ok = false;
    executeCommand({QStringLiteral("detach-client"), QStringLiteral("-s"), sessionName}, &ok);
    return ok;
}

bool TmuxManager::sendKeys(const QString &sessionName, const QString &keys)
{
    bool ok = false;
    executeCommand({QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName, keys}, &ok);
    return ok;
}

QString TmuxManager::capturePane(const QString &sessionName, int startLine, int endLine)
{
    bool ok = false;
    QString output = executeCommand({
        QStringLiteral("capture-pane"),
        QStringLiteral("-t"), sessionName,
        QStringLiteral("-p"),
        QStringLiteral("-S"), QString::number(startLine),
        QStringLiteral("-E"), QString::number(endLine)
    }, &ok);

    return ok ? output : QString();
}

QString TmuxManager::executeCommand(const QStringList &args, bool *ok) const
{
    QProcess process;
    process.start(QStringLiteral("tmux"), args);

    if (!process.waitForFinished(10000)) {
        if (ok) *ok = false;
        return QString();
    }

    if (ok) {
        *ok = (process.exitCode() == 0);
    }

    if (process.exitCode() != 0) {
        QString errorOutput = QString::fromUtf8(process.readAllStandardError());
        if (!errorOutput.isEmpty()) {
            Q_EMIT const_cast<TmuxManager*>(this)->errorOccurred(errorOutput);
        }
    }

    return QString::fromUtf8(process.readAllStandardOutput());
}

QList<TmuxManager::SessionInfo> TmuxManager::parseSessionList(const QString &output) const
{
    QList<SessionInfo> sessions;

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() >= 4) {
            SessionInfo info;
            info.name = parts[0];
            info.id = parts[0];
            info.windows = parts[1].toInt();
            info.created = parts[2];
            info.attached = (parts[3] == QLatin1String("1"));
            sessions.append(info);
        }
    }

    return sessions;
}

} // namespace Konsolai

#include "moc_TmuxManager.cpp"
