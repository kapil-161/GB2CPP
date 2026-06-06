#include "MainWindow.h"
#include "DataTableWidget.h"
#include "PlotWidget.h"
#include "CDECodesDialog.h"
#include <QApplication>
#include <QSettings>
#include <QClipboard>
#include <QMessageBox>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QRegularExpression>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QTextBrowser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <functional>

// Subclass overrides virtual drag events directly — more reliable than event filter
// for QAbstractItemView which installs its own internal viewport filter.
// No Q_OBJECT needed; callback is a std::function set after construction.
class OutFileListWidget : public QListWidget
{
public:
    explicit OutFileListWidget(QWidget *parent = nullptr) : QListWidget(parent)
    {
        setAcceptDrops(true);
    }
    std::function<void(const QStringList &)> onDrop;

protected:
    static bool isDssatFile(const QString &ext) {
        // Accept .CSV, any O-extension (.OUT .OSU .OPG etc.),
        // and T files (3-char extension ending in 't', e.g. .wht .mzt .sot)
        if (ext == "csv") return true;
        if (!ext.isEmpty() && ext[0] == 'o') return true;
        if (ext.length() == 3 && ext[2] == 't' && ext[0].isLetter() && ext[1].isLetter())
            return true;
        return false;
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls()) {
            for (const QUrl &url : event->mimeData()->urls()) {
                if (isDssatFile(QFileInfo(url.toLocalFile()).suffix().toLower())) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
        event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        event->acceptProposedAction();
    }

    void dropEvent(QDropEvent *event) override
    {
        QStringList paths;
        for (const QUrl &url : event->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (isDssatFile(QFileInfo(path).suffix().toLower()) && QFileInfo::exists(path))
                paths << path;
        }
        if (onDrop && !paths.isEmpty())
            onDrop(paths);
        event->acceptProposedAction();
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainSplitter(nullptr)
    , m_tabWidget(nullptr)
    , m_dataTableWidget(nullptr)
    , m_plotWidget(nullptr)
    , m_scatterPlotWidget(nullptr)
    , m_statsTSWidget(nullptr)
    , m_statsScatterWidget(nullptr)
    , m_statsTSHeader(nullptr)
    , m_statsScatterHeader(nullptr)
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
    , m_suppressPlotClear(false)
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
        "* { color: #000000; font-family: Arial; }"
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
    resetInterface();

    // Show window first so it appears instantly
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setVisible(true);
    raise();
    activateWindow();

    // In CLI mode, skip populateFolders — the CLI handler calls selectCropFolder()
    // which adds only the target crop on demand, avoiding a full scan of all crops.
    // In GUI mode, defer until after the window has painted.
    if (QApplication::arguments().size() < 2)
        QTimer::singleShot(0, this, &MainWindow::populateFolders);
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

    QAction *savePlotAction = fileMenu->addAction("Save &Plot Data...");
    savePlotAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(savePlotAction, &QAction::triggered, this, &MainWindow::onSavePlotData);

    QAction *copyPlotDataAction = fileMenu->addAction("&Copy Plot Data");
    copyPlotDataAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(copyPlotDataAction, &QAction::triggered, this, &MainWindow::onCopyPlotData);

    fileMenu->addSeparator();
    
    QAction *exportAction = fileMenu->addAction("&Export Plot...");
    exportAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportPlot);

    QAction *exportRCodeAction = fileMenu->addAction("Export &R Code...");
    exportRCodeAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(exportRCodeAction, &QAction::triggered, this, &MainWindow::onExportRCode);

    QAction *copyPlotAction = fileMenu->addAction("&Copy Plot");
    copyPlotAction->setShortcut(QKeySequence("Ctrl+Shift+C"));  // Use Ctrl+Shift+C to avoid conflict with standard copy
    connect(copyPlotAction, &QAction::triggered, this, &MainWindow::onCopyPlot);

    QAction *copyMetricsAction = fileMenu->addAction("Copy &Metrics");
    copyMetricsAction->setShortcut(QKeySequence("Ctrl+Shift+M"));
    connect(copyMetricsAction, &QAction::triggered, this, &MainWindow::onCopyMetrics);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Help Menu
    QMenu *helpMenu = menuBar->addMenu("&Help");

    QAction *cdeCodesAction = helpMenu->addAction("CDE &Codes Reference...");
    cdeCodesAction->setStatusTip("Search and browse DSSAT variable codes (CDE, label, description)");
    connect(cdeCodesAction, &QAction::triggered, this, &MainWindow::onCDECodesReference);

    QAction *userManualAction = helpMenu->addAction("&User Manual...");
    connect(userManualAction, &QAction::triggered, this, &MainWindow::onUserManual);

    QAction *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    // Plot Settings — direct menu entry after Help
    QAction *plotSettingsAction = menuBar->addAction("Plot &Settings");
    plotSettingsAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(plotSettingsAction, &QAction::triggered, this, [this]() {
        bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);
        PlotWidget *active = (isScatterTab && m_scatterPlotWidget) ? m_scatterPlotWidget : m_plotWidget;
        if (active) active->onSettingsButtonClicked();
    });

}

void MainWindow::setupMainWidget()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(1);
    m_mainSplitter->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_mainSplitter);
    
    setupControlPanel();
    setupDataPanel();
    
    // Set splitter proportions
    m_mainSplitter->setSizes({200, 800});
    m_mainSplitter->setCollapsible(0, false);
}

void MainWindow::setupControlPanel()
{
    QWidget *controlPanel = new QWidget();
    controlPanel->setMaximumWidth(220);
    controlPanel->setMinimumWidth(150);
    
    // Create scroll area for sidebar
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);  // Remove border so top aligns with tab bar
    scrollArea->setContentsMargins(0, 0, 0, 0);

    QWidget *sidebarWidget = new QWidget();
    QVBoxLayout *controlLayout = new QVBoxLayout(sidebarWidget);
    controlLayout->setContentsMargins(8, 2, 8, 0);
    controlLayout->setSpacing(6);

    scrollArea->setWidget(sidebarWidget);

    QVBoxLayout *panelLayout = new QVBoxLayout(controlPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);
    panelLayout->addWidget(scrollArea, 1);
    
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
    m_fileGroupLabel = titleLabel;
    headerLayout->addStretch(1);
    
    
    m_fileGroup = new QGroupBox();
    QVBoxLayout *fileLayout = new QVBoxLayout(m_fileGroup);
    m_fileGroup->setTitle("");
    fileLayout->addWidget(fileGroupHeader);
    
    // File search
    QLineEdit *fileSearch = new QLineEdit();
    fileSearch->setPlaceholderText("Search output files...");
    fileLayout->addWidget(fileSearch);
    m_fileSearchWidget = fileSearch;

    // File list container with unselect button overlaid
    QWidget *fileContainer = new QWidget();
    QHBoxLayout *fileContainerLayout = new QHBoxLayout(fileContainer);
    fileContainerLayout->setContentsMargins(0, 0, 0, 0);
    fileContainerLayout->setSpacing(0);

    auto *outFileList = new OutFileListWidget();
    m_fileListWidget = outFileList;
    outFileList->onDrop = [this](const QStringList &paths) {
        for (const QString &p : paths) addDroppedOutFile(p);
    };
    m_fileListWidget->setSelectionMode(QListWidget::MultiSelection);
    m_fileListWidget->setMinimumHeight(120);
    m_fileListWidget->setMaximumHeight(120);
    m_fileListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fileListWidget->setToolTip("Select output files to plot.\nDrag & drop .OUT, .OSU, .OPG, .OVT, .OPT, .CSV files from Explorer to add them.");
    fileContainerLayout->addWidget(m_fileListWidget);

    m_unselectFilesButton = new QPushButton("×", m_fileListWidget);
    m_unselectFilesButton->setStyleSheet(
        "QPushButton { background-color: #ffcccc; border: none; padding: 0px; margin: 0px; font-size: 8px; }"
        "QPushButton:hover { background-color: #ff9999; border-radius: 3px; }"
    );
    m_unselectFilesButton->setToolTip("Unselect All");
    m_unselectFilesButton->setFixedSize(14, 14);
    m_unselectFilesButton->move(m_fileListWidget->width() - 16, 2);
    
    fileLayout->addWidget(fileContainer);
    m_fileContainerWidget = fileContainer;
    controlLayout->addWidget(m_fileGroup, 0);  // Stretch factor 0 = no expansion

    // Time Series Variables Group
    QGroupBox *timeSeriesGroup = new QGroupBox("Time Series Variables");
    QVBoxLayout *tsLayout = new QVBoxLayout(timeSeriesGroup);
    tsLayout->setContentsMargins(6, 6, 6, 6);
    tsLayout->setSpacing(4);
    
    // X Variable
    QLabel *xVarLabel = new QLabel("X Variable");
    xVarLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    tsLayout->addWidget(xVarLabel);
    m_xVariableComboBox = new QComboBox();
    tsLayout->addWidget(m_xVariableComboBox);

    // Y Variables
    QLabel *yVarLabel = new QLabel("Y Variables");
    yVarLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    tsLayout->addWidget(yVarLabel);
    
    // Y Variable search
    m_yVarSearch = new QLineEdit();
    m_yVarSearch->setPlaceholderText("Search Y variables...");
    tsLayout->addWidget(m_yVarSearch);
    
    // Y Variable list container with unselect button overlaid via absolute position
    QWidget *yVarContainer = new QWidget();
    QHBoxLayout *yVarContainerLayout = new QHBoxLayout(yVarContainer);
    yVarContainerLayout->setContentsMargins(0, 0, 0, 0);
    yVarContainerLayout->setSpacing(0);

    QListWidget *yVarListWidget = new QListWidget();
    yVarListWidget->setSelectionMode(QListWidget::MultiSelection);
    yVarContainerLayout->addWidget(yVarListWidget);

    // Overlay the X button in the top-right corner of the list widget
    m_unselectYVarsButton = new QPushButton("×", yVarListWidget);
    m_unselectYVarsButton->setStyleSheet(
        "QPushButton { background-color: #ffcccc; border: none; padding: 0px; margin: 0px; font-size: 8px; }"
        "QPushButton:hover { background-color: #ff9999; border-radius: 3px; }"
    );
    m_unselectYVarsButton->setToolTip("Unselect All");
    m_unselectYVarsButton->setFixedSize(14, 14);
    m_unselectYVarsButton->move(yVarListWidget->width() - 16, 2);
    
    tsLayout->addWidget(yVarContainer, 1);  // Stretch factor 1 = expands to fill space
    controlLayout->addWidget(timeSeriesGroup, 1);  // Stretch factor 1 = expands to fill space

    // Store references for later use
    m_yVariableComboBox = yVarListWidget;

    m_yVariableComboBox->setMinimumHeight(100);
    m_yVariableComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_treatmentComboBox = new QComboBox();  // Keep for compatibility

    m_metricsButton = nullptr; // Statistics shown in Statistics tab

    // Plot button pinned at bottom of panel, outside scroll area
    m_updatePlotButton = new QPushButton("Plot");
    m_updatePlotButton->setToolTip("Plot when on Time Series tab, or refresh data table when on Data View tab");
    m_updatePlotButton->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 0px; margin: 0px; border-radius: 0px; }"
        "QPushButton:hover { background-color: #0b7dda; }"
        "QPushButton:disabled { background-color: #aaaaaa; color: #dddddd; }"
    );
    m_updatePlotButton->setEnabled(false);
    m_updatePlotButton->setFixedHeight(34);
    panelLayout->addWidget(m_updatePlotButton);
    
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
    m_plotWidget = new PlotWidget();
    timeSeriesLayout->addWidget(m_plotWidget);
    
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
    m_dataViewFileTypeComboBox->addItem("Current Plot Data", "plot");
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
    m_scatterPlotWidget->setPreplotPanelVisible(false);
    scatterPlotLayout->addWidget(m_scatterPlotWidget);
    
    m_tabWidget->addTab(scatterPlotWidget, "Scatter Plot");

    // Statistics Tab (index 3)
    QWidget *statsWidget = new QWidget();
    QVBoxLayout *statsLayout = new QVBoxLayout(statsWidget);
    statsLayout->setContentsMargins(8, 8, 8, 8);
    statsLayout->setSpacing(8);

    // Time Series section
    m_statsTSHeader = new QLabel("Time Series Statistics");
    m_statsTSHeader->setStyleSheet("font-weight: bold; font-size: 13px; color: #1565C0; padding: 4px 0;");
    statsLayout->addWidget(m_statsTSHeader);
    m_statsTSWidget = new MetricsTableWidget();
    statsLayout->addWidget(m_statsTSWidget);

    // Separator
    QFrame *separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    statsLayout->addWidget(separator);

    // Scatter Plot section
    m_statsScatterHeader = new QLabel("Scatter Plot Statistics");
    m_statsScatterHeader->setStyleSheet("font-weight: bold; font-size: 13px; color: #1565C0; padding: 4px 0;");
    statsLayout->addWidget(m_statsScatterHeader);
    m_statsScatterWidget = new MetricsTableWidget();
    statsLayout->addWidget(m_statsScatterWidget);

    m_tabWidget->addTab(statsWidget, "Statistics");

    // Wrap tab widget + status widget in a container so status only spans plot area
    QWidget *dataPanel = new QWidget();
    QVBoxLayout *dataPanelLayout = new QVBoxLayout(dataPanel);
    dataPanelLayout->setContentsMargins(0, 0, 0, 0);
    dataPanelLayout->setSpacing(0);
    dataPanelLayout->addWidget(m_tabWidget, 1);

    m_statusWidget = new StatusWidget(this);
    // Place scaling label in the right half of the status bar
    if (m_plotWidget)
        m_statusWidget->setRightWidget(m_plotWidget->scalingLabel());
    // Embed inside PlotWidget's left layout so the status bar ends at the plot edge, not under the legend
    if (m_plotWidget)
        m_plotWidget->setBottomStatusWidget(m_statusWidget);

    m_mainSplitter->addWidget(dataPanel);
    
    // Connect tab changed signal
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
}

// Remove setupPlotPanel - integrated into setupDataPanel

void MainWindow::setupStatusBar()
{
    // m_statusWidget is created in setupDataPanel and embedded in the plot area
    // Hide the native status bar — we don't use it anymore
    statusBar()->hide();
    statusBar()->setMaximumHeight(0);

    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumWidth(200);
    m_progressBar->hide();

    m_statusLabel = new QLabel("Ready");
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
    
    if (m_plotWidget) {
        connect(m_plotWidget, &PlotWidget::refreshFilesRequested, this, &MainWindow::onRefreshFiles);
    }
    if (m_scatterPlotWidget) {
        connect(m_scatterPlotWidget, &PlotWidget::refreshFilesRequested, this, &MainWindow::onRefreshFiles);
    }

    // Connect Y variable search
    if (m_yVarSearch) {
        connect(m_yVarSearch, &QLineEdit::textChanged, this, &MainWindow::filterYVars);
    }
}

void MainWindow::onOpenFile()
{
    
    // Observed data must come from the same DSSATPRO-configured directory as simulated data
    QString cropDirectory;
    if (m_dataProcessor && m_fileComboBox && m_fileComboBox->currentText() != "No DSSAT folders found") {
        QString currentFolder = m_fileComboBox->currentText();
        cropDirectory = m_dataProcessor->getActualFolderPath(currentFolder);
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
    
    
    if (!fileName.isEmpty()) {
        loadFile(fileName);
    } else {
    }
}

void MainWindow::onSaveData()
{
    bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);

    // On scatter tab, save the EVALUATE data instead of time-series data
    if (isScatterTab) {
        if (m_evaluateData.rowCount == 0) {
            m_statusWidget->showWarning("No EVALUATE data to save");
            return;
        }
        QString fileName = QFileDialog::getSaveFileName(
            this, "Save EVALUATE Data",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            "CSV Files (*.csv);;All Files (*)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_statusWidget->showError("Failed to open file for writing");
            return;
        }
        QTextStream out(&file);
        out << m_evaluateData.columnNames.join(",") << "\n";
        for (int row = 0; row < m_evaluateData.rowCount; ++row) {
            QStringList vals;
            for (const QString &col : m_evaluateData.columnNames) {
                QString v = m_evaluateData.getValue(row, col).toString();
                if (v.contains(',') || v.contains('"') || v.contains('\n'))
                    v = "\"" + v.replace("\"", "\"\"") + "\"";
                vals << v;
            }
            out << vals.join(",") << "\n";
        }
        file.close();
        m_statusWidget->showSuccess("Saved: " + QFileInfo(fileName).fileName());
        return;
    }

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

    if (fileName.isEmpty())
        return;

    // Helper: write a DataTable to a file path, returns error string or empty on success
    auto writeCSV = [](const QString &path, const DataTable &table) -> QString {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return "Failed to open file for writing: " + path;

        QTextStream out(&file);
        out << table.columnNames.join(",") << "\n";

        for (int row = 0; row < table.rowCount; ++row) {
            QStringList rowValues;
            for (const QString &colName : table.columnNames) {
                QString str = table.getValue(row, colName).toString();
                if (str.contains(',') || str.contains('"') || str.contains('\n'))
                    str = "\"" + str.replace("\"", "\"\"") + "\"";
                rowValues << str;
            }
            out << rowValues.join(",") << "\n";
        }
        file.close();
        return {};
    };

    // Build suffixed paths: strip extension, append suffix, re-add extension
    QFileInfo fi(fileName);
    QString base = fi.path() + "/" + fi.completeBaseName();
    QString ext  = fi.suffix().isEmpty() ? "csv" : fi.suffix();

    QString simPath = base + "_simulated." + ext;
    QString obsPath = base + "_observed."  + ext;

    QString simError = writeCSV(simPath, m_currentData);
    if (!simError.isEmpty()) {
        m_statusWidget->showError(simError);
        return;
    }

    if (m_currentObsData.rowCount > 0) {
        QString obsError = writeCSV(obsPath, m_currentObsData);
        if (!obsError.isEmpty()) {
            m_statusWidget->showError(obsError);
            return;
        }
        m_statusWidget->showSuccess("Saved: " + QFileInfo(simPath).fileName() +
                                    " and " + QFileInfo(obsPath).fileName());
    } else {
        m_statusWidget->showSuccess("Saved: " + QFileInfo(simPath).fileName());
    }
}

void MainWindow::onSavePlotData()
{
    bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);

    QString csv;
    if (isScatterTab) {
        csv = m_scatterPlotWidget ? m_scatterPlotWidget->getScatterCSV() : QString();
    } else {
        csv = m_plotWidget ? m_plotWidget->getPlotCSV() : QString();
    }

    if (csv.isEmpty()) {
        m_statusWidget->showWarning("No plot data to save — generate a plot first");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this, "Save Plot Data",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "CSV Files (*.csv);;Text Files (*.txt);;All Files (*)");

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_statusWidget->showError("Failed to open file for writing: " + fileName);
        return;
    }
    QTextStream(&file) << csv;
    file.close();
    m_statusWidget->showSuccess("Plot data saved: " + QFileInfo(fileName).fileName());
}

void MainWindow::onCopyPlotData()
{
    bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);

    QString csv;
    if (isScatterTab) {
        csv = m_scatterPlotWidget ? m_scatterPlotWidget->getScatterCSV() : QString();
    } else {
        csv = m_plotWidget ? m_plotWidget->getPlotCSV() : QString();
    }

    if (csv.isEmpty()) {
        m_statusWidget->showWarning("No plot data to copy — generate a plot first");
        return;
    }

    QGuiApplication::clipboard()->setText(csv);
    m_statusWidget->showSuccess("Plot data copied to clipboard");
}

void MainWindow::onExportPlot()
{
    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Plot",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "PNG Files (*.png);;JPG Files (*.jpg);;PDF Files (*.pdf);;All Files (*)",
        &selectedFilter
    );

    if (fileName.isEmpty()) return;

    // Determine format and ensure extension is present
    QString format = "PNG";
    QString ext;
    if (selectedFilter.contains("pdf", Qt::CaseInsensitive))      { format = "PDF"; ext = ".pdf"; }
    else if (selectedFilter.contains("jpg", Qt::CaseInsensitive)) { format = "JPG"; ext = ".jpg"; }
    else                                                           { format = "PNG"; ext = ".png"; }

    QFileInfo fi(fileName);
    if (fi.suffix().isEmpty())
        fileName += ext;


    bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);
    PlotWidget *activeWidget = (isScatterTab && m_scatterPlotWidget) ? m_scatterPlotWidget : m_plotWidget;
    if (activeWidget) {
        activeWidget->exportPlot(fileName, format);
        m_statusWidget->showSuccess("Plot exported successfully");
    }
}

void MainWindow::onExportRCode()
{
    if (!m_plotWidget) return;

    bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);
    if (isScatterTab) {
        m_statusWidget->showWarning("R code export is available for Time Series plots only");
        return;
    }

    QString rCode = m_plotWidget->getPlotRCode();
    if (rCode.isEmpty()) {
        m_statusWidget->showWarning("No plot data — generate a time series plot first");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this, "Export R Code",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "R Scripts (*.R);;All Files (*)");

    if (fileName.isEmpty()) return;

    QFileInfo fi(fileName);
    if (fi.suffix().isEmpty())
        fileName += ".R";

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_statusWidget->showError("Failed to write file: " + fileName);
        return;
    }
    QTextStream(&file) << rCode;
    file.close();
    m_statusWidget->showSuccess("R code saved: " + QFileInfo(fileName).fileName());
}

void MainWindow::onCopyPlot()
{
    bool isScatterTab = (m_tabWidget && m_tabWidget->currentIndex() == 2);
    PlotWidget *activeWidget = (isScatterTab && m_scatterPlotWidget) ? m_scatterPlotWidget : m_plotWidget;
    if (activeWidget) {
        activeWidget->copyPlotToClipboard();
        m_statusWidget->showSuccess("Plot copied to clipboard");
    }
}

void MainWindow::onCopyMetrics()
{
    if (m_currentMetrics.isEmpty()) {
        showWarning("No metrics data available to copy.");
        return;
    }

    bool isScatterPlot = (m_tabWidget && m_tabWidget->currentIndex() == 2);
    // Use a temporary MetricsTableWidget to copy — reuses its model and copy logic
    MetricsTableWidget *tmp = new MetricsTableWidget(this);
    tmp->setMetrics(m_currentMetrics, isScatterPlot);
    tmp->copyMetrics();
    tmp->deleteLater();

    m_statusWidget->showSuccess("Metrics copied to clipboard");
}


void MainWindow::onUserManual()
{
    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle("User Manual");
    dlg->resize(820, 640);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);

    QTextBrowser *browser = new QTextBrowser(dlg);
    browser->setOpenExternalLinks(true);
    browser->setHtml(R"(
<html><body style="font-family:Arial,sans-serif; font-size:13px; margin:16px; color:#222;">

<h1 style="color:#1565C0;">GB2 — User Manual</h1>
<p>GB2 is a visualization tool for DSSAT crop model output files. Use the left panel to browse and load output files, then configure X/Y variables and click <b>Plot</b>.</p>
<hr/>

<h2 style="color:#1565C0;">1. Loading Files</h2>
<ul>
  <li>Use the <b>Crop</b> dropdown to select a DSSAT crop folder — the outfile list updates automatically.</li>
  <li>Click <b>Open File</b> or use <b>File → Open</b> to load any DSSAT output file directly.</li>
  <li>Supported file types: <code>.OUT</code>, <code>.OSU</code>, <code>.OPG</code>, <code>.CSV</code>, <code>EVALUATE.OUT</code>, and observed T files (<code>.WHT</code>, <code>.MZT</code>, <code>.SOT</code>, …).</li>
  <li>Select multiple files in the outfile list to overlay their data on the same plot.</li>
  <li>EVALUATE.OUT files automatically switch to the <b>Scatter Plot</b> tab.</li>
  <li><b>Drag &amp; drop</b> — drag one or more DSSAT output files from Windows Explorer directly onto the outfile list.
    Supported extensions: <code>.OUT</code>, <code>.OSU</code>, <code>.OPG</code>, <code>.OVT</code>, <code>.OPT</code>, <code>.CSV</code>, all other O-extension files, and <b>observed T files</b> (<code>.WHT</code>, <code>.MZT</code>, <code>.SOT</code>, etc.).
    <ul>
      <li>If the file belongs to a known DSSAT crop folder, the crop selector switches automatically and the file is selected in the populated list.</li>
      <li>Files from outside DSSAT folders are added as standalone entries (shown in italic).</li>
      <li>T files are <em>not</em> shown in the outfile list — they are only accessible via drag &amp; drop or the command line.</li>
    </ul>
  </li>
  <li>Use the <b>Search</b> box above the outfile list to filter by filename.</li>
  <li>Click the <b>X</b> button beside the list to deselect all files at once.</li>
</ul>

<h2 style="color:#1565C0;">2. Time Series Tab</h2>
<ul>
  <li>Select an <b>X variable</b> (e.g. DATE, DAS, DAP) from the dropdown; use the <b>DAS / DAP / DATE</b> quick-switch buttons.</li>
  <li>Select one or more <b>Y variables</b> from the list (use the search box to filter).</li>
  <li>Click <b>Plot</b> to render the plot.</li>
  <li>The <b>Treatments</b> panel lets you enable/disable individual treatments before plotting.</li>
  <li>Click a legend entry to highlight that series; click again to reset.</li>
  <li>When a DATE x-axis has gaps between discontiguous periods, <b>axis breaks</b> collapse the gap automatically with a visible break mark.</li>
</ul>

<h3 style="color:#1976D2;">2a. Multi-Panel Time Series</h3>
<ul>
  <li>Enable <b>Multi-Panel Grid</b> in <b>Plot Settings</b> to display one panel per selected Y variable instead of overlaying all series on one chart.</li>
  <li>Each panel shares the same X axis and is synchronized — zooming or panning one panel affects all.</li>
  <li>Panels scroll vertically when more than 6 Y variables are selected.</li>
  <li>Error bars, axis breaks, hover tooltips, and double-click zoom all work per-panel.</li>
</ul>

<h3 style="color:#1976D2;">2b. Chart Interaction</h3>
<ul>
  <li><b>Scroll wheel</b> — zoom in/out on the chart.</li>
  <li><b>Right-click drag</b> — pan the chart.</li>
  <li><b>Double-click</b> — zoom in; double-click again to reset to full extent.</li>
  <li><b>Hover</b> — shows a tooltip with the variable name and value at the nearest data point.</li>
  <li><b>Legend</b> — the legend is a floating, draggable panel. Drag it anywhere on the chart. Click the pin icon to dock it back inside the chart area.</li>
</ul>

<h2 style="color:#1565C0;">3. Observed T Files</h2>
<p>T files contain field-measured time-series data — the observed counterpart of simulated <code>.OUT</code> files. Each crop has its own extension: <code>.WHT</code> (wheat), <code>.MZT</code> (maize), <code>.SOT</code> (soybean), <code>.RIT</code> (rice), etc.</p>
<ul>
  <li><b>How to load:</b> drag the T file (e.g. <code>KSAS8101.WHT</code>) from Windows Explorer and drop it onto the GB2 outfile list. T files do not appear in the outfile list — they are only accessible via drag &amp; drop or the command line.</li>
  <li><b>Plot style:</b> T file data is plotted as <b>scatter points</b> (not lines), reflecting its nature as discrete field observations.</li>
  <li><b>Treatment names:</b> GB2 automatically reads the corresponding <code>.X</code> experiment file (same basename, last extension letter <code>T→X</code>, e.g. <code>KSAS8101.WHX</code>) to retrieve treatment names from the <code>*TREATMENTS</code> section. These names appear in the plot legend.</li>
  <li><b>Date format:</b> T file dates are stored in YYDDD format (5-digit: 2-digit year + 3-digit day-of-year). GB2 converts these automatically to calendar dates for the X axis.</li>
  <li><b>Variables:</b> after loading, the variable lists populate from the T file columns. Select DATE (or another date column) as X and any measured variable as Y, then click <b>Plot</b>.</li>
  <li><b>Command line:</b> pass the T filename as a positional argument:
    <pre style="background:#F5F5F5; padding:6px; border-radius:4px;">GB2.exe C:/DSSAT48 C:/DSSAT48/Wheat KSAS8101.WHT --xvar DATE --yvar GWAD</pre>
  </li>
</ul>

<h2 style="color:#1565C0;">4. OSU Seasonal Summary Files</h2>
<ul>
  <li>OSU files contain one row per treatment × year. The default X axis is <b>WYEAR</b>.</li>
  <li>Use the <b>Box Plot</b> button to switch between line plot and box plot view.</li>
  <li><b>Sequence OSU</b> files (rotation experiments) show one series per rotation slot (R#).</li>
  <li>In Plot Settings, enable <b>Plot Mean of Replicates</b> to average replicates into one line per slot.</li>
</ul>

<h2 style="color:#1565C0;">5. Scatter Plot Tab</h2>
<ul>
  <li>Load an <code>EVALUATE.OUT</code> file — the app auto-switches to this tab.</li>
  <li>Simulated vs. observed values are plotted with a 1:1 reference line.</li>
  <li>Statistics (RMSE, R², d-stat, BIAS, N) are shown inside each panel.</li>
  <li>Configure which stats are shown via <b>Plot Settings → Scatter Panel Metrics</b>.</li>
  <li>Click <b>Statistics</b> to open a detailed metrics table for all variables.</li>
</ul>

<h2 style="color:#1565C0;">6. Data View Tab</h2>
<ul>
  <li>Displays the raw loaded data as a sortable table.</li>
  <li>Use the dropdown to switch between Regular .OUT data, EVALUATE.OUT data, or Current Plot Data.</li>
</ul>

<h2 style="color:#1565C0;">7. Saving &amp; Exporting</h2>
<table border="1" cellpadding="4" cellspacing="0" style="border-collapse:collapse;">
  <tr style="background:#E3F2FD;"><th>Action</th><th>Shortcut</th><th>Description</th></tr>
  <tr><td>Save Data</td><td>Ctrl+S</td><td>Saves simulated and observed data as CSV</td></tr>
  <tr><td>Save Plot Data</td><td>Ctrl+Shift+S</td><td>Saves merged plot data as a single CSV</td></tr>
  <tr><td>Export Plot Image</td><td>Ctrl+E</td><td>Exports the current plot as PNG/JPG/PDF</td></tr>
  <tr><td>Export R Code</td><td>Ctrl+Shift+R</td><td>Saves a ggplot2 R script that reproduces the current time series plot</td></tr>
  <tr><td>Copy Plot</td><td>Ctrl+Shift+C</td><td>Copies the plot image to the clipboard</td></tr>
  <tr><td>Copy Metrics</td><td>Ctrl+Shift+M</td><td>Copies metrics table as tab-separated text</td></tr>
</table>

<h2 style="color:#1565C0;">8. Plot Settings</h2>
<ul>
  <li><b>General:</b> grid lines, legend visibility, axis titles, tick spacing</li>
  <li><b>Style:</b> line width, marker size, color palette</li>
  <li><b>Axes:</b> custom min/max overrides for X and Y</li>
  <li><b>Error Bars:</b> SD or SE bars for replicated experiments (observed data points)</li>
  <li><b>Multi-Panel Grid:</b> one panel per Y variable instead of overlaid lines</li>
  <li><b>Hover Tooltip:</b> show/hide variable + value tooltip on mouse hover</li>
  <li><b>Plot Mean of Replicates:</b> average replicates (sequence OSU files)</li>
  <li><b>Scatter Panel Metrics:</b> choose which statistics appear inside each scatter panel</li>
</ul>

<h2 style="color:#1565C0;">9. Keyboard &amp; Mouse Shortcuts</h2>
<table border="1" cellpadding="4" cellspacing="0" style="border-collapse:collapse;">
  <tr style="background:#E3F2FD;"><th>Shortcut</th><th>Action</th></tr>
  <tr><td>Ctrl+O</td><td>Open file</td></tr>
  <tr><td>Ctrl+S</td><td>Save data CSV</td></tr>
  <tr><td>Ctrl+Shift+S</td><td>Save plot data CSV</td></tr>
  <tr><td>Ctrl+E</td><td>Export plot image</td></tr>
  <tr><td>Ctrl+Shift+R</td><td>Export ggplot2 R code</td></tr>
  <tr><td>Ctrl+Shift+C</td><td>Copy plot to clipboard</td></tr>
  <tr><td>Scroll wheel</td><td>Zoom in / out on chart</td></tr>
  <tr><td>Right-click drag</td><td>Pan chart</td></tr>
  <tr><td>Double-click</td><td>Zoom in; double-click again to reset</td></tr>
  <tr><td>Drag legend</td><td>Move floating legend anywhere on chart</td></tr>
</table>

<h2 style="color:#1565C0;">10. DSSAT File Types</h2>
<table border="1" cellpadding="4" cellspacing="0" style="border-collapse:collapse;">
  <tr style="background:#E3F2FD;"><th>Extension</th><th>Type</th><th>Plot Style</th><th>Default X Axis</th></tr>
  <tr><td>.OUT</td><td>Time series (daily/growth stage)</td><td>Line (simulated)</td><td>DATE / DAS / DAP</td></tr>
  <tr><td>.OSU</td><td>Seasonal summary</td><td>Line / Box</td><td>WYEAR</td></tr>
  <tr><td>.OPG</td><td>Plant growth</td><td>Line (simulated)</td><td>DATE</td></tr>
  <tr><td>.CSV</td><td>Comma-separated output</td><td>Line (simulated)</td><td>DATE</td></tr>
  <tr><td>EVALUATE.OUT</td><td>Simulated vs. Observed</td><td>Scatter (1:1 line)</td><td>— (scatter)</td></tr>
  <tr><td>.WHT, .MZT, .SOT, … (T files)</td><td>Observed field measurements</td><td>Scatter (observed)</td><td>DATE</td></tr>
</table>

<h2 style="color:#1565C0;">11. CDE Variable Reference</h2>
<p>Use <b>Help → CDE Codes Reference</b> to search and browse DSSAT variable codes, labels, and descriptions from the DSSAT CDE files.</p>

<h2 style="color:#1565C0;">12. Command-Line / Headless Mode</h2>
<p>GB2 can be driven from the terminal or called by DSSAT directly, with no manual interaction required.</p>

<h3 style="color:#1976D2;">Basic invocation (called by DSSAT)</h3>
<pre style="background:#F5F5F5; padding:8px; border-radius:4px;">GB2.exe &lt;DSSATBase&gt; &lt;CropDir&gt; [file1.OUT] [file2.OUT] ...</pre>
<ul>
  <li><code>&lt;DSSATBase&gt;</code> — root DSSAT installation path (e.g. <code>C:/DSSAT48</code>)</li>
  <li><code>&lt;CropDir&gt;</code> — crop output folder (e.g. <code>C:/DSSAT48/Maize</code>)</li>
  <li>Optional positional args — specific <code>.OUT</code> files to pre-select</li>
</ul>

<h3 style="color:#1976D2;">Optional flags</h3>
<table border="1" cellpadding="4" cellspacing="0" style="border-collapse:collapse;">
  <tr style="background:#E3F2FD;"><th>Flag</th><th>Argument</th><th>Description</th></tr>
  <tr><td><code>--xvar</code></td><td><code>VARNAME</code></td><td>Pre-select the X axis variable (e.g. <code>DAS</code>)</td></tr>
  <tr><td><code>--yvar</code></td><td><code>VAR1,VAR2</code></td><td>Pre-select one or more Y variables (comma-separated)</td></tr>
  <tr><td><code>--save</code></td><td><code>plot.png</code></td><td>Render and save the plot image, then exit (headless)</td></tr>
  <tr><td><code>--metrics</code></td><td><code>metrics.csv</code></td><td>Save time-series metrics to CSV (use with <code>--save</code>)</td></tr>
  <tr><td><code>--boxplot</code></td><td>—</td><td>Render as a box plot (OSU seasonal files)</td></tr>
  <tr><td><code>--scatter</code></td><td>—</td><td>Headless scatter plot mode (requires EVALUATE.OUT)</td></tr>
  <tr><td><code>--scatter-vars</code></td><td><code>VAR1,VAR2</code></td><td>Limit scatter panels to these variables</td></tr>
  <tr><td><code>--scatter-metrics</code></td><td><code>RMSE,R2</code></td><td>Override which statistics appear in scatter panels</td></tr>
  <tr><td><code>-v</code></td><td>—</td><td>Verbose debug output to console</td></tr>
</table>

<h3 style="color:#1976D2;">Examples</h3>
<pre style="background:#F5F5F5; padding:8px; border-radius:4px; white-space:pre-wrap;">
# Open GB2 with Maize folder pre-selected
GB2.exe C:/DSSAT48 C:/DSSAT48/Maize

# Pre-select files and variables, open interactively
GB2.exe C:/DSSAT48 C:/DSSAT48/Maize GROWTH.OUT --xvar DAS --yvar LAID,CWAD

# Save plot image without showing the window (headless)
GB2.exe C:/DSSAT48 C:/DSSAT48/Maize GROWTH.OUT --xvar DAS --yvar LAID --save growth.png

# Headless + save metrics CSV
GB2.exe C:/DSSAT48 C:/DSSAT48/Maize GROWTH.OUT --xvar DAS --yvar LAID --save growth.png --metrics metrics.csv

# Headless scatter plot from EVALUATE.OUT
GB2.exe C:/DSSAT48/Maize --scatter --scatter-vars HWAH,CWAH --save scatter.png

# Load and interactively plot an observed T file (wheat)
GB2.exe C:/DSSAT48 C:/DSSAT48/Wheat KSAS8101.WHT --xvar DATE --yvar GWAD

# Headless save of a T file plot
GB2.exe C:/DSSAT48 C:/DSSAT48/Wheat KSAS8101.WHT --xvar DATE --yvar GWAD --save tfile.png
</pre>

<p><b>Note:</b> When <code>--save</code> is used, GB2 renders the plot and exits automatically. Relative output paths are resolved against the terminal's working directory at the time GB2 was launched.</p>

</body></html>
    )");

    layout->addWidget(browser);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::accept);
    layout->addWidget(buttons);

    dlg->show();
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

void MainWindow::onCDECodesReference()
{
    CDECodesDialog *dialog = new CDECodesDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
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
    if (!m_suppressPlotClear && xVar.isEmpty()) {
        // Clear plots and metrics when X variable is unselected
        if (m_plotWidget) {
            m_plotWidget->clear();
        }
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->clear();
        }
        clearMetrics();
        return;
    }

    // Show prompt message based on current tab
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        m_statusWidget->showInfo("X variable changed. Click 'Plot' to update the time series plot");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        m_statusWidget->showInfo("X variable changed. Click 'Data' to update the data table");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
        m_statusWidget->showInfo("X variable changed. Click 'Plot' to update the scatter plot");
    }
    // Don't auto-plot, wait for manual refresh
}

void MainWindow::onYVariableChanged()
{
    // Mark that variable selection has changed
    m_variableSelectionChanged = true;
    markDataNeedsRefresh();

    // Check if no Y variables are selected and clear plots and metrics if so
    if (!m_suppressPlotClear && m_yVariableComboBox && m_yVariableComboBox->selectedItems().isEmpty()) {
        // Clear plots and metrics when Y variables are unselected
        if (m_plotWidget) {
            m_plotWidget->clear();
        }
        if (m_scatterPlotWidget) {
            m_scatterPlotWidget->clear();
        }
        clearMetrics();
        return;
    }

    // Show treatment selection panel so user can review before refreshing
    if (m_plotWidget) m_plotWidget->showTreatmentSelection();

    // Show prompt message based on current tab
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        m_statusWidget->showInfo("Y variable selection changed. Click 'Plot' to update the time series plot");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        m_statusWidget->showInfo("Y variable selection changed. Click 'Data' to update the data table");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
        m_statusWidget->showInfo("Y variable selection changed. Click 'Plot' to update the scatter plot");
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
        m_statusWidget->showInfo("Treatment selection changed. Click 'Plot' to update the time series plot");
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        m_statusWidget->showInfo("Treatment selection changed. Click 'Data' to update the data table");
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
    
    // Check which tab we're on and perform appropriate action
    if (m_tabWidget && m_tabWidget->currentIndex() == 0) {
        // Time Series tab - refresh time series plot
        updatePlot();
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
        // Data View tab - refresh data table based on selected file type
        onDataViewFileTypeChanged();
        m_dataNeedsRefresh = false;
    } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
        // Scatter Plot tab - refresh scatter plot
        updateScatterPlot();
    }
    
    // Mark data as refreshed
    m_dataNeedsRefresh = false;
    m_variableSelectionChanged = false;
}

void MainWindow::onTabChanged(int index)
{
    // Save Y/X selections when leaving Time Series tab (index 0)
    static int s_previousTab = 0;
    if (s_previousTab == 0 && index != 0 && m_yVariableComboBox) {
        m_savedYSelections.clear();
        for (int i = 0; i < m_yVariableComboBox->count(); ++i) {
            QListWidgetItem *it = m_yVariableComboBox->item(i);
            if (it && it->isSelected())
                m_savedYSelections << it->data(Qt::UserRole).toString();
        }
        m_savedXVar = m_xVariableComboBox ? m_xVariableComboBox->currentData().toString() : QString();
    }
    s_previousTab = index;

    // Handle tab switching - load data lazily like Python version
    if (index == 0) {
        // Time Series tab - don't auto-plot, wait for manual refresh
        if (m_updatePlotButton) {
            m_updatePlotButton->setText("Plot");
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
        m_suppressPlotClear = true;
        updateVariableComboBoxes();

        // Restore Y/X selections that were saved when we left this tab
        if (!m_savedYSelections.isEmpty() && m_yVariableComboBox) {
            for (int i = 0; i < m_yVariableComboBox->count(); ++i) {
                QListWidgetItem *it = m_yVariableComboBox->item(i);
                if (it && m_savedYSelections.contains(it->data(Qt::UserRole).toString()))
                    it->setSelected(true);
            }
        }
        if (!m_savedXVar.isEmpty() && m_xVariableComboBox) {
            int xi = m_xVariableComboBox->findData(m_savedXVar);
            if (xi != -1)
                m_xVariableComboBox->setCurrentIndex(xi);
        }
        m_suppressPlotClear = false;

        // Show prompt message if files are selected but plot needs refresh
        QList<QListWidgetItem*> selectedItems = m_fileListWidget ? m_fileListWidget->selectedItems() : QList<QListWidgetItem*>();
        if (!selectedItems.isEmpty() && (m_currentData.rowCount == 0 || m_variableSelectionChanged || m_dataNeedsRefresh)) {
            m_statusWidget->showInfo("Click 'Plot' to view the time series plot with current selections");
        } else if (selectedItems.isEmpty()) {
            m_statusWidget->showInfo("Click outfile and variables, then click 'Refresh Plot' to view time series");
        }

    } else if (index == 1) {
        // Data View tab
        if (m_updatePlotButton) {
            m_updatePlotButton->setText("Data");
        }

        // Update file type selector based on available data
        if (m_dataViewFileTypeComboBox) {
            bool hasRegular = (m_currentData.rowCount > 0);
            bool hasEvaluate = (m_evaluateData.rowCount > 0);
            bool hasPlot = m_plotWidget && !m_plotWidget->getPlotCSV().isEmpty();
            m_dataViewFileTypeComboBox->setEnabled(hasRegular || hasEvaluate || hasPlot);
            if (hasPlot)
                m_dataViewFileTypeComboBox->setCurrentIndex(2); // Current Plot Data
            else if (!hasRegular && hasEvaluate)
                m_dataViewFileTypeComboBox->setCurrentIndex(1); // EVALUATE.OUT
            else
                m_dataViewFileTypeComboBox->setCurrentIndex(0); // Regular .OUT
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
            m_updatePlotButton->setText("Plot");
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
        m_suppressPlotClear = true;
        updateVariableComboBoxes();
        m_suppressPlotClear = false;
        
        // Show prompt message if files are selected but plot needs refresh
        QList<QListWidgetItem*> selectedItems = m_fileListWidget ? m_fileListWidget->selectedItems() : QList<QListWidgetItem*>();
        if (!selectedItems.isEmpty() && (m_evaluateData.rowCount == 0 || m_variableSelectionChanged || m_dataNeedsRefresh)) {
            m_statusWidget->showInfo("Click 'Plot' to view the scatter plot with current selections");
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
    resetInterface(); // Clear existing data before loading new file
    
    try {
        m_currentFilePath = filePath;
        m_statusWidget->showInfo("Loading file...");
        m_progressBar->setRange(0, 0);  // Indeterminate
        m_progressBar->show();
        
        // Check if file exists
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            m_statusWidget->showError("File does not exist: " + filePath);
            m_progressBar->hide();
            return;
        }
        
        if (!fileInfo.isReadable()) {
            m_statusWidget->showError("File is not readable: " + filePath);
            m_progressBar->hide();
            return;
        }
        
        
        // Load file in data processor with error handling
        bool readResult = m_dataProcessor->readFile(filePath, m_currentData);
        
        if (readResult) {
            // File loaded successfully - manually call onDataProcessed since signal might not be emitted
            onDataProcessed(QString("Successfully loaded observed data: %1 rows, %2 columns")
                          .arg(m_currentData.rowCount)
                          .arg(m_currentData.columns.size()));
        } else {
            m_statusWidget->showError("Failed to read file: " + filePath);
            m_progressBar->hide();
        }
        
    } catch (const std::exception& e) {
        m_statusWidget->showError("Error loading file: " + QString(e.what()));
        m_progressBar->hide();
    } catch (...) {
        m_statusWidget->showError("Unknown error loading file");
        m_progressBar->hide();
    }
}

void MainWindow::updateVariableComboBoxes()
{
    if (m_xVariableComboBox) m_xVariableComboBox->blockSignals(true);
    if (m_yVariableComboBox) m_yVariableComboBox->blockSignals(true);

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

    auto unblockAll = [&]() {
        if (m_xVariableComboBox) m_xVariableComboBox->blockSignals(false);
        if (m_yVariableComboBox) m_yVariableComboBox->blockSignals(false);
    };

    if (isScatterTab) {
        isEvaluateFile = (m_evaluateData.rowCount > 0);
        if (!isEvaluateFile) {
            unblockAll();
            return;
        }
    } else {
        isEvaluateFile = false;
        if (m_currentData.rowCount == 0) {
            unblockAll();
            return;
        }
    }
    
    // For EVALUATE.OUT files (scatter plot tab): show base variable names (e.g. "ADAP", "CWAM")
    // in the Y list as a multi-select. X combo is hidden/unused for scatter.
    if (isEvaluateFile) {
        if (m_yVariableComboBox)
            m_yVariableComboBox->setSelectionMode(QListWidget::MultiSelection);

        // Collect base names that have both a simulated (S) and measured (M) column with valid data
        QStringList baseNames;
        QMap<QString, QString> baseToSim, baseToMeas; // uppercase base -> actual column name

        for (const QString &colName : m_evaluateData.columnNames) {
            QString upper = colName.toUpper();
            if (upper.length() <= 1) continue;
            if (upper.endsWith("S"))
                baseToSim[upper.left(upper.length() - 1)] = colName;
            else if (upper.endsWith("M"))
                baseToMeas[upper.left(upper.length() - 1)] = colName;
        }

        // Metadata columns that happen to end with S or M — exclude them
        static const QSet<QString> kExclude = {
            "RUN", "RUNNO", "TRNO", "EXPNO", "EXPERIMENT", "TREATMENT",
            "TRTNO", "TRT", "EXP", "EXCODE", "CR", "RN", "REP", "EXNAM"
        };

        for (auto it = baseToSim.begin(); it != baseToSim.end(); ++it) {
            QString base = it.key();
            if (kExclude.contains(base)) continue;
            if (!baseToMeas.contains(base)) continue;

            // Check both columns have at least one valid (non-missing, non-negative) value
            auto hasValid = [&](const QString &cn) {
                const DataColumn *col = m_evaluateData.getColumn(cn);
                if (!col) return false;
                for (const QVariant &v : col->data) {
                    if (DataProcessor::isMissingValue(v)) continue;
                    bool ok; double d = v.toDouble(&ok);
                    if (ok && d >= 0) return true;
                }
                return false;
            };
            if (!hasValid(it.value()) || !hasValid(baseToMeas[base])) continue;

            baseNames.append(base);
        }
        baseNames.sort();

        // Clear X combo (not used for scatter), hide label
        m_xVariableComboBox->clear();
        m_xVariableComboBox->setVisible(false);

        // Populate Y list with base names
        for (const QString &base : baseNames) {
            QPair<QString, QString> info = DataProcessor::getVariableInfo(base);
            QString label = info.first.isEmpty() ? base : QString("%1 (%2)").arg(info.first, base);
            QListWidgetItem *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, base); // store base name (e.g. "ADAP")
            m_yVariableComboBox->addItem(item);
        }

        unblockAll();
        return;
    }
    
    // For non-EVALUATE files (time series), allow multiple Y variable selection
    if (m_yVariableComboBox)
        m_yVariableComboBox->setSelectionMode(QListWidget::MultiSelection);

    // Restore X combo visibility (may have been hidden on scatter tab)
    if (m_xVariableComboBox)
        m_xVariableComboBox->setVisible(true);

    // Variables to exclude from Y variable list (these are typically X-axis or grouping variables)
    QStringList yVariableExclusions = {"YEAR", "WYEAR", "HYEAR", "RUNNO", "RUN", "CR", "FILEX",
                                       "EXPERIMENT", "DAS", "DAP", "DOY", "DATE", "TRT", "CROP", "TNAME",
                                       "R#", "O#", "P#", "MODEL", "XLAT", "LONG", "ELEV", "WSTA", "FNAM",
                                       "SOIL_ID", "EXNAME", "__SRCFILE__"};

    QStringList commonVariables;
    QStringList simOnlyVariables;

    // Identify common variables and simulated-only variables
    // Only skip internal/synthetic columns here; yVariableExclusions is applied later when adding to Y list
    for (const QString &columnName : m_currentData.columnNames) {
        if (columnName.startsWith("__")) continue;
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
            } else {
                simOnlyVariables.append(columnName);
            }
        } else {
            simOnlyVariables.append(columnName);
        }
    }

    // Sort by display description (falls back to code if no description)
    auto sortByDescription = [](QStringList &list) {
        std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
            auto infoA = DataProcessor::getVariableInfo(a);
            auto infoB = DataProcessor::getVariableInfo(b);
            QString labelA = infoA.first.isEmpty() ? a : infoA.first;
            QString labelB = infoB.first.isEmpty() ? b : infoB.first;
            return labelA.compare(labelB, Qt::CaseInsensitive) < 0;
        });
    };
    sortByDescription(commonVariables);
    sortByDescription(simOnlyVariables);

    // Add common variables first (with asterisk) to X combo
    for (const QString &columnName : commonVariables) {
        QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(columnName);
        QString displayLabel = varInfo.first.isEmpty() ? columnName : QString("%1 (%2)").arg(varInfo.first).arg(columnName);
        displayLabel = "* " + displayLabel;
        m_xVariableComboBox->addItem(displayLabel, columnName);
    }
    for (const QString &columnName : simOnlyVariables) {
        QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(columnName);
        QString displayLabel = varInfo.first.isEmpty() ? columnName : QString("%1 (%2)").arg(varInfo.first).arg(columnName);
        m_xVariableComboBox->addItem(displayLabel, columnName);
    }

    // Populate Y variable list — grouped by file if multiple files selected
    bool multiFile = m_fileColumnMap.size() > 1;

    // currentFile: non-empty inside a multi-file group, empty for single-file mode.
    // UserRole stores "filename::COL" in multi-file mode, plain "COL" in single-file mode.
    QString addYCurrentFile;
    auto addYItem = [&](const QString &columnName, bool hasObs) {
        if (yVariableExclusions.contains(columnName)) return;
        QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(columnName);
        QString displayLabel = varInfo.first.isEmpty() ? columnName : QString("%1 (%2)").arg(varInfo.first).arg(columnName);
        if (hasObs) displayLabel = "* " + displayLabel;
        QListWidgetItem *item = new QListWidgetItem(displayLabel);
        QString key = addYCurrentFile.isEmpty() ? columnName : (addYCurrentFile + "::" + columnName);
        item->setData(Qt::UserRole, key);
        m_yVariableComboBox->addItem(item);
    };

    if (!multiFile) {
        // Single file — flat list as before
        for (const QString &col : commonVariables)  addYItem(col, true);
        for (const QString &col : simOnlyVariables) addYItem(col, false);
    } else {
        // Multiple files — group by file with header separators
        for (auto it = m_fileColumnMap.constBegin(); it != m_fileColumnMap.constEnd(); ++it) {
            // Header item
            QListWidgetItem *header = new QListWidgetItem(QString("── %1 ──").arg(it.key()));
            header->setFlags(Qt::NoItemFlags);  // non-selectable
            header->setForeground(QColor("#555555"));
            QFont f = header->font(); f.setBold(true); header->setFont(f);
            header->setBackground(QColor("#e8e8e8"));
            m_yVariableComboBox->addItem(header);

            // Variables for this file, common first then sim-only
            addYCurrentFile = it.key();
            QStringList fileCols = it.value();
            QStringList fileCommon, fileSim;
            for (const QString &col : fileCols) {
                if (yVariableExclusions.contains(col)) continue;
                if (commonVariables.contains(col)) fileCommon.append(col);
                else if (simOnlyVariables.contains(col)) fileSim.append(col);
            }
            sortByDescription(fileCommon); sortByDescription(fileSim);
            for (const QString &col : fileCommon) addYItem(col, true);
            for (const QString &col : fileSim)    addYItem(col, false);
        }
        addYCurrentFile.clear();
    }

    // Set default x-variable.
    // OSU/summary files have WYEAR but no DAS/DAP — prefer WYEAR for seasonal/sequence data.
    // Regular time-series files have DAS/DAP/DATE — prefer DATE then DAP.
    bool isSummaryFile = m_currentData.columnNames.contains("WYEAR") &&
                         !m_currentData.columnNames.contains("DAS") &&
                         !m_currentData.columnNames.contains("DAP");
    if (isSummaryFile) {
        int index = m_xVariableComboBox->findData("WYEAR");
        if (index != -1) {
            m_xVariableComboBox->setCurrentIndex(index);
        }
    } else if (m_currentData.columnNames.contains("DATE")) {
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
    unblockAll();
}





void MainWindow::updateTreatmentComboBox()
{
    m_treatmentComboBox->clear();
    m_treatmentComboBox->addItem("All");

    QStringList sortedTreatments;

    // Detect sequence OSU files: all TRT values identical but R# column present with multiple values
    const DataColumn *trtColCheck = m_currentData.getColumn("TRT");
    const DataColumn *rseqColCheck = m_currentData.getColumn("R#");
    bool isSequenceFile = false;
    if (trtColCheck && rseqColCheck && trtColCheck->data.size() > 0) {
        QSet<QString> uniqueTrts;
        for (const QVariant &v : trtColCheck->data)
            uniqueTrts.insert(v.toString().trimmed());
        QSet<QString> uniqueRseq;
        for (const QVariant &v : rseqColCheck->data)
            uniqueRseq.insert(v.toString().trimmed());
        isSequenceFile = (uniqueTrts.size() == 1) && (uniqueRseq.size() > 1);
    }

    if (isSequenceFile) {
        // For sequence OSU files, group by R# (sequence slot) — each slot is a crop in the rotation
        const DataColumn *cropCol = m_currentData.getColumn("CROP");
        // Build crop code -> full name map from DETAIL.CDE
        QMap<QString, QString> cropCodeToName;
        for (const CropDetails &cd : DataProcessor::getCropDetails())
            cropCodeToName[cd.cropCode.toUpper()] = cd.cropName;
        struct TrtEntry { QString key, rseq, cropName; };
        QList<TrtEntry> entries;
        QSet<QString> seenKeys;
        for (int i = 0; i < rseqColCheck->data.size(); ++i) {
            QString rseq = rseqColCheck->data[i].toString().trimmed();
            if (rseq.isEmpty()) continue;
            if (seenKeys.contains(rseq)) continue;
            seenKeys.insert(rseq);
            QString cropCode = (cropCol && i < cropCol->data.size()) ? cropCol->data[i].toString().trimmed() : QString();
            QString cropName = cropCodeToName.value(cropCode.toUpper(), cropCode);
            if (cropName.isEmpty()) cropName = rseq;
            entries.append({rseq, rseq, cropName});
        }
        std::sort(entries.begin(), entries.end(), [](const TrtEntry &a, const TrtEntry &b) {
            bool aOk, bOk;
            int ai = a.rseq.toInt(&aOk), bi = b.rseq.toInt(&bOk);
            return (aOk && bOk) ? (ai < bi) : (a.rseq < b.rseq);
        });
        for (const TrtEntry &e : entries) {
            // Embed crop name as "R#::slotNum::cropName" so setAvailableTreatments can display it
            QString key = e.cropName.isEmpty() ? ("R#::" + e.key) : ("R#::" + e.key + "::" + e.cropName);
            sortedTreatments.append(key);
            QString label = e.cropName.isEmpty() ? e.key : QString("%1 - %2").arg(e.key, e.cropName);
            m_treatmentComboBox->addItem(label);
        }
    } else {
        // Look for treatment columns (normal case)
        QStringList treatmentCols = {"TRT", "TRNO", "TR"};
        for (const QString &colName : treatmentCols) {
            if (m_currentData.columnNames.contains(colName)) {
                const DataColumn *col = m_currentData.getColumn(colName);
                const DataColumn *expCol = m_currentData.getColumn("EXPERIMENT");
                if (col) {
                    bool multiExp = false;
                    if (expCol) {
                        QSet<QString> uniqueExps;
                        for (const QVariant &v : expCol->data) {
                            QString e = v.toString().trimmed();
                            if (!e.isEmpty() && e != "DEFAULT") uniqueExps.insert(e);
                        }
                        multiExp = uniqueExps.size() > 1;
                    }

                    struct TrtEntry { QString key, exp, trt; };
                    QList<TrtEntry> entries;
                    QSet<QString> seenKeys;
                    for (int i = 0; i < col->data.size(); ++i) {
                        if (DataProcessor::isMissingValue(col->data[i])) continue;
                        QString trt = col->data[i].toString();
                        QString exp;
                        if (expCol && i < expCol->data.size())
                            exp = expCol->data[i].toString().trimmed();
                        QString key = (multiExp && !exp.isEmpty()) ? (exp + "::" + trt) : trt;
                        if (!seenKeys.contains(key)) {
                            seenKeys.insert(key);
                            entries.append({key, exp, trt});
                        }
                    }

                    std::sort(entries.begin(), entries.end(), [](const TrtEntry &a, const TrtEntry &b) {
                        if (a.exp != b.exp) return a.exp < b.exp;
                        bool aOk, bOk;
                        int ai = a.trt.toInt(&aOk), bi = b.trt.toInt(&bOk);
                        return (aOk && bOk) ? (ai < bi) : (a.trt < b.trt);
                    });

                    for (const TrtEntry &e : entries) {
                        sortedTreatments.append(e.key);
                        m_treatmentComboBox->addItem(e.key);
                    }
                }
                break;
            }
        }
    }

    // Populate the pre-plot treatment selection panel
    if (m_plotWidget)
        m_plotWidget->setAvailableTreatments(sortedTreatments, m_treatmentNames);
}

void MainWindow::updateScatterPlot()
{

    if (m_evaluateData.rowCount == 0) {
        m_statusWidget->showWarning("No EVALUATE.OUT data available. Please select an EVALUATE file.");
        return;
    }

    // Collect selected base variable names from the Y list widget
    QStringList selectedVars;
    if (m_yVariableComboBox) {
        for (QListWidgetItem *item : m_yVariableComboBox->selectedItems())
            selectedVars.append(item->data(Qt::UserRole).toString());
    }

    if (selectedVars.isEmpty()) {
        m_statusWidget->showInfo("Select one or more variables to plot.");
        return;
    }

    // Cap at 9
    if (selectedVars.size() > 9) selectedVars = selectedVars.mid(0, 9);


    if (m_scatterPlotWidget) {
        m_scatterPlotWidget->plotScatter(m_evaluateData, selectedVars);
    } else {
        qWarning() << "MainWindow::updateScatterPlot() - Scatter plot widget is null!";
    }
}

void MainWindow::updatePlot()
{
    
    if (m_currentData.rowCount == 0) {
        return;
    }
    
    QString xVar = m_xVariableComboBox->currentData(Qt::UserRole).toString();
    
    // Get selected Y variables from ListWidget.
    // In multi-file mode UserRole holds "filename::COL"; decode into plain yVars + file filter.
    QStringList yVars;
    QMap<QString, QString> yVarFileFilter; // col → source filename (empty = no filter)
    QList<QListWidgetItem*> selectedItems = m_yVariableComboBox->selectedItems();
    for (QListWidgetItem* item : selectedItems) {
        QString key = item->data(Qt::UserRole).toString();
        int sep = key.indexOf("::");
        if (sep != -1) {
            QString file = key.left(sep);
            QString col  = key.mid(sep + 2);
            if (!yVars.contains(col)) yVars.append(col);
            yVarFileFilter[col] = file;
        } else {
            if (!yVars.contains(key)) yVars.append(key);
        }
    }
    
    
    if (xVar.isEmpty() || yVars.isEmpty()) {
        
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

    // Get treatment filter from the pre-plot selection panel (empty = show all)
    QStringList treatments = m_plotWidget ? m_plotWidget->getSelectedTreatments() : QStringList();

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
            if (m_dataTableWidget) {
                m_dataTableWidget->clear();
            }
            return; // Exit early since there's no data to plot
        }

        // Debug observed data before plotting

        // Call the enhanced plotTimeSeries method
        m_plotWidget->plotTimeSeries(
            m_currentData,
            m_selectedFolder,
            selectedFiles,
            m_selectedExperiment,
            treatments,
            xVar,
            yVars,
            m_currentObsData,
            m_treatmentNames,
            yVarFileFilter
        );
        
    } else {
    }
}

void MainWindow::checkAndAutoSwitchToScatterPlot(bool autoPlot)
{
    if (!m_tabWidget || m_tabWidget->currentIndex() != 0)
        return;

    bool isEvaluateFile = false;
    if (m_fileListWidget) {
        for (QListWidgetItem *item : m_fileListWidget->selectedItems()) {
            if (item->text().toUpper().contains("EVALUATE")) { isEvaluateFile = true; break; }
        }
    }
    if (!isEvaluateFile || m_evaluateData.rowCount == 0)
        return;

    m_tabWidget->setCurrentIndex(2);

    if (autoPlot) {
        if (m_yVariableComboBox) m_yVariableComboBox->selectAll();
        updateScatterPlot();
        m_statusWidget->showSuccess("Automatically switched to scatter plot for EVALUATE.OUT file");
    } else {
        m_statusWidget->showInfo("Switched to scatter plot tab. Click 'Plot' to view the scatter plot");
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
    
    m_fileComboBox->blockSignals(true);
    for (const QString &folder : folders) {
        m_fileComboBox->addItem(folder);
    }
    m_fileComboBox->blockSignals(false);

    // Restore last selected crop folder if the setting is enabled
    if (m_fileComboBox->count() > 0) {
        QSettings s("DSSAT", "GB2");
        int idx = 0;
        if (s.value("PlotSettings/rememberLastCropFolder", false).toBool()) {
            QString lastFolder = s.value("lastCropFolder").toString();
            int found = lastFolder.isEmpty() ? -1 : m_fileComboBox->findText(lastFolder);
            if (found >= 0) idx = found;
        }
        m_fileComboBox->setCurrentIndex(idx);
        onFolderSelectionChanged();
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::addDroppedOutFile(const QString &filePath)
{
    if (!m_fileListWidget || filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return;

    QString fileName = fi.fileName();
    QString fileDir  = QDir(fi.absolutePath()).canonicalPath();

    // Try to match the dropped file's directory to a known crop folder.
    // If found, switch the crop selector and populate the file list, then select this file.
    if (m_dataProcessor && m_fileComboBox && !fileDir.isEmpty()) {
        QStringList folderNames = m_dataProcessor->prepareFolders(true);
        for (const QString &folderName : folderNames) {
            QString folderPath = QDir(m_dataProcessor->getActualFolderPath(folderName)).canonicalPath();
            if (!folderPath.isEmpty() && folderPath.compare(fileDir, Qt::CaseInsensitive) == 0) {
                int idx = m_fileComboBox->findText(folderName);
                if (idx >= 0) {
                    // Block signals to avoid async dispatch; switch crop and populate directly.
                    // Relying on currentIndexChanged from within a drop event is unreliable.
                    m_fileComboBox->blockSignals(true);
                    m_fileComboBox->setCurrentIndex(idx);
                    m_fileComboBox->blockSignals(false);
                    m_selectedFolder = folderName;
                    populateFiles(folderName);
                }
                // Find and select the file by name in the now-populated list
                for (int i = 0; i < m_fileListWidget->count(); ++i) {
                    QListWidgetItem *item = m_fileListWidget->item(i);
                    if (item->text().compare(fileName, Qt::CaseInsensitive) == 0) {
                        m_fileListWidget->blockSignals(true);
                        m_fileListWidget->clearSelection();
                        m_fileListWidget->blockSignals(false);
                        item->setSelected(true);
                        m_fileListWidget->scrollToItem(item);
                        return;
                    }
                }
                // File was filtered out by prepareOutFiles — fall through and add it explicitly
                break;
            }
        }
    }

    // File is outside DSSAT folders (or was filtered) — add it to the current list.
    // Remove placeholder if present
    for (int i = 0; i < m_fileListWidget->count(); ++i) {
        if (m_fileListWidget->item(i)->text() == "No .OUT files found") {
            delete m_fileListWidget->takeItem(i);
            break;
        }
    }

    // Deduplicate by full path
    for (int i = 0; i < m_fileListWidget->count(); ++i) {
        QListWidgetItem *existing = m_fileListWidget->item(i);
        if (existing->data(Qt::UserRole).toString() == filePath) {
            m_fileListWidget->blockSignals(true);
            m_fileListWidget->clearSelection();
            m_fileListWidget->blockSignals(false);
            existing->setSelected(true);
            m_fileListWidget->scrollToItem(existing);
            return;
        }
    }

    QListWidgetItem *item = new QListWidgetItem(fileName);
    item->setData(Qt::UserRole, filePath);

    QMap<QString, QString> descs = DataProcessor::getOutfileDescriptions();
    QString desc = descs.value(fi.baseName());
    item->setToolTip(desc.isEmpty()
        ? filePath
        : QString("%1: %2\n%3").arg(fileName, desc, filePath));

    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);

    m_fileListWidget->addItem(item);
    m_fileListWidget->scrollToItem(item);

    m_fileListWidget->blockSignals(true);
    m_fileListWidget->clearSelection();
    m_fileListWidget->blockSignals(false);
    item->setSelected(true);
}

void MainWindow::populateFiles(const QString &folderName)
{
    if (!m_fileListWidget || !m_dataProcessor || folderName.isEmpty()) {
        return;
    }
    
    
    m_fileListWidget->clear();
    m_availableFiles.clear();
    
    QStringList outFiles = m_dataProcessor->prepareOutFiles(folderName);
    
    
    if (outFiles.isEmpty()) {
        m_fileListWidget->addItem("No .OUT files found");
        m_statusWidget->showInfo(QString("No output files found in folder: %1").arg(folderName));
        return;
    }
    
    m_availableFiles = outFiles;
    
    // Get outfile descriptions from OUTFILE.CDE
    QMap<QString, QString> outfileDescriptions = DataProcessor::getOutfileDescriptions();
    
    for (const QString &file : outFiles) {
        QListWidgetItem *item = new QListWidgetItem(file);
        
        // Extract base filename (without extension) and get description
        QString baseFilename = QFileInfo(file).baseName();
        QString description = outfileDescriptions.value(baseFilename, QString());
        
        
        if (!description.isEmpty()) {
            item->setToolTip(QString("%1: %2").arg(file).arg(description));
        } else {
            item->setToolTip(QString("DSSAT output file: %1").arg(file));
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
    if (!selectedFolder.isEmpty() && selectedFolder != "No DSSAT folders found") {
        QSettings s("DSSAT", "GB2");
        s.setValue("lastCropFolder", selectedFolder);
    }
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

        // Save current selection so we can restore it after repopulating
        QStringList previousSelection;
        if (m_fileListWidget) {
            for (QListWidgetItem *item : m_fileListWidget->selectedItems())
                previousSelection << item->text();
        }

        populateFiles(m_selectedFolder);

        // Restore previous selection (re-select files that still exist after refresh)
        if (m_fileListWidget && !previousSelection.isEmpty()) {
            for (int i = 0; i < m_fileListWidget->count(); ++i) {
                QListWidgetItem *item = m_fileListWidget->item(i);
                if (item && previousSelection.contains(item->text()))
                    item->setSelected(true);
            }
        }
    } else {
        populateFolders();
    }
}

void MainWindow::onFileSelectionChanged()
{
    if (!m_fileListWidget) {
        return;
    }

    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();

    if (selectedItems.isEmpty()) {
        m_updatePlotButton->setEnabled(false);

        // Clear data when no files are selected
        m_currentData.clear();
        m_currentObsData.clear();
        m_evaluateData.clear();

        // Clear and update variable combo boxes
        updateVariableComboBoxes();

        // Clear the plot and treatment list (treatments come from selected outfiles)
        if (m_plotWidget) {
            m_plotWidget->setAvailableTreatments(QStringList());
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
    m_updatePlotButton->setEnabled(true);

    // Load data from all selected files to get comprehensive Y variable list
    if (!selectedItems.isEmpty()) {
        
        // Clear previous data - separate storage for different file types
        m_currentData.clear();  // For time series (regular .OUT files)
        m_currentObsData.clear();  // For time series observed data
        m_evaluateData.clear();  // For scatter plots (EVALUATE.OUT files)
        m_fileColumnMap.clear();  // Reset per-file column tracking
        
        // Load and merge data from all selected files, separating by type
        QSet<QString> uniqueExperimentCodes;
        QMap<QString, QMap<QString, QString>> extractedTreatmentNames;
        QString firstValidFile;
        QString firstValidRegularFile;  // For observed data lookup
        bool hasEvaluateFile = false;
        bool hasRegularFile = false;
        
        for (QListWidgetItem* selectedItem : selectedItems) {
            QString selectedFile = selectedItem->text();
        
            if (selectedFile != "No .OUT files found") {
                QString dssatBase = m_dataProcessor->getDSSATBase();
                
                // Dropped external files store their full path in UserRole
                QString droppedPath = selectedItem->data(Qt::UserRole).toString();
                QString folderPath  = m_dataProcessor->getActualFolderPath(m_selectedFolder);
                QString filePath;
                if (!droppedPath.isEmpty()) {
                    filePath = droppedPath;
                } else if (!folderPath.isEmpty()) {
                    filePath = QDir(folderPath).absoluteFilePath(selectedFile);
                } else {
                    filePath = QDir(dssatBase).absoluteFilePath(m_selectedFolder + QDir::separator() + selectedFile);
                }
                
                // Check if THIS specific file is EVALUATE.OUT or evaluate.csv
                QString upperFileName = selectedFile.toUpper();
                bool isEvaluateFile = upperFileName.contains("EVALUATE");
                if (isEvaluateFile) {
                    hasEvaluateFile = true;
                } else {
                    hasRegularFile = true;
                    if (firstValidRegularFile.isEmpty()) {
                        firstValidRegularFile = filePath;
                    }
                }

                // Load data from current file using appropriate reader
                DataTable fileData;
                bool readSuccess = false;
                if (isEvaluateFile) {
                    // readFile dispatches .OUT to readEvaluateFile and .csv to readCsvFile
                    readSuccess = m_dataProcessor->readFile(filePath, fileData);
                } else {
                    readSuccess = m_dataProcessor->readFile(filePath, fileData);
                }
                
                if (readSuccess) {
                    
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
                        m_fileColumnMap[selectedFile] = fileData.columnNames;
                        // Stamp each row with the source filename so plotDatasets can filter by file
                        DataColumn srcCol("__SRCFILE__");
                        for (int i = 0; i < fileData.rowCount; ++i)
                            srcCol.data.append(selectedFile);
                        fileData.addColumn(srcCol);
                        if (m_currentData.rowCount == 0) {
                            m_currentData = fileData;
                        } else {
                            m_currentData.merge(fileData);
                        }
                    }
                    
                    // Extract experiment codes and treatment names from this file (only for regular files)
                    if (!isEvaluateFile && fileData.columnNames.contains("TRT") && fileData.columnNames.contains("TNAME")) {
                        const DataColumn* expCol = fileData.getColumn("EXPERIMENT");
                        const DataColumn* trtCol = fileData.getColumn("TRT");
                        const DataColumn* tnameCol = fileData.getColumn("TNAME");

                        if (trtCol && tnameCol) {
                            for (int i = 0; i < fileData.rowCount; ++i) {
                                QString expCode = expCol ? expCol->data[i].toString().trimmed() : QString();
                                QString trtCode = trtCol->data[i].toString().trimmed();
                                QString tname = tnameCol->data[i].toString().trimmed();

                                if (!expCode.isEmpty() && expCode != "DEFAULT") {
                                    uniqueExperimentCodes.insert(expCode);
                                }
                                if (!trtCode.isEmpty() && !tname.isEmpty()) {
                                    // T files have no EXPERIMENT column; store under "default" so
                                    // getTreatmentDisplayName's fallback can find them.
                                    QString key = expCode.isEmpty() ? "default" : expCode;
                                    extractedTreatmentNames[key][trtCode] = tname;
                                }
                            }
                        }
                    }
                } else {
                }
            }
        }
        
        // Process regular .OUT files (for time series plots)
        if (m_currentData.rowCount > 0) {
            m_treatmentNames = extractedTreatmentNames; // Assign to member variable

            // Set m_selectedExperiment to the first available experiment code
            if (!uniqueExperimentCodes.isEmpty()) {
                m_selectedExperiment = uniqueExperimentCodes.values().first();
            } else {
                m_selectedExperiment = ""; // Or a default value if no experiments are found
            }

            // Determine crop code
            QString cropCode = "XX";
            
            // Special handling for SensWork - extract crop code from the file itself
            if (m_selectedFolder.compare("SensWork", Qt::CaseInsensitive) == 0 && !firstValidFile.isEmpty()) {
                QPair<QString, QString> sensWorkCodes = m_dataProcessor->extractSensWorkCodes(firstValidFile);
                if (!sensWorkCodes.second.isEmpty()) {
                    cropCode = sensWorkCodes.second.toUpper();
                } else {
                }
            } else {
                // Regular crop folder - try to get crop code from the selected folder name by matching with crop details
                QVector<CropDetails> allCropDetails = m_dataProcessor->getCropDetails();
                QString selectedFolderLower = m_selectedFolder.toLower();

                // Two-pass: exact matches first, then partial (to avoid "Pea" matching before "Peanut")
                QString partialCode;
                for (const CropDetails& crop : allCropDetails) {
                    QString dirName = QFileInfo(crop.directory).fileName().toLower();
                    QString cropNameLower = crop.cropName.toLower();

                    bool dirNameMatch = (dirName == selectedFolderLower);
                    bool cropNameMatch = (cropNameLower == selectedFolderLower);
                    bool pathContainsFolder = crop.directory.toLower().contains("/" + selectedFolderLower) ||
                                             crop.directory.toLower().contains("\\" + selectedFolderLower);

                    if (dirNameMatch || cropNameMatch || pathContainsFolder) {
                        cropCode = crop.cropCode.toUpper();
                        break;
                    }

                    // Partial match: only keep first candidate, don't break
                    if (partialCode.isEmpty()) {
                        bool cropNameContains = cropNameLower.contains(selectedFolderLower) ||
                                               selectedFolderLower.contains(cropNameLower);
                        if (cropNameContains)
                            partialCode = crop.cropCode.toUpper();
                    }
                }

                // Fall back to partial match only if no exact match found
                if (cropCode == "XX" && !partialCode.isEmpty())
                    cropCode = partialCode;
            }

            // Add CROP column to simulated data if it doesn't exist
            if (!m_currentData.columnNames.contains("CROP")) {
                DataColumn cropCol("CROP");
                for (int r = 0; r < m_currentData.rowCount; ++r) {
                    cropCol.data.append(cropCode);
                }
                m_currentData.addColumn(cropCol);
            }

            // Attempt to load and merge observed data for each unique experiment code (only for regular files)
            // Special handling for SensWork files
            if (m_selectedFolder.compare("SensWork", Qt::CaseInsensitive) == 0) {
                
                // For SensWork, use the dynamic observed data lookup
                if (!firstValidRegularFile.isEmpty()) {
                    DataTable sensWorkObsData;
                    if (m_dataProcessor->readSensWorkObservedData(firstValidRegularFile, sensWorkObsData)) {
                        m_currentObsData.merge(sensWorkObsData);
                    } else {
                    }
                }
            } else {
                // Regular crop folder - use standard observed data lookup
                
                for (const QString& expCode : uniqueExperimentCodes) {
                    DataTable tempObsData;
                    // Use the first valid regular file path for observed data lookup
                    if (!firstValidRegularFile.isEmpty() && m_dataProcessor->readObservedData(firstValidRegularFile, expCode, cropCode, tempObsData)) {
                        m_currentObsData.merge(tempObsData);
                    } else {
                    }
                }
            }
            
            // Add DAS/DAP columns to observed data if it exists
            if (m_currentObsData.rowCount > 0) {
                m_dataProcessor->addDasDapColumns(m_currentObsData, m_currentData);
            } else {
            }
        }
        
        // Process EVALUATE.OUT files (for scatter plots)
        if (m_evaluateData.rowCount > 0) {
        }
        
        // Update variable combo boxes based on current tab
        updateVariableComboBoxes();
        if (hasRegularFile) {
            updateTreatmentComboBox();
        }

        // Mark data as needing refresh for the data table, but don't set it yet
        markDataNeedsRefresh();
        
        // If we're on Data View tab, update file type selector and refresh data
        if (m_tabWidget && m_tabWidget->currentIndex() == 1) {
            if (m_dataViewFileTypeComboBox) {
                bool hasRegular = (m_currentData.rowCount > 0);
                bool hasEvaluate = (m_evaluateData.rowCount > 0);
                bool hasPlot = m_plotWidget && !m_plotWidget->getPlotCSV().isEmpty();
                m_dataViewFileTypeComboBox->setEnabled(hasRegular || hasEvaluate || hasPlot);
                if (hasPlot)
                    m_dataViewFileTypeComboBox->setCurrentIndex(2); // Current Plot Data
                else if (!hasRegular && hasEvaluate)
                    m_dataViewFileTypeComboBox->setCurrentIndex(1); // EVALUATE.OUT
                else
                    m_dataViewFileTypeComboBox->setCurrentIndex(0); // Regular .OUT
            }

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
            m_statusWidget->showInfo(QString("Loaded %1 file(s). Click 'Data' to view data table").arg(selectedItems.size()));
        } else if (m_tabWidget && m_tabWidget->currentIndex() == 2) {
            // Scatter Plot tab
            if (hasEvaluateFile) {
                m_statusWidget->showInfo(QString("Loaded %1 EVALUATE.OUT file(s). Select X and Y variables and click 'Refresh Plot' to view scatter plot").arg(selectedItems.size()));
            } else {
                m_statusWidget->showInfo("No EVALUATE.OUT files selected. Please select EVALUATE.OUT files for scatter plots.");
            }
        }
    }
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
    
    m_selectedExperiment = expCode;
    
    if (!treatmentNumbers.isEmpty()) {
        m_selectedTreatments = treatmentNumbers;
    }
}

void MainWindow::selectTreatmentsByNumbers(const QStringList &treatmentNumbers)
{
    m_selectedTreatments = treatmentNumbers;
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
    updateStatisticsTab();
}

void MainWindow::onPlotWidgetXVariableChanged(const QString &xVariable)
{
    
    // Update the X variable combo box to reflect the change
    if (m_xVariableComboBox) {
        // Find the index for the new X variable
        int index = m_xVariableComboBox->findData(xVariable);
        if (index >= 0) {
            // Temporarily block signals to avoid infinite loop
            m_xVariableComboBox->blockSignals(true);
            m_xVariableComboBox->setCurrentIndex(index);
            m_xVariableComboBox->blockSignals(false);
            
            
            // Refresh the plot with the new X variable
            updatePlot();
        } else {
        }
    }
}

void MainWindow::onUnselectAllFiles()
{
    
    if (m_fileListWidget) {
        // Clear all selections in the file list
        m_fileListWidget->clearSelection();
        
        // Trigger the selection changed event to update the interface
        onFileSelectionChanged();
    }
}

void MainWindow::onUnselectAllYVars()
{
    
    if (m_yVariableComboBox) {
        // Clear all selections in the Y variable list
        m_yVariableComboBox->clearSelection();
        
        // Clear metrics data since no variables are selected
        clearMetrics();
        
        // Trigger the selection changed event to update the plot
        onYVariableChanged();
    }
}

void MainWindow::onShowMetrics()
{
    
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
    
    // Convert QVector<QMap> to QVariantList for compatibility
    QVariantList metricList;
    for (const auto &metric : metrics) {
        metricList.append(QVariant(metric));
    }
    
    m_timeSeriesMetrics = metricList;
    int currentTab = m_tabWidget ? m_tabWidget->currentIndex() : -1;
    if (currentTab == 0) {
        m_currentMetrics = m_timeSeriesMetrics;
    }

    updateMetricsButtonState();
    updateStatisticsTab();

}

void MainWindow::updateScatterMetrics(const QVector<QMap<QString, QVariant>> &metrics)
{

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
    updateStatisticsTab();
}

void MainWindow::updateStatisticsTab()
{
    if (m_statsTSWidget) {
        if (!m_timeSeriesMetrics.isEmpty()) {
            m_statsTSWidget->setMetrics(m_timeSeriesMetrics, false);
            if (m_statsTSHeader) m_statsTSHeader->setVisible(true);
            m_statsTSWidget->setVisible(true);
        } else {
            m_statsTSWidget->clear();
            if (m_statsTSHeader) m_statsTSHeader->setVisible(false);
            m_statsTSWidget->setVisible(false);
        }
    }

    if (m_statsScatterWidget) {
        if (!m_scatterMetrics.isEmpty()) {
            m_statsScatterWidget->setMetrics(m_scatterMetrics, true);
            if (m_statsScatterHeader) m_statsScatterHeader->setVisible(true);
            m_statsScatterWidget->setVisible(true);
        } else {
            m_statsScatterWidget->clear();
            if (m_statsScatterHeader) m_statsScatterHeader->setVisible(false);
            m_statsScatterWidget->setVisible(false);
        }
    }
}

// Command line integration methods

bool MainWindow::selectCropFolder(const QString &cropName)
{
    if (!m_fileComboBox) {
        qWarning() << "MainWindow::selectCropFolder - fileComboBox not initialized";
        return false;
    }

    // Search existing items first (GUI mode — combo already populated)
    for (int i = 0; i < m_fileComboBox->count(); ++i) {
        QString folderText = m_fileComboBox->itemText(i);
        if (folderText.compare(cropName, Qt::CaseInsensitive) == 0) {
            m_fileComboBox->setCurrentIndex(i);
            m_selectedFolder = folderText;
            onFolderSelectionChanged();
            return true;
        }
    }

    // CLI mode: combo is empty because populateFolders was skipped.
    // Verify the crop exists in DSSATPRO then add just this one entry.
    QVector<CropDetails> allCrops = m_dataProcessor->getCropDetails();
    for (const CropDetails &crop : allCrops) {
        if (crop.cropName.compare(cropName, Qt::CaseInsensitive) == 0 &&
            !crop.directory.isEmpty()) {
            m_fileComboBox->addItem(crop.cropName);
            m_fileComboBox->setCurrentIndex(m_fileComboBox->count() - 1);
            m_selectedFolder = crop.cropName;
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
    }
}

void MainWindow::loadOutputFiles()
{
    // This method would refresh the output files list
    // Using existing method if available
    refreshOutputFiles();
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
        bool found = false;
        for (int i = 0; i < m_fileListWidget->count(); ++i) {
            QListWidgetItem *item = m_fileListWidget->item(i);
            if (!item) continue;

            QString itemText = item->text();
            QString itemData = item->data(Qt::UserRole).toString();

            if (itemText.compare(fileName, Qt::CaseInsensitive) == 0 ||
                itemData.compare(fileName, Qt::CaseInsensitive) == 0) {
                item->setSelected(true);
                selectedCount++;
                found = true;
                break;
            }
        }

        if (!found) {
            // T files (e.g. KSAS8101.WHT) are not in the normal file list —
            // resolve the full path and add them directly.
            QString ext = QFileInfo(fileName).suffix().toUpper();
            bool isTFile = ext.length() == 3 && ext.endsWith('T') &&
                           ext[0].isLetter() && ext[1].isLetter();
            if (isTFile && m_dataProcessor) {
                QString fullPath;
                if (QFileInfo(fileName).isAbsolute()) {
                    fullPath = fileName;
                } else if (!m_selectedFolder.isEmpty()) {
                    QString cropPath = m_dataProcessor->getActualFolderPath(m_selectedFolder);
                    fullPath = QDir(cropPath).absoluteFilePath(fileName);
                }
                if (!fullPath.isEmpty() && QFileInfo::exists(fullPath)) {
                    QString baseName = QFileInfo(fullPath).fileName();
                    QListWidgetItem *newItem = new QListWidgetItem(baseName);
                    newItem->setData(Qt::UserRole, fullPath);
                    QFont f = newItem->font(); f.setItalic(true); newItem->setFont(f);
                    m_fileListWidget->addItem(newItem);
                    newItem->setSelected(true);
                    selectedCount++;
                }
            }
        }
    }

    if (selectedCount > 0) {
        onFileSelectionChanged();
    }

    return selectedCount;
}

void MainWindow::loadVariables()
{
    // This method would load variables for the selected files
    // Using existing variable update logic
    updateVariableComboBoxes();
}

void MainWindow::hideFileSelectionUI(bool hide)
{
    if (m_cropGroup) {
        m_cropGroup->setVisible(!hide);
        m_cropGroup->setMaximumHeight(hide ? 0 : QWIDGETSIZE_MAX);
    }

    if (hide) {
        // In CLI mode: hide the entire file group — Refresh is now in the plot bottom bar
        if (m_fileGroup) { m_fileGroup->setVisible(false); m_fileGroup->setMaximumHeight(0); }
        if (m_fileGroupLabel) m_fileGroupLabel->setVisible(false);
        if (m_fileSearchWidget) m_fileSearchWidget->setVisible(false);
        if (m_fileContainerWidget) m_fileContainerWidget->setVisible(false);
    } else {
        if (m_fileGroup) { m_fileGroup->setVisible(true); m_fileGroup->setMaximumHeight(QWIDGETSIZE_MAX); }
        if (m_fileGroupLabel) m_fileGroupLabel->setVisible(true);
        if (m_fileSearchWidget) m_fileSearchWidget->setVisible(true);
        if (m_fileContainerWidget) m_fileContainerWidget->setVisible(true);
    }

    if (hide) {
    } else {
    }
}

void MainWindow::updateTimeSeriesPlot()
{
    // This method would update the time series plot
    // Using existing plot update logic
    if (m_plotWidget) {
        onUpdatePlot();
    } else {
        qWarning() << "MainWindow: Plot widget not initialized";
    }
}

bool MainWindow::selectXVariable(const QString &varName)
{
    if (!m_xVariableComboBox) return false;

    for (int i = 0; i < m_xVariableComboBox->count(); ++i) {
        QString itemData = m_xVariableComboBox->itemData(i, Qt::UserRole).toString();
        if (itemData.compare(varName, Qt::CaseInsensitive) == 0) {
            m_xVariableComboBox->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

int MainWindow::selectYVariables(const QStringList &varNames)
{
    if (!m_yVariableComboBox) return 0;

    m_yVariableComboBox->clearSelection();

    int count = 0;
    for (int i = 0; i < m_yVariableComboBox->count(); ++i) {
        QListWidgetItem *item = m_yVariableComboBox->item(i);
        if (!item) continue;
        QString itemData = item->data(Qt::UserRole).toString();
        // In multi-file mode UserRole is "filename::COL"; strip prefix for matching
        int sep = itemData.indexOf("::");
        QString colPart = (sep != -1) ? itemData.mid(sep + 2) : itemData;
        for (const QString &varName : varNames) {
            if (colPart.compare(varName, Qt::CaseInsensitive) == 0) {
                item->setSelected(true);
                ++count;
                break;
            }
        }
    }
    return count;
}

bool MainWindow::saveMetricsToFile(const QString &filePath)
{
    if (m_currentMetrics.isEmpty()) return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "MainWindow::saveMetricsToFile: cannot open" << filePath;
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    // UTF-8 BOM
    out << "\xEF\xBB\xBF";

    // Header
    out << "Treatment,Treatment Name,Experiment,Crop,Variable,n,RMSE,d-stat\n";

    for (const QVariant &item : m_currentMetrics) {
        QVariantMap row = item.toMap();

        auto getVal = [&](const QStringList &keys) -> QString {
            for (const QString &k : keys) {
                if (row.contains(k)) return row[k].toString();
            }
            return QString();
        };

        QString treatment    = getVal({"Treatment", "treatment", "trt", "TRT"});
        QString treatmentName = getVal({"TreatmentName", "Treatment Name", "treatment_name", "trt_name"});
        QString experiment   = getVal({"Experiment", "experiment", "exp", "EXP"});
        QString crop         = getVal({"CropName", "Crop", "crop", "CROP"});
        QString variable     = getVal({"VariableName", "Variable", "variable", "var"});
        int     n            = (int)getVal({"n", "N", "samples", "count"}).toDouble();
        double  rmse         = getVal({"RMSE", "rmse"}).toDouble();
        double  dstat        = getVal({"d-stat", "Willmott's d-stat", "d_stat", "dstat", "willmott_d"}).toDouble();

        out << treatment << ","
            << treatmentName << ","
            << experiment << ","
            << crop << ","
            << variable << ","
            << n << ","
            << QString::number(rmse,  'f', 3) << ","
            << QString::number(dstat, 'f', 4) << "\n";
    }

    return true;
}

void MainWindow::onDataViewFileTypeChanged()
{
    if (!m_dataTableWidget || !m_dataViewFileTypeComboBox) {
        return;
    }
    
    QString selectedType = m_dataViewFileTypeComboBox->currentData().toString();
    
    if (selectedType == "evaluate") {
        m_dataTableWidget->setTabsVisible(true);
        if (m_evaluateData.rowCount > 0) {
            m_dataTableWidget->setData(m_evaluateData, DataTable());
        }
    } else if (selectedType == "plot") {
        m_dataTableWidget->setTabsVisible(false);
        QString csv = m_plotWidget ? m_plotWidget->getPlotCSV() : QString();
        if (!csv.isEmpty()) {
            m_dataTableWidget->setData(parseCsvToDataTable(csv), DataTable());
        } else {
            m_dataTableWidget->clear();
        }
    } else {
        m_dataTableWidget->setTabsVisible(true);
        if (m_currentData.rowCount > 0) {
            m_dataTableWidget->setData(m_currentData, m_currentObsData);
        }
    }
}

DataTable MainWindow::parseCsvToDataTable(const QString &csv)
{
    DataTable result;
    QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return result;

    QStringList headers = lines[0].split(',');
    for (const QString &h : headers) {
        DataColumn col(h.trimmed());
        col.dataType = "string";
        result.addColumn(col);
    }

    for (int i = 1; i < lines.size(); ++i) {
        QStringList fields = lines[i].split(',');
        for (int c = 0; c < headers.size(); ++c) {
            DataColumn *col = result.getColumn(headers[c].trimmed());
            if (!col) continue;
            QString val = (c < fields.size()) ? fields[c].trimmed() : QString();
            col->data.append(val.isEmpty() ? QVariant() : QVariant(val));
        }
        result.rowCount++;
    }
    return result;
}
