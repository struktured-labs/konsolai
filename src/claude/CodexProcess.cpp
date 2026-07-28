/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CodexProcess.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

namespace Konsolai
{

bool CodexProcess::isAvailable()
{
    return !executablePath().isEmpty();
}

QString CodexProcess::executablePath()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("codex"));
    if (!path.isEmpty()) {
        return path;
    }

    QStringList additionalDirs = {
        QDir::homePath() + QStringLiteral("/.local/bin"),
        QDir::homePath() + QStringLiteral("/.codex/bin"),
    };

    // npm-installed codex under nvm lives in a node-version-specific bin dir
    // that a non-login shell won't have on PATH. Search newest version first
    // so an upgraded node doesn't keep resolving a stale binary.
    QDir nvmVersions(QDir::homePath() + QStringLiteral("/.nvm/versions/node"));
    if (nvmVersions.exists()) {
        QStringList versions = nvmVersions.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        std::reverse(versions.begin(), versions.end());
        for (const QString &version : std::as_const(versions)) {
            additionalDirs << nvmVersions.absoluteFilePath(version) + QStringLiteral("/bin");
        }
    }

    return QStandardPaths::findExecutable(QStringLiteral("codex"), additionalDirs);
}

QString CodexProcess::buildCommand(const QString &workingDir, const QString &resumeSessionId, const QString &model, const QStringList &additionalArgs)
{
    QStringList args;
    args << QStringLiteral("codex");

    // `resume` is a subcommand and must precede its options; the session id is
    // its positional argument.
    if (!resumeSessionId.isEmpty()) {
        args << QStringLiteral("resume") << resumeSessionId;
    }

    if (!workingDir.isEmpty()) {
        args << QStringLiteral("-C") << workingDir;
    }
    if (!model.isEmpty()) {
        args << QStringLiteral("-m") << model;
    }

    args << additionalArgs;

    return args.join(QLatin1Char(' '));
}

QString CodexProcess::sessionsRoot()
{
    return QDir::homePath() + QStringLiteral("/.codex/sessions");
}

CodexConversation CodexProcess::parseSessionMeta(const QString &jsonlPath)
{
    CodexConversation conv;

    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return conv;
    }

    // The metadata is the first line; reading the whole transcript would mean
    // parsing megabytes per session just to build the picker.
    const QByteArray firstLine = file.readLine();
    file.close();
    if (firstLine.isEmpty()) {
        return conv;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(firstLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return conv;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("type")).toString() != QLatin1String("session_meta")) {
        return conv;
    }

    const QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
    conv.sessionId = payload.value(QStringLiteral("session_id")).toString();
    if (conv.sessionId.isEmpty()) {
        conv.sessionId = payload.value(QStringLiteral("id")).toString();
    }
    conv.workingDirectory = payload.value(QStringLiteral("cwd")).toString();
    conv.model = payload.value(QStringLiteral("model")).toString();
    if (conv.model.isEmpty()) {
        conv.model = payload.value(QStringLiteral("model_provider")).toString();
    }
    conv.created = QDateTime::fromString(payload.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
    conv.transcriptPath = jsonlPath;
    conv.modified = QFileInfo(jsonlPath).lastModified();

    return conv;
}

QList<CodexConversation> CodexProcess::discoverConversations(const QString &workingDir)
{
    QList<CodexConversation> results;

    QDir root(sessionsRoot());
    if (!root.exists()) {
        return results;
    }

    // Transcripts are partitioned YYYY/MM/DD, so recurse rather than guessing
    // date directories.
    QDirIterator it(root.absolutePath(), {QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const CodexConversation conv = parseSessionMeta(it.filePath());
        if (conv.sessionId.isEmpty()) {
            continue;
        }
        if (!workingDir.isEmpty() && conv.workingDirectory != workingDir) {
            continue;
        }
        results.append(conv);
    }

    std::sort(results.begin(), results.end(), [](const CodexConversation &a, const CodexConversation &b) {
        return a.modified > b.modified;
    });

    // Resuming a session writes a *new* rollout file carrying the same
    // session_id, so the same conversation appears once per resume. Keep only
    // the newest transcript per id — otherwise the picker lists one session
    // several times and they all resume to the same place.
    QSet<QString> seen;
    QList<CodexConversation> deduped;
    deduped.reserve(results.size());
    for (const CodexConversation &conv : std::as_const(results)) {
        if (seen.contains(conv.sessionId)) {
            continue;
        }
        seen.insert(conv.sessionId);
        deduped.append(conv);
    }

    return deduped;
}

}
