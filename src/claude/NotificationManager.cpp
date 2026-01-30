/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "NotificationManager.h"
#include "ClaudeSession.h"

#include <KLocalizedString>
#include <KNotification>

#if HAVE_KSTATUSNOTIFIERITEM
#include <KStatusNotifierItem>
#endif

#include <QDir>
#include <QSoundEffect>
#include <QStandardPaths>

namespace Konsolai
{

static NotificationManager *s_notificationManagerInstance = nullptr;

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
    if (!s_notificationManagerInstance) {
        s_notificationManagerInstance = this;
    }

    initSystemTray();
}

NotificationManager::~NotificationManager()
{
    if (s_notificationManagerInstance == this) {
        s_notificationManagerInstance = nullptr;
    }
}

NotificationManager *NotificationManager::instance()
{
    return s_notificationManagerInstance;
}

void NotificationManager::initSystemTray()
{
#if HAVE_KSTATUSNOTIFIERITEM
    m_systemTray = new KStatusNotifierItem(this);
    m_systemTray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_systemTray->setIconByName(QStringLiteral("utilities-terminal"));
    m_systemTray->setStatus(KStatusNotifierItem::Passive);
    m_systemTray->setToolTipTitle(i18n("Konsolai"));
    m_systemTray->setToolTipSubTitle(i18n("Claude-native terminal"));
#endif
}

void NotificationManager::notify(NotificationType type, const QString &title, const QString &message, ClaudeSession *session, Channels channels)
{
    // Apply enabled channel filter
    channels &= m_enabledChannels;

    if (channels.testFlag(Channel::SystemTray)) {
        updateSystemTray(type, message);
    }

    if (channels.testFlag(Channel::Desktop)) {
        showDesktopNotification(type, title, message);
    }

    if (channels.testFlag(Channel::Audio)) {
        playSound(type);
    }

    if (channels.testFlag(Channel::InTerminal)) {
        Q_EMIT showInTerminalNotification(type, title, message, session);
    }
}

void NotificationManager::updateSystemTray(NotificationType type, const QString &statusMessage)
{
    m_currentTrayStatus = type;

#if HAVE_KSTATUSNOTIFIERITEM
    if (!m_systemTray) {
        return;
    }

    QString iconStr = NotificationManager::iconName(type);
    m_systemTray->setIconByName(iconStr);

    KStatusNotifierItem::ItemStatus status;
    switch (type) {
    case NotificationType::Permission:
    case NotificationType::Error:
        status = KStatusNotifierItem::NeedsAttention;
        break;
    case NotificationType::WaitingInput:
        status = KStatusNotifierItem::Active;
        break;
    case NotificationType::TaskComplete:
    case NotificationType::Info:
    default:
        status = KStatusNotifierItem::Passive;
        break;
    }

    m_systemTray->setStatus(status);
    m_systemTray->setToolTipSubTitle(statusMessage);
#else
    Q_UNUSED(statusMessage);
#endif

    Q_EMIT systemTrayStatusChanged(type, statusMessage);
}

void NotificationManager::showDesktopNotification(NotificationType type, const QString &title, const QString &message)
{
    // Map type to KNotification urgency
    QString eventName;
    switch (type) {
    case NotificationType::Permission:
        eventName = QStringLiteral("permissionRequired");
        break;
    case NotificationType::Error:
        eventName = QStringLiteral("error");
        break;
    case NotificationType::WaitingInput:
        eventName = QStringLiteral("waitingInput");
        break;
    case NotificationType::TaskComplete:
        eventName = QStringLiteral("taskComplete");
        break;
    case NotificationType::Info:
    default:
        eventName = QStringLiteral("info");
        break;
    }

    KNotification *notification = new KNotification(eventName, KNotification::CloseOnTimeout);
    notification->setTitle(title);
    notification->setText(message);
    notification->setIconName(iconName(type));
    notification->setComponentName(QStringLiteral("konsolai"));

    notification->sendEvent();
}

void NotificationManager::playSound(NotificationType type)
{
    // Only play sounds for important notifications
    if (type == NotificationType::Info || type == NotificationType::TaskComplete) {
        return;
    }

    QString path = soundPath(type);
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }

    QSoundEffect *sound = new QSoundEffect(this);
    sound->setSource(QUrl::fromLocalFile(path));
    sound->setVolume(m_audioVolume);

    connect(sound, &QSoundEffect::playingChanged, sound, [sound]() {
        if (!sound->isPlaying()) {
            sound->deleteLater();
        }
    });

    sound->play();
}

void NotificationManager::enableChannel(Channel channel, bool enable)
{
    if (enable) {
        m_enabledChannels |= channel;
    } else {
        m_enabledChannels &= ~Channels(channel);
    }
}

bool NotificationManager::isChannelEnabled(Channel channel) const
{
    return m_enabledChannels.testFlag(channel);
}

void NotificationManager::setAudioVolume(qreal volume)
{
    m_audioVolume = qBound(0.0, volume, 1.0);
}

QString NotificationManager::soundPath(NotificationType type)
{
    QString soundName;
    switch (type) {
    case NotificationType::Permission:
        soundName = QStringLiteral("permission");
        break;
    case NotificationType::Error:
        soundName = QStringLiteral("error");
        break;
    case NotificationType::WaitingInput:
        soundName = QStringLiteral("waiting");
        break;
    case NotificationType::TaskComplete:
        soundName = QStringLiteral("complete");
        break;
    case NotificationType::Info:
    default:
        return QString();
    }

    // Look for sound files in standard locations
    const QStringList searchPaths = {
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("konsolai/sounds/%1.wav").arg(soundName)),
        QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("sounds/%1.wav").arg(soundName)),
    };

    for (const QString &path : searchPaths) {
        if (!path.isEmpty() && QFile::exists(path)) {
            return path;
        }
    }

    return QString();
}

QString NotificationManager::iconName(NotificationType type)
{
    switch (type) {
    case NotificationType::Permission:
        return QStringLiteral("dialog-warning");
    case NotificationType::Error:
        return QStringLiteral("dialog-error");
    case NotificationType::WaitingInput:
        return QStringLiteral("dialog-question");
    case NotificationType::TaskComplete:
        return QStringLiteral("dialog-ok");
    case NotificationType::Info:
    default:
        return QStringLiteral("dialog-information");
    }
}

} // namespace Konsolai

#include "moc_NotificationManager.cpp"
