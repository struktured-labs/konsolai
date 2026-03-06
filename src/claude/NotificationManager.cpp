/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "NotificationManager.h"
#include "ClaudeSession.h"
#include "KonsolaiSettings.h"

#include <KLocalizedString>
#include <KNotification>

#if HAVE_KSTATUSNOTIFIERITEM
#include <KStatusNotifierItem>
#endif

#include <QCoreApplication>
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

    loadSettings();
    initSystemTray();
}

NotificationManager::~NotificationManager()
{
    if (m_audioThread) {
        m_audioThread->quit();
        m_audioThread->wait(1000);
        delete m_soundEffect; // lives on m_audioThread, safe after thread exits
        delete m_audioThread;
    }
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
    qDebug() << "NotificationManager::notify:" << static_cast<int>(type) << title << "channels:" << static_cast<int>(channels)
             << "enabled:" << static_cast<int>(m_enabledChannels);

    // Apply enabled channel filter
    channels &= m_enabledChannels;

    if (channels.testFlag(Channel::SystemTray)) {
        updateSystemTray(type, message);
    }

    if (channels.testFlag(Channel::Desktop)) {
        showDesktopNotification(type, title, message);
    }

    // Always play sound via QSoundEffect — KNotification Sound= is unreliable
    // due to stale user-local overrides in ~/.local/share/knotifications6/.
    // The .notifyrc has Action=Popup (no Sound) to prevent double-playing.
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
    case NotificationType::YoloApproval:
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
    case NotificationType::YoloApproval:
        eventName = QStringLiteral("yoloApproval");
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

void NotificationManager::ensureAudioThread()
{
    if (m_audioThread) {
        return;
    }
    // QSoundEffect must live on a dedicated thread so PipeWire/PulseAudio
    // interactions (setSource, play) never block the UI thread.
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName(QStringLiteral("KonsolaiAudio"));
    m_audioThread->start();

    m_soundEffect = new QSoundEffect(); // no parent — moved to thread
    m_soundEffect->moveToThread(m_audioThread);
    // Set volume on the audio thread — calling methods after moveToThread
    // from the main thread is a Qt threading contract violation.
    qreal vol = m_audioVolume;
    QMetaObject::invokeMethod(
        m_soundEffect,
        [this, vol]() {
            m_soundEffect->setVolume(vol);
        },
        Qt::QueuedConnection);
}

void NotificationManager::playSound(NotificationType type)
{
    // Skip sounds for Info type (no sound file)
    if (type == NotificationType::Info) {
        return;
    }

    // YoloApproval: skip if yolo notifications are disabled, or apply cooldown (max 1 per 5s)
    if (type == NotificationType::YoloApproval) {
        if (!m_yoloNotificationsEnabled) {
            return;
        }
        QDateTime now = QDateTime::currentDateTime();
        if (m_lastYoloNotificationTime.isValid() && m_lastYoloNotificationTime.msecsTo(now) < 5000) {
            return;
        }
        m_lastYoloNotificationTime = now;
    }

    QString path = soundPath(type);
    if (path.isEmpty() || !QFile::exists(path)) {
        qDebug() << "NotificationManager::playSound: No sound file for type" << static_cast<int>(type) << "path:" << path;
        return;
    }

    qDebug() << "NotificationManager::playSound: Playing" << path << "volume:" << m_audioVolume;

    ensureAudioThread();

    // Invoke on the audio thread via queued connection to avoid blocking UI
    qreal volume = m_audioVolume;
    QString loadedPath = m_loadedSoundPath;
    QMetaObject::invokeMethod(
        m_soundEffect,
        [this, path, volume, loadedPath]() {
            m_soundEffect->setVolume(volume);
            if (loadedPath != path) {
                m_soundEffect->setSource(QUrl::fromLocalFile(path));
            }
            m_soundEffect->play();
        },
        Qt::QueuedConnection);
    m_loadedSoundPath = path;
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
    case NotificationType::YoloApproval:
        soundName = QStringLiteral("yolo");
        break;
    case NotificationType::Info:
    default:
        return QString();
    }

    // Look for sound files in standard locations
    // KNotification resolves Sound= relative to /usr/share/sounds/, so files are at sounds/konsolai/
    const QStringList searchPaths = {
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("sounds/konsolai/%1.wav").arg(soundName)),
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("konsolai/sounds/%1.wav").arg(soundName)),
        QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("sounds/%1.wav").arg(soundName)),
    };

    for (const QString &path : searchPaths) {
        if (!path.isEmpty() && QFile::exists(path)) {
            return path;
        }
    }

    // Fallback: look relative to the executable (for uninstalled builds)
    QString exeDir = QCoreApplication::applicationDirPath();
    // From build/bin/ → source/data/sounds/
    QString devPath = QDir(exeDir).filePath(QStringLiteral("../../data/sounds/%1.wav").arg(soundName));
    if (QFile::exists(devPath)) {
        return QDir(devPath).canonicalPath();
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
    case NotificationType::YoloApproval:
        return QStringLiteral("dialog-ok-apply");
    case NotificationType::Info:
    default:
        return QStringLiteral("dialog-information");
    }
}

void NotificationManager::setYoloNotificationsEnabled(bool enabled)
{
    m_yoloNotificationsEnabled = enabled;
    saveSettings();
}

void NotificationManager::loadSettings()
{
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    // Rebuild enabled channels from individual settings
    m_enabledChannels = Channel::None;
    if (settings->notificationAudioEnabled()) {
        m_enabledChannels |= Channel::Audio;
    }
    if (settings->notificationDesktopEnabled()) {
        m_enabledChannels |= Channel::Desktop;
    }
    if (settings->notificationSystemTrayEnabled()) {
        m_enabledChannels |= Channel::SystemTray;
    }
    if (settings->notificationInTerminalEnabled()) {
        m_enabledChannels |= Channel::InTerminal;
    }
    m_audioVolume = qBound(0.0, settings->notificationAudioVolume(), 1.0);
    m_yoloNotificationsEnabled = settings->notificationYoloEnabled();
}

void NotificationManager::saveSettings()
{
    auto *settings = KonsolaiSettings::instance();
    if (!settings) {
        return;
    }

    settings->setNotificationAudioEnabled(m_enabledChannels.testFlag(Channel::Audio));
    settings->setNotificationDesktopEnabled(m_enabledChannels.testFlag(Channel::Desktop));
    settings->setNotificationSystemTrayEnabled(m_enabledChannels.testFlag(Channel::SystemTray));
    settings->setNotificationInTerminalEnabled(m_enabledChannels.testFlag(Channel::InTerminal));
    settings->setNotificationAudioVolume(m_audioVolume);
    settings->setNotificationYoloEnabled(m_yoloNotificationsEnabled);
    settings->save();
}

} // namespace Konsolai

#include "moc_NotificationManager.cpp"
