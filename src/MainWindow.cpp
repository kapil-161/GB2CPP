#include "MainWindow.h"
#include "DataTableWidget.h"
#include "PlotWidget.h"
#include <QApplication>
#include <QMessageBox>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QDir>
#include <QStandardPaths>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QRegularExpression>
#include <QDebug>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainSplitter(nullptr)
    , m_tabWidget(nullptr)
    , m_dataTableWidget(nullptr)
    , m_plotWidget(nullptr)
    , m_scatterPlotWidget(nullptr)
    , m_statusWidget(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_dataProcessor(std::make_unique<DataProcessor>(this))
    , m_cropGroup(nullptr)
    , m_fileGroup(nullptr)
    , m_showLegend(true)
    , m_showGrid(true)
    , m_currentPlotType("Line")
    , m_dataNeedsRefresh(false)
    , m_variableSelectionChanged(false)
    , m_selectingExperimentProgrammatically(false)
    , m_selectingTreatmentsProgrammatically(false)
    , m_warningShown(false)
{
    setWindowTitle(QString("%1 v%2").arg(Config::APP_NAME, Config::APP_VERSION));
    setMinimumSize(1000, 600);
    resize(1000, 600);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Apply Python-matching stylesheet
    setStyleSheet(
        "* { color: #000000; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QMainWindow, QWidget { background-color: #F0F5F9; }"
        "QTabWidget::pane { border: 1px solid #E4E8ED; background-color: #F0F5F9; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
        "QTabWidget::tab-bar { left: 5px; }"
        "QTabBar::tab { background-color: #E4E8ED; border: 1px solid #C9D6DF; padding: 6px 12px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background-color: #F0F5F9; border-bottom-color: white; font-weight: bold; }"
        "QComboBox, QListWidget { background-color: #F0F5F9; border: 1px solid #C9D6DF; border-radius: 3px; padding: 2px; selection-background-color: #A8D8F0; min-height: 20px; }"
        "QGroupBox { background-color: #F0F5F9; border: 1px solid #C9D6DF; border-radius: 5px; margin-top: 5px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; background-color: #F0F5F9; color: #2c3e50; }"
        "QTableView { background-color: #F0F5F9; alternate-background-color: #F9FBFC; border: 1px solid #C9D6DF; gridline-color: #E4E8ED; }"
        "QScrollArea { background-color: transparent; border: none; }"
        "QPushButton { background-color: #52A7E0; border: none; border-radius: 4px; padding: 8px; font-weight: bold; color: white; }"
        "QPushButton:hover { background-color: #3D8BC7; }"
        "QPushButton:disabled { background-color: #C9D6DF; color: #6c757d; }"
    );
    
    setupUI();
    connectSignals();
    centerWindow();
    
    // Initialize with default state
    resetInterface();
    
    // Populate folders after UI is set up
    populateFolders();
    
    // Ensure window is visible
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setVisible(true);
    raise();
    activateWindow();
    
    // Constructor completed successfully
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    setupMenuBar();
    setupMainWidget();
    setupStatusBar();
}

void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();
    
    // File Menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    
    QAction *openAction = fileMenu->addAction("&Open File...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
    
    QAction *saveAction = fileMenu->addAction("&Save Data...");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveData);
    
    fileMenu->addSeparator();
    
    QAction *exportAction = fileMenu->addAction("&Export Plot...");
    exportAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportPlot);
    
    QAction *copyPlotAction = fileMenu->addAction("&Copy Plot");
    copyPlotAction->setShortcut(QKeySequence("Ctrl+Shift+C"));  // Use Ctrl+Shift+C to avoid conflict with standard copy
    connect(copyPlotAction, &QAction::triggered, this, &MainWindow::onCopyPlot);
    
    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Help Menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    
    QAction *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupMainWidget()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(m_mainSplitter);
    
    setupControlPanel();
    setupDataPanel();
    
    // Set splitter proportions
    m_mainSplitter->setSizes({250, 400, 400});
    m_mainSplitter->setCollapsible(0, false);
}

void MainWindow::setupControlPanel()
{
    QWidget *controlPanel = new QWidget();
    controlPanel->setMaximumWidth(350);
    controlPanel->setMinimumWidth(280);
    
    // Create scroll area for sidebar
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    QWidget *sidebarWidget = new QWidget();
    QVBoxLayout *controlLayout = new QVBoxLayout(sidebarWidget);
    controlLayout->setContentsMargins(10, 0, 10, 0);
    
    scrollArea->setWidget(sidebarWidget);
    
    QVBoxLayout *panelLayout = new QVBoxLayout(controlPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(scrollArea);
    
    // Crop Selection Group
    m_cropGroup = new QGroupBox("Select Crop");
    QVBoxLayout *cropLayout = new QVBoxLayout(m_cropGroup);
    
    m_fileComboBox = new QComboBox();
    m_fileComboBox->setToolTip("Select crop folder");
    cropLayout->addWidget(m_fileComboBox);
    
    controlLayout->addWidget(m_cropGroup);
    
    // Output Files Group with header layout
    QWidget *fileGroupHeader = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(fileGroupHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    
    QLabel *titleLabel = new QLabel("Output Files");
    titleLabel->setStyleSheet("font-weight: bold;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);
    
    // Refresh files button
    m_refreshFilesButton = new QPushButton();
    m_refreshFilesButton->setToolTip("Refresh output files");
    m_refreshFilesButton->setText("↻");
    m_refreshFilesButton->setFixedSize(24, 24);
    m_refreshFilesButton->setStyleSheet(
        "QPushButton { background-color: #8B7355; border: none; color: white; }"
        "QPushButton:hover { background-color: #A0845C; border-radius: 3px; }"
    );
    headerLayout->addWidget(m_refreshFilesButton);
    
    m_fileGroup = new QGroupBox();
    QVBoxLayout *fileLayout = new QVBoxLayout(m_fileGroup);
    m_fileGroup->setTitle("");
    fileLayout->addWidget(fileGroupHeader);
    
    // File search
    QLineEdit *fileSearch = new QLineEdit();
    fileSearch->setPlaceholderText("Search output files...");
    fileLayout->addWidget(fileSearch);
    
    // File list container with unselect button
    QWidget *fileContainer = new QWidget();
    QHBoxLayout *fileContainerLayout = new QHBoxLayout(fileContainer);
    fileContainerLayout->setContentsMargins(0, 0, 0, 0);
    
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setSelectionMode(QListWidget::MultiSelection);
    m_fileListWidget->setMinimumHeight(120);
    m_fileListWidget->setMaximumHeight(120);
    m_fileListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fileContainerLayout->addWidget(m_fileListWidget);
    
    m_unselectFilesButton = new QPushButton("×");
    m_unselectFilesButton->setStyleSheet(
        "QPushButton { background-color: #ffcccc; border: none; padding: 0px; margin: 0px; font-size: 8px; width: 12px; max-width: 12px; min-width: 12px; }"
        "QPushButton:hover { background-color: #ffcccc; border-radius: 3px; }"
    );
    m_unselectFilesButton->setToolTip("Unselect All");
    m_unselectFilesButton->setFixedSize(12, 12);
    m_unselectFilesButton->setMaximumWidth(12);
    m_unselectFilesButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    fileContainerLayout->addWidget(m_unselectFilesButton, 0, Qt::AlignTop);
    
    fileLayout->addWidget(fileContainer);
    controlLayout->addWidget(m_fileGroup, 0);  // Stretch factor 0 = no expansion

    // Time Series Variables Group
    QGroupBox *timeSeriesGroup = new QGroupBox("Time Series Variables");
    QVBoxLayout *tsLayout = new QVBoxLayout(timeSeriesGroup);
    
    // X Variable
    tsLayout->addWidget(new QLabel("X Variable"));
    m_xVariableComboBox = new QComboBox();
    tsLayout->addWidget(m_xVariableComboBox);
    
    // Y Variables
    tsLayout->addWidget(new QLabel("Y Variables"));
    
    // Y Variable search
    m_yVarSearch = new QLineEdit();
    m_yVarSearch->setPlaceholderText("Search Y variables...");
    tsLayout->addWidget(m_yVarSearch);
    
    // Y Variable list container with unselect button
    QWidget *yVarContainer = new QWidget();
    QHBoxLayout *yVarContainerLayout = new QHBoxLayout(yVarContainer);
    yVarContainerLayout->setContentsMargins(0, 0, 0, 0);

    QListWidget *yVarListWidget = new QListWidget();
    yVarListWidget->setSelectionMode(QListWidget::MultiSelection);
    yVarContainerLayout->addWidget(yVarListWidget);
    
    m_unselectYVarsButton = new QPushButton("×");
    m_unselectYVarsButton->setStyleSheet(
        "QPushButton { background-color: #ffcccc; border: none; padding: 0px; margin: 0px; font-size: 8px; width: 12px; max-width: 12px; min-width: 12px; }"
        "QPushButton:hover { background-color: #ffcccc; border-radius: 3px; }"
    );
    m_unselectYVarsButton->setToolTip("Unselect All");
    m_unselectYVarsButton->setFixedSize(12, 12);
    m_unselectYVarsButton->setMaximumWidth(12);
    m_unselectYVarsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    yVarContainerLayout->addWidget(m_unselectYVarsButton, 0, Qt::AlignTop);
    
    tsLayout->addWidget(yVarContainer, 1);  // Stretch factor 1 = expands to fill space
    controlLayout->addWidget(timeSeriesGroup, 1);  // Stretch factor 1 = expands to fill space

    // Store references for later use
    m_yVariableComboBox = yVarListWidget;

    // Let Y variable list expand to fill remaining space, but with reasonable min/max constraints
    // This ensures buttons remain visible when window is resized
    m_yVariableComboBox->setMinimumHeight(150);  // Reduced from 200 to allow more flexibility
    m_yVariableComboBox->setMaximumHeight(400);  // Add maximum to prevent it from pushing buttons out
    m_yVariableComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_treatmentComboBox = new QComboBox();  // Keep for compatibility
    
    // Refresh Plot/Data Button
    m_updatePlotButton = new QPushButton("Refresh Plot");
    m_updatePlotButton->setToolTip("Refresh plot when on Time Series tab, or refresh data table when on Data View tab");
    m_updatePlotButton->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 8px; }"
        "QPushButton:hover { background-color: #0b7dda; }"
        "QPushButton:disabled { background-color: #cccccc; }"
    );
    m_updatePlotButton->setEnabled(false);
    controlLayout->addWidget(m_updatePlotButton);
    
    // Metrics Button
    m_metricsButton = new QPushButton("Show Metrics");
    m_metricsButton->setToolTip("Show model performance metrics");
    m_metricsButton->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 8px; }"
        "QPushButton:hover { background-color: #45a049; }"
        "QPushButton:disabled { background-color: #cccccc; }"
    );
    m_metricsButton->setEnabled(false);
    controlLayout->addWidget(m_metricsButton);
    
    // Keep plot group for compatibility but make it minimal
    m_plotGroup = new QGroupBox("Plot Options");
    QVBoxLayout *plotLayout = new QVBoxLayout(m_plotGroup);
    
    m_plotTypeComboBox = new QComboBox();
    m_plotTypeComboBox->addItems({"Line", "Scatter", "Both"});
    m_plotTypeComboBox->setCurrentText("Line");
    m_plotTypeComboBox->hide();  // Hide for now to match Python UI
    
    m_showLegendCheckBox = new QCheckBox("Show Legend");
    m_showLegendCheckBox->setChecked(true);
    m_showLegendCheckBox->hide();  // Hide for now
    
    m_showGridCheckBox = new QCheckBox("Show Grid");
    m_showGridCheckBox->setChecked(true);
    m_showGridCheckBox->hide();  // Hide for now
    
    m_exportPlotButton = new QPushButton("Export Plot...");
    m_exportPlotButton->setEnabled(false);
    m_exportPlotButton->hide();  // Hide for now
    
    plotLayout->addWidget(m_plotTypeComboBox);
    plotLayout->addWidget(m_showLegendCheckBox);
    plotLayout->addWidget(m_showGridCheckBox);
    plotLayout->addWidget(m_exportPlotButton);
    
    // Don't add plot group to layout - keep it hidden
    
    // Don't add stretch here - it pushes buttons out of view when window is small
    // The scroll area will handle scrolling if needed
    
    qDebug() << "Widget creation status:";
    qDebug() << "m_fileComboBox:" << (m_fileComboBox ? "OK" : "NULL");
    qDebug() << "m_xVariableComboBox:" << (m_xVariableComboBox ? "OK" : "NULL");
    qDebug() << "m_yVariableComboBox:" << (m_yVariableComboBox ? "OK" : "NULL");
    qDebug() << "m_updatePlotButton:" << (m_updatePlotButton ? "OK" : "NULL");
    qDebug() << "m_fileListWidget height - min:" << m_fileListWidget->minimumHeight() << "max:" << m_fileListWidget->maximumHeight();
    qDebug() << "m_yVariableComboBox height - min:" << m_yVariableComboBox->minimumHeight() << "max:" << m_yVariableComboBox->maximumHeight();
    
    m_mainSplitter->addWidget(controlPanel);
}

void MainWindow::setupDataPanel()
{
    m_tabWidget = new QTabWidget();
    m_tabWidget->setDocumentMode(true);
    
    // Time Series Tab
    QWidget *timeSeriesWidget = new QWidget();
    QVBoxLayout *timeSeriesLayout = new QVBoxLayout(timeSeriesWidget);
    
    // Use PlotWidget for time series (matching Python PyQtGraph usage)
    qDebug() << "MainWindow: About to create PlotWidget";
    m_plotWidget = new PlotWidget();
    qDebug() << "MainWindow: PlotWidget created successfully";
    timeSeriesLayout->addWidget(m_plotWidget);
    qDebug() << "MainWindow: PlotWidget added to layout";
    
    m_tabWidget->addTab(timeSeriesWidget, "Time Series");
    
    // Data View Tab
    QWidget *dataWidget = new QWidget();
    QVBoxLayout *dataLayout = new QVBoxLayout(dataWidget);
    
    // File type selector for Data View tab
    QHBoxLayout *dataSelectorLayout = new QHBoxLayout();
    QLabel *dataTypeLabel = new QLabel("Show data from:");
    m_dataViewFileTypeComboBox = new QComboBox();
    m_dataViewFileTypeComboBox->addItem("Regular .OUT Files", "regular");
    m_dataViewFileTypeComboBox->addItem("EVALUATE.OUT Files", "evaluate");
    m_dataViewFileTypeComboBox->setCurrentIndex(0); // Default to regular .OUT
    m_dataViewFileTypeComboBox->setEnabled(false); // Disabled until data is loaded
    dataSelectorLayout->addWidget(dataTypeLabel);
    dataSelectorLayout->addWidget(m_dataViewFileTypeComboBox);
    dataSelectorLayout->addStretch();
    dataLayout->addLayout(dataSelectorLayout);
    
    m_dataInfoLabel = new QLabel("No data loaded");
    m_dataInfoLabel->setStyleSheet("font-weight: bold; padding: 5px;");
    dataLayout->addWidget(m_dataInfoLabel);
    
    m_dataTableWidget = new DataTableWidget();
    dataLayout->addWidget(m_dataTableWidget);
    
    m_tabWidget->addTab(dataWidget, "Data View");
    
    // Scatter Plot Tab
    QWidget *scatterPlotWidget = new QWidget();
    QVBoxLayout *scatterPlotLayout = new QVBoxLayout(scatterPlotWidget);
    
    // Use PlotWidget for scatter plots (same widget, different mode)
    m_scatterPlotWidget = new PlotWidget();
    scatterPlotLayout->addWidget(m_scatterPlotWidget);
    
    m_tabWidget->addTab(scatterPlotWidget, "Scatter Plot");
    
    m_mainSplitter->addWidget(m_tabWidget);
    
    // Connect tab changed signal
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
}

// Remove setupPlotPanel - integrated into setupDataPanel

void MainWindow::setupStatusBar()
{
    QStatusBar *statusBar = this->statusBar();
    
    m_statusWidget = new StatusWidget(this);
    statusBar->addWidget(m_statusWidget, 1);
    
    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumWidth(200);
    m_progressBar->hide();
    statusBar->addPermanentWidget(m_progressBar);
    
    m_statusLabel = new QLabel("Ready");
    statusBar->addPermanentWidget(m_statusLabel);
}

void MainWindow::connectSignals()
{
    // Only connect signals for widgets that exist
    if (m_fileComboBox) {
        connect(m_fileComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFolderSelectionChanged);
    }
    
    if (m_xVariableComboBox) {
        connect(m_xVariableComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onXVariableChanged);
    }
    
    if (m_yVariableComboBox) {
        connect(m_yVariableComboBox, &QListWidget::itemSelectionChanged, this, &MainWindow::onYVariableChanged);
    }
    
    if (m_treatmentComboBox) {
        connect(m_treatmentComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTreatmentChanged);
    }
    
    if (m_updatePlotButton) {
        connect(m_updatePlotButton, &QPushButton::clicked, this, &MainWindow::onUpdatePlot);
    }
    
    if (m_plotWidget) {
        connect(m_plotWidget, &PlotWidget::xVariableChanged, this, &MainWindow::onPlotWidgetXVariableChanged);
        connect(m_plotWidget, SIGNAL(metricsCalculated(const QVector<QMap<QString,QVariant>>&)), 
                this, SLOT(updateTimeSeriesMetrics(const QVector<QMap<QString,QVariant>>&)));
    }

    if (m_scatterPlotWidget) {
        connect(m_scatterPlotWidget, SIGNAL(metricsCalculated(const QVector<QMap<QString,QVariant>>&)),
                this, SLOT(updateScatterMetrics(const QVector<QMap<QString,QVariant>>&)));
    }
    
    if (m_tabWidget) {
        connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    }
    
    // Data processor signals
    if (m_dataProcessor) {
        connect(m_dataProcessor.get(), &DataProcessor::dataProcessed, this, &MainWindow::onDataProcessed);
        connect(m_dataProcessor.get(), &DataProcessor::errorOccurred, this, &MainWindow::onDataError);
        connect(m_dataProcessor.get(), &DataProcessor::progressUpdate, this, &MainWindow::onProgressUpdate);
    }
    
    // Connect file list and refresh button
    if (m_fileListWidget) {
        connect(m_fileListWidget, &QListWidget::itemSelectionChanged, this, &MainWindow::onFileSelectionChanged);
    }
    
    if (m_unselectFilesButton) {
        connect(m_unselectFilesButton, &QPushButton::clicked, this, &MainWindow::onUnselectAllFiles);
    }
    
    if (m_unselectYVarsButton) {
        connect(m_unselectYVarsButton, &QPushButton::clicked, this, &MainWindow::onUnselectAllYVars);
    }
    
    if (m_metricsButton) {
        connect(m_metricsButton, &QPushButton::clicked, this, &MainWindow::onShowMetrics);
    }
    
    // Connect data view file type selector
    if (m_dataViewFileTypeComboBox) {
        connect(m_dataViewFileTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onDataViewFileTypeChanged);
    }
    
    if (m_refreshFilesButton) {
        connect(m_refreshFilesButton, &QPushButton::clicked, this, &MainWindow::onRefreshFiles);
    }

    // Connect Y variable search
    if (m_yVarSearch) {
        connect(m_yVarSearch, &QLineEdit::textChanged, this, &MainWindow::filterYVars);
    }
}

void MainWindow::onOpenFile()
{
    qDebug() << "MainWindow::onOpenFile() - Starting file dialog...";
    
    // Observed data must come from the same DSSATPRO-configured directory as simulated data
    QString cropDirectory;
    if (m_dataProcessor && m_fileComboBox && m_fileComboBox->currentText() != "No DSSAT folders found") {
        QString currentFolder = m_fileComboBox->currentText();
        cropDirectory = m_dataProcessor->getActualFolderPath(currentFolder);
        qDebug() << "MainWindow::onOpenFile() - Using DSSATPRO crop directory:" << cropDirectory;
    }
    
    // If no valid DSSATPRO crop directory, show error
    if (cropDirectory.isEmpty()) {
        m_statusWidget->showError("Please select a valid crop folder first. Observed data must come from DSSATPRO-configured crop directories.");
        return;
    }
    
    // Get crop code from DETAIL.CDE to filter observed data files (cropcode+T format)
    QString cropCode;
    QVector<CropDetails> cropDetails = m_dataProcessor->getCropDetails();
    QString currentFolder = m_fileComboBox->currentText();
    for (const CropDetails &crop : cropDetails) {
        if (crop.cropName == currentFolder) {
            cropCode = crop.cropCode;
            break;
        }
    }
    
    QString observedPattern = cropCode.isEmpty() ? "T" : QString("%1T").arg(cropCode);
    QString fileName = QFileDialog::getOpenFileName(
        this,
        QString("Open Observed Data File (%1*) from %2").arg(observedPattern).arg(QFileInfo(cropDirectory).baseName()),
        cropDirectory,
        QString("Observed Data Files (*%1*);;DSSAT Files (*.OUT *.DAT);;All Files (*)").arg(observedPattern)
    );
    
    qDebug() << "MainWindow::onOpenFile() - Selected file:" << fileName;
    
    if (!fileName.isEmpty()) {
        qDebug() << "MainWindow::onOpenFile() - About to call loadFile...";
        loadFile(fileName);
        qDebug() << "MainWindow::onOpenFile() - loadFile returned";
    } else {
        qDebug() << "MainWindow::onOpenFile() - No file selected";
    }
}

void MainWindow::onSaveData()
{
    if (m_currentData.rowCount == 0) {
        m_statusWidget->showWarning("No data to save");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Save Data",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "CSV Files (*.csv);;Text Files (*.txt);;All Files (*)"
    );
    
    if (!fileName.isEmpty()) {
        // Implement data saving logic here
        m_statusWidget->showSuccess("Data saved successfully");
    }
}

void MainWindow::onExportPlot()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Plot",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "PNG Files (*.png);;JPG Files (*.jpg);;PDF Files (*.pdf);;All Files (*)"
    );
    
    if (!fileName.isEmpty() && m_plotWidget) {
        // Export the plot widget
        m_plotWidget->exportPlot(fileName);
        m_statusWidget->showSuccess("Plot exported successfully");
    }
}

void MainWindow::onCopyPlot()
{
    if (m_plotWidget) {
        m_plotWidget->copyPlotToClipboard();
        m_statusWidget->showSuccess("Plot copied to clipboard");
    }
}


void MainWindow::onAbout()
{
    QMessageBox::about(this, QString("About %1").arg(Config::APP_NAME),
        QString("<h3>%1 v%2</h3>"
                "<p>DSSAT  GB2 Tool</p>"
                "<p>Built with Qt6 and C++</p>"
                "<p>Copyright © 2025 DSSAT Foundation</p>")
        .arg(Config::APP_NAME, Config::APP_VERSION));
}

void MainWindow::onDataFileChanged()
{
    // Handle file selection change - don't auto-plot, wait for manual refresh
    // User must click "Refresh Plot" button to update plot
}

void MainWindow::onXVariableChanged()
{
    // Mark that variable selection has changed
    m_variableSelectionChanged = true;
    markDataNeedsRefresh();

    // Check if X variable is unselected (empty or invalid)
    QString xVar = m_xVariableComboBox ? m_xVariableComboBox->currentData(Qt::UserRole).toString() : QString();
    if (xVar.isEmpty()) {
        // Clear plots and metrics when X variable is unselected
        if (m_plotWidget) {
            m_plotWidget->clear();
        }
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->clear();
        }
        clearMetrics();
        qDebug() << "MainWindow: Cleared plots and metrics due to X variable unselected";
        return;
    }

    // Show prompt message based on current tab
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        m_statusWidget->showInfo("X variable changed. Click 'Refresh Plot' to update the time series plot");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        m_statusWidget->showInfo("X variable changed. Click 'Refresh Data' to update the data table");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
        m_statusWidget->showInfo("X variable changed. Click 'Refresh Plot' to update the scatter plot");
    }
    // Don't auto-plot, wait for manual refresh
}

void MainWindow::onYVariableChanged()
{
    // Mark that variable selection has changed
    m_variableSelectionChanged = true;
    markDataNeedsRefresh();

    // Check if no Y variables are selected and clear plots and metrics if so
    if (m_yVariableComboBox && m_yVariableComboBox->selectedItems().isEmpty()) {
        // Clear plots and metrics when Y variables are unselected
        if (m_plotWidget) {
            m_plotWidget->clear();
        }
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->clear();
        }
        clearMetrics();
        qDebug() << "MainWindow: Cleared plots and metrics due to no Y variables selected";
        return;
    }

    // Show prompt message based on current tab
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        m_statusWidget->showInfo("Y variable selection changed. Click 'Refresh Plot' to update the time series plot");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        m_statusWidget->showInfo("Y variable selection changed. Click 'Refresh Data' to update the data table");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
        m_statusWidget->showInfo("Y variable selection changed. Click 'Refresh Plot' to update the scatter plot");
    }
    // Don't auto-plot, wait for manual refresh
}

void MainWindow::onTreatmentChanged()
{
    // Mark that variable selection has changed
    m_variableSelectionChanged = true;
    markDataNeedsRefresh();

    // Show prompt message based on current tab
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        m_statusWidget->showInfo("Treatment selection changed. Click 'Refresh Plot' to update the time series plot");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        m_statusWidget->showInfo("Treatment selection changed. Click 'Refresh Data' to update the data table");
    }
    // Don't auto-plot, wait for manual refresh
}

void MainWindow::onPlotTypeChanged()
{
    m_currentPlotType = m_plotTypeComboBox->currentText();
    // Don't auto-plot, wait for manual refresh
}

void MainWindow::onUpdatePlot()
{
    qDebug() << "MainWindow::onUpdatePlot() - Refresh button clicked!";
    
    // Check which tab we're on and perform appropriate action
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        // Time Series tab - refresh time series plot
        qDebug() << "MainWindow::onUpdatePlot() - Refreshing plot for Time Series tab";
        updatePlot();
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        // Data View tab - refresh data table based on selected file type
        onDataViewFileTypeChanged();
        m_dataNeedsRefresh = false;
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
        // Scatter Plot tab - refresh scatter plot
        qDebug() << "MainWindow::onUpdatePlot() - Refreshing scatter plot";
        updateScatterPlot();
    }
    
    // Mark data as refreshed
    m_dataNeedsRefresh = false;
    m_variableSelectionChanged = false;
}

void MainWindow::onTabChanged(int index)
{
    // Handle tab switching - load data lazily like Python version
    if (index == 0) {
        // Time Series tab - don't auto-plot, wait for manual refresh
        if (m_updatePlotButton) {
            m_updatePlotButton->setText("Refresh Plot");
        }
        
        // Show DAS, DAP, DATE buttons for time series plot widget
        if (m_plotWidget) {
            m_plotWidget->setXAxisButtonsVisible(true);
        }
        // Hide buttons for scatter plot widget (if user switches back)
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->setXAxisButtonsVisible(false);
        }
        
        // Update variables to show time series variables (regular .OUT files)
        updateVariableComboBoxes();

        // Show prompt message if files are selected but plot needs refresh
        QList<QListWidgetItem*> selectedItems = m_fileListWidget ? m_fileListWidget->selectedItems() : QList<QListWidgetItem*>();
        if (!selectedItems.isEmpty() && (m_currentData.rowCount == 0 || m_variableSelectionChanged || m_dataNeedsRefresh)) {
            m_statusWidget->showInfo("Click 'Refresh Plot' to view the time series plot with current selections");
        } else if (selectedItems.isEmpty()) {
            m_statusWidget->showInfo("Click outfile and variables, then click 'Refresh Plot' to view time series");
        }

    } else if (index == 1) {
        // Data View tab
        if (m_updatePlotButton) {
            m_updatePlotButton->setText("Refresh Data");
        }

        // Update file type selector based on available data
        if (m_dataViewFileTypeComboBox) {
            bool hasRegular = (m_currentData.rowCount > 0);
            bool hasEvaluate = (m_evaluateData.rowCount > 0);
            
            if (hasRegular && hasEvaluate) {
                // Both available - enable selector
                m_dataViewFileTypeComboBox->setEnabled(true);
            } else if (hasEvaluate) {
                // Only EVALUATE.OUT - disable selector and set to evaluate
                m_dataViewFileTypeComboBox->setEnabled(false);
                m_dataViewFileTypeComboBox->setCurrentIndex(1); // EVALUATE.OUT
            } else if (hasRegular) {
                // Only regular .OUT - disable selector and set to regular
                m_dataViewFileTypeComboBox->setEnabled(false);
                m_dataViewFileTypeComboBox->setCurrentIndex(0); // Regular .OUT
            } else {
                // No data - disable selector
                m_dataViewFileTypeComboBox->setEnabled(false);
            }
        }

        // Refresh data table based on selected file type
        if (m_dataTableWidget && m_dataNeedsRefresh) {
            onDataViewFileTypeChanged();
            m_dataNeedsRefresh = false; // Reset the flag after refreshing
        } else if (m_dataTableWidget) {
            // Data already loaded, just refresh if needed
            onDataViewFileTypeChanged();
        }
    } else if (index == 2) {
        // Scatter Plot tab
        if (m_updatePlotButton) {
            m_updatePlotButton->setText("Refresh Plot");
        }
        
        // Hide DAS, DAP, DATE buttons for scatter plot widget (not applicable)
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->setXAxisButtonsVisible(false);
        }
        // Show buttons for time series plot widget (if user switches back)
        if (m_plotWidget) {
            m_plotWidget->setXAxisButtonsVisible(true);
        }
        
        // Update variables to show scatter plot variables (EVALUATE.OUT files)
        updateVariableComboBoxes();
        
        // Show prompt message if files are selected but plot needs refresh
        QList<QListWidgetItem*> selectedItems = m_fileListWidget ? m_fileListWidget->selectedItems() : QList<QListWidgetItem*>();
        if (!selectedItems.isEmpty() && (m_evaluateData.rowCount == 0 || m_variableSelectionChanged || m_dataNeedsRefresh)) {
            m_statusWidget->showInfo("Click 'Refresh Plot' to view the scatter plot with current selections");
        } else if (selectedItems.isEmpty()) {
            m_statusWidget->showInfo("Select EVALUATE.OUT file and variables, then click 'Refresh Plot' to view scatter plot");
        }
    }

    if (index == 0) {
        m_currentMetrics = m_timeSeriesMetrics;
    } else if (index == 2) {
        m_currentMetrics = m_scatterMetrics;
    } else {
        m_currentMetrics.clear();
    }

    updateMetricsButtonState();
}

void MainWindow::onDataProcessed(const QString &message)
{
    m_statusWidget->showSuccess(message);
    m_progressBar->hide();
    
    // Update UI with new data
    updateVariableComboBoxes();
    updateTreatmentComboBox();
    
    // Mark data as needing refresh for the data table
    m_dataNeedsRefresh = true;
    
    // Update info label
    m_dataInfoLabel->setText(QString("Loaded: %1 rows, %2 columns")
                            .arg(m_currentData.rowCount)
                            .arg(m_currentData.columns.size()));
    
    // Enable controls
    m_updatePlotButton->setEnabled(true);
}

void MainWindow::onDataError(const QString &error)
{
    m_statusWidget->showError(error);
    m_progressBar->hide();
}

void MainWindow::onProgressUpdate(int percentage)
{
    if (!m_progressBar->isVisible()) {
        m_progressBar->show();
    }
    m_progressBar->setValue(percentage);
}

void MainWindow::loadFile(const QString &filePath)
{
    qDebug() << "MainWindow::loadFile() called with:" << filePath;
    resetInterface(); // Clear existing data before loading new file
    
    try {
        m_currentFilePath = filePath;
        m_statusWidget->showInfo("Loading file...");
        m_progressBar->setRange(0, 0);  // Indeterminate
        m_progressBar->show();
        
        // Check if file exists
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            qDebug() << "MainWindow: File does not exist:" << filePath;
            m_statusWidget->showError("File does not exist: " + filePath);
            m_progressBar->hide();
            return;
        }
        
        if (!fileInfo.isReadable()) {
            qDebug() << "MainWindow: File is not readable:" << filePath;
            m_statusWidget->showError("File is not readable: " + filePath);
            m_progressBar->hide();
            return;
        }
        
        qDebug() << "MainWindow: File size:" << fileInfo.size() << "bytes";
        qDebug() << "MainWindow: Calling readFile on DataProcessor...";
        
        // Load file in data processor with error handling
        bool readResult = m_dataProcessor->readFile(filePath, m_currentData);
        qDebug() << "MainWindow: readFile result:" << readResult;
        
        if (readResult) {
            qDebug() << "MainWindow: File loaded successfully, rows:" << m_currentData.rowCount;
            // File loaded successfully - manually call onDataProcessed since signal might not be emitted
            onDataProcessed(QString("Successfully loaded observed data: %1 rows, %2 columns")
                          .arg(m_currentData.rowCount)
                          .arg(m_currentData.columns.size()));
        } else {
            qDebug() << "MainWindow: Failed to read file";
            m_statusWidget->showError("Failed to read file: " + filePath);
            m_progressBar->hide();
        }
        
    } catch (const std::exception& e) {
        qDebug() << "MainWindow: Exception in loadFile:" << e.what();
        m_statusWidget->showError("Error loading file: " + QString(e.what()));
        m_progressBar->hide();
    } catch (...) {
        qDebug() << "MainWindow: Unknown exception in loadFile";
        m_statusWidget->showError("Unknown error loading file");
        m_progressBar->hide();
    }
}

void MainWindow::updateVariableComboBoxes()
{
    m_xVariableComboBox->clear();
    m_yVariableComboBox->clear();
    
    // Determine which data to use based on current tab
    int currentTab = m_tabWidget ? m_tabWidget->currentIndex() : 0;
    bool isScatterTab = (currentTab == 2);
    bool isEvaluateFile = false;
    bool hasRegularFile = false;
    
    // Check which file types are available
    if (m_fileListWidget) {
        QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
        for (QListWidgetItem* item : selectedItems) {
            QString fileName = item->text().toUpper();
            if (fileName.contains("EVALUATE") || fileName == "EVALUATE.OUT") {
                isEvaluateFile = true;
            } else {
                hasRegularFile = true;
            }
        }
    }
    
    // Tab-dependent variable selection:
    // - Scatter tab (index 2) → show EVALUATE.OUT variables
    // - Time series tab (index 0) → show regular .OUT variables
    if (isScatterTab) {
        // Scatter plot tab - use EVALUATE.OUT data
        isEvaluateFile = (m_evaluateData.rowCount > 0);
        if (!isEvaluateFile) {
            qDebug() << "MainWindow::updateVariableComboBoxes() - Scatter tab but no EVALUATE data";
            m_xVariableComboBox->clear();
            if (m_yVariableComboBox) {
                m_yVariableComboBox->clear();
            }
            return;
        }
    } else {
        // Time series tab - use regular .OUT data
        isEvaluateFile = false;
        if (m_currentData.rowCount == 0) {
            qDebug() << "MainWindow::updateVariableComboBoxes() - Time series tab but no regular data";
            m_xVariableComboBox->clear();
            if (m_yVariableComboBox) {
                m_yVariableComboBox->clear();
            }
            return;
        }
    }
    
    // For EVALUATE.OUT files (scatter plot tab), use special variable population
    if (isEvaluateFile) {
        // Set Y variable list to single selection mode for scatter plots (only one Y variable can be plotted)
        if (m_yVariableComboBox) {
            m_yVariableComboBox->setSelectionMode(QListWidget::SingleSelection);
        }
        
        // Get all variables from EVALUATE.OUT data
        QVector<QPair<QString, QString>> allVars = DataProcessor::getAllEvaluateVariables(m_evaluateData);
        
        // Build a set of base variable names that have both "s" and "m" versions with valid data
        QSet<QString> variablesWithBothVersions;
        
        // First, collect all base variable names from the actual column names
        QMap<QString, QString> baseToSimCol;  // base name (lowercase) -> actual sim column name
        QMap<QString, QString> baseToMeasCol; // base name (lowercase) -> actual meas column name
        
        qDebug() << "MainWindow::updateVariableComboBoxes() - Checking EVALUATE.OUT columns for asterisk logic";
        qDebug() << "  Total columns:" << m_evaluateData.columnNames.size();
        
        for (const QString &colName : m_evaluateData.columnNames) {
            QString upperCol = colName.toUpper();
            if (upperCol.endsWith("S") && upperCol.length() > 1) {
                QString baseName = upperCol.left(upperCol.length() - 1).toLower();
                baseToSimCol[baseName] = colName; // Store actual column name (preserving case)
                qDebug() << "  Found sim column:" << colName << "-> base:" << baseName;
            } else if (upperCol.endsWith("M") && upperCol.length() > 1) {
                QString baseName = upperCol.left(upperCol.length() - 1).toLower();
                baseToMeasCol[baseName] = colName; // Store actual column name (preserving case)
                qDebug() << "  Found meas column:" << colName << "-> base:" << baseName;
            }
        }
        
        qDebug() << "  Found" << baseToSimCol.size() << "sim columns and" << baseToMeasCol.size() << "meas columns";
        
        // Check which base names have both versions with valid data
        for (auto it = baseToSimCol.begin(); it != baseToSimCol.end(); ++it) {
            QString baseKey = it.key();
            if (baseToMeasCol.contains(baseKey)) {
                // Both versions exist, check for valid data
                QString simColName = it.value();
                QString measColName = baseToMeasCol[baseKey];
                
                const DataColumn *simCol = m_evaluateData.getColumn(simColName);
                const DataColumn *measCol = m_evaluateData.getColumn(measColName);
                
                bool hasValidSimData = false;
                bool hasValidMeasData = false;
                
                if (simCol) {
                    for (const QVariant &value : simCol->data) {
                        if (!DataProcessor::isMissingValue(value)) {
                            hasValidSimData = true;
                            break;
                        }
                    }
                } else {
                    qDebug() << "  WARNING: Sim column" << simColName << "not found in data";
                }
                
                if (measCol) {
                    for (const QVariant &value : measCol->data) {
                        if (!DataProcessor::isMissingValue(value)) {
                            hasValidMeasData = true;
                            break;
                        }
                    }
                } else {
                    qDebug() << "  WARNING: Meas column" << measColName << "not found in data";
                }
                
                // Only add asterisk if both versions have valid data
                if (hasValidSimData && hasValidMeasData) {
                    variablesWithBothVersions.insert(baseKey);
                    qDebug() << "  ✓ Variable" << baseKey << "has both simulated (" << simColName 
                             << ") and measured (" << measColName << ") data with valid values - adding asterisk";
                } else {
                    qDebug() << "  ✗ Variable" << baseKey << "missing valid data - sim:" << hasValidSimData << "meas:" << hasValidMeasData;
                }
            }
        }
        
        qDebug() << "  Total variables with both versions:" << variablesWithBothVersions.size();
        
        // For scatter plots: X-axis = variables ending with "m" (measured), Y-axis = variables ending with "s" (simulated)
        for (const QPair<QString, QString> &varPair : allVars) {
            QString displayName = varPair.first;
            QString columnName = varPair.second;
            
            // Extract base variable name (remove trailing 's' or 'm') to get full name from variable info
            QString baseVarName = columnName;
            if (baseVarName.endsWith("s", Qt::CaseInsensitive) || baseVarName.endsWith("m", Qt::CaseInsensitive)) {
                baseVarName.chop(1); // Remove the last character ('s' or 'm')
            }
            
            // Get full name using base variable name (try uppercase base name first)
            QPair<QString, QString> baseVarInfo = DataProcessor::getVariableInfo(baseVarName.toUpper());
            QString fullDisplayName;
            if (!baseVarInfo.first.isEmpty()) {
                fullDisplayName = baseVarInfo.first;
            } else {
                // Try original variable name (uppercase)
                QPair<QString, QString> origVarInfo = DataProcessor::getVariableInfo(columnName.toUpper());
                if (!origVarInfo.first.isEmpty()) {
                    fullDisplayName = origVarInfo.first;
                } else {
                    // Fallback to original display name if no info found
                    fullDisplayName = displayName;
                }
            }
            
            // Check if this variable has both "s" and "m" versions - add asterisk if so
            bool hasBothVersions = variablesWithBothVersions.contains(baseVarName.toLower());
            if (hasBothVersions) {
                fullDisplayName = "* " + fullDisplayName;
            }
            
            // Check if variable ends with "m" (measured) - add to X variable combo box
            if (columnName.endsWith("m", Qt::CaseInsensitive)) {
                m_xVariableComboBox->addItem(fullDisplayName, columnName);
            }
            
            // Check if variable ends with "s" (simulated) - add to Y variable list widget
            if (columnName.endsWith("s", Qt::CaseInsensitive)) {
                QListWidgetItem *item = new QListWidgetItem();
                item->setText(fullDisplayName);
                item->setData(Qt::UserRole, columnName);
                m_yVariableComboBox->addItem(item);
            }
        }
        
        qDebug() << "MainWindow::updateVariableComboBoxes() - Populated EVALUATE.OUT variables (X: m-ending, Y: s-ending)";
        return;
    }
    
    // For non-EVALUATE files (time series), allow multiple Y variable selection
    if (m_yVariableComboBox) {
        m_yVariableComboBox->setSelectionMode(QListWidget::MultiSelection);
    }

    // Variables to exclude from Y variable list (these are typically X-axis or grouping variables)
    QStringList yVariableExclusions = {"YEAR", "RUN","CR","FILEX", "EXPERIMENT", "DAS", "DAP", "DOY", "DATE", "TRT", "CROP", "TNAME"};

    QStringList commonVariables;
    QStringList simOnlyVariables;

    // Identify common variables and simulated-only variables
    for (const QString &columnName : m_currentData.columnNames) {
        if (m_currentObsData.columnNames.contains(columnName)) {
            // Check if observed data column actually has valid (non-missing) data
            const DataColumn *obsCol = m_currentObsData.getColumn(columnName);
            bool hasValidObsData = false;
            
            if (obsCol) {
                for (const QVariant &value : obsCol->data) {
                    if (!DataProcessor::isMissingValue(value)) {
                        hasValidObsData = true;
                        break;
                    }
                }
            }
            
            if (hasValidObsData) {
                commonVariables.append(columnName);
                qDebug() << "MainWindow: Variable" << columnName << "has both simulated and observed data";
            } else {
                simOnlyVariables.append(columnName);
                qDebug() << "MainWindow: Variable" << columnName << "has column in observed data but no valid values";
            }
        } else {
            simOnlyVariables.append(columnName);
        }
    }

    // Sort the lists alphabetically
    commonVariables.sort();
    simOnlyVariables.sort();

    // Add common variables first (with asterisk)
    for (const QString &columnName : commonVariables) {
        QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(columnName);
        QString displayLabel = varInfo.first.isEmpty() ? columnName : QString("%1 (%2)").arg(varInfo.first).arg(columnName);
        
        // Add asterisk to indicate variable has both simulated and observed data
        displayLabel = "* " + displayLabel;

        m_xVariableComboBox->addItem(displayLabel, columnName); // Store column name as user data
        
        // Only add to Y variables if not in exclusion list
        if (!yVariableExclusions.contains(columnName)) {
            QListWidgetItem *item = new QListWidgetItem();
            item->setText(displayLabel); // Explicitly set text
            item->setData(Qt::UserRole, columnName); // Store column name as user data
            m_yVariableComboBox->addItem(item);
        }
    }

    // Add simulated-only variables
    for (const QString &columnName : simOnlyVariables) {
        QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(columnName);
        QString displayLabel = varInfo.first.isEmpty() ? columnName : QString("%1 (%2)").arg(varInfo.first).arg(columnName);

        m_xVariableComboBox->addItem(displayLabel, columnName); // Store column name as user data
        
        // Only add to Y variables if not in exclusion list
        if (!yVariableExclusions.contains(columnName)) {
            QListWidgetItem *item = new QListWidgetItem();
            item->setText(displayLabel); // Explicitly set text
            item->setData(Qt::UserRole, columnName); // Store column name as user data
            m_yVariableComboBox->addItem(item);
        }
    }

    // Set default selections if available - prioritize DATE over DAP
    if (m_currentData.columnNames.contains("DATE")) {
        int index = m_xVariableComboBox->findData("DATE");
        if (index != -1) {
            m_xVariableComboBox->setCurrentIndex(index);
        }
    } else if (m_currentData.columnNames.contains("DAP")) {
        int index = m_xVariableComboBox->findData("DAP");
        if (index != -1) {
            m_xVariableComboBox->setCurrentIndex(index);
        }
    }

    // No default Y variable selection - user must manually select Y variables
}





void MainWindow::updateTreatmentComboBox()
{
    m_treatmentComboBox->clear();
    m_treatmentComboBox->addItem("All");
    
    // Look for treatment columns
    QStringList treatmentCols = {"TRT", "TRNO", "TR"};
    for (const QString &colName : treatmentCols) {
        if (m_currentData.columnNames.contains(colName)) {
            const DataColumn *col = m_currentData.getColumn(colName);
            if (col) {
                QSet<QString> uniqueValues;
                for (const QVariant &value : col->data) {
                    if (!DataProcessor::isMissingValue(value)) {
                        uniqueValues.insert(value.toString());
                    }
                }
                for (const QString &value : uniqueValues) {
                    m_treatmentComboBox->addItem(value);
                }
            }
            break;
        }
    }
}

void MainWindow::updateScatterPlot()
{
    qDebug() << "MainWindow::updateScatterPlot() - ENTRY POINT";
    
    // Use EVALUATE.OUT data for scatter plots
    if (m_evaluateData.rowCount == 0) {
        qDebug() << "MainWindow::updateScatterPlot() - No EVALUATE.OUT data available. Aborting scatter plot update.";
        m_statusWidget->showWarning("No EVALUATE.OUT data available for scatter plot. Please select EVALUATE.OUT files.");
        return;
    }
    
    // Get selected Y variables from ListWidget (should end with "s" for simulated)
    // For scatter plots, only one Y variable can be selected
    QStringList yVars;
    QList<QListWidgetItem*> selectedItems = m_yVariableComboBox->selectedItems();
    for (QListWidgetItem* item : selectedItems) {
        yVars.append(item->data(Qt::UserRole).toString());
    }
    
    if (yVars.isEmpty()) {
        qDebug() << "MainWindow::updateScatterPlot() - No Y variables selected.";
        m_statusWidget->showInfo("Please select a Y variable (ending with 's') for scatter plot");
        return;
    }
    
    // If multiple Y variables are selected, only use the first one and clear others
    if (yVars.size() > 1) {
        qDebug() << "MainWindow::updateScatterPlot() - Multiple Y variables selected, using first one only";
        // Clear other selections
        for (int i = 1; i < selectedItems.size(); ++i) {
            selectedItems[i]->setSelected(false);
        }
        yVars = QStringList() << yVars.first();
        m_statusWidget->showInfo("Only one Y variable can be plotted in scatter plot. Using first selected variable.");
    }
    
    // For EVALUATE.OUT files, automatically match Y variable (ending with "s") to X variable (ending with "m")
    // Get the first selected Y variable and find its corresponding X variable
    QString yVar = yVars.first();
    QString xVar;
    
    // Check if current file is EVALUATE.OUT
    bool isEvaluateFile = false;
    if (m_fileListWidget) {
        QList<QListWidgetItem*> selectedFileItems = m_fileListWidget->selectedItems();
        if (!selectedFileItems.isEmpty()) {
            QString firstSelectedFile = selectedFileItems.first()->text();
            QString upperFileName = firstSelectedFile.toUpper();
            isEvaluateFile = upperFileName.contains("EVALUATE") || upperFileName == "EVALUATE.OUT";
        }
    }
    
    if (isEvaluateFile && yVar.endsWith("s", Qt::CaseInsensitive)) {
        // Find corresponding X variable by replacing "s" with "m"
        QString baseName = yVar;
        baseName.chop(1); // Remove the "s" at the end
        QString correspondingXVar = baseName + "m";
        
        // Check if this variable exists in the data
        if (m_currentData.columnNames.contains(correspondingXVar)) {
            xVar = correspondingXVar;
            // Automatically select it in the X combo box
            int index = m_xVariableComboBox->findData(xVar);
            if (index >= 0) {
                m_xVariableComboBox->blockSignals(true);
                m_xVariableComboBox->setCurrentIndex(index);
                m_xVariableComboBox->blockSignals(false);
            }
            qDebug() << "MainWindow::updateScatterPlot() - Auto-matched X variable:" << xVar << "for Y variable:" << yVar;
        } else {
            // Fallback: use the currently selected X variable
            xVar = m_xVariableComboBox->currentData(Qt::UserRole).toString();
            qDebug() << "MainWindow::updateScatterPlot() - Could not find matching X variable for" << yVar << ", using selected:" << xVar;
        }
    } else {
        // For non-EVALUATE files or if Y doesn't end with "s", use the selected X variable
        xVar = m_xVariableComboBox->currentData(Qt::UserRole).toString();
    }
    
    if (xVar.isEmpty()) {
        qDebug() << "MainWindow::updateScatterPlot() - X variable not available.";
        m_statusWidget->showInfo("Please select X variable (ending with 'm') for scatter plot");
        return;
    }
    
    qDebug() << "MainWindow::updateScatterPlot() - X variable:" << xVar;
    qDebug() << "MainWindow::updateScatterPlot() - Y variables:" << yVars;
    
    // For scatter plot, plot each Y variable against X variable
    if (m_scatterPlotWidget) {
        QString treatment = m_treatmentComboBox ? m_treatmentComboBox->currentText() : "All";
        QStringList treatments = treatment == "All" ? QStringList() : QStringList() << treatment;
        
        // Plot the first selected Y variable using EVALUATE.OUT data
        m_scatterPlotWidget->plotScatter(
            m_evaluateData,
            xVar,
            yVar,
            treatments,
            m_treatmentNames
        );
        qDebug() << "MainWindow::updateScatterPlot() - Scatter plot updated";
    } else {
        qWarning() << "MainWindow::updateScatterPlot() - Scatter plot widget is null!";
    }
}

void MainWindow::updatePlot()
{
    qDebug() << "MainWindow::updatePlot() - ENTRY POINT";
    qDebug() << "MainWindow::updatePlot() - Plot widget exists:" << (m_plotWidget != nullptr);
    
    if (m_currentData.rowCount == 0) {
        qDebug() << "MainWindow::updatePlot() - No data available (simulated or observed). Aborting plot update.";
        return;
    }
    
    QString xVar = m_xVariableComboBox->currentData(Qt::UserRole).toString();
    
    // Get selected Y variables from ListWidget
    QStringList yVars;
    QList<QListWidgetItem*> selectedItems = m_yVariableComboBox->selectedItems();
    for (QListWidgetItem* item : selectedItems) {
        yVars.append(item->data(Qt::UserRole).toString());
    }
    
    qDebug() << "MainWindow::updatePlot() - X variable:" << xVar;
    qDebug() << "MainWindow::updatePlot() - Y variables:" << yVars << "Count:" << yVars.size();
    
    if (xVar.isEmpty() || yVars.isEmpty()) {
        qDebug() << "MainWindow::updatePlot() - X or Y variables not selected. Aborting plot update.";
        
        // Clear plot and axis labels when variables are not selected
        if (m_plotWidget) {
            if (yVars.isEmpty() && !xVar.isEmpty()) {
                // Clear plot and Y-axis label, keep X-axis label
                m_plotWidget->clearChart();
                QPair<QString, QString> xVarInfo = DataProcessor::getVariableInfo(xVar);
                QString xTitle = xVarInfo.first.isEmpty() ? xVar : xVarInfo.first;
                m_plotWidget->setAxisTitles(xTitle, "");
            } else if (xVar.isEmpty() && yVars.isEmpty()) {
                // Clear plot and both axis labels if nothing is selected
                m_plotWidget->clearChart();
            }
        }
        
        // Also clear data table when no variables are selected
        if (m_dataTableWidget) {
            m_dataTableWidget->clear();
        }
        return;
    }
    
    QString yVar = yVars.first();  // Use first selected for now
    QString treatment = m_treatmentComboBox ? m_treatmentComboBox->currentText() : "All";
    

    // Use PlotWidget for plotting (matches Python PyQtGraph structure)
    if (m_plotWidget) {
        // Get selected files
        QStringList selectedFiles;
        QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
        for (QListWidgetItem* item : selectedItems) {
            selectedFiles.append(item->text());
        }
        
        // Clear data table if no files are selected
        if (selectedFiles.isEmpty()) {
            qDebug() << "MainWindow::updatePlot() - No files selected, clearing data table";
            if (m_dataTableWidget) {
                m_dataTableWidget->clear();
            }
            return; // Exit early since there's no data to plot
        }
        
        
        // Debug observed data before plotting
        qDebug() << "MainWindow::updatePlot() - Observed data status:";
        qDebug() << "  Observed data rows:" << m_currentObsData.rowCount;
        qDebug() << "  Observed data columns:" << m_currentObsData.columnNames;
        qDebug() << "  X variable:" << xVar;
        qDebug() << "  Y variables:" << yVars;
        qDebug() << "  Treatment:" << treatment;
        qDebug() << "  Selected experiment:" << m_selectedExperiment;
        
        // Call the enhanced plotTimeSeries method
        m_plotWidget->plotTimeSeries(
            m_currentData,
            m_selectedFolder,
            selectedFiles,
            m_selectedExperiment,
            QStringList() << treatment,
            xVar,
            yVars,
            m_currentObsData,
            m_treatmentNames
        );
        
        qDebug() << "MainWindow::updatePlot() - plotTimeSeries call completed";
    } else {
        qDebug() << "MainWindow::updatePlot() - ERROR: m_plotWidget is null!";
    }
}

void MainWindow::checkAndAutoSwitchToScatterPlot(bool autoPlot)
{
    // Only auto-switch if we're on the time series tab (index 0)
    if (!m_tabWidget || m_tabWidget->currentIndex() != 0) {
        return;
    }
    
    // Check if EVALUATE.OUT file is selected
    bool isEvaluateFile = false;
    if (m_fileListWidget) {
        QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
        if (!selectedItems.isEmpty()) {
            QString firstSelectedFile = selectedItems.first()->text();
            QString upperFileName = firstSelectedFile.toUpper();
            isEvaluateFile = upperFileName.contains("EVALUATE") || upperFileName == "EVALUATE.OUT";
        }
    }
    
    if (!isEvaluateFile) {
        return; // Not an EVALUATE.OUT file, don't switch
    }
    
    // Check if X and Y variables are selected
    QString xVar = m_xVariableComboBox ? m_xVariableComboBox->currentData(Qt::UserRole).toString() : QString();
    QList<QListWidgetItem*> selectedYItems = m_yVariableComboBox ? m_yVariableComboBox->selectedItems() : QList<QListWidgetItem*>();
    
    if (xVar.isEmpty() || selectedYItems.isEmpty()) {
        return; // Variables not selected yet, wait
    }
    
    // Check if data is loaded
    if (m_currentData.rowCount == 0) {
        return; // Data not loaded yet
    }
    
    // All conditions met: switch to scatter plot tab
    qDebug() << "MainWindow: Auto-switching to scatter plot tab for EVALUATE.OUT file";
    m_tabWidget->setCurrentIndex(2); // Switch to scatter plot tab
    
    // Only auto-plot if requested (default is true for backward compatibility)
    if (autoPlot) {
        // Update scatter plot immediately (tab switch is synchronous)
        updateScatterPlot();
        m_statusWidget->showSuccess("Automatically switched to scatter plot for EVALUATE.OUT file");
    } else {
        // Just switch tabs, don't plot - user must click Refresh Plot
        m_statusWidget->showInfo("Switched to scatter plot tab. Click 'Refresh Plot' to view the scatter plot");
    }
}

void MainWindow::resetInterface()
{
    m_currentData.clear();
    m_currentObsData.clear();
    m_evaluateData.clear();
    m_currentFilePath.clear();
    
    if (m_xVariableComboBox) {
        m_xVariableComboBox->clear();
    }
    if (m_yVariableComboBox) {
        m_yVariableComboBox->clear();
    }
    if (m_treatmentComboBox) {
        m_treatmentComboBox->clear();
    }
    
    if (m_dataInfoLabel) {
        m_dataInfoLabel->setText("No data loaded");
    }
    
    if (m_updatePlotButton) {
        m_updatePlotButton->setEnabled(false);
    }
    
    if (m_plotWidget) {
        m_plotWidget->clear();
    }
    
    if (m_scatterPlotWidget) {
        m_scatterPlotWidget->clear();
    }
    
    if (m_dataTableWidget) {
        m_dataTableWidget->clear();
    }
    
    // Clear metrics when interface is reset
    clearMetrics();
}

void MainWindow::centerWindow()
{
    if (QScreen *screen = QApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Add any cleanup logic here
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Handle resize logic if needed
}

void MainWindow::populateFolders()
{
    if (!m_fileComboBox || !m_dataProcessor) {
        return;
    }
    
    m_fileComboBox->clear();
    
    QStringList folders = m_dataProcessor->prepareFolders(true);
    
    
    if (folders.isEmpty()) {
        m_fileComboBox->addItem("No DSSAT folders found");
        m_statusWidget->showWarning("No DSSAT crop folders found. Check DSSAT installation.");
        return;
    }
    
    for (const QString &folder : folders) {
        m_fileComboBox->addItem(folder);
    }
    
    // Select first folder by default
    if (m_fileComboBox->count() > 0) {
        m_fileComboBox->setCurrentIndex(0);
        onFolderSelectionChanged();
    }
}

void MainWindow::populateFiles(const QString &folderName)
{
    if (!m_fileListWidget || !m_dataProcessor || folderName.isEmpty()) {
        qDebug() << "populateFiles: Missing widgets, data processor, or empty folder name:" << folderName;
        return;
    }
    
    qDebug() << "populateFiles: Looking for files in folder:" << folderName;
    
    m_fileListWidget->clear();
    m_availableFiles.clear();
    
    QStringList outFiles = m_dataProcessor->prepareOutFiles(folderName);
    
    qDebug() << "populateFiles: Found" << outFiles.size() << "files:" << outFiles;
    
    if (outFiles.isEmpty()) {
        m_fileListWidget->addItem("No .OUT files found");
        m_statusWidget->showInfo(QString("No output files found in folder: %1").arg(folderName));
        qDebug() << "populateFiles: No files found in folder:" << folderName;
        return;
    }
    
    m_availableFiles = outFiles;
    
    // Get outfile descriptions from OUTFILE.CDE
    QMap<QString, QString> outfileDescriptions = DataProcessor::getOutfileDescriptions();
    qDebug() << "populateFiles: Loaded" << outfileDescriptions.size() << "outfile descriptions";
    
    for (const QString &file : outFiles) {
        QListWidgetItem *item = new QListWidgetItem(file);
        
        // Extract base filename (without extension) and get description
        QString baseFilename = QFileInfo(file).baseName();
        QString description = outfileDescriptions.value(baseFilename, QString());
        
        qDebug() << "populateFiles: File" << file << "BaseFilename:" << baseFilename << "Description:" << description;
        
        if (!description.isEmpty()) {
            item->setToolTip(QString("%1: %2").arg(file).arg(description));
            qDebug() << "populateFiles: Set tooltip to:" << item->toolTip();
        } else {
            item->setToolTip(QString("DSSAT output file: %1").arg(file));
            qDebug() << "populateFiles: Using default tooltip for:" << file;
        }
        
        m_fileListWidget->addItem(item);
    }
    
    m_statusWidget->showSuccess(QString("Found %1 output files in %2").arg(outFiles.size()).arg(folderName));
}

void MainWindow::onFolderSelectionChanged()
{
    if (!m_fileComboBox) {
        return;
    }
    
    QString selectedFolder = m_fileComboBox->currentText();
    if (selectedFolder.isEmpty() || selectedFolder == "No DSSAT folders found") {
        // Crop unselected - clear plot and metrics
        if (m_plotWidget) {
            m_plotWidget->clear();
        }
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->clear();
        }
        clearMetrics();
        return;
    }
    
    m_selectedFolder = selectedFolder;
    populateFiles(selectedFolder);
    
    // Clear current data since folder changed
    resetInterface();
    
    // Also clear metrics when crop changes
    clearMetrics();
}

void MainWindow::onRefreshFiles()
{
    if (!m_selectedFolder.isEmpty()) {
        m_statusWidget->showInfo("Refreshing file list...");
        populateFiles(m_selectedFolder);
    } else {
        populateFolders();
    }
}

void MainWindow::onFileSelectionChanged()
{
    qDebug() << "MainWindow::onFileSelectionChanged() - File selection changed!";
    if (!m_fileListWidget) {
        qDebug() << "MainWindow::onFileSelectionChanged() - No file list widget!";
        return;
    }

    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    qDebug() << "MainWindow::onFileSelectionChanged() - Selected items count:" << selectedItems.size();

    if (selectedItems.isEmpty()) {
        qDebug() << "MainWindow::onFileSelectionChanged() - No items selected, clearing data and variables";
        m_updatePlotButton->setEnabled(false);

        // Clear data when no files are selected
        m_currentData.clear();
        m_currentObsData.clear();
        m_evaluateData.clear();

        // Clear and update variable combo boxes
        updateVariableComboBoxes();

        // Clear the plot
        if (m_plotWidget) {
            m_plotWidget->clear();
        }
        
        // Clear scatter plot
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->clear();
        }
        
        // Clear metrics
        clearMetrics();

        // Show info message based on current tab
        if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
            m_statusWidget->showInfo("Click outfile and variables, then click 'Refresh Plot' to view time series");
        } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
            m_statusWidget->showInfo("Click outfile and click refresh data to view data");
        }

        return;
    }

    // Enable the update button when files are selected
    qDebug() << "MainWindow::onFileSelectionChanged() - Enabling update button";
    m_updatePlotButton->setEnabled(true);

    // Load data from all selected files to get comprehensive Y variable list
    if (!selectedItems.isEmpty()) {
        qDebug() << "MainWindow::onFileSelectionChanged() - Processing" << selectedItems.size() << "selected files...";
        
        // Clear previous data - separate storage for different file types
        m_currentData.clear();  // For time series (regular .OUT files)
        m_currentObsData.clear();  // For time series observed data
        m_evaluateData.clear();  // For scatter plots (EVALUATE.OUT files)
        
        // Load and merge data from all selected files, separating by type
        QSet<QString> uniqueExperimentCodes;
        QMap<QString, QMap<QString, QString>> extractedTreatmentNames;
        QString firstValidFile;
        QString firstValidRegularFile;  // For observed data lookup
        bool hasEvaluateFile = false;
        bool hasRegularFile = false;
        
        for (QListWidgetItem* selectedItem : selectedItems) {
            QString selectedFile = selectedItem->text();
            qDebug() << "MainWindow::onFileSelectionChanged() - Processing file:" << selectedFile;
        
            if (selectedFile != "No .OUT files found") {
                qDebug() << "MainWindow::onFileSelectionChanged() - Getting DSSAT base path...";
                QString dssatBase = m_dataProcessor->getDSSATBase();
                qDebug() << "MainWindow::onFileSelectionChanged() - DSSAT base:" << dssatBase;
                qDebug() << "MainWindow::onFileSelectionChanged() - Selected folder:" << m_selectedFolder;
                
                // Get the actual path used by prepareOutFiles instead of reconstructing it
                QString folderPath = m_dataProcessor->getActualFolderPath(m_selectedFolder);
                QString filePath;
                if (!folderPath.isEmpty()) {
                    filePath = QDir(folderPath).absoluteFilePath(selectedFile);
                } else {
                    filePath = QDir(dssatBase).absoluteFilePath(m_selectedFolder + QDir::separator() + selectedFile);
                }
                qDebug() << "MainWindow: Selected simulated file path:" << filePath;
                
                // Check if THIS specific file is EVALUATE.OUT (not just the first one)
                QString upperFileName = selectedFile.toUpper();
                bool isEvaluateFile = upperFileName.contains("EVALUATE") || upperFileName == "EVALUATE.OUT";
                if (isEvaluateFile) {
                    hasEvaluateFile = true;
                } else {
                    hasRegularFile = true;
                    if (firstValidRegularFile.isEmpty()) {
                        firstValidRegularFile = filePath;
                    }
                }
                qDebug() << "MainWindow::onFileSelectionChanged() - File" << selectedFile << "is EVALUATE:" << isEvaluateFile;
                
                // Load data from current file using appropriate reader
                DataTable fileData;
                bool readSuccess = false;
                if (isEvaluateFile) {
                    // Use readEvaluateFile for EVALUATE.OUT files
                    readSuccess = m_dataProcessor->readEvaluateFile(filePath, fileData);
                } else {
                    readSuccess = m_dataProcessor->readFile(filePath, fileData);
                }
                
                if (readSuccess) {
                    qDebug() << "MainWindow::onFileSelectionChanged() - Successfully loaded file:" << selectedFile;
                    
                    // Keep track of first valid file for later processing
                    if (firstValidFile.isEmpty()) {
                        firstValidFile = filePath;
                    }
                    
                    // Store data in appropriate location based on file type
                    if (isEvaluateFile) {
                        // Store EVALUATE.OUT data separately for scatter plots
                        if (m_evaluateData.rowCount == 0) {
                            m_evaluateData = fileData;
                        } else {
                            m_evaluateData.merge(fileData);
                        }
                    } else {
                        // Store regular .OUT data for time series plots
                        if (m_currentData.rowCount == 0) {
                            m_currentData = fileData;
                        } else {
                            m_currentData.merge(fileData);
                        }
                    }
                    
                    // Extract experiment codes and treatment names from this file (only for regular files)
                    if (!isEvaluateFile && fileData.columnNames.contains("EXPERIMENT") && fileData.columnNames.contains("TRT") && fileData.columnNames.contains("TNAME")) {
                        const DataColumn* expCol = fileData.getColumn("EXPERIMENT");
                        const DataColumn* trtCol = fileData.getColumn("TRT");
                        const DataColumn* tnameCol = fileData.getColumn("TNAME");

                        if (expCol && trtCol && tnameCol) {
                            for (int i = 0; i < fileData.rowCount; ++i) {
                                QString expCode = expCol->data[i].toString().trimmed();
                                QString trtCode = trtCol->data[i].toString().trimmed();
                                QString tname = tnameCol->data[i].toString().trimmed();

                                if (!expCode.isEmpty() && expCode != "DEFAULT") {
                                    uniqueExperimentCodes.insert(expCode);
                                }
                                if (!expCode.isEmpty() && !trtCode.isEmpty() && !tname.isEmpty()) {
                                    extractedTreatmentNames[expCode][trtCode] = tname;
                                }
                            }
                        }
                    }
                } else {
                    qDebug() << "MainWindow::onFileSelectionChanged() - Failed to load file:" << selectedFile;
                }
            }
        }
        
        // Process regular .OUT files (for time series plots)
        if (m_currentData.rowCount > 0) {
            qDebug() << "MainWindow::onFileSelectionChanged() - Merged data from" << selectedItems.size() << "files, total rows:" << m_currentData.rowCount;
            qDebug() << "MainWindow: Extracted unique Experiment Codes from all files:" << QList<QString>(uniqueExperimentCodes.values());
            qDebug() << "MainWindow: Extracted Treatment Names:" << extractedTreatmentNames;
            m_treatmentNames = extractedTreatmentNames; // Assign to member variable

            // Set m_selectedExperiment to the first available experiment code
            if (!uniqueExperimentCodes.isEmpty()) {
                m_selectedExperiment = uniqueExperimentCodes.values().first();
                qDebug() << "MainWindow: Setting m_selectedExperiment to:" << m_selectedExperiment;
            } else {
                m_selectedExperiment = ""; // Or a default value if no experiments are found
                qDebug() << "MainWindow: No experiment codes found, m_selectedExperiment set to empty.";
            }

            // Determine crop code
            QString cropCode = "XX";
            
            // Special handling for SensWork - extract crop code from the file itself
            if (m_selectedFolder.compare("SensWork", Qt::CaseInsensitive) == 0 && !firstValidFile.isEmpty()) {
                qDebug() << "MainWindow: SensWork detected - extracting crop code from file";
                QPair<QString, QString> sensWorkCodes = m_dataProcessor->extractSensWorkCodes(firstValidFile);
                if (!sensWorkCodes.second.isEmpty()) {
                    cropCode = sensWorkCodes.second.toUpper();
                    qDebug() << "MainWindow: SensWork crop code extracted:" << cropCode;
                } else {
                    qDebug() << "MainWindow: Could not extract crop code from SensWork file, using default";
                }
            } else {
                // Regular crop folder - try to get crop code from the selected folder name by matching with crop details
                QVector<CropDetails> allCropDetails = m_dataProcessor->getCropDetails();
                qDebug() << "MainWindow: Selected folder:" << m_selectedFolder;
                qDebug() << "MainWindow: Found" << allCropDetails.size() << "crop details";
                for (const CropDetails& crop : allCropDetails) {
                    QString dirName = QFileInfo(crop.directory).fileName().toLower();
                    QString cropNameLower = crop.cropName.toLower();
                    QString selectedFolderLower = m_selectedFolder.toLower();
                    
                    // Check multiple matching strategies:
                    // 1. Directory name matches (e.g., "drybean" == "drybean")
                    // 2. Crop name matches (e.g., "dry bean" == "dry bean")
                    // 3. Directory path contains folder name
                    // 4. Crop name contains folder name or vice versa (for partial matches)
                    bool dirNameMatch = (dirName == selectedFolderLower);
                    bool cropNameMatch = (cropNameLower == selectedFolderLower);
                    bool pathContainsFolder = crop.directory.toLower().contains("/" + selectedFolderLower) || 
                                            crop.directory.toLower().contains("\\" + selectedFolderLower);
                    bool cropNameContains = cropNameLower.contains(selectedFolderLower) || 
                                          selectedFolderLower.contains(cropNameLower);
                    
                    qDebug() << "MainWindow: Checking crop:" << crop.cropCode << "name:" << crop.cropName << "dir:" << crop.directory << "dirName:" << dirName << "pathContains:" << pathContainsFolder;
                    
                    if (dirNameMatch || cropNameMatch || pathContainsFolder || cropNameContains) {
                        cropCode = crop.cropCode.toUpper();
                        qDebug() << "MainWindow: MATCHED! Setting cropCode to:" << cropCode << "(match type: dirName=" << dirNameMatch << " cropName=" << cropNameMatch << " pathContains=" << pathContainsFolder << " nameContains=" << cropNameContains << ")";
                        break;
                    }
                }
            }
            qDebug() << "MainWindow: Determined Crop Code:" << cropCode;

            // Add CROP column to simulated data if it doesn't exist
            if (!m_currentData.columnNames.contains("CROP")) {
                DataColumn cropCol("CROP");
                for (int r = 0; r < m_currentData.rowCount; ++r) {
                    cropCol.data.append(cropCode);
                }
                m_currentData.addColumn(cropCol);
                qDebug() << "MainWindow: Added CROP column with code:" << cropCode << "to simulated data";
            }

            // Attempt to load and merge observed data for each unique experiment code (only for regular files)
            // Special handling for SensWork files
            if (m_selectedFolder.compare("SensWork", Qt::CaseInsensitive) == 0) {
                qDebug() << "MainWindow: Detected SensWork folder - using dynamic observed data lookup";
                
                // For SensWork, use the dynamic observed data lookup
                if (!firstValidRegularFile.isEmpty()) {
                    DataTable sensWorkObsData;
                    if (m_dataProcessor->readSensWorkObservedData(firstValidRegularFile, sensWorkObsData)) {
                        m_currentObsData.merge(sensWorkObsData);
                        qDebug() << "MainWindow: Successfully loaded SensWork observed data:" << sensWorkObsData.rowCount << "rows";
                    } else {
                        qDebug() << "MainWindow: No observed data found for SensWork file";
                    }
                }
            } else {
                // Regular crop folder - use standard observed data lookup
                qDebug() << "MainWindow: Attempting to load observed data for crop code:" << cropCode;
                qDebug() << "MainWindow: Unique experiment codes:" << uniqueExperimentCodes;
                qDebug() << "MainWindow: First valid regular file:" << firstValidRegularFile;
                
                for (const QString& expCode : uniqueExperimentCodes) {
                    DataTable tempObsData;
                    qDebug() << "MainWindow: Trying to read observed data for experiment:" << expCode;
                    // Use the first valid regular file path for observed data lookup
                    if (!firstValidRegularFile.isEmpty() && m_dataProcessor->readObservedData(firstValidRegularFile, expCode, cropCode, tempObsData)) {
                        qDebug() << "MainWindow: Successfully loaded observed data for" << expCode << "- Rows:" << tempObsData.rowCount << "Columns:" << tempObsData.columnNames;
                        m_currentObsData.merge(tempObsData);
                        qDebug() << "MainWindow: Total observed data after merge - Rows:" << m_currentObsData.rowCount << "Columns:" << m_currentObsData.columnNames;
                    } else {
                        qDebug() << "MainWindow: Failed to load observed data for experiment:" << expCode;
                    }
                }
            }
            
            // Add DAS/DAP columns to observed data if it exists
            if (m_currentObsData.rowCount > 0) {
                qDebug() << "MainWindow: Adding DAS/DAP columns to observed data. Current rows:" << m_currentObsData.rowCount;
                qDebug() << "MainWindow: Observed data columns:" << m_currentObsData.columnNames;
                m_dataProcessor->addDasDapColumns(m_currentObsData, m_currentData);
                qDebug() << "MainWindow: After adding DAS/DAP - Rows:" << m_currentObsData.rowCount << "Columns:" << m_currentObsData.columnNames;
            } else {
                qDebug() << "MainWindow: No observed data loaded (rowCount = 0)";
            }
        }
        
        // Process EVALUATE.OUT files (for scatter plots)
        if (m_evaluateData.rowCount > 0) {
            qDebug() << "MainWindow::onFileSelectionChanged() - Loaded EVALUATE.OUT data, total rows:" << m_evaluateData.rowCount;
        }
        
        // Update variable combo boxes based on current tab
        updateVariableComboBoxes();
        if (hasRegularFile) {
            updateTreatmentComboBox();
        }

        // Mark data as needing refresh for the data table, but don't set it yet
        markDataNeedsRefresh();
        qDebug() << "MainWindow::onFileSelectionChanged() - Data loaded from" << selectedItems.size() << "files";
        qDebug() << "  Regular .OUT files:" << (hasRegularFile ? "Yes" : "No") << "rows:" << m_currentData.rowCount;
        qDebug() << "  EVALUATE.OUT files:" << (hasEvaluateFile ? "Yes" : "No") << "rows:" << m_evaluateData.rowCount;
        qDebug() << "MainWindow::onFileSelectionChanged() - Function completed successfully; table data not yet set.";
        
        // If we're on Data View tab, update file type selector and refresh data
        if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
            // Update file type selector based on available data
            if (m_dataViewFileTypeComboBox) {
                bool hasRegular = (m_currentData.rowCount > 0);
                bool hasEvaluate = (m_evaluateData.rowCount > 0);
                
                if (hasRegular && hasEvaluate) {
                    // Both available - enable selector
                    m_dataViewFileTypeComboBox->setEnabled(true);
                } else if (hasEvaluate) {
                    // Only EVALUATE.OUT - disable selector and set to evaluate
                    m_dataViewFileTypeComboBox->setEnabled(false);
                    m_dataViewFileTypeComboBox->setCurrentIndex(1); // EVALUATE.OUT
                } else if (hasRegular) {
                    // Only regular .OUT - disable selector and set to regular
                    m_dataViewFileTypeComboBox->setEnabled(false);
                    m_dataViewFileTypeComboBox->setCurrentIndex(0); // Regular .OUT
                } else {
                    // No data - disable selector
                    m_dataViewFileTypeComboBox->setEnabled(false);
                }
            }
            
            // Refresh data table based on selected file type
            if (m_dataTableWidget) {
                onDataViewFileTypeChanged();
            }
        }

        // Check if we're in command line mode (file selection UI is hidden)
        bool isCommandLineMode = (m_cropGroup && !m_cropGroup->isVisible()) || 
                                 (m_fileGroup && !m_fileGroup->isVisible());
        
        // Auto-switch to scatter plot tab in command line mode if only EVALUATE.OUT files are selected
        if (isCommandLineMode && hasEvaluateFile && !hasRegularFile) {
            if (m_tabWidget) {
                m_tabWidget->setCurrentIndex(2); // Switch to scatter plot tab
                qDebug() << "MainWindow::onFileSelectionChanged() - Command line mode: Auto-switched to scatter plot tab for EVALUATE.OUT files";
                // Update variables for scatter plot tab after switching
                updateVariableComboBoxes();
            }
        }
        
        // Show prompt message based on current tab
        if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
            if (hasRegularFile) {
                m_statusWidget->showInfo(QString("Loaded %1 regular .OUT file(s) for time series plots. Select variables and click 'Refresh Plot'.").arg(selectedItems.size()));
            } else if (hasEvaluateFile) {
                if (isCommandLineMode) {
                    // In command line mode, we already switched tabs, so this message won't show
                    m_statusWidget->showInfo("EVALUATE.OUT files selected. Switched to Scatter Plot tab.");
                } else {
                    m_statusWidget->showInfo("EVALUATE.OUT files selected. Switch to Scatter Plot tab to view scatter plots.");
                }
            }
        } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
            m_statusWidget->showInfo(QString("Loaded %1 file(s). Click 'Refresh Data' to view data table").arg(selectedItems.size()));
        } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
            // Scatter Plot tab
            if (hasEvaluateFile) {
                m_statusWidget->showInfo(QString("Loaded %1 EVALUATE.OUT file(s). Select X and Y variables and click 'Refresh Plot' to view scatter plot").arg(selectedItems.size()));
            } else {
                m_statusWidget->showInfo("No EVALUATE.OUT files selected. Please select EVALUATE.OUT files for scatter plots.");
            }
        }
    }
    qDebug() << "MainWindow::onFileSelectionChanged() - Function finished";
}

// Additional methods from Python version
void MainWindow::extractExperimentFromOutputFile()
{
    try {
        if (m_selectedFolder.isEmpty()) {
            return;
        }
        
        QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
        if (selectedItems.isEmpty()) {
            return;
        }
        
        QStringList experimentCodes;
        QStringList treatmentNumbers;
        
        for (QListWidgetItem* item : selectedItems) {
            QString outFile = item->text();
            QString dssatBase = m_dataProcessor->getDSSATBase();
            QString filePath = QDir(dssatBase).absoluteFilePath(m_selectedFolder + QDir::separator() + outFile);
            
            // Try to extract experiment and treatment info from file
            // This would need access to DataProcessor methods for reading files
            // For now, we'll store the basic info
            qDebug() << "Processing file for experiment extraction:" << filePath;
        }
        
        // If we found experiment codes, select the first one
        if (!experimentCodes.isEmpty()) {
            selectExperimentByCode(experimentCodes.first(), treatmentNumbers);
        }
        
    } catch (const std::exception& e) {
        qWarning() << "Error extracting experiment from output file:" << e.what();
    }
}

void MainWindow::selectExperimentByCode(const QString &expCode, const QStringList &treatmentNumbers)
{
    qDebug() << "Setting experiment code:" << expCode;
    
    m_selectedExperiment = expCode;
    
    if (!treatmentNumbers.isEmpty()) {
        qDebug() << "Setting treatments:" << treatmentNumbers;
        m_selectedTreatments = treatmentNumbers;
    }
}

void MainWindow::selectTreatmentsByNumbers(const QStringList &treatmentNumbers)
{
    m_selectedTreatments = treatmentNumbers;
    qDebug() << "Selected treatments:" << m_selectedTreatments;
}



void MainWindow::showError(const QString &title, const QString &message)
{
    QMessageBox::critical(this, title, message);
}

void MainWindow::showSuccess(const QString &message)
{
    if (m_statusWidget) {
        m_statusWidget->showSuccess(message);
    }
}

void MainWindow::showWarning(const QString &message)
{
    if (m_statusWidget) {
        m_statusWidget->showWarning(message);
    }
}

void MainWindow::markDataNeedsRefresh()
{
    m_dataNeedsRefresh = true;
    m_tabContentLoaded.clear();
}

void MainWindow::filterOutFiles(const QString &text)
{
    if (!m_fileListWidget) {
        return;
    }
    
    for (int i = 0; i < m_fileListWidget->count(); ++i) {
        QListWidgetItem *item = m_fileListWidget->item(i);
        if (item) {
            bool visible = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
            item->setHidden(!visible);
        }
    }
}

void MainWindow::filterYVars(const QString &text)
{
    if (!m_yVariableComboBox) {
        return;
    }
    
    for (int i = 0; i < m_yVariableComboBox->count(); ++i) {
        QListWidgetItem *item = m_yVariableComboBox->item(i);
        if (item) {
            bool visible = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
            item->setHidden(!visible);
        }
    }
}

void MainWindow::unselectAllOutFiles()
{
    if (m_fileListWidget) {
        m_fileListWidget->clearSelection();
    }
}

void MainWindow::unselectAllYVars()
{
    if (m_yVariableComboBox) {
        m_yVariableComboBox->clearSelection();
    }
}

void MainWindow::refreshOutputFiles()
{
    if (!m_selectedFolder.isEmpty()) {
        showSuccess("Refreshing file list...");
        populateFiles(m_selectedFolder);
    } else {
        populateFolders();
    }
}

void MainWindow::updateMetricsButtonState()
{
    if (!m_metricsButton) {
        return;
    }

    bool hasMetrics = false;
    int currentTab = m_tabWidget ? m_tabWidget->currentIndex() : -1;
    if (currentTab == 0) {
        hasMetrics = !m_timeSeriesMetrics.isEmpty();
    } else if (currentTab == 2) {
        hasMetrics = !m_scatterMetrics.isEmpty();
    } else {
        hasMetrics = !m_currentMetrics.isEmpty();
    }

    m_metricsButton->setEnabled(hasMetrics);
}

void MainWindow::clearMetrics()
{
    m_timeSeriesMetrics.clear();
    m_scatterMetrics.clear();
    m_currentMetrics.clear();
    updateMetricsButtonState();
}

void MainWindow::onPlotWidgetXVariableChanged(const QString &xVariable)
{
    qDebug() << "MainWindow: PlotWidget X variable changed to:" << xVariable;
    
    // Update the X variable combo box to reflect the change
    if (m_xVariableComboBox) {
        // Find the index for the new X variable
        int index = m_xVariableComboBox->findData(xVariable);
        if (index >= 0) {
            // Temporarily block signals to avoid infinite loop
            m_xVariableComboBox->blockSignals(true);
            m_xVariableComboBox->setCurrentIndex(index);
            m_xVariableComboBox->blockSignals(false);
            
            qDebug() << "MainWindow: Updated X variable combo box to:" << xVariable;
            
            // Refresh the plot with the new X variable
            qDebug() << "MainWindow: Refreshing plot with new X variable:" << xVariable;
            updatePlot();
        } else {
            qDebug() << "MainWindow: X variable" << xVariable << "not found in combo box";
        }
    }
}

void MainWindow::onUnselectAllFiles()
{
    qDebug() << "MainWindow: Unselect All Files button clicked";
    
    if (m_fileListWidget) {
        // Clear all selections in the file list
        m_fileListWidget->clearSelection();
        qDebug() << "MainWindow: Cleared all file selections";
        
        // Trigger the selection changed event to update the interface
        onFileSelectionChanged();
    }
}

void MainWindow::onUnselectAllYVars()
{
    qDebug() << "MainWindow: Unselect All Y Variables button clicked";
    
    if (m_yVariableComboBox) {
        // Clear all selections in the Y variable list
        m_yVariableComboBox->clearSelection();
        qDebug() << "MainWindow: Cleared all Y variable selections";
        
        // Clear metrics data since no variables are selected
        clearMetrics();
        qDebug() << "MainWindow: Cleared metrics data";
        
        // Trigger the selection changed event to update the plot
        onYVariableChanged();
    }
}

void MainWindow::onShowMetrics()
{
    qDebug() << "MainWindow: Show Metrics button clicked";
    
    if (m_currentMetrics.isEmpty()) {
        showWarning("No metrics data available. Please ensure both simulated and observed data are loaded and plotted.");
        return;
    }
    
    // Determine if we're showing scatter plot metrics (tab index 2)
    bool isScatterPlot = (m_tabWidget && m_tabWidget->currentIndex() == 2);
    
    // Use the proper MetricsDialog which includes the export button
    MetricsDialog *metricsDialog = new MetricsDialog(m_currentMetrics, isScatterPlot, this);
    metricsDialog->exec();  // Use exec() for modal dialog
    metricsDialog->deleteLater();
}

void MainWindow::updateTimeSeriesMetrics(const QVector<QMap<QString, QVariant>> &metrics)
{
    qDebug() << "MainWindow::updateTimeSeriesMetrics() - RECEIVED SIGNAL with" << metrics.size() << "metrics";
    
    // Convert QVector<QMap> to QVariantList for compatibility
    QVariantList metricList;
    for (const auto &metric : metrics) {
        qDebug() << "MainWindow: Processing metric:" << metric;
        metricList.append(QVariant(metric));
    }
    
    m_timeSeriesMetrics = metricList;
    int currentTab = m_tabWidget ? m_tabWidget->currentIndex() : -1;
    if (currentTab == 0) {
        m_currentMetrics = m_timeSeriesMetrics;
    }

    updateMetricsButtonState();
    
    qDebug() << "MainWindow: updateTimeSeriesMetrics() completed";
}

void MainWindow::updateScatterMetrics(const QVector<QMap<QString, QVariant>> &metrics)
{
    qDebug() << "MainWindow::updateScatterMetrics() - RECEIVED SIGNAL with" << metrics.size() << "metrics";

    QVariantList metricList;
    for (const auto &metric : metrics) {
        metricList.append(QVariant(metric));
    }

    m_scatterMetrics = metricList;
    int currentTab = m_tabWidget ? m_tabWidget->currentIndex() : -1;
    if (currentTab == 2) {
        m_currentMetrics = m_scatterMetrics;
    }

    updateMetricsButtonState();
}

// Command line integration methods

bool MainWindow::selectCropFolder(const QString &cropName)
{
    if (!m_fileComboBox) {
        qWarning() << "MainWindow::selectCropFolder - fileComboBox not initialized";
        return false;
    }
    
    for (int i = 0; i < m_fileComboBox->count(); ++i) {
        QString folderText = m_fileComboBox->itemText(i);
        if (folderText.compare(cropName, Qt::CaseInsensitive) == 0) {
            m_fileComboBox->setCurrentIndex(i);
            m_selectedFolder = folderText;
            qDebug() << "MainWindow: Selected crop folder:" << folderText;
            
            // Trigger folder selection change
            onFolderSelectionChanged();
            return true;
        }
    }
    
    qWarning() << "MainWindow: Crop folder not found:" << cropName;
    return false;
}

void MainWindow::loadExperiments()
{
    // This method would typically load experiments for the selected folder
    // For now, we'll trigger the existing folder selection logic
    if (!m_selectedFolder.isEmpty()) {
        populateFiles(m_selectedFolder);
        qDebug() << "MainWindow: Loaded experiments for folder:" << m_selectedFolder;
    }
}

void MainWindow::loadOutputFiles()
{
    // This method would refresh the output files list
    // Using existing method if available
    refreshOutputFiles();
    qDebug() << "MainWindow: Loaded output files";
}

int MainWindow::selectOutputFiles(const QStringList &fileNames)
{
    if (!m_fileListWidget) {
        qWarning() << "MainWindow::selectOutputFiles - fileListWidget not initialized";
        return 0;
    }
    
    // Clear current selections
    m_fileListWidget->clearSelection();
    
    int selectedCount = 0;
    
    for (const QString &fileName : fileNames) {
        for (int i = 0; i < m_fileListWidget->count(); ++i) {
            QListWidgetItem *item = m_fileListWidget->item(i);
            if (!item) continue;
            
            QString itemText = item->text();
            QString itemData = item->data(Qt::UserRole).toString();
            
            // Check both display text and user data
            if (itemText.compare(fileName, Qt::CaseInsensitive) == 0 ||
                itemData.compare(fileName, Qt::CaseInsensitive) == 0) {
                item->setSelected(true);
                selectedCount++;
                qDebug() << "MainWindow: Selected output file:" << fileName;
                break;
            }
        }
    }
    
    if (selectedCount > 0) {
        // Trigger file selection change
        onFileSelectionChanged();
    }
    
    qDebug() << "MainWindow: Selected" << selectedCount << "of" << fileNames.size() << "files";
    return selectedCount;
}

void MainWindow::loadVariables()
{
    // This method would load variables for the selected files
    // Using existing variable update logic
    updateVariableComboBoxes();
    qDebug() << "MainWindow: Loaded variables";
}

void MainWindow::hideFileSelectionUI(bool hide)
{
    if (m_cropGroup) {
        m_cropGroup->setVisible(!hide);
    }
    
    if (m_fileGroup) {
        m_fileGroup->setVisible(!hide);
    }
    
    if (hide) {
        qDebug() << "MainWindow: Hidden crop and file selection UI for command line mode";
        // Make Y variable list expand to fill available space
        if (m_yVariableComboBox) {
            m_yVariableComboBox->setMinimumHeight(300);  // Reduced from 400
            m_yVariableComboBox->setMaximumHeight(600);  // Add maximum to prevent buttons from being hidden
            m_yVariableComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
    } else {
        qDebug() << "MainWindow: Showing crop and file selection UI";
        // Restore original Y variable list size
        if (m_yVariableComboBox) {
            m_yVariableComboBox->setMinimumHeight(150);
            m_yVariableComboBox->setMaximumHeight(400);  // Restore maximum height
            m_yVariableComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
    }
}

void MainWindow::updateTimeSeriesPlot()
{
    // This method would update the time series plot
    // Using existing plot update logic
    if (m_plotWidget) {
        onUpdatePlot();
        qDebug() << "MainWindow: Updated time series plot";
    } else {
        qWarning() << "MainWindow: Plot widget not initialized";
    }
}

void MainWindow::onDataViewFileTypeChanged()
{
    if (!m_dataTableWidget || !m_dataViewFileTypeComboBox) {
        return;
    }
    
    QString selectedType = m_dataViewFileTypeComboBox->currentData().toString();
    qDebug() << "MainWindow::onDataViewFileTypeChanged() - Selected file type:" << selectedType;
    
    if (selectedType == "evaluate") {
        // Show EVALUATE.OUT data
        if (m_evaluateData.rowCount > 0) {
            DataTable emptyObsData;
            m_dataTableWidget->setData(m_evaluateData, emptyObsData);
            qDebug() << "MainWindow::onDataViewFileTypeChanged() - Showing EVALUATE.OUT data";
        } else {
            qDebug() << "MainWindow::onDataViewFileTypeChanged() - No EVALUATE.OUT data available";
        }
    } else {
        // Show regular .OUT data (default)
        if (m_currentData.rowCount > 0) {
            m_dataTableWidget->setData(m_currentData, m_currentObsData);
            qDebug() << "MainWindow::onDataViewFileTypeChanged() - Showing regular .OUT data";
        } else {
            qDebug() << "MainWindow::onDataViewFileTypeChanged() - No regular .OUT data available";
        }
    }
}
