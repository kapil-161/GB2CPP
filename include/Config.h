#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QStringList>
#include <QColor>
#include <QSize>
#include <set>

// Debug output control - disable for release builds
#if defined(ENABLE_DEBUG_OUTPUT) && ENABLE_DEBUG_OUTPUT
    #include <QDebug>
    #define DEBUG_OUTPUT(x) qDebug() << x
    #define WARNING_OUTPUT(x) qWarning() << x
    #define CRITICAL_OUTPUT(x) qCritical() << x
#else
    // Release build - all debug output disabled
    #define DEBUG_OUTPUT(x) ((void)0)
    #define WARNING_OUTPUT(x) ((void)0)
    #define CRITICAL_OUTPUT(x) ((void)0)
#endif

namespace Config {
    // Application information
    const QString APP_NAME = "GB2";
    const QString APP_VERSION = "2.0.0";
    const QString ORGANIZATION_NAME = "DSSAT";
    
    // DSSAT paths
#ifdef Q_OS_WIN
    const QString DSSAT_BASE = "C:\\DSSAT48";
    const QString DSSAT_EXE = "DSCSM048.EXE";
    const QStringList DSSAT_SEARCH_PATHS = {
        "C:\\DSSAT48",
        "C:\\Program Files\\DSSAT48", 
        "C:\\Program Files (x86)\\DSSAT48"
    };
#else
    const QString DSSAT_BASE = "/Applications/DSSAT48";
    const QString DSSAT_EXE = "DSCSM048";
    const QStringList DSSAT_SEARCH_PATHS = {
        "/Applications/DSSAT48",
        "/usr/local/DSSAT48"
    };
#endif
    
    // File encoding
    const QString DEFAULT_ENCODING = "UTF-8";
    const QString FALLBACK_ENCODING = "ISO-8859-1";
    
    // Missing values for DSSAT files
    const std::set<double> MISSING_VALUES = {-99, -99.0, -99.9, -99.99};
    const QStringList MISSING_VALUE_STRINGS = {"-99", "-99.0", "-99.9", "-99.99"};
    
    // Plot styling
    const QStringList LINE_STYLES = {"solid", "dash", "dot"};
    const QStringList MARKER_SYMBOLS = {"circle", "square", "diamond", "triangle", "plus", "cross", "pentagon", "hexagon", "star"};
    const QList<QColor> PLOT_COLORS = {
        QColor("#1f77b4"), QColor("#ff7f0e"), QColor("#2ca02c"), QColor("#d62728"),
        QColor("#9467bd"), QColor("#8c564b"), QColor("#e377c2"), QColor("#7f7f7f"), 
        QColor("#bcbd22"), QColor("#17becf")
    };
    
    // Window configuration
    struct WindowConfig {
        static const int WIDTH = 1000;
        static const int HEIGHT = 600;
        static const int MIN_WIDTH = 800;
        static const int MIN_HEIGHT = 600;
    };
    
    // Status message colors
    const QColor SUCCESS_COLOR = QColor("#4CAF50");
    const QColor ERROR_COLOR = QColor("#F44336");
    const QColor WARNING_COLOR = QColor("#FF9800");
    const QColor INFO_COLOR = QColor("#2196F3");
}

#endif // CONFIG_H