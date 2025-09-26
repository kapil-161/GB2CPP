#include "CommandLineHandler.h"
#include "MainWindow.h"
#include "DataProcessor.h"
#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QMessageBox>
#include <QListWidget>
#include <QRegularExpression>

CommandLineHandler::CommandLineHandler(QObject *parent)
    : QObject(parent), m_mainWindow(nullptr)
{
}

CommandLineArgs CommandLineHandler::parseCommandLineArgs(const QStringList &args)
{
    CommandLineArgs result;
    
    try {
        if (args.size() < 2) {
            qDebug() << "No command line arguments provided";
            return result;
        }
        
        // Join all arguments after the first one (in case paths have spaces)
        QStringList paramList = args.mid(1);
        QString paramString = paramList.join(" ");
        
        // Remove trailing period and quotes if present
        paramString = paramString.remove(QRegularExpression("[.\"']+$"));
        paramString = paramString.remove(QRegularExpression("^[\"']+"));
        
        // Split by comma
        QStringList params;
        for (const QString &param : paramString.split(',')) {
            QString cleaned = param.trimmed();
            cleaned = cleaned.remove(QRegularExpression("^[\"']+"));
            cleaned = cleaned.remove(QRegularExpression("[\"']+$"));
            if (!cleaned.isEmpty()) {
                params.append(cleaned);
            }
        }
        
        if (params.size() < 2) {
            qWarning() << "Insufficient parameters:" << params;
            return result;
        }
        
        result.dssatBase = params[0];
        result.cropDir = params[1];
        
        // Extract crop name from crop directory by finding the crop folder
        result.cropName = extractCropNameFromPath(result.cropDir);
        
        // Get output files if provided
        if (params.size() > 2) {
            for (int i = 2; i < params.size(); ++i) {
                QString file = params[i].trimmed();
                if (!file.isEmpty()) {
                    result.outputFiles.append(file);
                }
            }
        }
        
        result.isValid = true;
        
        qDebug() << "Parsed command line args:";
        qDebug() << "  DSSAT Base:" << result.dssatBase;
        qDebug() << "  Crop Dir:" << result.cropDir;
        qDebug() << "  Crop Name:" << result.cropName;
        qDebug() << "  Output Files:" << result.outputFiles;
        
    } catch (const std::exception &e) {
        qCritical() << "Error parsing command line arguments:" << e.what();
    }
    
    return result;
}

void CommandLineHandler::setupCommandLineIntegration(MainWindow *mainWindow, const QStringList &args)
{
    m_mainWindow = mainWindow;
    m_args = parseCommandLineArgs(args);
    
    if (m_args.isValid) {
        qDebug() << "Processing command line arguments...";
        // Use a timer to apply args after UI is fully initialized
        QTimer::singleShot(500, this, &CommandLineHandler::applyCommandLineArgsToUI);
    } else {
        qDebug() << "No valid command line arguments provided, starting normally";
    }
}

void CommandLineHandler::applyCommandLineArgsToUI()
{
    if (!m_mainWindow || !m_args.isValid) {
        return;
    }
    
    try {
        qDebug() << "Applying command line args - Crop:" << m_args.cropName 
                 << "Files:" << m_args.outputFiles;
        
        // Step 1: Select the crop folder
        if (!selectCropFolder(m_args.cropName)) {
            QString message = QString("Crop folder '%1' not found").arg(m_args.cropName);
            QMessageBox::warning(m_mainWindow, "Command Line Warning", message);
            return;
        }
        
        // Step 2: Load data for the selected folder
        m_mainWindow->loadExperiments();
        m_mainWindow->loadOutputFiles();
        
        // Step 3: Select output files (with delay to ensure UI is ready)
        QTimer::singleShot(200, this, &CommandLineHandler::selectOutputFiles);
        
    } catch (const std::exception &e) {
        QString message = QString("Failed to apply parameters: %1").arg(e.what());
        QMessageBox::critical(m_mainWindow, "Command Line Error", message);
    }
}

bool CommandLineHandler::selectCropFolder(const QString &cropName)
{
    return m_mainWindow->selectCropFolder(cropName);
}

void CommandLineHandler::selectOutputFiles()
{
    if (!m_mainWindow || m_args.outputFiles.isEmpty()) {
        qDebug() << "No output files specified in command line";
        return;
    }
    
    try {
        int selectedCount = m_mainWindow->selectOutputFiles(m_args.outputFiles);
        
        if (selectedCount > 0) {
            QString message = QString("Loaded %1 with %2 output files")
                                .arg(m_args.cropName).arg(selectedCount);
            QMessageBox::information(m_mainWindow, "Success", message);
            
            // Load the first tab content
            QTimer::singleShot(100, this, &CommandLineHandler::loadInitialContent);
        } else {
            QString message = QString("No valid output files found from: %1")
                                .arg(m_args.outputFiles.join(", "));
            QMessageBox::warning(m_mainWindow, "Warning", message);
        }
        
    } catch (const std::exception &e) {
        QString message = QString("Error selecting output files: %1").arg(e.what());
        QMessageBox::critical(m_mainWindow, "File Selection Error", message);
    }
}

void CommandLineHandler::loadInitialContent()
{
    if (!m_mainWindow) {
        return;
    }
    
    try {
        QTabWidget *tabWidget = m_mainWindow->getTabWidget();
        if (tabWidget) {
            int currentTab = tabWidget->currentIndex();
            
            if (currentTab == 0) {  // Time series tab
                // Load variables but don't auto-plot
                m_mainWindow->loadVariables();
                
                // Don't automatically select Y variable or update plot
                // User needs to manually select Y variable and click plot button
            }
            
            qDebug() << "Loaded initial content for tab" << currentTab;
        }
        
    } catch (const std::exception &e) {
        qCritical() << "Error loading initial content:" << e.what();
    }
}

QString CommandLineHandler::extractCropNameFromPath(const QString &cropDirPath)
{
    // Get crop details to find matching directory
    QVector<CropDetails> cropDetails = DataProcessor::getCropDetails();
    
    // Try to find which crop directory this path belongs to
    for (const CropDetails &crop : cropDetails) {
        if (!crop.directory.isEmpty()) {
            // Check if the provided path is within this crop directory
            QDir cropDir(crop.directory);
            QString canonicalCropDir = cropDir.canonicalPath();
            QDir providedDir(cropDirPath);
            QString canonicalProvidedDir = providedDir.canonicalPath();

            qDebug() << "CommandLineHandler: Comparing paths:";
            qDebug() << "  Crop directory from DSSATPRO:" << crop.directory;
            qDebug() << "  Canonical crop dir:" << canonicalCropDir;
            qDebug() << "  Provided path:" << cropDirPath;
            qDebug() << "  Canonical provided dir:" << canonicalProvidedDir;

            // Check if provided path starts with or equals the crop directory
            if (!canonicalProvidedDir.isEmpty() && !canonicalCropDir.isEmpty() &&
                canonicalProvidedDir.startsWith(canonicalCropDir, Qt::CaseInsensitive)) {
                qDebug() << "CommandLineHandler: Matched crop directory:" << crop.directory
                         << "for path:" << cropDirPath << "-> crop name:" << crop.cropName;
                return crop.cropName;
            }

            // Fallback: try non-canonical path comparison for cases where canonicalization fails
            if (cropDirPath.startsWith(crop.directory, Qt::CaseInsensitive) ||
                crop.directory.startsWith(cropDirPath, Qt::CaseInsensitive)) {
                qDebug() << "CommandLineHandler: Fallback matched crop directory:" << crop.directory
                         << "for path:" << cropDirPath << "-> crop name:" << crop.cropName;
                return crop.cropName;
            }
        }
    }
    
    // Fallback: use the last directory name if no match found
    QString fallbackName = QDir(cropDirPath).dirName();

    // Try to match the fallback name against known crop names
    for (const CropDetails &crop : cropDetails) {
        if (crop.cropName.compare(fallbackName, Qt::CaseInsensitive) == 0) {
            qDebug() << "CommandLineHandler: Fallback matched crop name:" << fallbackName;
            return crop.cropName;
        }
    }

    qWarning() << "CommandLineHandler: No matching crop directory found for:" << cropDirPath
               << "using fallback directory name:" << fallbackName;
    return fallbackName;
}