#ifndef COMMANDLINEHANDLER_H
#define COMMANDLINEHANDLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSizeF>
#include <QTimer>
#include "DataProcessor.h"

class MainWindow;

struct CommandLineArgs {
    QString dssatBase;
    QString cropDir;
    QString cropName;
    QStringList outputFiles;
    bool isValid = false;

    // Headless plot mode (set by --xvar, --yvar, --save, --metrics flags)
    QString xVar;               // --xvar DAS
    QStringList yVars;          // --yvar LAID,TOPMD
    QString savePlotPath;       // --save plot.png
    QString saveMetricsPath;    // --metrics metrics.csv
    QSizeF saveSizeInches;      // --size 4.1x2.9 : figure size in inches (empty = default canvas)
    bool saveLegend = true;     // --no-legend : export without the legend
    bool legendInside = false;  // --legend-inside : float the legend over the plot
    bool headlessMode = false;  // true when --save is present

    // Scatter headless mode
    bool scatterMode = false;        // --scatter
    QStringList scatterVars;         // --scatter-vars ADAP,CWAM
    QStringList scatterMetrics;      // --scatter-metrics "RMSE,R2,d-stat"

    bool boxPlotMode = false;        // --boxplot
    bool gridMode = false;           // --grid : experiment × variable multi-panel grid
};

class CommandLineHandler : public QObject
{
    Q_OBJECT

public:
    // Process exit codes reported to the calling program in headless mode
    enum ExitCode {
        ExitOk = 0,
        ExitInternalError = 1,
        ExitCropNotFound = 2,
        ExitNoOutputFiles = 3,
        ExitEmptyData = 4,
        ExitExportFailed = 5,
        ExitTimeout = 6
    };
    static constexpr int kRenderTimeoutMs = 60000;  // safety net only, not a normal wait

    explicit CommandLineHandler(QObject *parent = nullptr);
    
    static CommandLineArgs parseCommandLineArgs(const QStringList &args);
    void setupCommandLineIntegration(MainWindow *mainWindow, const QStringList &args);

private slots:
    void applyCommandLineArgsToUI();
    void selectOutputFiles();
    void loadInitialContent();
    void headlessAutoPlot();
    void headlessScatterPlot();
    void runScatterPlot(const DataTable &evalData);

private:
    MainWindow *m_mainWindow;
    CommandLineArgs m_args;
    
    bool selectCropFolder(const QString &cropName);
    static QString extractCropNameFromPath(const QString &cropDirPath);
};

#endif // COMMANDLINEHANDLER_H