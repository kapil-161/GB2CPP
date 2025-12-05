#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QComboBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QProgressBar>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QStandardItemModel>
#include <QListWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <memory>

#include "StatusWidget.h"
#include "DataProcessor.h"
#include "Config.h"
#include "MetricsDialog.h"
#include "DataTableWidget.h"

class PlotWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    // Command line integration methods
    bool selectCropFolder(const QString &cropName);
    void loadExperiments();
    void loadOutputFiles();
    int selectOutputFiles(const QStringList &fileNames);
    void loadVariables();
    void updateTimeSeriesPlot();
    void hideFileSelectionUI(bool hide = true);  // Hide crop and file selection UI for command line mode
    
    // Public accessors for command line handler
    QComboBox* getFolderSelector() const { return m_fileComboBox; }
    QListWidget* getFileListWidget() const { return m_fileListWidget; }
    QComboBox* getXVariableSelector() const { return m_xVariableComboBox; }
    QListWidget* getYVariableSelector() const { return m_yVariableComboBox; }
    QTabWidget* getTabWidget() const { return m_tabWidget; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onOpenFile();
    void onSaveData();
    void onExportPlot();
    void onCopyPlot();
    void onAbout();
    void onDataFileChanged();
    void onXVariableChanged();
    void onYVariableChanged();
    void onTreatmentChanged();
    void onPlotTypeChanged();
    void onUpdatePlot();
    void onTabChanged(int index);
    void onPlotWidgetXVariableChanged(const QString &xVariable);
    void onDataProcessed(const QString &message);
    void onDataError(const QString &error);
    void onProgressUpdate(int percentage);
    void onFolderSelectionChanged();
    void onRefreshFiles();
    void onFileSelectionChanged();
    void onUnselectAllFiles();
    void onUnselectAllYVars();
    void onShowMetrics();
    void updateTimeSeriesMetrics(const QVector<QMap<QString, QVariant>> &metrics);
    void updateScatterMetrics(const QVector<QMap<QString, QVariant>> &metrics);

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupMainWidget();
    void setupControlPanel();
    void setupDataPanel();
    
    void connectSignals();
    void loadFile(const QString &filePath);
    void updateVariableComboBoxes();
    void updateTreatmentComboBox();
    void updatePlot();
    void updateScatterPlot();  // Update scatter plot for scatter plot tab
    void checkAndAutoSwitchToScatterPlot(bool autoPlot = true);  // Auto-switch to scatter plot tab for EVALUATE.OUT files
    void resetInterface();
    void centerWindow();
    void populateFolders();
    void populateFiles(const QString &folderName);
    
    // Additional methods from Python version
    void extractExperimentFromOutputFile();
    void selectExperimentByCode(const QString &expCode, const QStringList &treatmentNumbers = QStringList());
    void selectTreatmentsByNumbers(const QStringList &treatmentNumbers);
    
    void showError(const QString &title, const QString &message);
    void showSuccess(const QString &message);
    void showWarning(const QString &message);
    void markDataNeedsRefresh();
    void filterOutFiles(const QString &text);
    void filterYVars(const QString &text);
    void unselectAllOutFiles();
    void unselectAllYVars();
    void refreshOutputFiles();
    void updateMetricsButtonState();
    void clearMetrics();
    
    // UI Components
    QWidget *m_centralWidget;
    QSplitter *m_mainSplitter;
    QTabWidget *m_tabWidget;
    
    // Control Panel - matching Python UI structure
    QComboBox *m_fileComboBox;              // Crop folder selector
    QPushButton *m_openFileButton;
    QPushButton *m_saveDataButton;
    
    // X/Y Variable selectors (Python uses ComboBox for X, ListWidget for Y)
    QComboBox *m_xVariableComboBox;
    QListWidget *m_yVariableComboBox;       // ListWidget to match Python
    QLineEdit *m_yVarSearch;                // Search box for Y variables
    QComboBox *m_treatmentComboBox;         // Keep for compatibility
    
    // Plot controls (mostly hidden to match Python)
    QGroupBox *m_plotGroup;
    QComboBox *m_plotTypeComboBox;
    QCheckBox *m_showLegendCheckBox;
    QCheckBox *m_showGridCheckBox;
    QPushButton *m_updatePlotButton;        // Main refresh button
    QPushButton *m_metricsButton;           // Show metrics button
    QPushButton *m_exportPlotButton;
    QPushButton *m_unselectFilesButton;     // Unselect all outfiles button
    QPushButton *m_unselectYVarsButton;     // Unselect all Y variables button
    
    // UI groups for hiding/showing in command line mode
    QGroupBox *m_cropGroup;                 // Crop selection group
    QGroupBox *m_fileGroup;                 // File selection group
    
    // Data Panel - matching Python's tab structure
    DataTableWidget *m_dataTableWidget;
    QLabel *m_dataInfoLabel;
    
    // Plot Panel (now integrated into tabs like Python)
    PlotWidget *m_plotWidget;               // Main plotting widget for time series (like Python PyQtGraph)
    PlotWidget *m_scatterPlotWidget;       // Plotting widget for scatter plots
    
    // Status and Progress
    StatusWidget *m_statusWidget;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    
    // Data Management
    std::unique_ptr<DataProcessor> m_dataProcessor;
    DataTable m_currentData;
    DataTable m_currentObsData;
    QString m_currentFilePath;
    QStringList m_availableFiles;
    QString m_selectedFolder;
    
    // UI component references for file handling
    QListWidget *m_fileListWidget;
    QPushButton *m_refreshFilesButton;
    
    // Settings
    bool m_showLegend;
    bool m_showGrid;
    QString m_currentPlotType;
    
    // Additional state variables from Python version
    QStringList m_selectedTreatments;
    QString m_selectedExperiment;
    QVariantList m_timeSeriesMetrics;
    QVariantList m_scatterMetrics;
    QVariantList m_currentMetrics;
    QMap<int, bool> m_tabContentLoaded;
    bool m_dataNeedsRefresh;
    bool m_variableSelectionChanged;
    QMap<QString, QMap<QString, QString>> m_treatmentNames;
    bool m_selectingExperimentProgrammatically;
    bool m_selectingTreatmentsProgrammatically;
    bool m_warningShown;
};

#endif // MAINWINDOW_H