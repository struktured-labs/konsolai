/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include "config-konsole.h"
#include "konsoleprivate_export.h"
#include <QObject>
#include <QString>
#include <QUrl>

#if HAVE_KSTATUSNOTIFIERITEM
class KStatusNotifierItem;
#endif

namespace Konsolai
{

class ClaudeSession;

/**
 * NotificationManager handles all Claude-related notifications.
 *
 * Four notification channels:
 * 1. System Tray - KStatusNotifierItem with status icon
 * 2. Desktop Popup - KNotification framework
 * 3. Audio Alerts - QSoundEffect with custom wav files
 * 4. In-Terminal Visual - Overlay widget on TerminalDisplay
 *
 * Notification types:
 * - Permission required (orange, sound)
 * - Task complete (green)
 * - Waiting for input (yellow, sound)
 * - Error (red, sound)
 */
class KONSOLEPRIVATE_EXPORT NotificationManager : public QObject
{
    Q_OBJECT

public:
    /**
     * Notification type/priority
     */
    enum class NotificationType {
        Info,           // General information
        TaskComplete,   // Task completed successfully
        WaitingInput,   // Waiting for user input
        Permission,     // Permission required
        Error           // Error occurred
    };
    Q_ENUM(NotificationType)

    /**
     * Notification channel flags
     */
    enum class Channel {
        None = 0,
        SystemTray = 1 << 0,
        Desktop = 1 << 1,
        Audio = 1 << 2,
        InTerminal = 1 << 3,
        All = SystemTray | Desktop | Audio | InTerminal
    };
    Q_DECLARE_FLAGS(Channels, Channel)
    Q_FLAG(Channels)

    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() override;

    /**
     * Get the singleton instance
     */
    static NotificationManager* instance();

    /**
     * Show a notification
     *
     * @param type Notification type
     * @param title Notification title
     * @param message Notification message
     * @param session The Claude session (for context)
     * @param channels Which channels to use (default: All)
     */
    void notify(NotificationType type,
                const QString &title,
                const QString &message,
                ClaudeSession *session = nullptr,
                Channels channels = Channel::All);

    /**
     * Update system tray status
     *
     * @param type Current notification state
     * @param statusMessage Status tooltip
     */
    void updateSystemTray(NotificationType type, const QString &statusMessage);

    /**
     * Show desktop notification
     */
    void showDesktopNotification(NotificationType type,
                                  const QString &title,
                                  const QString &message);

    /**
     * Play audio alert for notification type
     */
    void playSound(NotificationType type);

    /**
     * Get/set enabled channels
     */
    Channels enabledChannels() const { return m_enabledChannels; }
    void setEnabledChannels(Channels channels) { m_enabledChannels = channels; }

    /**
     * Enable/disable specific channel
     */
    void enableChannel(Channel channel, bool enable = true);
    bool isChannelEnabled(Channel channel) const;

    /**
     * Get/set audio volume (0.0 - 1.0)
     */
    qreal audioVolume() const { return m_audioVolume; }
    void setAudioVolume(qreal volume);

    /**
     * Get sound file path for notification type
     */
    static QString soundPath(NotificationType type);

    /**
     * Get icon name for notification type
     */
    static QString iconName(NotificationType type);

Q_SIGNALS:
    /**
     * Emitted when an in-terminal notification should be shown
     */
    void showInTerminalNotification(NotificationType type,
                                    const QString &title,
                                    const QString &message,
                                    ClaudeSession *session);

    /**
     * Emitted when system tray status changes
     */
    void systemTrayStatusChanged(NotificationType type, const QString &message);

private:
    void initSystemTray();
    void initSounds();

    static NotificationManager *s_instance;

#if HAVE_KSTATUSNOTIFIERITEM
    KStatusNotifierItem *m_systemTray = nullptr;
#endif
    Channels m_enabledChannels = Channel::All;
    qreal m_audioVolume = 0.7;
    NotificationType m_currentTrayStatus = NotificationType::Info;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(NotificationManager::Channels)

} // namespace Konsolai

#endif // NOTIFICATIONMANAGER_H
