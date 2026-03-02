#include "CommandLineHandler.h"
#include "MainWindow.h"
#include "DataProcessor.h"
#include "PlotWidget.h"
#include <QApplication>
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

        // First pass: strip --xvar, --yvar, --save, --metrics flags; collect positional tokens
        QStringList positional;
        QStringList tokens = args.mid(1);
        for (int i = 0; i < tokens.size(); ++i) {
            const QString &tok = tokens[i];
            if (tok == "--xvar" && i + 1 < tokens.size()) {
                result.xVar = tokens[++i];
            } else if (tok == "--yvar" && i + 1 < tokens.size()) {
                result.yVars = tokens[++i].split(',', Qt::SkipEmptyParts);
            } else if (tok == "--save" && i + 1 < tokens.size()) {
                result.savePlotPath = tokens[++i];
                result.headlessMode = true;
            } else if (tok == "--metrics" && i + 1 < tokens.size()) {
                result.saveMetricsPath = tokens[++i];
            } else {
                positional.append(tok);
            }
        }

        // Second pass: parse positional args — support both comma-separated (DSSAT) and
        // space-separated (terminal) formats
        QString paramString = positional.join(" ");
        paramString = paramString.remove(QRegularExpression("[.\"']+$"));
        paramString = paramString.remove(QRegularExpression("^[\"']+"));

        QStringList params;
        if (paramString.contains(',')) {
            // Comma-separated format (DSSAT invocation)
            for (const QString &param : paramString.split(',')) {
                QString cleaned = param.trimmed();
                cleaned = cleaned.remove(QRegularExpression("^[\"']+"));
                cleaned = cleaned.remove(QRegularExpression("[\"']+$"));
                if (!cleaned.isEmpty()) {
                    params.append(cleaned);
                }
            }
        } else {
            // Space-separated format (terminal invocation)
            for (const QString &param : positional) {
                QString cleaned = param.trimmed();
                cleaned = cleaned.remove(QRegularExpression("^[\"']+"));
                cleaned = cleaned.remove(QRegularExpression("[\"']+$"));
                if (!cleaned.isEmpty()) {
                    params.append(cleaned);
                }
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
        qDebug() << "  xVar:" << result.xVar;
        qDebug() << "  yVars:" << result.yVars;
        qDebug() << "  savePlotPath:" << result.savePlotPath;
        qDebug() << "  saveMetricsPath:" << result.saveMetricsPath;
        qDebug() << "  headlessMode:" << result.headlessMode;

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
        // UI is already hidden in main.cpp before window is shown
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
        // This automatically populates the file list via onFolderSelectionChanged()
        if (!selectCropFolder(m_args.cropName)) {
            QString message = QString("Crop folder '%1' not found").arg(m_args.cropName);
            QMessageBox::warning(m_mainWindow, "Command Line Warning", message);
            return;
        }
        
        // Hide crop and file selection UI immediately (synchronously) since they're auto-selected
        // This prevents any visible UI change - hide before window is fully rendered
        m_mainWindow->hideFileSelectionUI(true);
        
        // Step 2: Select output files (with delay to ensure file list is populated)
        // Note: selectCropFolder() already populated files via onFolderSelectionChanged(),
        // so we just need to wait for UI to be ready before selecting files
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
        // UI is already hidden in applyCommandLineArgsToUI, so just return
        return;
    }
    
    try {
        int selectedCount = m_mainWindow->selectOutputFiles(m_args.outputFiles);
        
        if (selectedCount > 0) {
            QString message = QString("Loaded %1 with %2 output files")
                                .arg(m_args.cropName).arg(selectedCount);
            if (!m_args.headlessMode) {
                QMessageBox::information(m_mainWindow, "Success", message);
            }

            // Load the first tab content
            QTimer::singleShot(100, this, &CommandLineHandler::loadInitialContent);
        } else {
            QString message = QString("No valid output files found from: %1")
                                .arg(m_args.outputFiles.join(", "));
            if (m_args.headlessMode) {
                qCritical() << "CommandLineHandler (headless):" << message;
                QApplication::quit();
            } else {
                QMessageBox::warning(m_mainWindow, "Warning", message);
            }
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

                if (m_args.headlessMode) {
                    QTimer::singleShot(300, this, &CommandLineHandler::headlessAutoPlot);
                }
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

void CommandLineHandler::headlessAutoPlot()
{
    if (!m_mainWindow) {
        QApplication::quit();
        return;
    }

    PlotWidget *plot = m_mainWindow->getPlotWidget();
    if (!plot) {
        qCritical() << "CommandLineHandler (headless): no PlotWidget";
        QApplication::quit();
        return;
    }

    // Select X variable
    if (!m_args.xVar.isEmpty()) {
        if (!m_mainWindow->selectXVariable(m_args.xVar)) {
            qWarning() << "CommandLineHandler (headless): xvar not found:" << m_args.xVar;
        }
    }

    // Select Y variables
    if (!m_args.yVars.isEmpty()) {
        int n = m_mainWindow->selectYVariables(m_args.yVars);
        if (n == 0) {
            qWarning() << "CommandLineHandler (headless): no yvar matched:" << m_args.yVars;
        }
    }

    // Trigger plot update
    m_mainWindow->updateTimeSeriesPlot();

    // After 5000 ms: export plot and/or save metrics, then quit.
    // Large files (many runs/treatments) need more rendering time than small datasets.
    QTimer::singleShot(5000, this, [this]() {
        PlotWidget *plot = m_mainWindow->getPlotWidget();
        if (plot && !m_args.savePlotPath.isEmpty()) {
            plot->exportPlotComposite(m_args.savePlotPath, "PNG", 1200, 800, 96);
            qDebug() << "CommandLineHandler (headless): plot saved to" << m_args.savePlotPath;
        }
        if (!m_args.saveMetricsPath.isEmpty()) {
            if (!m_mainWindow->saveMetricsToFile(m_args.saveMetricsPath)) {
                qWarning() << "CommandLineHandler (headless): failed to save metrics to"
                           << m_args.saveMetricsPath;
            } else {
                qDebug() << "CommandLineHandler (headless): metrics saved to"
                         << m_args.saveMetricsPath;
            }
        }
        QApplication::quit();
    });
}