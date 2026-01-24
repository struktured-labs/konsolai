/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    konsolai-hook-handler - Claude hook handler binary

    This small binary is called by Claude hooks to send events back to Konsolai.
    It reads event data from stdin (JSON) and sends it to the Konsolai socket.

    Usage:
        konsolai-hook-handler --socket <path> --event <type>

    The event data is read from stdin as JSON.
*/

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QFile>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("konsolai-hook-handler"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Claude hook handler for Konsolai"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption socketOption(
        QStringList() << QStringLiteral("s") << QStringLiteral("socket"),
        QStringLiteral("Path to Konsolai socket"),
        QStringLiteral("path")
    );
    parser.addOption(socketOption);

    QCommandLineOption eventOption(
        QStringList() << QStringLiteral("e") << QStringLiteral("event"),
        QStringLiteral("Event type (Stop, Notification, PreToolUse, PostToolUse)"),
        QStringLiteral("type")
    );
    parser.addOption(eventOption);

    QCommandLineOption timeoutOption(
        QStringList() << QStringLiteral("t") << QStringLiteral("timeout"),
        QStringLiteral("Connection timeout in milliseconds (default: 5000)"),
        QStringLiteral("ms"),
        QStringLiteral("5000")
    );
    parser.addOption(timeoutOption);

    parser.process(app);

    // Validate required options
    if (!parser.isSet(socketOption)) {
        QTextStream err(stderr);
        err << "Error: --socket option is required\n";
        return 1;
    }

    if (!parser.isSet(eventOption)) {
        QTextStream err(stderr);
        err << "Error: --event option is required\n";
        return 1;
    }

    QString socketPath = parser.value(socketOption);
    QString eventType = parser.value(eventOption);
    int timeout = parser.value(timeoutOption).toInt();

    // Read event data from stdin
    QFile stdinFile;
    stdinFile.open(stdin, QIODevice::ReadOnly);
    QByteArray stdinData = stdinFile.readAll();
    stdinFile.close();

    // Parse stdin as JSON (if any)
    QJsonObject eventData;
    if (!stdinData.isEmpty()) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(stdinData, &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            eventData = doc.object();
        }
    }

    // Add environment variables that might be useful
    eventData[QStringLiteral("session_id")] = QString::fromLocal8Bit(qgetenv("KONSOLAI_SESSION_ID"));
    eventData[QStringLiteral("working_dir")] = QString::fromLocal8Bit(qgetenv("PWD"));

    // Connect to socket and send event
    QLocalSocket socket;
    socket.connectToServer(socketPath);

    if (!socket.waitForConnected(timeout)) {
        QTextStream err(stderr);
        err << "Error: Failed to connect to Konsolai socket: " << socket.errorString() << "\n";
        return 2;
    }

    // Build message
    QJsonObject msg;
    msg[QStringLiteral("event_type")] = eventType;
    msg[QStringLiteral("data")] = eventData;

    QJsonDocument doc(msg);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

    socket.write(data);
    if (!socket.waitForBytesWritten(timeout)) {
        QTextStream err(stderr);
        err << "Error: Failed to write to Konsolai socket: " << socket.errorString() << "\n";
        return 3;
    }

    socket.disconnectFromServer();
    return 0;
}
