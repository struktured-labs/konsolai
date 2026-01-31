/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later

    konsolai-hook-handler - Claude hook handler binary

    This small binary is called by Claude hooks to send events back to Konsolai.
    It reads event data from stdin (JSON) and sends it to the Konsolai socket.

    For PermissionRequest events, it can auto-approve if yolo mode is enabled.

    Usage:
        konsolai-hook-handler --socket <path> --event <type>

    The event data is read from stdin as JSON.
*/

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>
#include <QTimer>

// Check if yolo mode is enabled for this session
// Yolo state is stored in: ~/.local/share/konsolai/sessions/{session-id}.yolo
bool isYoloEnabled(const QString &socketPath)
{
    // Derive yolo file path from socket path
    // Socket: /path/to/sessions/{session-id}.sock
    // Yolo:   /path/to/sessions/{session-id}.yolo
    QString yoloPath = socketPath;
    yoloPath.replace(QStringLiteral(".sock"), QStringLiteral(".yolo"));

    return QFileInfo::exists(yoloPath);
}

// Output JSON to auto-approve a PermissionRequest
void outputApprovalJson()
{
    QJsonObject decision;
    decision[QStringLiteral("behavior")] = QStringLiteral("allow");

    QJsonObject hookOutput;
    hookOutput[QStringLiteral("hookEventName")] = QStringLiteral("PermissionRequest");
    hookOutput[QStringLiteral("decision")] = decision;

    QJsonObject output;
    output[QStringLiteral("hookSpecificOutput")] = hookOutput;

    QTextStream out(stdout);
    out << QJsonDocument(output).toJson(QJsonDocument::Compact) << "\n";
}

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

    QCommandLineOption eventOption(QStringList() << QStringLiteral("e") << QStringLiteral("event"),
                                   QStringLiteral("Event type (Stop, Notification, PreToolUse, PostToolUse, PermissionRequest)"),
                                   QStringLiteral("type"));
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

    // For PermissionRequest events, check yolo mode and auto-approve if enabled
    bool yoloApproved = false;
    if (eventType == QStringLiteral("PermissionRequest") && isYoloEnabled(socketPath)) {
        outputApprovalJson();
        yoloApproved = true;
        // Mark in event data that we auto-approved
        eventData[QStringLiteral("yolo_approved")] = true;
    }

    // Add environment variables that might be useful
    eventData[QStringLiteral("session_id")] = QString::fromLocal8Bit(qgetenv("KONSOLAI_SESSION_ID"));
    eventData[QStringLiteral("working_dir")] = QString::fromLocal8Bit(qgetenv("PWD"));

    // If the socket file doesn't exist, the Konsolai session is not running.
    // Exit silently — this handles stale hooks left in settings.local.json.
    if (!QFileInfo::exists(socketPath)) {
        return 0;
    }

    // Connect to socket and send event to Konsolai (for tracking/notifications)
    QLocalSocket socket;
    socket.connectToServer(socketPath);

    if (!socket.waitForConnected(timeout)) {
        // If we already approved via yolo, exit success even if socket fails
        if (yoloApproved) {
            return 0;
        }
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
        // If we already approved via yolo, exit success even if socket fails
        if (yoloApproved) {
            return 0;
        }
        QTextStream err(stderr);
        err << "Error: Failed to write to Konsolai socket: " << socket.errorString() << "\n";
        return 3;
    }

    socket.disconnectFromServer();
    return 0;
}
