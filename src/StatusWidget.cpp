#include "StatusWidget.h"
#include <QApplication>

StatusWidget::StatusWidget(QWidget *parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_messageLabel(nullptr)
    , m_progressBar(nullptr)
    , m_clearTimer(new QTimer(this))
    , m_flashTimer(new QTimer(this))
    , m_flashCount(0)
    , m_flashVisible(true)
    , m_flashCenter(false)
{
    setupUI();
    
    // Setup timers
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, &StatusWidget::onClearTimer);
    
    connect(m_flashTimer, &QTimer::timeout, this, &StatusWidget::onFlashTimer);
}

void StatusWidget::setupUI()
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(5, 2, 5, 2);
    
    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet("padding: 2px 5px;");
    m_layout->addWidget(m_messageLabel);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);  // Indeterminate progress
    m_progressBar->setFixedWidth(100);
    m_progressBar->hide();
    m_layout->addWidget(m_progressBar);
    
    m_layout->addStretch(1);
}

void StatusWidget::showCenterMessage(const QString &message, const QColor &bgColor)
{
    QMessageBox *msg = new QMessageBox(this);
    msg->setWindowTitle("");
    msg->setText(message);
    msg->setStandardButtons(QMessageBox::NoButton);
    msg->setWindowFlags(Qt::FramelessWindowHint);
    
    QString style = QString(
        "QMessageBox {"
        "    background-color: %1;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 14px;"
        "    border-radius: 10px;"
        "    border: 3px solid white;"
        "    min-width: 300px;"
        "    min-height: 80px;"
        "}"
    ).arg(bgColor.name());
    
    msg->setStyleSheet(style);
    msg->show();
    
    // Auto-close after 250ms for flash effect
    QTimer::singleShot(250, msg, &QMessageBox::close);
    QTimer::singleShot(300, msg, &QMessageBox::deleteLater);
    
    QApplication::processEvents();
}

void StatusWidget::startFlash(const QString &message, const QString &style, bool center, const QColor &bgColor)
{
    m_flashCount = 0;
    m_flashVisible = true;
    m_flashMessage = message;
    m_flashStyle = style;
    m_flashCenter = center;
    m_flashBgColor = bgColor;
    
    // Show initial message
    if (center) {
        showCenterMessage(message, bgColor);
    } else {
        m_messageLabel->setText(message);
        m_messageLabel->setStyleSheet(style);
    }
    
    // Start flashing at 300ms intervals
    m_flashTimer->start(300);
}

void StatusWidget::onFlashTimer()
{
    if (m_flashCenter) {
        // For center messages, show the message box again
        if (m_flashVisible) {
            showCenterMessage(m_flashMessage, m_flashBgColor);
        }
    } else {
        // For regular messages, toggle between original color and white background
        if (m_flashVisible) {
            // Show original style
            m_messageLabel->setText(m_flashMessage);
            m_messageLabel->setStyleSheet(m_flashStyle);
        } else {
            // Show inverted style (white background, dark text)
            m_messageLabel->setText(m_flashMessage);
            m_messageLabel->setStyleSheet("padding: 2px 5px; background-color: white; color: black; border-radius: 3px; border: 2px solid gray;");
        }
    }
    
    m_flashVisible = !m_flashVisible;
    m_flashCount++;
    
    // Stop flashing after 6 toggles (3 complete flash cycles)
    if (m_flashCount >= 6) {
        m_flashTimer->stop();
        // Show final state with original style
        if (!m_flashCenter) {
            m_messageLabel->setText(m_flashMessage);
            m_messageLabel->setStyleSheet(m_flashStyle);
        }
        // Auto-clear after 2 seconds
        m_clearTimer->start(2000);
    }
}

void StatusWidget::showSuccess(const QString &message, int timeout, bool center, bool flash)
{
    QString style = "padding: 2px 5px; background-color: #4CAF50; color: white; border-radius: 3px;";
    QColor bgColor(76, 175, 80, 220);
    
    if (flash) {
        startFlash(message, style, center, bgColor);
    } else {
        if (center) {
            showCenterMessage(message, bgColor);
        } else {
            m_messageLabel->setText(message);
            m_messageLabel->setStyleSheet(style);
            if (timeout > 0) {
                m_clearTimer->start(timeout);
            }
        }
    }
}

void StatusWidget::showError(const QString &message, int timeout, bool center, bool flash)
{
    QString style = "padding: 2px 5px; background-color: #F44336; color: white; border-radius: 3px;";
    QColor bgColor(244, 67, 54, 220);
    
    if (flash) {
        startFlash(message, style, center, bgColor);
    } else {
        if (center) {
            showCenterMessage(message, bgColor);
        } else {
            m_messageLabel->setText(message);
            m_messageLabel->setStyleSheet(style);
            if (timeout > 0) {
                m_clearTimer->start(timeout);
            }
        }
    }
}

void StatusWidget::showWarning(const QString &message, int timeout, bool center, bool flash)
{
    QString style = "padding: 2px 5px; background-color: #FF9800; color: white; border-radius: 3px;";
    QColor bgColor(255, 152, 0, 220);
    
    if (flash) {
        startFlash(message, style, center, bgColor);
    } else {
        if (center) {
            showCenterMessage(message, bgColor);
        } else {
            m_messageLabel->setText(message);
            m_messageLabel->setStyleSheet(style);
            if (timeout > 0) {
                m_clearTimer->start(timeout);
            }
        }
    }
}

void StatusWidget::showInfo(const QString &message, int timeout, bool center, bool flash)
{
    QString style = "padding: 2px 5px; background-color: #2196F3; color: white; border-radius: 3px;";
    QColor bgColor(33, 150, 243, 220);
    
    if (flash) {
        startFlash(message, style, center, bgColor);
    } else {
        if (center) {
            showCenterMessage(message, bgColor);
        } else {
            m_messageLabel->setText(message);
            m_messageLabel->setStyleSheet(style);
            if (timeout > 0) {
                m_clearTimer->start(timeout);
            }
        }
    }
}

void StatusWidget::showProgress(const QString &message)
{
    m_messageLabel->setText(message);
    m_messageLabel->setStyleSheet("padding: 2px 5px; background-color: #2196F3; color: white; border-radius: 3px;");
    m_progressBar->show();
}

void StatusWidget::hideProgress()
{
    m_progressBar->hide();
    clear();
}

void StatusWidget::clear()
{
    m_messageLabel->clear();
    m_messageLabel->setStyleSheet("");
    m_progressBar->hide();
}

void StatusWidget::onClearTimer()
{
    clear();
}