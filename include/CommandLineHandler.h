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

private:
    MainWindow *m_mainWindow;
    CommandLineArgs m_args;
    
    bool selectCropFolder(const QString &cropName);
    static QString extractCropNameFromPath(const QString &cropDirPath);
};

#endif // COMMANDLINEHANDLER_H