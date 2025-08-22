#ifndef STATUSWIDGET_H
#define STATUSWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QMessageBox>
#include "Config.h"

class StatusWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StatusWidget(QWidget *parent = nullptr);
    
    void showSuccess(const QString &message, int timeout = 3000, bool center = false, bool flash = false);
    void showError(const QString &message, int timeout = 5000, bool center = false, bool flash = false);
    void showWarning(const QString &message, int timeout = 4000, bool center = false, bool flash = false);
    void showInfo(const QString &message, int timeout = 3000, bool center = false, bool flash = false);
    void showProgress(const QString &message);
    void hideProgress();
    void clear();

private slots:
    void onClearTimer();
    void onFlashTimer();

private:
    void setupUI();
    void showCenterMessage(const QString &message, const QColor &bgColor);
    void startFlash(const QString &message, const QString &style, bool center = false, const QColor &bgColor = QColor());
    
    QHBoxLayout *m_layout;
    QLabel *m_messageLabel;
    QProgressBar *m_progressBar;
    QTimer *m_clearTimer;
    QTimer *m_flashTimer;
    
    // Flash functionality
    int m_flashCount;
    bool m_flashVisible;
    QString m_flashMessage;
    QString m_flashStyle;
    bool m_flashCenter;
    QColor m_flashBgColor;
};

#endif // STATUSWIDGET_H