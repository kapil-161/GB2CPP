#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <QIcon>
#include <QScreen>
#include <QLoggingCategory>
#include <QDebug>

#include "MainWindow.h"
#include "Config.h"
#include "CommandLineHandler.h"
#include "SingleInstanceApp.h"

// Enable/disable debug output
Q_LOGGING_CATEGORY(appCategory, "app")

void setupApplicationIcon(QApplication &app)
{
    // Set application icon
    QIcon appIcon;
    
    // Try to load icon from resources directory
    QString resourcesPath = QDir::currentPath() + QDir::separator() + "resources";
    QStringList iconFiles = {"final.ico", "final.png", "icon.png", "app.png"};
    
    bool iconLoaded = false;
    for (const QString &iconFile : iconFiles) {
        QString iconPath = resourcesPath + QDir::separator() + iconFile;
        if (QFile::exists(iconPath)) {
            appIcon.addFile(iconPath);
            iconLoaded = true;
            qCDebug(appCategory) << "Loaded application icon:" << iconPath;
            break;
        }
    }
    
    if (!iconLoaded) {
        qCWarning(appCategory) << "No application icon found in resources directory";
    }
    
    app.setWindowIcon(appIcon);
    
#ifdef Q_OS_WIN
    // Set Windows-specific application ID for taskbar grouping
    app.setProperty("applicationId", QString("%1.%2").arg(Config::ORGANIZATION_NAME, Config::APP_NAME));
#endif
}

void centerWindow(QWidget *window)
{
    if (QScreen *screen = QApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - window->width()) / 2;
        int y = (screenGeometry.height() - window->height()) / 2;
        window->move(x, y);
    }
}

void setupApplicationStyle(QApplication &app)
{
    // Set application style
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Set application-wide stylesheet for modern look
    QString styleSheet = R"(
        QMainWindow {
            background-color: #f0f0f0;
        }
        
        QGroupBox {
            font-weight: bold;
            border: 2px solid #cccccc;
            border-radius: 5px;
            margin-top: 1ex;
            padding-top: 10px;
        }
        
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px 0 5px;
        }
        
        QPushButton {
            background-color: #e1e1e1;
            border: 1px solid #999999;
            border-radius: 3px;
            padding: 5px 10px;
            min-width: 80px;
        }
        
        QPushButton:hover {
            background-color: #d4d4d4;
        }
        
        QPushButton:pressed {
            background-color: #c4c4c4;
        }
        
        QPushButton:disabled {
            background-color: #f0f0f0;
            color: #999999;
        }
        
        QComboBox {
            border: 1px solid #999999;
            border-radius: 3px;
            padding: 3px 5px;
            background-color: white;
        }
        
        QComboBox:hover {
            border: 1px solid #666666;
        }
        
        QLineEdit {
            border: 1px solid #999999;
            border-radius: 3px;
            padding: 3px 5px;
            background-color: white;
        }
        
        QLineEdit:focus {
            border: 2px solid #0078d4;
        }
        
        QTableView {
            border: 1px solid #cccccc;
            selection-background-color: #0078d4;
            alternate-background-color: #f9f9f9;
            gridline-color: #dddddd;
        }
        
        QTableView::item {
            padding: 3px;
            border-bottom: 1px solid #eeeeee;
        }
        
        QTableView::item:selected {
            background-color: #0078d4;
            color: white;
        }
        
        QHeaderView::section {
            background-color: #e9e9e9;
            padding: 5px;
            border: 1px solid #cccccc;
            font-weight: bold;
        }
        
        QTabWidget::pane {
            border: 1px solid #cccccc;
            border-radius: 3px;
        }
        
        QTabBar::tab {
            background-color: #e1e1e1;
            border: 1px solid #999999;
            padding: 8px 16px;
            margin-right: 2px;
        }
        
        QTabBar::tab:selected {
            background-color: white;
            border-bottom: 1px solid white;
        }
        
        QTabBar::tab:hover:!selected {
            background-color: #d4d4d4;
        }
    )";
    
    app.setStyleSheet(styleSheet);
}

int main(int argc, char *argv[])
{
    // Create single instance application
    SingleInstanceApp app(argc, argv);
    
    // Set application properties
    QCoreApplication::setApplicationName(Config::APP_NAME);
    QCoreApplication::setApplicationVersion(Config::APP_VERSION);
    QCoreApplication::setOrganizationName(Config::ORGANIZATION_NAME);
    
    // High DPI scaling is enabled by default in Qt6
    
    qCInfo(appCategory) << "Starting" << Config::APP_NAME << "version" << Config::APP_VERSION;
    qCInfo(appCategory) << "Qt version:" << QT_VERSION_STR;
    qCInfo(appCategory) << "Platform:" << QApplication::platformName();
    
    // Check if another instance is already running
    if (!app.isFirstInstance()) {
        qCWarning(appCategory) << "Another instance of" << Config::APP_NAME << "is already running";
        app.showAlreadyRunningMessage();
        return 1;
    }
    
    try {
        // Setup application appearance
        setupApplicationIcon(app);
        setupApplicationStyle(app);
        
        // Create main window
        MainWindow window;
        
        // Check for command line args before showing window
        // This allows us to hide UI elements before they're visible
        // Simple check: if we have at least 2 args (program name + params), hide UI
        if (app.arguments().size() >= 2) {
            // Hide file selection UI before window is shown to prevent any visible change
            window.hideFileSelectionUI(true);
        }
        
        // Center window on screen
        centerWindow(&window);
        
        // Show window
        window.show();
        
        // Setup command line integration (will apply args after UI is ready)
        CommandLineHandler cmdHandler;
        cmdHandler.setupCommandLineIntegration(&window, app.arguments());
        
        qCInfo(appCategory) << "Application started successfully";
        
        // Run event loop
        int result = app.exec();
        
        qCInfo(appCategory) << "Application exiting with code:" << result;
        return result;
        
    } catch (const std::exception &e) {
        qCCritical(appCategory) << "Fatal error:" << e.what();
        
        QMessageBox::critical(
            nullptr,
            "Fatal Error",
            QString("A fatal error occurred:\n\n%1\n\nThe application will now exit.").arg(e.what())
        );
        
        return 1;
        
    } catch (...) {
        qCCritical(appCategory) << "Unknown fatal error occurred";
        
        QMessageBox::critical(
            nullptr,
            "Fatal Error",
            "An unknown fatal error occurred.\n\nThe application will now exit."
        );
        
        return 1;
    }
}