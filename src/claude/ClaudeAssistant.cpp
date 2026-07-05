/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeAssistant.h"

#include "KonsolaiLogging.h"
#include "KonsolaiSettings.h"

#include <QDir>
#include <QStandardPaths>
#include <QTimer>

namespace Konsolai
{

ClaudeAssistant::ClaudeAssistant(QObject *parent)
    : QObject(parent)
{
}

ClaudeAssistant::~ClaudeAssistant()
{
    teardownProcess();
}

QString ClaudeAssistant::claudeExecutablePath()
{
    // First check PATH.
    QString path = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!path.isEmpty()) {
        return path;
    }

    // Fall back to common installation directories — mirrors ClaudeProcess::executablePath().
    const QStringList additionalDirs = {
        QDir::homePath() + QStringLiteral("/.local/bin"),
        QDir::homePath() + QStringLiteral("/.claude/local"),
    };
    path = QStandardPaths::findExecutable(QStringLiteral("claude"), additionalDirs);
    return path;
}

void ClaudeAssistant::ask(const QString &prompt, bool jsonOutput)
{
    // Ignore reentrant asks while one is in flight — caller's job to serialize.
    if (m_process) {
        emitDeferredFailure(QStringLiteral("request already in flight"));
        return;
    }

    const QString exe = claudeExecutablePath();
    if (exe.isEmpty()) {
        emitDeferredFailure(QStringLiteral("claude CLI not found on PATH"));
        return;
    }

    QStringList args;
    args << QStringLiteral("-p") << prompt;
    args << QStringLiteral("--allowedTools") << QString(); // pure-text, no tools

    if (jsonOutput) {
        args << QStringLiteral("--output-format") << QStringLiteral("json");
    }

    auto *settings = KonsolaiSettings::instance();
    if (settings) {
        const QString model = settings->defaultModel().trimmed();
        if (!model.isEmpty()) {
            args << QStringLiteral("--model") << model;
        }
    }

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_cancelled = false;
    m_finishedEmitted = false;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &ClaudeAssistant::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (m_process) {
            m_stderrBuffer.append(QString::fromUtf8(m_process->readAllStandardError()));
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ClaudeAssistant::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ClaudeAssistant::onProcessErrorOccurred);

    // Arm the timeout guard.
    if (!m_timeoutTimer) {
        m_timeoutTimer = new QTimer(this);
        m_timeoutTimer->setSingleShot(true);
        connect(m_timeoutTimer, &QTimer::timeout, this, &ClaudeAssistant::onTimeout);
    }
    m_timeoutTimer->start(m_timeoutMs);

    qCDebug(KonsolaiLog) << "ClaudeAssistant::ask launching" << exe << args.mid(0, 2);
    m_process->start(exe, args);
}

void ClaudeAssistant::cancel()
{
    if (!m_process) {
        return;
    }
    m_cancelled = true;
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
    }
    // Try graceful, then force-kill.
    m_process->terminate();
    if (!m_process->waitForFinished(500)) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        Q_EMIT failed(QStringLiteral("cancelled"));
    }
    teardownProcess();
}

void ClaudeAssistant::onReadyReadStdout()
{
    if (!m_process) {
        return;
    }
    m_stdoutBuffer.append(QString::fromUtf8(m_process->readAllStandardOutput()));
}

void ClaudeAssistant::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
    }
    if (m_cancelled || m_finishedEmitted) {
        teardownProcess();
        return;
    }
    m_finishedEmitted = true;

    if (status == QProcess::CrashExit) {
        Q_EMIT failed(QStringLiteral("claude CLI crashed"));
    } else {
        // Drain any remaining stdout.
        onReadyReadStdout();
        Q_EMIT finished(m_stdoutBuffer, exitCode);
    }
    teardownProcess();
}

void ClaudeAssistant::onProcessErrorOccurred(QProcess::ProcessError error)
{
    if (m_finishedEmitted || m_cancelled) {
        return;
    }
    if (error == QProcess::FailedToStart) {
        m_finishedEmitted = true;
        if (m_timeoutTimer) {
            m_timeoutTimer->stop();
        }
        Q_EMIT failed(QStringLiteral("failed to start claude CLI"));
        teardownProcess();
    }
    // Other errors (Crashed, Timedout, ReadError, WriteError, Unknown) are surfaced
    // through onProcessFinished's ExitStatus check, so no double-emit here.
}

void ClaudeAssistant::onTimeout()
{
    if (m_finishedEmitted) {
        return;
    }
    m_cancelled = true;
    m_finishedEmitted = true;
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    Q_EMIT failed(QStringLiteral("timed out after %1s").arg(m_timeoutMs / 1000));
    teardownProcess();
}

void ClaudeAssistant::emitDeferredFailure(const QString &message)
{
    // Post to event loop so callers see the failure asynchronously — matches
    // the pattern used when a real request finishes.
    QTimer::singleShot(0, this, [this, message]() {
        Q_EMIT failed(message);
    });
}

void ClaudeAssistant::teardownProcess()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
}

} // namespace Konsolai

#include "moc_ClaudeAssistant.cpp"
