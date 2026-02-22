/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeNotificationWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QIcon>

namespace Konsolai
{

ClaudeNotificationWidget::ClaudeNotificationWidget(QWidget *parent)
    : QWidget(parent)
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new QLabel(this))
    , m_messageLabel(new QLabel(this))
    , m_autoHideTimer(new QTimer(this))
    , m_fadeAnimation(new QPropertyAnimation(this, "opacity", this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAutoFillBackground(false);

    // Setup layout
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(8);

    m_iconLabel->setFixedSize(24, 24);
    layout->addWidget(m_iconLabel);

    auto *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    textLayout->addWidget(m_titleLabel);

    m_messageLabel->setWordWrap(true);
    textLayout->addWidget(m_messageLabel);

    layout->addLayout(textLayout, 1);

    // Setup auto-hide timer
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, &ClaudeNotificationWidget::hideWithAnimation);

    // Setup fade animation
    m_fadeAnimation->setDuration(ANIMATION_DURATION);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacity <= 0.0) {
            hide();
        }
    });

    // Start hidden
    hide();
    m_opacity = 0.0;
}

ClaudeNotificationWidget::~ClaudeNotificationWidget() = default;

void ClaudeNotificationWidget::show(NotificationManager::NotificationType type,
                                     const QString &title,
                                     const QString &message,
                                     int autoHideMs)
{
    updateColors(type);

    // Set icon
    QString iconName = NotificationManager::iconName(type);
    m_iconLabel->setPixmap(QIcon::fromTheme(iconName).pixmap(24, 24));

    // Set text
    m_titleLabel->setText(title);
    m_messageLabel->setText(message);

    // Update style based on colors
    QString style = QStringLiteral("color: %1;").arg(m_textColor.name());
    m_titleLabel->setStyleSheet(style + QStringLiteral(" font-weight: bold;"));
    m_messageLabel->setStyleSheet(style);

    // Position and show
    updatePosition();
    QWidget::show();
    raise();

    // Fade in
    fadeIn();

    // Setup auto-hide
    if (autoHideMs > 0) {
        m_autoHideTimer->start(autoHideMs);
    } else {
        m_autoHideTimer->stop();
    }
}

void ClaudeNotificationWidget::hideWithAnimation()
{
    m_autoHideTimer->stop();
    fadeOut();
}

void ClaudeNotificationWidget::setOpacity(qreal opacity)
{
    m_opacity = opacity;
    update();
}

void ClaudeNotificationWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    if (m_opacity <= 0.0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_opacity);

    // Draw rounded rect background
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), BORDER_RADIUS, BORDER_RADIUS);

    painter.fillPath(path, m_backgroundColor);

    // Draw border
    QPen pen(m_borderColor, 1.5);
    painter.setPen(pen);
    painter.drawPath(path);
}

void ClaudeNotificationWidget::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    hideWithAnimation();
}

void ClaudeNotificationWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePosition();
}

void ClaudeNotificationWidget::updateColors(NotificationManager::NotificationType type)
{
    switch (type) {
    case NotificationManager::NotificationType::Permission:
        m_backgroundColor = QColor(255, 152, 0, 230);  // Orange
        m_textColor = QColor(255, 255, 255);
        m_borderColor = QColor(230, 126, 0);
        break;
    case NotificationManager::NotificationType::Error:
        m_backgroundColor = QColor(244, 67, 54, 230);  // Red
        m_textColor = QColor(255, 255, 255);
        m_borderColor = QColor(211, 47, 47);
        break;
    case NotificationManager::NotificationType::WaitingInput:
        m_backgroundColor = QColor(255, 193, 7, 230);  // Yellow/Amber
        m_textColor = QColor(33, 33, 33);
        m_borderColor = QColor(255, 160, 0);
        break;
    case NotificationManager::NotificationType::TaskComplete:
        m_backgroundColor = QColor(76, 175, 80, 230);  // Green
        m_textColor = QColor(255, 255, 255);
        m_borderColor = QColor(56, 142, 60);
        break;
    case NotificationManager::NotificationType::YoloApproval:
        m_backgroundColor = QColor(255, 179, 0, 180); // Gold (semi-transparent)
        m_textColor = QColor(33, 33, 33);
        m_borderColor = QColor(255, 143, 0);
        break;
    case NotificationManager::NotificationType::Info:
    default:
        m_backgroundColor = QColor(66, 66, 66, 230);   // Gray
        m_textColor = QColor(255, 255, 255);
        m_borderColor = QColor(97, 97, 97);
        break;
    }
}

void ClaudeNotificationWidget::updatePosition()
{
    if (!parentWidget()) {
        return;
    }

    // Calculate size based on content
    adjustSize();

    // Position at top center of parent, with margin
    int x = (parentWidget()->width() - width()) / 2;
    int y = MARGIN;

    move(x, y);
}

void ClaudeNotificationWidget::fadeIn()
{
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
}

void ClaudeNotificationWidget::fadeOut()
{
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}

} // namespace Konsolai

#include "moc_ClaudeNotificationWidget.cpp"
