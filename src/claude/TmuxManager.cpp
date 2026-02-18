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

    // Suppress DCS passthrough to prevent XTVERSION response leaking into Claude Code prompt
    // \; chains a second tmux command after new-session completes
    args << QStringLiteral("\\;") << QStringLiteral("set-option") << QStringLiteral("-p") << QStringLiteral("allow-passthrough") << QStringLiteral("off");

    return args.join(QLatin1Char(' '));
}

QString TmuxManager::buildAttachCommand(const QString &sessionName) const
{
    // tmux attach-session -t <session-name>
    // Also suppress DCS passthrough to prevent XTVERSION response leaking
    return QStringLiteral("tmux attach-session -t %1 \\; set-option -p allow-passthrough off").arg(sessionName);
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

    // Replace trailing \n with \r (carriage return) so the Enter key is
    // included in a single tmux send-keys -l call.  This avoids a race
    // between a separate "send-keys Enter" command and also fixes the case
    // where the text is empty (just an Enter press) — previously `ok`
    // stayed false and Enter was never sent.
    QString text = keys;
    if (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
        text.append(QLatin1Char('\r'));
    }

    if (!text.isEmpty()) {
        executeCommand({QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName, QStringLiteral("-l"), text}, &ok);
    }

    return ok;
}

bool TmuxManager::sendKeySequence(const QString &sessionName, const QString &keyName)
{
    bool ok = false;
    executeCommand({QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName, keyName}, &ok);
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

QString TmuxManager::getPaneWorkingDirectory(const QString &sessionName) const
{
    bool ok = false;
    QString output =
        executeCommand({QStringLiteral("display-message"), QStringLiteral("-p"), QStringLiteral("-t"), sessionName, QStringLiteral("#{pane_current_path}")},
                       &ok);

    return ok ? output.trimmed() : QString();
}

qint64 TmuxManager::getPanePid(const QString &sessionName) const
{
    bool ok = false;
    QString output =
        executeCommand({QStringLiteral("display-message"), QStringLiteral("-p"), QStringLiteral("-t"), sessionName, QStringLiteral("#{pane_pid}")}, &ok);

    if (!ok) {
        return 0;
    }
    bool converted = false;
    qint64 pid = output.trimmed().toLongLong(&converted);
    return converted ? pid : 0;
}

void TmuxManager::getPanePidAsync(const QString &sessionName, std::function<void(qint64)> callback)
{
    executeCommandAsync({QStringLiteral("display-message"), QStringLiteral("-p"), QStringLiteral("-t"), sessionName, QStringLiteral("#{pane_pid}")},
                        [callback](bool ok, const QString &output) {
                            if (!ok || !callback) {
                                if (callback) {
                                    callback(0);
                                }
                                return;
                            }
                            bool converted = false;
                            qint64 pid = output.trimmed().toLongLong(&converted);
                            callback(converted ? pid : 0);
                        });
}

void TmuxManager::getPaneWorkingDirectoryAsync(const QString &sessionName, std::function<void(const QString &)> callback)
{
    executeCommandAsync({QStringLiteral("display-message"), QStringLiteral("-p"), QStringLiteral("-t"), sessionName, QStringLiteral("#{pane_current_path}")},
                        [callback](bool ok, const QString &output) {
                            if (callback) {
                                callback(ok ? output.trimmed() : QString());
                            }
                        });
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

void TmuxManager::executeCommandAsync(const QStringList &args, std::function<void(bool, const QString &)> callback)
{
    auto *process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process, callback](int exitCode, QProcess::ExitStatus) {
        bool ok = (exitCode == 0);
        QString output = QString::fromUtf8(process->readAllStandardOutput());
        if (!ok) {
            QString errorOutput = QString::fromUtf8(process->readAllStandardError());
            if (!errorOutput.isEmpty()) {
                Q_EMIT errorOccurred(errorOutput);
            }
        }
        if (callback) {
            callback(ok, output);
        }
        process->deleteLater();
    });
    process->start(QStringLiteral("tmux"), args);
}

void TmuxManager::capturePaneAsync(const QString &sessionName, int startLine, int endLine, std::function<void(bool, const QString &)> callback)
{
    executeCommandAsync({QStringLiteral("capture-pane"),
                         QStringLiteral("-t"),
                         sessionName,
                         QStringLiteral("-p"),
                         QStringLiteral("-S"),
                         QString::number(startLine),
                         QStringLiteral("-E"),
                         QString::number(endLine)},
                        callback);
}

void TmuxManager::capturePaneAsync(const QString &sessionName, std::function<void(bool, const QString &)> callback)
{
    executeCommandAsync({QStringLiteral("capture-pane"), QStringLiteral("-t"), sessionName, QStringLiteral("-p")}, callback);
}

void TmuxManager::sessionExistsAsync(const QString &sessionName, std::function<void(bool)> callback)
{
    executeCommandAsync({QStringLiteral("has-session"), QStringLiteral("-t"), sessionName}, [callback](bool ok, const QString &) {
        if (callback) {
            callback(ok);
        }
    });
}

void TmuxManager::listKonsolaiSessionsAsync(std::function<void(const QList<SessionInfo> &)> callback)
{
    executeCommandAsync(
        {QStringLiteral("list-sessions"), QStringLiteral("-F"), QStringLiteral("#{session_name}:#{session_windows}:#{session_created}:#{session_attached}")},
        [this, callback](bool ok, const QString &output) {
            QList<SessionInfo> result;
            if (ok) {
                QList<SessionInfo> all = parseSessionList(output);
                static const QRegularExpression pattern(QStringLiteral("^konsolai-"));
                for (const auto &session : all) {
                    if (pattern.match(session.name).hasMatch()) {
                        result.append(session);
                    }
                }
            }
            if (callback) {
                callback(result);
            }
        });
}

void TmuxManager::sendKeysAsync(const QString &sessionName, const QString &keys)
{
    QString text = keys;
    if (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
        text.append(QLatin1Char('\r'));
    }
    if (!text.isEmpty()) {
        executeCommandAsync({QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName, QStringLiteral("-l"), text}, nullptr);
    }
}

void TmuxManager::sendKeySequenceAsync(const QString &sessionName, const QString &keyName)
{
    executeCommandAsync({QStringLiteral("send-keys"), QStringLiteral("-t"), sessionName, keyName}, nullptr);
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
