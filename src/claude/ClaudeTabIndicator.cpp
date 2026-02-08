/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeTabIndicator.h"
#include "ClaudeSession.h"

#include <QPaintEvent>
#include <QPainter>

namespace Konsolai
{

ClaudeTabIndicator::ClaudeTabIndicator(QWidget *parent)
    : QWidget(parent)
    , m_animationTimer(new QTimer(this))
    , m_suggestionDelayTimer(new QTimer(this))
{
    setFixedSize(SIZE, SIZE);

    m_animationTimer->setInterval(100); // 10 FPS
    connect(m_animationTimer, &QTimer::timeout, this, &ClaudeTabIndicator::updateAnimation);

    m_suggestionDelayTimer->setSingleShot(true);
    m_suggestionDelayTimer->setInterval(3000); // 3s idle → suggestion available
    connect(m_suggestionDelayTimer, &QTimer::timeout, this, &ClaudeTabIndicator::onSuggestionDelayElapsed);
}

ClaudeTabIndicator::~ClaudeTabIndicator() = default;

void ClaudeTabIndicator::setSession(ClaudeSession *session)
{
    if (m_session == session) {
        return;
    }

    // Disconnect old session
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
    }

    m_session = session;

    // Connect new session
    if (m_session) {
        connect(m_session, &ClaudeSession::stateChanged, this, &ClaudeTabIndicator::updateState);
        connect(m_session, &QObject::destroyed, this, &ClaudeTabIndicator::onSessionDestroyed);
        connect(m_session, &ClaudeSession::yoloModeChanged, this, [this](bool) {
            update();
        });
        connect(m_session, &ClaudeSession::doubleYoloModeChanged, this, [this](bool) {
            update();
        });
        connect(m_session, &ClaudeSession::tripleYoloModeChanged, this, [this](bool) {
            update();
        });
        connect(m_session, &ClaudeSession::approvalCountChanged, this, [this]() {
            update();
        });

        updateState(m_session->claudeState());
    } else {
        m_currentState = ClaudeProcess::State::NotRunning;
        m_suggestionAvailable = false;
        m_animationTimer->stop();
        m_suggestionDelayTimer->stop();
        update();
    }
}

QSize ClaudeTabIndicator::sizeHint() const
{
    return QSize(SIZE, SIZE);
}

QSize ClaudeTabIndicator::minimumSizeHint() const
{
    return QSize(SIZE, SIZE);
}

void ClaudeTabIndicator::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Determine yolo level for border color
    int yoloLevel = 0;
    if (m_session) {
        if (m_session->tripleYoloMode()) {
            yoloLevel = 3;
        } else if (m_session->doubleYoloMode()) {
            yoloLevel = 2;
        } else if (m_session->yoloMode()) {
            yoloLevel = 1;
        }
    }

    // Draw colored dot
    QColor color = stateColor(m_currentState);
    if (m_currentState == ClaudeProcess::State::Idle && m_suggestionAvailable) {
        color = QColor(0, 188, 212); // Cyan/teal for suggestion available
    }
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);

    int centerX = width() / 2;
    int centerY = height() / 2;

    if (m_currentState == ClaudeProcess::State::Working) {
        // Draw spinning animation
        painter.save();
        painter.translate(centerX, centerY);
        painter.rotate(m_animationPhase * 360.0);

        // Draw arc
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 2));
        painter.drawArc(-DOT_SIZE / 2, -DOT_SIZE / 2, DOT_SIZE, DOT_SIZE, 0, 270 * 16);

        painter.restore();
    } else {
        // Draw solid dot
        painter.drawEllipse(centerX - DOT_SIZE / 2, centerY - DOT_SIZE / 2, DOT_SIZE, DOT_SIZE);
    }

    // Draw yolo mode border ring
    if (yoloLevel > 0) {
        QColor yoloColor;
        if (yoloLevel == 3) {
            yoloColor = QColor(170, 0, 255); // Purple for triple
        } else if (yoloLevel == 2) {
            yoloColor = QColor(33, 150, 243); // Blue for double
        } else {
            yoloColor = QColor(255, 215, 0); // Gold for single
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(yoloColor, 1.5));
        int ringSize = DOT_SIZE + 4;
        painter.drawEllipse(centerX - ringSize / 2, centerY - ringSize / 2, ringSize, ringSize);

        // Draw lightning bolt count at bottom
        if (yoloLevel > 1) {
            QFont font = painter.font();
            font.setPixelSize(6);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(yoloColor);
            painter.drawText(rect(), Qt::AlignBottom | Qt::AlignHCenter, QString::number(yoloLevel));
        }
    }
}

void ClaudeTabIndicator::updateState(ClaudeProcess::State state)
{
    m_currentState = state;
    m_suggestionAvailable = false;

    if (state == ClaudeProcess::State::Working) {
        m_animationTimer->start();
        m_suggestionDelayTimer->stop();
    } else if (state == ClaudeProcess::State::Idle) {
        m_animationTimer->stop();
        m_animationPhase = 0.0;
        m_suggestionDelayTimer->start();
    } else {
        m_animationTimer->stop();
        m_animationPhase = 0.0;
        m_suggestionDelayTimer->stop();
    }

    update();
}

void ClaudeTabIndicator::onSuggestionDelayElapsed()
{
    if (m_currentState == ClaudeProcess::State::Idle) {
        m_suggestionAvailable = true;
        update();
    }
}

void ClaudeTabIndicator::updateAnimation()
{
    m_animationPhase += 0.05;
    if (m_animationPhase >= 1.0) {
        m_animationPhase = 0.0;
    }
    update();
}

void ClaudeTabIndicator::onSessionDestroyed()
{
    m_session = nullptr;
    m_currentState = ClaudeProcess::State::NotRunning;
    m_suggestionAvailable = false;
    m_animationTimer->stop();
    m_suggestionDelayTimer->stop();
    update();
}

QColor ClaudeTabIndicator::stateColor(ClaudeProcess::State state) const
{
    switch (state) {
    case ClaudeProcess::State::NotRunning:
        return QColor(117, 117, 117); // Gray
    case ClaudeProcess::State::Starting:
        return QColor(33, 150, 243); // Blue
    case ClaudeProcess::State::Idle:
        return QColor(76, 175, 80); // Green
    case ClaudeProcess::State::Working:
        return QColor(33, 150, 243); // Blue
    case ClaudeProcess::State::WaitingInput:
        return QColor(255, 152, 0); // Orange
    case ClaudeProcess::State::Error:
        return QColor(244, 67, 54); // Red
    default:
        return QColor(158, 158, 158); // Light gray
    }
}

} // namespace Konsolai

#include "moc_ClaudeTabIndicator.cpp"
