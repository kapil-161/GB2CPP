#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <QIcon>
#include <QScreen>
#include <QLoggingCategory>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "MainWindow.h"
#include "Config.h"
#include "CommandLineHandler.h"
#include "SingleInstanceApp.h"

// Enable/disable debug output
Q_LOGGING_CATEGORY(appCategory, "app")

// Global flag for verbose mode (set from command line)
static bool g_verboseMode = false;

// Message handler to suppress all debug output in release builds (unless verbose mode)
void releaseMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Always show observed data messages, even in release builds
    if (g_verboseMode || msg.contains("observed", Qt::CaseInsensitive) || 
        msg.contains("Observed", Qt::CaseInsensitive) ||
        msg.contains("readObservedData", Qt::CaseInsensitive) ||
        msg.contains("readSensWorkObservedData", Qt::CaseInsensitive) ||
        msg.contains("readTFile", Qt::CaseInsensitive) ||
        msg.contains("DataProcessor: Attempting to find", Qt::CaseInsensitive) ||
        msg.contains("DataProcessor: Found observed", Qt::CaseInsensitive) ||
        msg.contains("DataProcessor: Reading observed", Qt::CaseInsensitive) ||
        msg.contains("DataProcessor: Successfully read observed", Qt::CaseInsensitive) ||
        msg.contains("MainWindow: Attempting to load observed", Qt::CaseInsensitive) ||
        msg.contains("MainWindow: Successfully loaded observed", Qt::CaseInsensitive)) {
        // Print to stderr so it shows in console
        const char *typeStr = "";
        switch (type) {
            case QtDebugMsg: typeStr = "DEBUG"; break;
            case QtInfoMsg: typeStr = "INFO"; break;
            case QtWarningMsg: typeStr = "WARNING"; break;
            case QtCriticalMsg: typeStr = "CRITICAL"; break;
            case QtFatalMsg: typeStr = "FATAL"; break;
        }
        fprintf(stderr, "[%s] %s\n", typeStr, msg.toLocal8Bit().constData());
        return;
    }
    
    Q_UNUSED(type);
    Q_UNUSED(context);
    Q_UNUSED(msg);
    // Suppress all other messages in release builds
}

// Verbose message handler that shows all debug output
void verboseMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    
    const char *typeStr = "";
    switch (type) {
        case QtDebugMsg: typeStr = "DEBUG"; break;
        case QtInfoMsg: typeStr = "INFO"; break;
        case QtWarningMsg: typeStr = "WARNING"; break;
        case QtCriticalMsg: typeStr = "CRITICAL"; break;
        case QtFatalMsg: typeStr = "FATAL"; break;
    }
    
    fprintf(stderr, "[%s] %s\n", typeStr, msg.toLocal8Bit().constData());
}

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
    // Check for verbose/debug flag before creating application
    QStringList args;
    for (int i = 0; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }
    
    // Check for --verbose or --debug flags
    g_verboseMode = args.contains("--verbose", Qt::CaseInsensitive) || 
                    args.contains("--debug", Qt::CaseInsensitive) ||
                    args.contains("-v", Qt::CaseInsensitive);
    
    // Remove verbose flags from args so they don't interfere with other processing
    args.removeAll("--verbose");
    args.removeAll("--debug");
    args.removeAll("-v");
    
    // Convert back to argc/argv format for QApplication
    QVector<QByteArray> argvData;
    QVector<char*> argvPtrs;
    for (const QString &arg : args) {
        argvData.append(arg.toLocal8Bit());
        argvPtrs.append(argvData.last().data());
    }
    argvPtrs.append(nullptr);
    
    int newArgc = argvPtrs.size() - 1;
    char **newArgv = argvPtrs.data();
    
    // Setup message handler based on build type and verbose mode
    if (g_verboseMode) {
        // Verbose mode: show all debug output
        qInstallMessageHandler(verboseMessageHandler);
    } else {
#ifndef ENABLE_DEBUG_OUTPUT
        // Release build without verbose: suppress most output but allow observed data
        qInstallMessageHandler(releaseMessageHandler);
#endif
    }

#ifdef Q_OS_WIN
    // Allocate console for Windows to see debug output when run from command line
    // Strategy:
    // 1. Always try to attach to parent console (if run from terminal, this succeeds)
    // 2. Only allocate NEW console if verbose mode is enabled (to avoid popup windows)
    // 3. Observed data messages will be visible if console exists (attached or allocated)
#ifdef ENABLE_DEBUG_OUTPUT
    bool shouldAllocConsole = true;
#else
    // In release builds, try to attach to existing console first
    // Only allocate new console if verbose mode is explicitly enabled
    bool shouldAllocConsole = g_verboseMode;
#endif
    
    // Try to attach to parent console first (works if run from command prompt)
    bool consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS);
    
    // If not attached and we should allocate, create new console
    if (!consoleAttached && shouldAllocConsole) {
        consoleAttached = AllocConsole();
    }
    
    if (consoleAttached) {
        FILE* pCout;
        FILE* pCerr;
        FILE* pCin;
        freopen_s(&pCout, "CONOUT$", "w", stdout);
        freopen_s(&pCerr, "CONOUT$", "w", stderr);
        freopen_s(&pCin, "CONIN$", "r", stdin);
        std::ios::sync_with_stdio();
        
        if (g_verboseMode) {
            fprintf(stdout, "\n=== GB2 Verbose Mode Enabled ===\n");
            fprintf(stdout, "All debug output will be shown, especially observed data messages.\n\n");
        } else {
            fprintf(stdout, "\n=== GB2 Running ===\n");
            fprintf(stdout, "Observed data messages will be shown in this console.\n");
            fprintf(stdout, "Use --verbose or --debug flag to see all debug messages.\n\n");
        }
    }
#endif

    // Create single instance application with modified args
    SingleInstanceApp app(newArgc, newArgv);
    
    // Set application properties
    QCoreApplication::setApplicationName(Config::APP_NAME);
    QCoreApplication::setApplicationVersion(Config::APP_VERSION);
    QCoreApplication::setOrganizationName(Config::ORGANIZATION_NAME);
    
    // High DPI scaling is enabled by default in Qt6
    
#ifdef ENABLE_DEBUG_OUTPUT
    qCInfo(appCategory) << "Starting" << Config::APP_NAME << "version" << Config::APP_VERSION;
    qCInfo(appCategory) << "Qt version:" << QT_VERSION_STR;
    qCInfo(appCategory) << "Platform:" << QApplication::platformName();
#else
    if (g_verboseMode) {
        qCInfo(appCategory) << "Starting" << Config::APP_NAME << "version" << Config::APP_VERSION;
        qCInfo(appCategory) << "Qt version:" << QT_VERSION_STR;
        qCInfo(appCategory) << "Platform:" << QApplication::platformName();
        qCInfo(appCategory) << "Verbose mode: ENABLED - All debug output will be shown";
    }
#endif
    
    // Check if another instance is already running
    if (!app.isFirstInstance()) {
#ifdef ENABLE_DEBUG_OUTPUT
        qCWarning(appCategory) << "Another instance of" << Config::APP_NAME << "is already running";
#endif
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
        
#ifdef ENABLE_DEBUG_OUTPUT
        qCInfo(appCategory) << "Application started successfully";
#else
        if (g_verboseMode) {
            qCInfo(appCategory) << "Application started successfully";
            qCInfo(appCategory) << "Observed data debug messages will appear in this console";
        }
#endif
        
        // Run event loop
        int result = app.exec();
        
#ifdef ENABLE_DEBUG_OUTPUT
        qCInfo(appCategory) << "Application exiting with code:" << result;
#endif
        return result;
        
    } catch (const std::exception &e) {
#ifdef ENABLE_DEBUG_OUTPUT
        qCCritical(appCategory) << "Fatal error:" << e.what();
#endif
        
        QMessageBox::critical(
            nullptr,
            "Fatal Error",
            QString("A fatal error occurred:\n\n%1\n\nThe application will now exit.").arg(e.what())
        );
        
        return 1;
        
    } catch (...) {
#ifdef ENABLE_DEBUG_OUTPUT
        qCCritical(appCategory) << "Unknown fatal error occurred";
#endif
        
        QMessageBox::critical(
            nullptr,
            "Fatal Error",
            "An unknown fatal error occurred.\n\nThe application will now exit."
        );
        
        return 1;
    }
}