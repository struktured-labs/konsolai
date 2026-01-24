/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDENOTIFICATIONWIDGET_H
#define CLAUDENOTIFICATIONWIDGET_H

#include "NotificationManager.h"

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>

namespace Konsolai
{

/**
 * ClaudeNotificationWidget displays in-terminal notifications.
 *
 * This widget is an overlay that appears at the top of the terminal
 * to show Claude status notifications. It supports:
 * - Auto-hide after timeout
 * - Fade in/out animations
 * - Color-coded by notification type
 * - Click to dismiss
 */
class ClaudeNotificationWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit ClaudeNotificationWidget(QWidget *parent = nullptr);
    ~ClaudeNotificationWidget() override;

    /**
     * Show a notification
     *
     * @param type Notification type (affects color)
     * @param title Short title
     * @param message Detailed message
     * @param autoHideMs Auto-hide after this many ms (0 = don't auto-hide)
     */
    void show(NotificationManager::NotificationType type,
              const QString &title,
              const QString &message,
              int autoHideMs = 5000);

    /**
     * Hide the notification with animation
     */
    void hideWithAnimation();

    /**
     * Get/set opacity
     */
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateColors(NotificationManager::NotificationType type);
    void updatePosition();
    void fadeIn();
    void fadeOut();

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_messageLabel = nullptr;

    QTimer *m_autoHideTimer = nullptr;
    QPropertyAnimation *m_fadeAnimation = nullptr;

    qreal m_opacity = 0.0;
    QColor m_backgroundColor;
    QColor m_textColor;
    QColor m_borderColor;

    static constexpr int MARGIN = 10;
    static constexpr int PADDING = 8;
    static constexpr int BORDER_RADIUS = 6;
    static constexpr int ANIMATION_DURATION = 200;
};

} // namespace Konsolai

#endif // CLAUDENOTIFICATIONWIDGET_H
