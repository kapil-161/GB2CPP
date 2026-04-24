#ifndef COMMANDLINEHANDLER_H
#define COMMANDLINEHANDLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

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
    bool headlessMode = false;  // true when --save is present

    // Scatter headless mode
    bool scatterMode = false;        // --scatter
    QStringList scatterVars;         // --scatter-vars ADAP,CWAM
    QStringList scatterMetrics;      // --scatter-metrics "RMSE,R2,d-stat"
};

class CommandLineHandler : public QObject
{
    Q_OBJECT

public:
    explicit CommandLineHandler(QObject *parent = nullptr);
    
    static CommandLineArgs parseCommandLineArgs(const QStringList &args);
    void setupCommandLineIntegration(MainWindow *mainWindow, const QStringList &args);

private slots:
    void applyCommandLineArgsToUI();
    void selectOutputFiles();
    void loadInitialContent();
    void headlessAutoPlot();
    void headlessScatterPlot();

private:
    MainWindow *m_mainWindow;
    CommandLineArgs m_args;
    
    bool selectCropFolder(const QString &cropName);
    static QString extractCropNameFromPath(const QString &cropDirPath);
};

#endif // COMMANDLINEHANDLER_H