#include <QSettings>
#include <QTextStream>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QVector>
#include "PlotWidget.h"
#include "MetricsCalculator.h"
#include "Config.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <QFrame>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QDateTime>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QPixmap>
#include <QPainter>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QFrame>
#include <QSizePolicy>
#include <QTimer>
#include <QDateTime>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QPdfWriter>
#include <QEnterEvent>
#include <QContextMenuEvent>
#include <QDrag>
#include <QMimeData>
#include <QBuffer>
#include <algorithm>
#include <cmath>

// ErrorBarChartView implementation → see PlotWidget_ErrorBar.cpp

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_leftContainer(nullptr)
    , m_leftLayout(nullptr)
    , m_legendStack(nullptr)
    , m_preplotPanel(nullptr)
    , m_treatmentSelectList(nullptr)
    , m_chart(nullptr)
    , m_chartView(nullptr)
    , m_bottomContainer(nullptr)
    , m_bottomLayout(nullptr)
    , m_dateButton(nullptr)
    , m_dasButton(nullptr)
    , m_dapButton(nullptr)
    , m_settingsButton(nullptr)
    , m_boxPlotButton(nullptr)
    , m_treatmentsButton(nullptr)
    , m_scalingLabel(nullptr)
    , m_legendScrollArea(nullptr)
    , m_legendWidget(nullptr)
    , m_legendLayout(nullptr)
    , m_showLegend(true)
    , m_showGrid(true)
    , m_preplotPanelEnabled(true)
    , m_currentPlotType("Line")
    , m_dataProcessor(new DataProcessor(this))
    , m_currentXVar("DAP")
    , m_isScatterMode(false)
    , m_isBoxPlotMode(false)
{
    qDebug() << "PlotWidget::PlotWidget() - CONSTRUCTOR CALLED";
    
    setupUI();
    setupChart();
    qDebug() << "PlotWidget::PlotWidget() - setupChart() completed";
    
    // Initialize plot colors (matching Python PLOT_COLORS)
    m_plotColors = {
        QColor("#1f77b4"), QColor("#ff7f0e"), QColor("#2ca02c"), QColor("#d62728"),
        QColor("#9467bd"), QColor("#8c564b"), QColor("#e377c2"), QColor("#7f7f7f"),
        QColor("#bcbd22"), QColor("#17becf"), QColor("#FFB6C1"), QColor("#20B2AA"),
        QColor("#FF6347"), QColor("#4169E1"), QColor("#32CD32"), QColor("#FF69B4"),
        QColor("#8A2BE2"), QColor("#DC143C"), QColor("#00CED1"), QColor("#FF4500")
    };

    m_markerSymbols = {"o", "s", "d", "t", "v"};
    
    // Initialize optimization variables
    m_autoFitPending = false;
    m_autoFitTimer = new QTimer(this);
    m_autoFitTimer->setSingleShot(true);
    m_autoFitTimer->setInterval(100);  // 100ms delay for auto-fit
    connect(m_autoFitTimer, &QTimer::timeout, this, &PlotWidget::autoFitAxes);
    
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Restore persisted plot settings from previous session
    loadSettings();
}

PlotWidget::~PlotWidget()
{
}

void PlotWidget::testScalingFunctionality()
{
    qDebug() << "PlotWidget: Testing scaling functionality...";
    
    // Create test data with different magnitudes
    DataTable testSimData;
    testSimData.tableName = "TestData";
    
    // Add test columns with different orders of magnitude
    DataColumn var1("VAR1"); // Small values (0.1 - 1.0)
    DataColumn var2("VAR2"); // Large values (1000 - 10000)
    
    for (int i = 0; i < 10; ++i) {
        var1.data.append(0.1 + i * 0.1);  // 0.1, 0.2, ..., 1.0
        var2.data.append(1000 + i * 1000); // 1000, 2000, ..., 10000
    }
    
    testSimData.addColumn(var1);
    testSimData.addColumn(var2);
    testSimData.rowCount = 10;
    
    // Create empty observed data
    DataTable testObsData;
    
    // Test scaling calculation
    QStringList testYVars = {"VAR1", "VAR2"};
    QMap<QString, QMap<QString, ScalingInfo>> scaleFactors = 
        calculateScalingFactors(testSimData, testObsData, testYVars);
    
    qDebug() << "PlotWidget: Test scaling results:";
    for (auto expIt = scaleFactors.begin(); expIt != scaleFactors.end(); ++expIt) {
        for (auto varIt = expIt.value().begin(); varIt != expIt.value().end(); ++varIt) {
            qDebug() << "  " << varIt.key() << ": scale factor ="
                     << varIt.value().scaleFactor << ", offset =" << varIt.value().offset;
        }
    }
    
    // Apply the test and update UI to verify scaling works
    m_scaleFactors = scaleFactors;
    updateScalingLabel(testYVars);
    qDebug() << "PlotWidget: Test scaling label should now be visible if scaling is working";
}

void PlotWidget::setupUI()
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);

    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Left container for plot and controls
    m_leftContainer = new QWidget();
    m_leftContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftLayout = new QVBoxLayout(m_leftContainer);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);
    
    // Chart view will be added in setupChart()
    
    // Bottom container with X-axis buttons and scaling label
    m_bottomContainer = new QWidget();
    m_bottomLayout = new QHBoxLayout(m_bottomContainer);
    m_bottomLayout->setContentsMargins(5, 5, 5, 5);
    m_bottomLayout->setSpacing(0); // Remove gaps between buttons
    
    // X-axis selection buttons (like Python version)
    
    m_dasButton = new QPushButton("DAS");
    m_dapButton = new QPushButton("DAP");
    m_dateButton = new QPushButton("DATE");
    m_settingsButton = new QPushButton("⚙");
    
    // Simple, clean button styling
    QString buttonStyle = "QPushButton { "
                         "padding: 6px 12px; "
                         "background-color: #f8f9fa; "
                         "border: 1px solid #cccccc; "
                         "border-radius: 4px; "
                         "color: #333333; "
                         "font-size: 12px; "
                         "} "
                         "QPushButton:hover { background-color: #e9ecef; } "
                         "QPushButton:pressed { background-color: #dee2e6; } "
                         "QPushButton:checked { background-color: #0078d4; color: white; border-color: #0078d4; }";
    
    m_dasButton->setStyleSheet(buttonStyle);
    m_dapButton->setStyleSheet(buttonStyle);
    m_dateButton->setStyleSheet(buttonStyle);
    
    // Settings button with minimal styling - just clickable icon
    QString settingsStyle = "QPushButton { "
                           "border: none; background: transparent; padding: 0px; margin: 2px; "
                           "font-size: 16px; "
                           "} "
                           "QPushButton:hover { background-color: rgba(0,0,0,0.1); border-radius: 3px; } "
                           "QPushButton:pressed { background-color: rgba(0,0,0,0.2); }";
    m_settingsButton->setStyleSheet(settingsStyle);
    m_settingsButton->setToolTip("Plot Settings");
    m_settingsButton->setFixedSize(24, 24);

    // Make buttons checkable to show active state
    m_dasButton->setCheckable(true);
    m_dapButton->setCheckable(true);
    m_dateButton->setCheckable(true);
    m_dateButton->setChecked(true); // Default to DATE
    
    // Simple approach: just add buttons to layout with no gaps
    m_bottomLayout->addWidget(m_dasButton);
    m_bottomLayout->addWidget(m_dapButton);
    m_bottomLayout->addWidget(m_dateButton);

    // Box Plot toggle button (visible only for OSU seasonal/summary files)
    m_boxPlotButton = new QPushButton("Box Plot");
    m_boxPlotButton->setCheckable(true);
    m_boxPlotButton->setChecked(false);
    m_boxPlotButton->setVisible(false);
    m_boxPlotButton->setStyleSheet(buttonStyle);
    m_bottomLayout->addSpacing(10);
    m_bottomLayout->addWidget(m_boxPlotButton);

    // Treatments button — opens treatment selection/review panel
    m_treatmentsButton = new QPushButton("Treatments");
    m_treatmentsButton->setStyleSheet(buttonStyle);
    m_treatmentsButton->setToolTip("Show treatment selection panel");
    m_bottomLayout->addSpacing(6);
    m_bottomLayout->addWidget(m_treatmentsButton);

    // Connect button signals
    connect(m_dasButton, &QPushButton::clicked, this, &PlotWidget::onDasButtonClicked);
    connect(m_dapButton, &QPushButton::clicked, this, &PlotWidget::onDapButtonClicked);
    connect(m_dateButton, &QPushButton::clicked, this, &PlotWidget::onDateButtonClicked);
    connect(m_boxPlotButton, &QPushButton::clicked, this, &PlotWidget::onBoxPlotButtonClicked);
    connect(m_treatmentsButton, &QPushButton::clicked, this, &PlotWidget::showTreatmentSelection);
    connect(m_settingsButton, &QPushButton::clicked, this, &PlotWidget::onSettingsButtonClicked);
    
    // Add spacer
    m_bottomLayout->addSpacing(20);
    
    // Simple scaling label
    m_scalingLabel = new QLabel();
    m_scalingLabel->setStyleSheet("padding: 8px; font-size: 10pt; font-weight: bold; background-color: #fff3cd; border: 1px solid #ffeaa7; border-radius: 3px; color: #856404;");
    m_scalingLabel->setWordWrap(true);
    m_scalingLabel->setAlignment(Qt::AlignCenter);
    m_scalingLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_scalingLabel->setVisible(false); // Start hidden
    m_bottomLayout->addWidget(m_scalingLabel, 1);
    
    
    // Chart placeholder (replaced in setupChart)
    QWidget* chartPlaceholder = new QWidget();
    chartPlaceholder->setStyleSheet("background-color: #f0f0f0; border: 1px dashed #ccc;");
    chartPlaceholder->setMinimumHeight(200);
    m_leftLayout->addWidget(chartPlaceholder, 1);
    m_leftLayout->addWidget(m_bottomContainer, 0);

    m_mainLayout->addWidget(m_leftContainer, 80);

    // Legend scroll area (simple, matching Python)
    m_legendScrollArea = new QScrollArea();
    m_legendScrollArea->setWidgetResizable(true);
    m_legendScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_legendScrollArea->setFrameShape(QFrame::NoFrame);
    // No fixed width — inherits from panel

    // Legend container
    m_legendWidget = new QWidget();
    m_legendLayout = new QVBoxLayout(m_legendWidget);
    m_legendLayout->setSpacing(2);
    m_legendLayout->setContentsMargins(5, 0, 5, 0);
    m_legendLayout->setAlignment(Qt::AlignTop);

    // Outer legend widget with stretch
    QWidget *legendOuterWidget = new QWidget();
    QVBoxLayout *legendOuterLayout = new QVBoxLayout(legendOuterWidget);
    legendOuterLayout->setContentsMargins(0, 0, 0, 0);
    legendOuterLayout->addWidget(m_legendWidget, 0, Qt::AlignTop);
    legendOuterLayout->addStretch(1);

    m_legendScrollArea->setWidget(legendOuterWidget);

    // Legend stack: page 0 = treatment pre-selection, page 1 = legend
    setupPreplotPanel();
    m_legendStack = new QStackedWidget();
    // No fixed width — inherits from panel
    m_legendStack->addWidget(m_preplotPanel);       // page 0 — shown before first plot
    m_legendStack->addWidget(m_legendScrollArea);   // page 1 — shown after plotting
    m_legendStack->setCurrentIndex(0);

    // Drag handle bar
    m_legendHandle = new QWidget();
    m_legendHandle->setFixedHeight(22);
    m_legendHandle->setCursor(Qt::SizeAllCursor);
    m_legendHandle->setStyleSheet("background-color: #e8e8e8; border-bottom: 1px solid #cccccc;");
    m_legendHandle->setToolTip("Drag to float legend over chart");
    QHBoxLayout* handleLayout = new QHBoxLayout(m_legendHandle);
    handleLayout->setContentsMargins(5, 2, 2, 2);
    QLabel* gripLabel = new QLabel("⠇  Legend");
    gripLabel->setStyleSheet("color: #555555; font-size: 11px;");
    handleLayout->addWidget(gripLabel);
    handleLayout->addStretch();

    QPushButton* dockBtn = new QPushButton("→");
    dockBtn->setFixedSize(18, 18);
    dockBtn->setToolTip("Dock legend back to panel");
    dockBtn->setStyleSheet(
        "QPushButton { border: none; background: transparent; color: #555555; font-size: 12px; padding: 0; }"
        "QPushButton:hover { color: #0078d4; }");
    dockBtn->hide();  // only visible when floating
    dockBtn->setObjectName("legendDockBtn");
    connect(dockBtn, &QPushButton::clicked, this, &PlotWidget::dockLegend);
    handleLayout->addWidget(dockBtn);

    m_legendHandle->installEventFilter(this);

    // Inner container: handle + stack
    QWidget *legendInner = new QWidget();
    QVBoxLayout *legendInnerLayout = new QVBoxLayout(legendInner);
    legendInnerLayout->setContentsMargins(0, 0, 0, 0);
    legendInnerLayout->setSpacing(0);
    legendInnerLayout->addWidget(m_legendHandle);
    legendInnerLayout->addWidget(m_legendStack);

    // Left-edge resize strip — drag to change width
    m_legendResizeStrip = new QWidget();
    m_legendResizeStrip->setFixedWidth(5);
    m_legendResizeStrip->setCursor(Qt::SizeHorCursor);
    m_legendResizeStrip->setStyleSheet("background-color: transparent;");
    m_legendResizeStrip->setToolTip("Drag to resize width");
    m_legendResizeStrip->installEventFilter(this);

    // Middle row: left strip + inner content
    QWidget *legendMidRow = new QWidget();
    QHBoxLayout *legendMidLayout = new QHBoxLayout(legendMidRow);
    legendMidLayout->setContentsMargins(0, 0, 0, 0);
    legendMidLayout->setSpacing(0);
    legendMidLayout->addWidget(m_legendResizeStrip);
    legendMidLayout->addWidget(legendInner, 1);

    // Bottom resize strip — drag to change height (visible when floating)
    m_legendResizeBottom = new QWidget();
    m_legendResizeBottom->setFixedHeight(5);
    m_legendResizeBottom->setCursor(Qt::SizeVerCursor);
    m_legendResizeBottom->setStyleSheet("background-color: transparent;");
    m_legendResizeBottom->setToolTip("Drag to resize height");
    m_legendResizeBottom->installEventFilter(this);
    m_legendResizeBottom->hide();  // only visible when floating

    // Legend panel: mid row + bottom strip stacked vertically
    m_legendPanel = new QWidget();
    m_legendPanel->setFixedWidth(m_legendUserWidth);
    QVBoxLayout* legendPanelLayout = new QVBoxLayout(m_legendPanel);
    legendPanelLayout->setContentsMargins(0, 0, 0, 0);
    legendPanelLayout->setSpacing(0);
    legendPanelLayout->addWidget(legendMidRow, 1);
    legendPanelLayout->addWidget(m_legendResizeBottom);

    m_mainLayout->addWidget(m_legendPanel);

}

void PlotWidget::setupChart()
{
    if (m_chart) {
        delete m_chart;
    }
    
    m_chart = new QChart();
    m_chart->setTheme(QChart::ChartThemeLight);
    m_chartView = new ErrorBarChartView(m_chart);
    m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Enable zoom and pan functionality
    m_chartView->setDragMode(QGraphicsView::RubberBandDrag);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    
    // Enable pan with right mouse button by setting drag mode programmatically
    // Pan will be handled in eventFilter for right mouse button
    
    // Install event filter on both the view and its viewport.
    // Mouse events (including moves) are delivered to the viewport widget, not the
    // QChartView itself. Both need the filter so zoom, pan, and hit-testing all work.
    m_chartView->installEventFilter(this);
    m_chartView->viewport()->installEventFilter(this);

    // Enable mouse tracking so MouseMove fires without a button held (needed for hover).
    m_chartView->setMouseTracking(true);
    m_chartView->viewport()->setMouseTracking(true);
    
    // Replace placeholder in left layout
    QLayoutItem* item = m_leftLayout->itemAt(0);
    if (item && item->widget()) {
        QWidget* placeholder = item->widget();
        m_leftLayout->removeWidget(placeholder);
        placeholder->deleteLater();
    }
    m_leftLayout->insertWidget(0, m_chartView, 1);
    
    styleChart();
}

void PlotWidget::styleChart()
{
    if (!m_chart) return;
    
    // Chart styling
    m_chart->setBackgroundBrush(QBrush(Qt::white));
    m_chart->setPlotAreaBackgroundBrush(QBrush(Qt::white));
    m_chart->setPlotAreaBackgroundVisible(true);
    
    // Remove default legend (we have custom legend)
    m_chart->legend()->setVisible(false);
    
    // Grid styling
    m_chartView->setRenderHint(QPainter::Antialiasing);
    
    // Create default axes to show chart framework
    QValueAxis *xAxis = new QValueAxis();
    QValueAxis *yAxis = new QValueAxis();
    
    xAxis->setRange(0, 100);
    yAxis->setRange(0, 100);
    xAxis->setTitleText("");
    yAxis->setTitleText("");
    xAxis->setGridLineVisible(m_showGrid);
    yAxis->setGridLineVisible(m_showGrid);
    xAxis->setLabelsVisible(true);
    yAxis->setLabelsVisible(true);

    // Add minor ticks
    xAxis->setMinorTickCount(4);
    yAxis->setMinorTickCount(4);
    xAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
    yAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);

    m_chart->addAxis(xAxis, Qt::AlignBottom);
    m_chart->addAxis(yAxis, Qt::AlignLeft);

    // Set after addAxis — theme application on addAxis would otherwise override these
    xAxis->setLinePen(QPen(Qt::black));
    yAxis->setLinePen(QPen(Qt::black));
    xAxis->setLabelsBrush(QBrush(Qt::black));
    yAxis->setLabelsBrush(QBrush(Qt::black));
    
    m_chart->setTitle("");
}

void PlotWidget::setupAxes(const QString &xVar)
{
    qDebug() << "PlotWidget::setupAxes() - CALLED with X variable:" << xVar;
    
    // Remove existing axes
    auto existingAxes = m_chart->axes();
    qDebug() << "PlotWidget::setupAxes() - Removing" << existingAxes.size() << "existing axes";
    for (auto axis : existingAxes) {
        m_chart->removeAxis(axis);
    }
    
    // Create appropriate X-axis based on variable type
    QAbstractAxis *xAxis = nullptr;
    if (xVar == "DATE" && m_axisBreaks.isEmpty()) {
        QDateTimeAxis *dateAxis = new QDateTimeAxis();
        dateAxis->setFormat("MMM dd, yyyy");
        dateAxis->setTitleText("Date");
        int optimalTicks = calculateOptimalDateTickCount();
        dateAxis->setTickCount(optimalTicks);
        xAxis = dateAxis;
        qDebug() << "PlotWidget::setupAxes() - Created QDateTimeAxis for DATE variable";
    } else if (xVar == "DATE" && !m_axisBreaks.isEmpty()) {
        // Axis breaks active: use QValueAxis with hidden labels (we paint our own)
        QValueAxis *valueAxis = new QValueAxis();
        valueAxis->setTitleText("Date");
        valueAxis->setLabelsVisible(false); // custom labels drawn in paintEvent
        valueAxis->setTickCount(2);         // just endpoints; we paint our own ticks
        valueAxis->setMinorTickCount(0);
        valueAxis->setMinorGridLineVisible(false);
        xAxis = valueAxis;
        qDebug() << "PlotWidget::setupAxes() - Created QValueAxis (axis-break mode) for DATE variable";
    } else {
        QValueAxis *valueAxis = new QValueAxis();
        
        // Set appropriate title based on variable info from CDE file
        QPair<QString, QString> xVarInfo = DataProcessor::getVariableInfo(xVar);
        QString xTitle;
        // For DAP/DAS the short label is truncated (e.g. "Days after pl") so prefer
        // the full description from DATA.CDE (e.g. "Days after planting (#)"),
        // stripping the DSSAT unit marker at the end.
        if ((xVar == "DAP" || xVar == "DAS") && !xVarInfo.second.isEmpty()) {
            xTitle = xVarInfo.second;
            xTitle = xTitle.remove(" (#)").remove(".").trimmed();
        } else {
            xTitle = xVarInfo.first.isEmpty() ? xVar : xVarInfo.first;
        }
        valueAxis->setTitleText(xTitle);
        
        // Add minor ticks to X value axis
        valueAxis->setMinorTickCount(4);
        valueAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);

        xAxis = valueAxis;
        qDebug() << "PlotWidget::setupAxes() - Created QValueAxis for" << xVar;
    }
    
    // Create Y-axis (always value axis)
    QValueAxis *yAxis = new QValueAxis();
    
    yAxis->setTitleText("");
    
    // Add minor ticks to Y axis
    yAxis->setMinorTickCount(4);
    yAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);

    // Configure axis appearance
    xAxis->setGridLineVisible(m_showGrid);
    yAxis->setGridLineVisible(m_showGrid);
    xAxis->setLabelsVisible(true);
    yAxis->setLabelsVisible(true);

    // Add axes to chart first — Qt theme is applied on addAxis and would override pens set before
    m_chart->addAxis(xAxis, Qt::AlignBottom);
    m_chart->addAxis(yAxis, Qt::AlignLeft);

    // Set colors after addAxis so they are not overridden by the chart theme
    if (auto *dtAxis = qobject_cast<QDateTimeAxis*>(xAxis)) {
        dtAxis->setLinePen(QPen(Qt::black));
        dtAxis->setLabelsBrush(QBrush(Qt::black));
    } else {
        xAxis->setLinePen(QPen(Qt::black));
        xAxis->setLabelsBrush(QBrush(Qt::black));
    }
    yAxis->setLinePen(QPen(Qt::black));
    yAxis->setLabelsBrush(QBrush(Qt::black));
    
    qDebug() << "PlotWidget::setupAxes() - Added axes for X variable:" << xVar;
}

void PlotWidget::enforceAxisColors()
{
    if (!m_chart) return;
    QColor c = m_plotSettings.axisLineColor;
    // Push color to the painter-drawn border lines (overrides Qt theme gray)
    if (m_chartView)
        m_chartView->setAxisLineColor(c);
    // Also set on axis objects for tick marks and labels
    for (auto axis : m_chart->axes()) {
        axis->setLinePenColor(c);
        axis->setLinePen(QPen(c, 1));
        axis->setLabelsBrush(QBrush(Qt::black));
    }
}

void PlotWidget::autoFitAxes()
{
    m_autoFitPending = false;
    m_isZoomed = false;
    
    qDebug() << "PlotWidget::autoFitAxes() - CALLED";
    
    auto series = m_chart->series();
    if (series.isEmpty()) {
        qDebug() << "PlotWidget::autoFitAxes() - No series to fit";
        return;
    }
    
    // Force chart to update its layout before auto-fitting
    if (m_chart) {
        m_chart->update();
    }
    
    // Calculate data bounds
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    bool hasData = false;
    
    for (auto s : series) {
        if (auto lineSeries = qobject_cast<QLineSeries*>(s)) {
            auto points = lineSeries->points();
            for (const QPointF &point : points) {
                minX = qMin(minX, point.x());
                maxX = qMax(maxX, point.x());
                minY = qMin(minY, point.y());
                maxY = qMax(maxY, point.y());
                hasData = true;
            }
        } else if (auto scatterSeries = qobject_cast<QScatterSeries*>(s)) {
            auto points = scatterSeries->points();
            for (const QPointF &point : points) {
                minX = qMin(minX, point.x());
                maxX = qMax(maxX, point.x());
                minY = qMin(minY, point.y());
                maxY = qMax(maxY, point.y());
                hasData = true;
            }
        }
    }
    
    if (!hasData) {
        qDebug() << "PlotWidget::autoFitAxes() - No data points found";
        return;
    }
    
    // Include error bar extent in Y bounds when error bars are enabled
    if (m_plotSettings.showErrorBars) {
        for (const QSharedPointer<PlotData> &plotData : m_plotDataList) {
            if (!plotData || plotData->errorBars.isEmpty()) continue;
            for (const ErrorBarData &errorBar : plotData->errorBars) {
                double yTop = errorBar.meanY + errorBar.errorValue;
                double yBottom = errorBar.meanY - errorBar.errorValue;
                maxY = qMax(maxY, yTop);
                minY = qMin(minY, yBottom);
            }
        }
    }
    
    // Add padding (no padding on origin side, 5% on far side for X, top padding for Y)
    double xRightPadding = (maxX - minX) * 0.05;
    double xLeftPadding = 0;  // No padding on origin side
    double yPadding = (maxY - 0) * 0.05;  // Calculate padding from 0, not minY

    // Handle edge case where all values are the same
    if (xRightPadding == 0) xRightPadding = qAbs(maxX) * 0.1 + 1;
    // No edge case handling needed for xLeftPadding since it's always 0
    if (yPadding == 0) yPadding = maxY * 0.1 + 1;

    // Force Y-axis to start from 0
    minY = 0;
    
    // Apply ranges to axes
    auto axes = m_chart->axes();
    for (auto axis : axes) {
        if (axis->orientation() == Qt::Horizontal) {
            if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
                // In axis-break mode the x coords are virtual; just use tight range with no padding
                if (!m_axisBreaks.isEmpty() && m_currentXVar == "DATE") {
                    double vPad = m_virtualAxisMax * 0.02;
                    valueAxis->setRange(minX - vPad, maxX + vPad);
                    valueAxis->setTickCount(2);
                    valueAxis->setLabelsVisible(false);
                    // Pass break positions to chart view for painting
                    if (m_chartView) {
                        QVector<ErrorBarChartView::BreakInfo> breakInfos;
                        for (int i = 0; i < m_axisBreaks.size(); ++i) {
                            ErrorBarChartView::BreakInfo bi;
                            double segVEnd = m_axisSegments[i].virtualStart + (m_axisSegments[i].end - m_axisSegments[i].start);
                            bi.virtualX  = segVEnd + BREAK_VIRTUAL_WIDTH / 2.0;
                            bi.realStart = m_axisBreaks[i].gapStart;
                            bi.realEnd   = m_axisBreaks[i].gapEnd;
                            breakInfos.append(bi);
                        }
                        QVector<ErrorBarChartView::SegmentInfo> segInfos;
                        for (const AxisSegment &s : m_axisSegments) {
                            ErrorBarChartView::SegmentInfo si;
                            si.virtualStart = s.virtualStart;
                            si.virtualEnd   = s.virtualStart + (s.end - s.start);
                            si.realStart    = s.start;
                            si.realEnd      = s.end;
                            segInfos.append(si);
                        }
                        m_chartView->setAxisBreaks(breakInfos, segInfos);
                    }
                    continue;
                }

                valueAxis->setRange(minX - xLeftPadding, maxX + xRightPadding);

                // Check if this is a day-based variable (DAS, DAP, etc.)
                QString xVarName = m_currentXVar.toUpper();
                if (xVarName.contains("DAS") || xVarName.contains("DAP") ||
                    xVarName.contains("DAY") || xVarName.contains("DATE") ||
                    xVarName == "WYEAR") {
                    // For day variables, use clean round intervals
                    double dataRange = maxX - minX;
                    double tickInterval = calculateNiceXInterval(dataRange);
                    
                    // Align range to clean multiples of the interval
                    double cleanMinX = std::floor(minX / tickInterval) * tickInterval;
                    double cleanMaxX = std::ceil(maxX / tickInterval) * tickInterval;
                    
                    // Snap range to clean multiples then derive tickCount (same strategy as Y axis)
                    int xTickCount = qRound((cleanMaxX - cleanMinX) / tickInterval) + 1;
                    if (xTickCount < 3) xTickCount = 3;
                    valueAxis->setRange(cleanMinX, cleanMaxX);
                    valueAxis->setTickCount(xTickCount);
                    // Label format: user decimals or auto (0 for day-based)
                    if (m_plotSettings.xAxisDecimals >= 0)
                        valueAxis->setLabelFormat(QString("%.%1f").arg(m_plotSettings.xAxisDecimals));
                    else
                        valueAxis->setLabelFormat("%.0f");

                    qDebug() << "PlotWidget::autoFitAxes() - Set X day-based axis: range" << cleanMinX << "to" << cleanMaxX << "interval:" << tickInterval << "tickCount:" << xTickCount;
                } else {
                    // Label format: user decimals or auto (2dp default)
                    if (m_plotSettings.xAxisDecimals >= 0)
                        valueAxis->setLabelFormat(QString("%.%1f").arg(m_plotSettings.xAxisDecimals));
                    else
                        valueAxis->setLabelFormat("%.2f");
                }

                valueAxis->setMinorTickCount(m_plotSettings.xAxisMinorTickCount);
                valueAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
                qDebug() << "PlotWidget::autoFitAxes() - Set X ValueAxis range:" << (minX - xLeftPadding) << "to" << (maxX + xRightPadding);
            } else if (auto dateAxis = qobject_cast<QDateTimeAxis*>(axis)) {
                QDateTime minDateTime = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(minX - xLeftPadding));
                QDateTime maxDateTime = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(maxX + xRightPadding));
                dateAxis->setRange(minDateTime, maxDateTime);
                int optimalTicks = calculateOptimalDateTickCount();
                dateAxis->setTickCount(optimalTicks);
                dateAxis->setLinePen(QPen(Qt::black));
                dateAxis->setLabelsBrush(QBrush(Qt::black));
                qDebug() << "PlotWidget::autoFitAxes() - Set X DateTimeAxis range:" << minDateTime.toString() << "to" << maxDateTime.toString();
            }
        } else if (axis->orientation() == Qt::Vertical) {
            if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
                // Start with data maximum and minimal padding (5%)
                double dataMax = maxY;
                double minimalPadding = dataMax * 0.05;
                double targetMax = dataMax + minimalPadding;

                // Use user-defined tick spacing if set, otherwise auto
                double tickInterval;
                if (m_plotSettings.yAxisTickSpacing > 0.0) {
                    tickInterval = m_plotSettings.yAxisTickSpacing;
                } else {
                    tickInterval = calculateNiceYInterval(targetMax);
                }

                // Snap max to a clean multiple of the interval so tick labels are always round
                double alignedMax = std::ceil(targetMax / tickInterval) * tickInterval;
                if (alignedMax <= dataMax) alignedMax += tickInterval;

                // Derive tickCount: number of ticks = number of intervals + 1
                // e.g. range 0–10000, interval 1000 → 10 intervals → 11 ticks (0,1000,...,10000)
                int numIntervals = qRound(alignedMax / tickInterval);
                if (numIntervals < 2) numIntervals = 2;
                int tickCount = numIntervals + 1;

                valueAxis->setRange(0, alignedMax);
                valueAxis->setTickCount(tickCount);
                valueAxis->setMinorTickCount(m_plotSettings.yAxisMinorTickCount);
                valueAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
                if (m_chartView && !m_chartView->boxPlotStats().isEmpty())
                    m_chartView->setBoxPlotYBounds(0, alignedMax);

                // Label format: user decimals or auto-detect from tick interval
                if (m_plotSettings.yAxisDecimals >= 0) {
                    valueAxis->setLabelFormat(QString("%.%1f").arg(m_plotSettings.yAxisDecimals));
                } else {
                    // Pick enough decimal places so tick labels are distinct and not truncated.
                    // Values >= 100 never need decimals regardless of tick interval.
                    int autoDecimals = 0;
                    if (alignedMax < 100.0) {
                        if (tickInterval < 0.01)       autoDecimals = 4;
                        else if (tickInterval < 0.1)   autoDecimals = 3;
                        else if (tickInterval < 1.0)   autoDecimals = 2;
                        else if (tickInterval < 10.0)  autoDecimals = 1;
                        else                           autoDecimals = 0;
                    }
                    valueAxis->setLabelFormat(QString("%.%1f").arg(autoDecimals));
                }

                qDebug() << "PlotWidget::autoFitAxes() - Y-axis: alignedMax=" << alignedMax << "interval=" << tickInterval << "tickCount=" << tickCount;
            }
        }
    }
    
    qDebug() << "PlotWidget::autoFitAxes() - Completed with data bounds: X(" << minX << "," << maxX << ") Y(" << minY << "," << maxY << ")";

    // Apply user-defined axis range overrides (override auto-fit result)
    if (!m_isScatterMode) {
        auto hAxesAf = m_chart->axes(Qt::Horizontal);
        auto vAxesAf = m_chart->axes(Qt::Vertical);
        if (!hAxesAf.isEmpty()) {
            if (auto xAx = qobject_cast<QValueAxis*>(hAxesAf.first())) {
                // Discard overrides that look like epoch milliseconds on a numeric axis
                if (m_plotSettings.xAxisMin > 1e10) { m_plotSettings.useCustomXMin = false; m_plotSettings.xAxisMin = xAx->min(); }
                if (m_plotSettings.xAxisMax > 1e10) { m_plotSettings.useCustomXMax = false; m_plotSettings.xAxisMax = xAx->max(); }
                double lo = xAx->min(), hi = xAx->max();
                if (m_plotSettings.useCustomXMin) lo = m_plotSettings.xAxisMin;
                if (m_plotSettings.useCustomXMax) hi = m_plotSettings.xAxisMax;
                if (m_plotSettings.useCustomXMin || m_plotSettings.useCustomXMax) {
                    double tickInterval = m_plotSettings.xAxisTickSpacing > 0.0
                        ? m_plotSettings.xAxisTickSpacing
                        : calculateNiceXInterval(hi - lo);
                    double snappedLo = std::floor(lo / tickInterval) * tickInterval;
                    double snappedHi = std::ceil(hi / tickInterval) * tickInterval;
                    int tickCount = qRound((snappedHi - snappedLo) / tickInterval) + 1;
                    if (tickCount < 3) tickCount = 3;
                    xAx->setRange(snappedLo, snappedHi);
                    xAx->setTickCount(tickCount);
                }
            } else if (auto xDtAx = qobject_cast<QDateTimeAxis*>(hAxesAf.first())) {
                QDateTime lo = xDtAx->min(), hi = xDtAx->max();
                if (m_plotSettings.useCustomXMin) lo = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m_plotSettings.xAxisMin));
                if (m_plotSettings.useCustomXMax) hi = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m_plotSettings.xAxisMax));
                if (m_plotSettings.useCustomXMin || m_plotSettings.useCustomXMax)
                    xDtAx->setRange(lo, hi);
                xDtAx->setLinePen(QPen(Qt::black));
                xDtAx->setLabelsBrush(QBrush(Qt::black));
            }
        }
        if (!vAxesAf.isEmpty()) {
            if (auto yAx = qobject_cast<QValueAxis*>(vAxesAf.first())) {
                double lo = yAx->min(), hi = yAx->max();
                if (m_plotSettings.useCustomYMin) lo = m_plotSettings.yAxisMin;
                if (m_plotSettings.useCustomYMax) hi = m_plotSettings.yAxisMax;
                if (m_plotSettings.useCustomYMin || m_plotSettings.useCustomYMax) {
                    // Recalculate a clean tick interval for the new range so labels stay round
                    double range = hi - lo;
                    double tickInterval = m_plotSettings.yAxisTickSpacing > 0.0
                        ? m_plotSettings.yAxisTickSpacing
                        : calculateNiceYInterval(hi);
                    // Snap hi up to the nearest clean multiple of the interval
                    double snappedHi = std::ceil(hi / tickInterval) * tickInterval;
                    double snappedLo = (lo == 0.0) ? 0.0 : std::floor(lo / tickInterval) * tickInterval;
                    int tickCount = qRound((snappedHi - snappedLo) / tickInterval) + 1;
                    if (tickCount < 3) tickCount = 3;
                    yAx->setRange(snappedLo, snappedHi);
                    yAx->setTickCount(tickCount);
                    if (m_chartView && !m_chartView->boxPlotStats().isEmpty())
                        m_chartView->setBoxPlotYBounds(snappedLo, snappedHi);
                }
            }
        }
    }

    // Force chart to repaint with new axis settings
    if (m_chart) {
        m_chart->update();
    }

    QTimer::singleShot(50, this, &PlotWidget::enforceAxisColors);
}

double PlotWidget::calculateNiceMax(double rawMax)
{
    if (rawMax <= 0) return 10;
    
    // Find the order of magnitude
    double magnitude = std::pow(10, std::floor(std::log10(rawMax)));
    double normalized = rawMax / magnitude;
    
    // Round up to nice numbers: 1, 2, 5, 10
    double niceNormalized;
    if (normalized <= 1) niceNormalized = 1;
    else if (normalized <= 2) niceNormalized = 2;
    else if (normalized <= 5) niceNormalized = 5;
    else niceNormalized = 10;
    
    return niceNormalized * magnitude;
}

double PlotWidget::calculateNiceInterval(double max)
{
    if (max <= 0) return 1;
    
    // Target about 10-15 ticks for better readability
    double rawInterval = max / 12;
    
    // Find magnitude
    double magnitude = std::pow(10, std::floor(std::log10(rawInterval)));
    double normalized = rawInterval / magnitude;
    
    // Round to nice intervals: 1, 2, 2.5, 5 with preference for smaller intervals
    double niceNormalized;
    if (normalized <= 1) niceNormalized = 1;
    else if (normalized <= 1.5) niceNormalized = 1;
    else if (normalized <= 2) niceNormalized = 2;
    else if (normalized <= 2.5) niceNormalized = 2;
    else if (normalized <= 4) niceNormalized = 2.5;
    else if (normalized <= 5) niceNormalized = 5;
    else niceNormalized = 5;
    
    return niceNormalized * magnitude;
}

double PlotWidget::calculateNiceXInterval(double range)
{
    if (range <= 0) return 1;
    
    // Define clean interval options (in order of preference)
    QVector<double> cleanIntervals = {1, 2, 5, 10, 15, 20, 25, 30, 50, 100, 150, 200, 250, 500, 1000};
    
    // Target 8-15 ticks for good readability
    double targetTicks = 12.0;
    double idealInterval = range / targetTicks;
    
    // Find the closest clean interval that gives us reasonable tick count
    double bestInterval = cleanIntervals.last(); // Default to largest
    for (double interval : cleanIntervals) {
        double tickCount = range / interval;
        // Accept intervals that give us 6-20 ticks (prefer 8-15)
        if (tickCount >= 6 && tickCount <= 20) {
            bestInterval = interval;
            break; // Take the first (smallest) acceptable interval
        }
    }
    
    qDebug() << "calculateNiceXInterval: range=" << range << "ideal=" << idealInterval << "chosen=" << bestInterval << "ticks=" << (range/bestInterval);
    return bestInterval;
}

double PlotWidget::calculateNiceYInterval(double max)
{
    if (max <= 0) return 1;
    
    // Define clean Y-axis interval options
    QVector<double> cleanIntervals = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000, 20000, 25000, 50000, 100000};
    
    // Target 8-12 ticks for Y-axis
    double targetTicks = 10.0;
    double idealInterval = max / targetTicks;
    
    // Find the closest clean interval
    double bestInterval = cleanIntervals.last(); // Default to largest
    for (double interval : cleanIntervals) {
        double tickCount = max / interval;
        // Accept intervals that give us 6-15 ticks
        if (tickCount >= 6 && tickCount <= 15) {
            bestInterval = interval;
            break; // Take the first (smallest) acceptable interval
        }
    }
    
    qDebug() << "calculateNiceYInterval: max=" << max << "ideal=" << idealInterval << "chosen=" << bestInterval << "ticks=" << (max/bestInterval);
    return bestInterval;
}

int PlotWidget::calculateOptimalDateTickCount() const
{
    // Get current plot area size
    QSizeF plotSize = m_chart->plotArea().size();
    double plotWidth = plotSize.width();
    
    // Fallback to widget size if plot area not available yet
    if (plotWidth <= 0) {
        plotWidth = this->width() * 0.8; // Estimate plot area as 80% of widget width
    }
    
    qDebug() << "PlotWidget::calculateOptimalDateTickCount() - Plot width:" << plotWidth;
    
    // Base calculation: "MMM dd, yyyy" labels are ~110px wide at typical DPI,
    // so use 110px per tick to prevent Qt from clipping tick labels.
    double pixelsPerTick = 110.0;
    int baseTicks = qMax(4, qRound(plotWidth / pixelsPerTick));

    // Apply size-based scaling
    int optimalTicks;
    if (plotWidth < 400) {
        // Small plot: fewer ticks to avoid crowding
        optimalTicks = qBound(3, baseTicks, 5);
    } else if (plotWidth < 800) {
        // Medium plot: standard tick count
        optimalTicks = qBound(4, baseTicks, 8);
    } else if (plotWidth < 1200) {
        // Large plot: more ticks for detail
        optimalTicks = qBound(5, baseTicks, 10);
    } else {
        // Very large plot: maximum detail
        optimalTicks = qBound(6, baseTicks, 13);
    }
    
    qDebug() << "PlotWidget::calculateOptimalDateTickCount() - Width:" << plotWidth 
             << "Base ticks:" << baseTicks << "Optimal ticks:" << optimalTicks;
    
    return optimalTicks;
}

// resizeScatterPanels -> see PlotWidget_Scatter.cpp

void PlotWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // Update date axis tick count when plot is resized
    if (m_chart) {
        auto axes = m_chart->axes(Qt::Horizontal);
        for (auto axis : axes) {
            if (auto dateAxis = qobject_cast<QDateTimeAxis*>(axis)) {
                int newTickCount = calculateOptimalDateTickCount();
                dateAxis->setTickCount(newTickCount);
                break;
            }
        }
    }

    // Keep scatter panels square on window resize
    if (m_isScatterMode && m_scatterPanelContainer && m_scatterPanelContainer->isVisible())
        resizeScatterPanels();
}

void PlotWidget::plotTimeSeries(
    const DataTable &simData,
    const QString &selectedFolder,
    const QStringList &selectedOutFiles,
    const QString &selectedExperiment,
    const QStringList &selectedTreatments,
    const QString &xVar,
    const QStringList &yVars,
    const DataTable &obsData,
    const QMap<QString, QMap<QString, QString>> &treatmentNames)
{
    m_isScatterMode = false;

    // Restore legend panel (may have been hidden by scatter mode)
    if (m_legendPanel) m_legendPanel->setVisible(true);

    // Show DAS, DAP, DATE buttons for time series plots (they're applicable)
    setXAxisButtonsVisible(true);

    // Show Box Plot toggle only for OSU seasonal summary files
    {
        bool isSummaryOsu = simData.columnNames.contains("WYEAR") &&
                            !simData.columnNames.contains("DAS") &&
                            !simData.columnNames.contains("DAP");
        setBoxPlotButtonVisible(isSummaryOsu);
    }

    try {
        clear();  // also resets legend stack to page 0

        // Switch legend area to show the legend (page 1)
        if (m_legendStack) m_legendStack->setCurrentIndex(1);

        m_simData = simData;
        m_obsData = obsData;
        
        // Debug data assignment
        
        m_selectedFolder = selectedFolder;
        m_selectedExperiment = selectedExperiment;
        m_currentTreatments = selectedTreatments;

        // If X axis type changes (date <-> numeric), discard stale range overrides
        bool prevIsDate = (m_currentXVar.toUpper() == "DATE");
        bool newIsDate  = (xVar.toUpper() == "DATE");
        if (prevIsDate != newIsDate) {
            m_plotSettings.useCustomXMin = false;
            m_plotSettings.useCustomXMax = false;
        }

        m_currentXVar = xVar;
        m_currentYVars = yVars;
        m_treatmentNames = treatmentNames;

        // Reset series filter when dataset or variables change
        // (filter persists only for same data + same Y vars, or via DAS/DAP/DATE switches)
        {
            QMap<QString, QStringList> newExpTrts;
            const DataColumn *trtCheck = m_simData.getColumn("TRT");
            const DataColumn *expCheck = m_simData.getColumn("EXPERIMENT");
            if (trtCheck) {
                QSet<QString> seenPairs;
                for (int i = 0; i < m_simData.rowCount; ++i) {
                    QString trtId = trtCheck->data.value(i).toString();
                    if (trtId.isEmpty()) continue;
                    QString expId = m_selectedExperiment;
                    if (expCheck && i < expCheck->data.size()) {
                        QString e = expCheck->data[i].toString();
                        if (!e.isEmpty()) expId = e;
                    }
                    QString pk = expId + "::" + trtId;
                    if (!seenPairs.contains(pk)) {
                        seenPairs.insert(pk);
                        newExpTrts[expId].append(trtId);
                    }
                }
            }
            if (newExpTrts != m_plotSettings.experimentTreatments || yVars != m_plotSettings.lastYVars) {
                m_plotSettings.excludedSeriesKeys.clear();
                m_plotSettings.experimentTreatments = newExpTrts;
                m_plotSettings.availableExperiments = newExpTrts.keys();
                m_plotSettings.availableYVars = yVars;
                m_plotSettings.lastYVars = yVars;

                // Build treatment display names
                m_plotSettings.treatmentDisplayNames.clear();
                for (auto it = newExpTrts.constBegin(); it != newExpTrts.constEnd(); ++it) {
                    for (const QString &trtId : it.value())
                        m_plotSettings.treatmentDisplayNames[it.key() + "::" + trtId] =
                            getTreatmentDisplayName(trtId, it.key());
                }

                // Build Y variable display names
                for (const QString &yVar : yVars) {
                    if (!m_plotSettings.yVarDisplayNames.contains(yVar)) {
                        QPair<QString, QString> vi = DataProcessor::getVariableInfo(yVar);
                        if (vi.first.isEmpty() && yVar.length() > 1)
                            vi = DataProcessor::getVariableInfo(yVar.left(yVar.length() - 1));
                        m_plotSettings.yVarDisplayNames[yVar] = vi.first.isEmpty() ? yVar : vi.first;
                    }
                }

                // Refresh the panel tree if it is currently visible
            }
        }

        if (m_simData.rowCount == 0) {
            qWarning() << "PlotWidget: No simulated data available";
            return;
        }

        // Initial plot generation with scaling
        updatePlotWithScaling();

        if (m_obsData.rowCount > 0) {
            qDebug() << "PlotWidget: Current Y vars:" << m_currentYVars;
            qDebug() << "PlotWidget: Current treatments:" << m_currentTreatments;
            calculateMetrics();
        } else {
            qDebug() << "PlotWidget: No observed data for metrics calculation";
        }

        emit plotUpdated();

    } catch (const std::exception& e) {
        QString error = QString("Error in plotTimeSeries: %1").arg(e.what());
        qWarning() << error;
        emit errorOccurred(error);
    }
}





QMap<QString, QMap<QString, ScalingInfo>> PlotWidget::calculateScalingFactors(const DataTable &simData, const DataTable &obsData, 
                                                               const QStringList &yVars)
{
    QMap<QString, QMap<QString, ScalingInfo>> scaleFactors;
    
    qDebug() << "PlotWidget: calculateScalingFactors called with" << yVars.size() << "variables:" << yVars;
    
    // If only one variable, no scaling needed (matching Python logic)
    if (yVars.size() <= 1) {
        qDebug() << "PlotWidget: Single variable detected, no scaling applied";
        for (const QString &var : yVars) {
            ScalingInfo info;
            info.scaleFactor = 1.0;
            info.offset = 0.0;
            info.originalUnit = "";
            scaleFactors["default"][var] = info;
        }
        return scaleFactors;
    }
    
    qDebug() << "PlotWidget: Multiple variables detected, calculating scaling factors...";
    
    QMap<QString, double> magnitudes;
    QMap<QString, double> maxValues;
    
    // Collect statistics from both simulated and observed data
    for (const QString &var : yVars) {
        QVector<double> values;
        
        // Collect values from simulated data
        const DataColumn *simColumn = simData.getColumn(var);
        if (simColumn) {
            for (const QVariant &val : simColumn->data) {
                if (!DataProcessor::isMissingValue(val)) {
                    bool ok;
                    double numVal = val.toDouble(&ok);
                    if (ok) values.append(numVal);
                }
            }
        }
        
        // Collect values from observed data
        const DataColumn *obsColumn = obsData.getColumn(var);
        if (obsColumn) {
            for (const QVariant &val : obsColumn->data) {
                if (!DataProcessor::isMissingValue(val)) {
                    bool ok;
                    double numVal = val.toDouble(&ok);
                    if (ok) values.append(numVal);
                }
            }
        }
        
        if (values.isEmpty()) {
            qDebug() << "PlotWidget: No valid values found for variable" << var;
            continue;
        }
        
        double minVal = *std::min_element(values.begin(), values.end());
        double maxVal = *std::max_element(values.begin(), values.end());
        
        // Skip if constant values or very small range
        if (qAbs(maxVal - minVal) < 1e-10) {
            qDebug() << "PlotWidget: Variable" << var << "has constant values, skipping scaling";
            continue;
        }
        
        // Calculate magnitude (log10 of mean of absolute non-zero values)
        QVector<double> absValues;
        for (double val : values) {
            if (qAbs(val) > 1e-10) {
                absValues.append(qAbs(val));
            }
        }
        
        if (!absValues.isEmpty()) {
            double meanAbs = std::accumulate(absValues.begin(), absValues.end(), 0.0) / absValues.size();
            if (meanAbs > 0) {
                magnitudes[var] = std::floor(std::log10(meanAbs));
                maxValues[var] = maxVal;
                qDebug() << "PlotWidget: Variable" << var << "- values count:" << values.size() 
                         << "min:" << minVal << "max:" << maxVal << "meanAbs:" << meanAbs 
                         << "magnitude:" << magnitudes[var] << "(from sim + obs data)";
            } else {
                qDebug() << "PlotWidget: Variable" << var << "has zero mean absolute value, skipping";
            }
        } else {
            qDebug() << "PlotWidget: Variable" << var << "has no non-zero values, skipping";
        }
    }
    
    // Find target maximum from all data
    QVector<double> allMaxes;
    for (const QString &var : yVars) {
        const DataColumn *simColumn = simData.getColumn(var);
        if (simColumn) {
            for (const QVariant &val : simColumn->data) {
                if (!DataProcessor::isMissingValue(val)) {
                    bool ok;
                    double numVal = val.toDouble(&ok);
                    if (ok) allMaxes.append(numVal);
                }
            }
        }
        
        const DataColumn *obsColumn = obsData.getColumn(var);
        if (obsColumn) {
            for (const QVariant &val : obsColumn->data) {
                if (!DataProcessor::isMissingValue(val)) {
                    bool ok;
                    double numVal = val.toDouble(&ok);
                    if (ok) allMaxes.append(numVal);
                }
            }
        }
    }
    
    double targetMax = allMaxes.isEmpty() ? std::numeric_limits<double>::infinity() : 
                      *std::max_element(allMaxes.begin(), allMaxes.end());
    double targetThreshold = targetMax * 1.1;
    
    // Calculate scaling factors
    qDebug() << "PlotWidget: Target threshold:" << targetThreshold << "Magnitudes found:" << magnitudes.size();
    if (magnitudes.size() >= 2) {
        double referenceMagnitude = 0;
        if (!magnitudes.isEmpty()) {
            // Use MAXIMUM magnitude as reference to scale smaller values UP
            referenceMagnitude = *std::max_element(magnitudes.begin(), magnitudes.end());
        }
        qDebug() << "PlotWidget: Reference magnitude (maximum):" << referenceMagnitude;
        
        for (auto it = magnitudes.begin(); it != magnitudes.end(); ++it) {
            QString var = it.key();
            double magnitude = it.value();
            
            double scaleFactor = std::pow(10.0, referenceMagnitude - magnitude);
            qDebug() << "PlotWidget: Variable" << var << "initial scale factor:" << scaleFactor;
            
            // Ensure scale factor is reasonable (between 0.001 and 1000)
            if (scaleFactor > 1000.0) {
                scaleFactor = 1000.0;
                qDebug() << "PlotWidget: Clamped scale factor to maximum 1000 for variable" << var;
            } else if (scaleFactor < 0.001) {
                scaleFactor = 0.001;
                qDebug() << "PlotWidget: Clamped scale factor to minimum 0.001 for variable" << var;
            }
            
            // Additional check: if scaled max would be too large, reduce scale factor
            if (maxValues.contains(var)) {
                double scaledMax = maxValues[var] * scaleFactor;
                while (scaledMax > targetThreshold && scaleFactor > 0.001) {
                    scaleFactor /= 10.0;
                    scaledMax = maxValues[var] * scaleFactor;
                }
                qDebug() << "PlotWidget: Variable" << var << "final scale factor:" << scaleFactor;
            }
            
            ScalingInfo info;
            info.scaleFactor = scaleFactor;
            info.offset = 0.0;
            info.originalUnit = "";
            scaleFactors["default"][var] = info;
        }
    } else if (magnitudes.size() == 1) {
        // Single variable - still add to scale factors with factor 1.0 for consistency
        QString var = magnitudes.keys().first();
        ScalingInfo info;
        info.scaleFactor = 1.0;
        info.offset = 0.0;
        info.originalUnit = "";
        scaleFactors["default"][var] = info;
        qDebug() << "PlotWidget: Single variable" << var << "assigned scale factor 1.0";
    }
    
    // Add default scaling for remaining variables
    for (const QString &var : yVars) {
        if (!scaleFactors["default"].contains(var)) {
            ScalingInfo info;
            info.scaleFactor = 1.0;
            info.offset = 0.0;
            info.originalUnit = "";
            scaleFactors["default"][var] = info;
            qDebug() << "PlotWidget: Variable" << var << "assigned default scale factor 1.0";
        }
    }
    
    qDebug() << "PlotWidget: Final scaling factors summary:";
    bool hasSignificantScaling = false;
    for (const QString &var : yVars) {
        if (scaleFactors["default"].contains(var)) {
            const ScalingInfo &info = scaleFactors["default"][var];
            qDebug() << "  " << var << ": scale =" << info.scaleFactor << ", offset =" << info.offset;
            if (qAbs(info.scaleFactor - 1.0) > 0.01) {
                hasSignificantScaling = true;
            }
        }
    }
    
    if (yVars.size() > 1 && !hasSignificantScaling && magnitudes.size() >= 2) {
        // Check if variables have very different magnitudes but scaling wasn't applied
        double minMag = *std::min_element(magnitudes.begin(), magnitudes.end());
        double maxMag = *std::max_element(magnitudes.begin(), magnitudes.end());
        if (maxMag - minMag >= 2) {
            qDebug() << "PlotWidget: Variables have very different magnitudes (" << minMag << " to " << maxMag << ") but scaling was not applied";
        }
    }
    
    return scaleFactors;
}

DataTable PlotWidget::applyScaling(const DataTable &data, const QStringList &yVars)
{
    DataTable scaledData = data;
    
    // Clear previous scaling factors for label
    m_appliedScalingFactors.clear();
    qDebug() << "PlotWidget: applyScaling called for variables:" << yVars;
    qDebug() << "PlotWidget: Available scale factors keys:" << m_scaleFactors.keys();
    if (m_scaleFactors.contains("default")) {
        qDebug() << "PlotWidget: Default scale factors for variables:" << m_scaleFactors["default"].keys();
    }
    
    for (const QString &var : yVars) {
        if (!m_scaleFactors.contains("default") || !m_scaleFactors["default"].contains(var)) {
            qDebug() << "PlotWidget: No scale factor found for variable:" << var;
            continue;
        }
        
        const ScalingInfo &info = m_scaleFactors["default"][var];
        qDebug() << "PlotWidget: Applying scaling to" << var << "- factor:" << info.scaleFactor << "offset:" << info.offset;
        
        // Always store the scale factor for label display, even if it's 1.0
        m_appliedScalingFactors[var] = info.scaleFactor;
        qDebug() << "PlotWidget: Stored scaling factor for label:" << var << "=" << info.scaleFactor;
        
        if (qAbs(info.scaleFactor - 1.0) < 0.001 && qAbs(info.offset) < 0.001) {
            qDebug() << "PlotWidget: No significant scaling needed for" << var << "- factor:" << info.scaleFactor;
            continue;
        }
        
        qDebug() << "PlotWidget: WILL APPLY SCALING to" << var << "- factor:" << info.scaleFactor;

        DataColumn *column = scaledData.getColumn(var);
        if (!column) {
            qDebug() << "PlotWidget: Column not found for variable:" << var;
            continue;
        }

        QString originalVarName = var + "_original";
        if (!scaledData.getColumn(originalVarName)) {
            DataColumn originalColumn(originalVarName);
            originalColumn.data = column->data;
            scaledData.addColumn(originalColumn);
            qDebug() << "PlotWidget: Created backup column:" << originalVarName;
            
            // Get column reference again after adding backup column
            column = scaledData.getColumn(var);
            if (!column) {
                qDebug() << "PlotWidget: Column disappeared after backup creation:" << var;
                continue;
            }
        }
        
        int scaledCount = 0;
        double sampleOriginal = 0, sampleScaled = 0;
        bool hasSample = false;

        // Fix: Use index-based loop instead of reference-based to ensure modification
        for (int i = 0; i < column->data.size(); ++i) {
            QVariant &val = column->data[i];
            if (!DataProcessor::isMissingValue(val)) {
                bool ok;
                double numVal = val.toDouble(&ok);
                if (ok && qAbs(numVal) > 1e-10) { // Only scale non-zero values
                    if (!hasSample) {
                        sampleOriginal = numVal;
                        hasSample = true;
                    }
                    double scaledVal = numVal * info.scaleFactor + info.offset;

                    // Scaling factor already stored earlier for label display

                    val = QVariant(scaledVal); // Explicit QVariant construction
                    scaledCount++;
                    if (scaledCount == 1) {
                        sampleScaled = scaledVal;
                    }
                }
            }
        }
        qDebug() << "PlotWidget: Scaled" << scaledCount << "values for variable" << var;
        if (hasSample) {
            qDebug() << "PlotWidget: Sample transformation:" << var << ":" << sampleOriginal << "->" << sampleScaled;
        }
    }
    
    return scaledData;
}

QVector<ErrorBarData> PlotWidget::aggregateReplicates(const QVector<QPointF> &points, const QString &xVar, double xTolerance)
{
    QVector<ErrorBarData> errorBars;
    if (points.isEmpty()) return errorBars;
    
    // Determine appropriate tolerance based on X variable type
    double effectiveTolerance = xTolerance;
    if (xVar == "DATE") {
        // For DATE, group by same day (24 hours = 86400000 milliseconds)
        effectiveTolerance = 86400000.0;  // 1 day in milliseconds
    }
    
    // Group points by X value (with tolerance)
    QMap<double, QVector<double>> groups;  // Map of rounded X -> list of Y values
    
    for (const QPointF &point : points) {
        // Round X to nearest tolerance to group replicates
        double roundedX = qRound(point.x() / effectiveTolerance) * effectiveTolerance;
        groups[roundedX].append(point.y());
    }
    
    // Calculate statistics for each group
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const QVector<double> &yValues = it.value();
        if (yValues.isEmpty()) continue;
        
        double meanX = it.key();
        
        // Calculate mean Y
        double sumY = 0.0;
        for (double y : yValues) {
            sumY += y;
        }
        double meanY = sumY / yValues.size();
        
        // Calculate standard deviation
        double sumSquaredDiff = 0.0;
        for (double y : yValues) {
            double diff = y - meanY;
            sumSquaredDiff += diff * diff;
        }
        double variance = yValues.size() > 1 ? sumSquaredDiff / (yValues.size() - 1) : 0.0;
        double sd = qSqrt(variance);
        
        ErrorBarData errorBar;
        errorBar.meanX = meanX;
        errorBar.meanY = meanY;
        errorBar.errorValue = sd;  // Will be converted to SE later if needed
        errorBar.n = yValues.size();
        
        errorBars.append(errorBar);
    }
    
    // Sort by X value
    std::sort(errorBars.begin(), errorBars.end(), 
              [](const ErrorBarData &a, const ErrorBarData &b) {
                  return a.meanX < b.meanX;
              });
    
    return errorBars;
}

void PlotWidget::plotDatasets(const DataTable &simData, const DataTable &obsData,
                             const QString &xVar, const QStringList &yVars, 
                             const QStringList &treatments, const QString &selectedExperiment)
{
    // Clear existing chart and set up appropriate axes
    clearChart();
    setupAxes(xVar);

    // Reset chart margins to default
    if (m_chart) {
        m_chart->setMargins(QMargins(0, 0, 0, 0));  // Use default margins
    }

    qDebug() << "PlotWidget::plotDatasets() - ENTRY with Y vars:" << yVars;
    
    // Debug: Check if we're receiving scaled data
    for (const QString &yVar : yVars) {
        const DataColumn *column = simData.getColumn(yVar);
        if (column && !column->data.isEmpty()) {
            qDebug() << "PlotWidget::plotDatasets() - First value of" << yVar << "=" << column->data[0];
        }
    }
    
    if (!m_chart) return;
    
    // Clear all series (already done in clearChart(), but ensure it's cleared)
    m_chart->removeAllSeries();
    
    // Clear error bars when replotting
    if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
    }
    
    // Clear treatment color map to ensure consistent color assignment
    m_treatmentColorMap.clear();
    
    QVector<PlotData> plotDataList;
    int colorIndex = 0;
    QSet<QString> simulatedTreatmentKeys; // Collect treatment keys (including run info) from simulated data
    
    // Keep track of which actual sim TRT corresponds to the matchKey for SQ
    QMap<QString, QString> sqDateToSimTrt;
    
    qDebug() << "PlotWidget: plotDatasets - Plotting simulated data...";
    // Plot simulated data
    for (const QString &yVar : yVars) {
        const DataColumn *xColumn = simData.getColumn(xVar);
        const DataColumn *yColumn = simData.getColumn(yVar);
        const DataColumn *trtColumn = simData.getColumn("TRT");
        const DataColumn *expColumn = simData.getColumn("EXPERIMENT");
        
        if (!xColumn || !yColumn || !trtColumn) {
            qDebug() << "PlotWidget: Missing column for simulated data:" << xVar << "," << yVar << "or TRT";
            qDebug() << "PlotWidget: Available columns:" << simData.columnNames;
            continue;
        }
        
        qDebug() << "PlotWidget: EXPERIMENT column exists:" << (expColumn != nullptr);
        
        // Group data by experiment and treatment combination
        QMap<QString, QVector<QPointF>> experimentTreatmentData;

        // Detect summary OSU files (seasonal/yearly rows, no time-series DAS/DAP).
        // For these files the RUN column enumerates one row per year — do NOT split by RUN.
        bool isSummaryOsu = simData.columnNames.contains("WYEAR") &&
                            !simData.columnNames.contains("DAS") &&
                            !simData.columnNames.contains("DAP");

        // Detect sequence OSU: all TRT values identical but R# column has multiple unique values.
        // For these files each R# slot is a distinct crop in the rotation.
        const DataColumn *rseqColumn = simData.getColumn("R#");
        const DataColumn *tnameColumnSim = simData.getColumn("TNAME");
        bool isSequenceOsu = false;
        if (isSummaryOsu && rseqColumn) {
            QSet<QString> uniqueTrts, uniqueRseq;
            for (const QVariant &v : trtColumn->data) uniqueTrts.insert(v.toString().trimmed());
            for (const QVariant &v : rseqColumn->data) uniqueRseq.insert(v.toString().trimmed());
            isSequenceOsu = (uniqueTrts.size() == 1) && (uniqueRseq.size() > 1);
        }

        for (int row = 0; row < simData.rowCount; ++row) {
            if (row >= xColumn->data.size() || row >= yColumn->data.size() || row >= trtColumn->data.size()) {
                continue;
            }

            QString trt = trtColumn->data[row].toString();

            // For sequence OSU, use R# slot as the effective treatment key
            QString rseqSlot;
            if (isSequenceOsu && rseqColumn && row < rseqColumn->data.size())
                rseqSlot = rseqColumn->data[row].toString().trimmed();
            QString effectiveTrt = isSequenceOsu ? rseqSlot : trt;

            // Get experiment for this row, or use selectedExperiment as fallback
            QString experiment = selectedExperiment;
            if (expColumn && row < expColumn->data.size()) {
                QString expFromData = expColumn->data[row].toString();
                if (!expFromData.isEmpty()) {
                    experiment = expFromData;
                }
            }

            // Treatment filter: match plain trt/slot or compound R#::slot key
            if (!treatments.isEmpty() && !treatments.contains("All")) {
                if (isSequenceOsu) {
                    if (!treatments.contains("R#::" + effectiveTrt)) continue;
                } else {
                    if (!treatments.contains(trt) && !treatments.contains(experiment + "::" + trt)) continue;
                }
            }

            // Per-variable filter: skip if this var::exp::trt is excluded
            if (m_plotSettings.excludedSeriesKeys.contains(yVar + "::" + experiment + "::" + effectiveTrt)) {
                continue;
            }

            // Get crop for this row if available
            QString crop = "XX";  // Default crop code
            const DataColumn* cropColumn = simData.getColumn("CROP");
            if (cropColumn && row < cropColumn->data.size()) {
                QString cropFromData = cropColumn->data[row].toString();
                if (!cropFromData.isEmpty()) {
                    crop = cropFromData;
                }
            }

            // Create unique key for crop-experiment-treatment(+run) combination
            QString runStr;
            const DataColumn* runColumn = simData.getColumn("RUN");
            if (runColumn && row < runColumn->data.size()) {
                QString rv = runColumn->data[row].toString();
                if (!rv.isEmpty()) {
                    runStr = QString("RUN%1").arg(rv);
                }
            }
            // For summary OSU files each row is one year; don't split by RUN.
            // For sequence OSU, split by P# (rep) unless plotMeanReps is on.
            QString expTrtKey;
            if (isSequenceOsu) {
                if (m_plotSettings.plotMeanReps) {
                    // Merge all reps into one key per R# slot — will be averaged below
                    expTrtKey = QString("%1__%2__%3").arg(crop).arg(experiment).arg(effectiveTrt);
                } else {
                    const DataColumn* pnumCol = simData.getColumn("P#");
                    QString pnum = (pnumCol && row < pnumCol->data.size())
                        ? pnumCol->data[row].toString().trimmed() : QString();
                    expTrtKey = pnum.isEmpty()
                        ? QString("%1__%2__%3").arg(crop).arg(experiment).arg(effectiveTrt)
                        : QString("%1__%2__%3__P%4").arg(crop).arg(experiment).arg(effectiveTrt).arg(pnum);
                }
            } else {
                expTrtKey = (runStr.isEmpty() || isSummaryOsu)
                    ? QString("%1__%2__%3").arg(crop).arg(experiment).arg(effectiveTrt)
                    : QString("%1__%2__%3__%4").arg(crop).arg(experiment).arg(effectiveTrt).arg(runStr);
            }
            
            QVariant xVal = xColumn->data[row];
            QVariant yVal = yColumn->data[row];
            
            if (DataProcessor::isMissingValue(xVal) || DataProcessor::isMissingValue(yVal)) {
                continue;
            }
            
            // Map the date for sequence experiments so we can find exactly which TRT this simulated output was from
            if (crop == "SQ") {
                const DataColumn* dateColumn = simData.getColumn("DATE");
                if (dateColumn && row < dateColumn->data.size()) {
                    QString simDateStr = dateColumn->data[row].toString();
                    if (!simDateStr.isEmpty()) {
                        QString sqKey = QString("SQ_ALL_%1_%2").arg(experiment, simDateStr);
                        sqDateToSimTrt[sqKey] = trt;
                    }
                }
            }
            
            double x, y;
            bool xOk = false, yOk = false;
            
            // Handle DATE and other date-related variables specially
            if (xVar == "DATE") {
                QString dateStr = xVal.toString();
                // Use cached date parsing (reduces logging and improves performance)
                if (parseDateCached(dateStr, x, false)) {
                    xOk = true;
                } else {
                    continue; // Skip invalid dates
                }
            } else if (xVar == "SDAT" || xVar == "PDAT" || xVar == "HDAT" || xVar == "MDAT" || 
                       xVar == "EDAT" || xVar == "ADAT") {
                // Handle DSSAT YYYYDOY format dates
                QString dateStr = xVal.toString();
                qDebug() << "PlotWidget: *** DSSAT DATE PARSING ***";
                qDebug() << "PlotWidget: Variable:" << xVar << "Value:" << dateStr << "Length:" << dateStr.length();
                
                if (dateStr.length() == 7 && dateStr != "-99") {
                    int year = dateStr.left(4).toInt();
                    int doy = dateStr.mid(4).toInt();
                    if (year > 0 && doy > 0 && doy <= 366) {
                        QDateTime dateTime = DataProcessor::unifiedDateConvert(year, doy);
                        if (dateTime.isValid()) {
                            x = dateTime.toMSecsSinceEpoch();
                            xOk = true;
                            qDebug() << "PlotWidget: *** SUCCESS *** Parsed" << xVar << dateStr << "-> Year:" << year << "DOY:" << doy << "-> timestamp:" << x;
                        } else {
                            qDebug() << "PlotWidget: Failed to convert" << xVar << dateStr << "to valid date";
                            continue;
                        }
                    } else {
                        qDebug() << "PlotWidget: Invalid year/doy in" << xVar << ":" << year << "/" << doy;
                        continue;
                    }
                } else {
                    qDebug() << "PlotWidget: Invalid" << xVar << "format:" << dateStr;
                    continue;
                }
            } else {
                x = xVal.toDouble(&xOk);
                if (!xOk) {
                    qDebug() << "PlotWidget: Failed to convert" << xVar << "to double:" << xVal.toString();
                    continue; // Skip non-numeric X values
                }
            }
            
            y = yVal.toDouble(&yOk);
            if (!yOk) {
                continue; // Skip non-numeric Y values
            }
            
            experimentTreatmentData[expTrtKey].append(QPointF(x, y));
        }

        // If plotting mean of reps, average duplicate x-values within each key
        if (isSequenceOsu && m_plotSettings.plotMeanReps) {
            for (auto it = experimentTreatmentData.begin(); it != experimentTreatmentData.end(); ++it) {
                QMap<double, QPair<double, int>> xToSumCount;
                for (const QPointF &pt : it.value()) {
                    xToSumCount[pt.x()].first  += pt.y();
                    xToSumCount[pt.x()].second += 1;
                }
                QVector<QPointF> averaged;
                for (auto jt = xToSumCount.begin(); jt != xToSumCount.end(); ++jt)
                    averaged.append(QPointF(jt.key(), jt.value().first / jt.value().second));
                it.value() = averaged;
            }
        }

        qDebug() << "PlotWidget: Selected treatments filter:" << treatments;
        
        // Collect treatment keys from this variable's data (including run when present).
        // Also add base keys (crop__experiment__treatment) so observed data (no run) can match.
        for (auto it = experimentTreatmentData.begin(); it != experimentTreatmentData.end(); ++it) {
            simulatedTreatmentKeys.insert(it.key());
            QStringList keyParts = it.key().split("__");
            if (keyParts.size() >= 3) {
                QString baseKey = QString("%1__%2__%3").arg(keyParts[0]).arg(keyParts[1]).arg(keyParts[2]);
                simulatedTreatmentKeys.insert(baseKey);
            }
        }
        
        // Pre-compute how many runs exist per crop+experiment+treatment to decide if RUN needs to be shown
        QMap<QString, int> baseKeyToRunCount;
        for (auto it = experimentTreatmentData.begin(); it != experimentTreatmentData.end(); ++it) {
            QStringList keyParts = it.key().split("__");
            if (keyParts.size() >= 3) {
                QString baseKey = QString("%1__%2__%3").arg(keyParts[0]).arg(keyParts[1]).arg(keyParts[2]);
                baseKeyToRunCount[baseKey] = baseKeyToRunCount.value(baseKey, 0) + 1;
            }
        }

        // Create plot data for each experiment-treatment combination
        for (auto it = experimentTreatmentData.begin(); it != experimentTreatmentData.end(); ++it) {
            // Parse the crop-experiment-treatment-tname key
            QStringList keyParts = it.key().split("__");
            if (keyParts.size() < 3) continue;
            
            QString crop = keyParts[0];
            QString experiment = keyParts[1];
            QString treatment = keyParts[2];
            // Optional RUN part may be present in keyParts[3+]
            QString runPart;
            if (keyParts.size() >= 4) {
                for (int i = 3; i < keyParts.size(); ++i) {
                    if (keyParts[i].startsWith("RUN")) {
                        runPart = keyParts[i];
                        break;
                    }
                }
            }
            
            PlotData plotData;
            plotData.crop = crop;
            plotData.treatment = treatment;
            plotData.experiment = experiment;
            // For sequence OSU, label by crop name + R# slot only (e.g. "Dry bean (R#1)")
            // P# reps share the same legend entry — only the series data differs
            if (isSequenceOsu) {
                QString cropName = getCropNameFromCode(crop);
                if (cropName.isEmpty() || cropName == crop)
                    plotData.treatmentName = QString("%1 (R#%2)").arg(crop).arg(treatment);
                else
                    plotData.treatmentName = QString("%1 (R#%2)").arg(cropName).arg(treatment);
            } else {
                plotData.treatmentName = getTreatmentDisplayName(treatment, experiment, crop);
            }
            // Only append RUN if there are multiple runs under the same crop+experiment+treatment
            QString baseKey = QString("%1__%2__%3").arg(crop).arg(experiment).arg(treatment);
            if (!runPart.isEmpty() && baseKeyToRunCount.value(baseKey, 0) > 1) {
                plotData.treatmentName += QString(" (%1)").arg(runPart);
            }
            plotData.variable = yVar;
            plotData.points = it.value();
            // Always use crop__experiment__treatment as color key so observed and simulated
            // data for the same treatment share the same color
            QString treatmentId = QString("%1__%2__%3").arg(crop).arg(experiment).arg(treatment);
            plotData.color = getColorForTreatment(treatmentId, colorIndex);
            // Line style based on variable index, not treatment index
            plotData.lineStyleIndex = yVars.indexOf(yVar); // modulo applied at render time
            // Marker based on variable index to ensure each variable gets a different marker
            plotData.symbolIndex = yVars.indexOf(yVar);
            colorIndex++;
            // T files (isObservedOnly) are observed measurements — render as scatter
            plotData.isObserved = simData.isObservedOnly;

            plotDataList.append(plotData);
        }
    }
    
    // Collect treatment keys from simulated data to filter observed data
    // Keys are collected inside the yVar loop above, so simulatedTreatmentKeys is already populated
    
    qDebug() << "PlotWidget: plotDatasets - Plotting observed data (if available). Row count:" << obsData.rowCount << ", Columns:" << obsData.columnNames;
    qDebug() << "PlotWidget: Available simulated treatment keys:" << simulatedTreatmentKeys;
    
    // Plot observed data (if available) - only for treatments that match simulated data
    if (obsData.rowCount > 0) {
        for (const QString &yVar : yVars) {
            // Check if required columns exist in observed data
            if (!obsData.columnNames.contains(xVar)) {
                qDebug() << "PlotWidget: Observed data missing X variable column:" << xVar;
                continue;
            }
            if (!obsData.columnNames.contains(yVar)) {
                qDebug() << "PlotWidget: Observed data missing Y variable column:" << yVar;
                continue;
            }
            if (!obsData.columnNames.contains("TRT")) {
                qDebug() << "PlotWidget: Observed data missing TRT column.";
                continue;
            }

            const DataColumn *xColumn = obsData.getColumn(xVar);
            const DataColumn *yColumn = obsData.getColumn(yVar);
            const DataColumn *trtColumn = obsData.getColumn("TRT");
            const DataColumn *expColumn = obsData.getColumn("EXPERIMENT");
            
            
            // Group observed data by experiment and treatment combination
            QMap<QString, QVector<QPointF>> experimentTreatmentData;
            
            for (int row = 0; row < obsData.rowCount; ++row) {
                if (row >= xColumn->data.size() || row >= yColumn->data.size() || row >= trtColumn->data.size()) {
                    continue;
                }
                
                QString trt = trtColumn->data[row].toString();

                // Get experiment for this row, or use selectedExperiment as fallback
                QString experiment = selectedExperiment;
                if (expColumn && row < expColumn->data.size()) {
                    QString expFromData = expColumn->data[row].toString();
                    if (!expFromData.isEmpty()) {
                        experiment = expFromData;
                    }
                }

                // Treatment filter: match plain trt OR compound exp::trt (same as sim loop)
                if (!treatments.isEmpty() && !treatments.contains("All")
                    && !treatments.contains(trt)
                    && !treatments.contains(experiment + "::" + trt)) {
                    continue;
                }

                // Per-variable filter: skip if this var::exp::trt is excluded
                if (m_plotSettings.excludedSeriesKeys.contains(yVar + "::" + experiment + "::" + trt)) {
                    continue;
                }

                // Get crop for this row if available
                QString crop = "XX";  // Default crop code
                const DataColumn* cropColumn = obsData.getColumn("CROP");
                if (cropColumn && row < cropColumn->data.size()) {
                    QString cropFromData = cropColumn->data[row].toString();
                    if (!cropFromData.isEmpty()) {
                        crop = cropFromData;
                    }
                }

                // If it's SQ, remap the generic `trt` ('1') using the date to match the simulated granular TRT
                if (crop == "SQ") {
                    const DataColumn* obsDateCol = obsData.getColumn("DATE");
                    if (obsDateCol && row < obsDateCol->data.size()) {
                        QString obsDateMask = obsDateCol->data[row].toString();
                        if (!obsDateMask.isEmpty()) {
                            QString sqKey = QString("SQ_ALL_%1_%2").arg(experiment, obsDateMask);
                            if (sqDateToSimTrt.contains(sqKey)) {
                                trt = sqDateToSimTrt[sqKey];
                            }
                        }
                    }
                }

                // Observed data has no run; always use base key crop__experiment__treatment
                QString expTrtKey = QString("%1__%2__%3").arg(crop).arg(experiment).arg(trt);
                
                // Skip observed data if base treatment key doesn't exist in simulated data
                if (!simulatedTreatmentKeys.contains(expTrtKey)) {
                    qDebug() << "PlotWidget: Skipping observed data for treatment key not found in simulated data:" << expTrtKey;
                    continue;
                }
                
                QVariant xVal = xColumn->data[row];
                QVariant yVal = yColumn->data[row];
                
                if (DataProcessor::isMissingValue(xVal) || DataProcessor::isMissingValue(yVal)) {
                    continue;
                }
                
                double x, y;
                bool xOk = false, yOk = false;
                
                // Handle DATE variable specially
                if (xVar == "DATE") {
                    QString dateStr = xVal.toString();
                    // Use cached date parsing (reduces logging and improves performance)
                    if (parseDateCached(dateStr, x, true)) {
                        xOk = true;
                    } else {
                        continue; // Skip invalid dates
                    }
                } else {
                    x = xVal.toDouble(&xOk);
                    if (!xOk) {
                        continue; // Skip non-numeric X values
                    }
                }
                
                y = yVal.toDouble(&yOk);
                if (!yOk) {
                    continue; // Skip non-numeric Y values
                }
                
                experimentTreatmentData[expTrtKey].append(QPointF(x, y));
            }
            
            // Create plot data for observed data (each experiment-treatment combination)
            // Observed data has no run; keys are always crop__experiment__treatment
            for (auto it = experimentTreatmentData.begin(); it != experimentTreatmentData.end(); ++it) {
                QStringList keyParts = it.key().split("__");
                if (keyParts.size() < 3) continue;
                
                QString crop = keyParts[0];
                QString experiment = keyParts[1];
                QString treatment = keyParts[2];
                
                PlotData plotData;
                plotData.crop = crop;
                plotData.treatment = treatment;
                plotData.experiment = experiment;
                plotData.treatmentName = getTreatmentDisplayName(treatment, experiment, crop);
                plotData.variable = yVar;
                // Observed has no run; always use base treatment ID
                QString treatmentId = QString("%1__%2__%3").arg(crop).arg(experiment).arg(treatment);
                
                // Aggregate replicates if error bars are enabled (ONLY for observed data, not simulated)
                if (m_plotSettings.showErrorBars && it.value().size() > 0) {
                    QVector<ErrorBarData> errorBars = aggregateReplicates(it.value(), xVar);
                    
                    // Convert SD to SE if needed
                    if (m_plotSettings.errorBarType == "SE") {
                        for (ErrorBarData &errorBar : errorBars) {
                            if (errorBar.n > 1) {
                                errorBar.errorValue = errorBar.errorValue / qSqrt(errorBar.n);  // SE = SD / sqrt(n)
                            }
                        }
                    }
                    
                    // Create points from aggregated data (mean points)
                    QVector<QPointF> meanPoints;
                    for (const ErrorBarData &errorBar : errorBars) {
                        meanPoints.append(errorBar.meanPoint());
                    }
                    plotData.points = meanPoints;
                    plotData.errorBars = errorBars;
                } else {
                    plotData.points = it.value();
                    plotData.errorBars.clear();  // No error bars if disabled or no replicates
                }
                
                plotData.color = getColorForTreatment(treatmentId, colorIndex); // Use same treatmentId as simulated data for consistent colors
                // Line style based on variable index, not treatment index
                plotData.lineStyleIndex = yVars.indexOf(yVar); // modulo applied at render time
                // In multi-panel mode each variable has its own panel, so use one consistent
                // marker shape (index 0 = circle) across all panels. In overlay mode, vary
                // by variable so different variables on the same axes can be distinguished.
                plotData.symbolIndex = m_plotSettings.multiPanelTimeSeries ? 0 : yVars.indexOf(yVar);
                colorIndex++;
                plotData.isObserved = true;
                
                plotDataList.append(plotData);
            }
        }
    }
    
    qDebug() << "plotDatasets: Generated" << plotDataList.size() << "plot data items before adding to chart";

    // Compute axis breaks (DATE x-axis only) — must happen before setupAxes so
    // setupAxes can pick QValueAxis vs QDateTimeAxis based on whether breaks exist.
    computeAxisBreaks(plotDataList);

    // If breaks were found, re-run setupAxes now that m_axisBreaks is populated
    // (the first call at the top of plotDatasets saw empty m_axisBreaks).
    if (!m_axisBreaks.isEmpty()) {
        // Remove axes created by the first setupAxes call
        for (auto *axis : m_chart->axes())
            m_chart->removeAxis(axis);
        setupAxes(xVar);

        // Remap all x-coordinates to virtual space
        for (PlotData &pd : plotDataList) {
            for (QPointF &pt : pd.points)
                pt.setX(remapX(pt.x()));
        }
    }

    // Add series to chart
    addSeriesToPlot(plotDataList);
    
    // Update error bar data in chart view (ONLY for observed data series, never for simulated data)
    if (m_plotSettings.showErrorBars && m_chartView) {
        QMap<QAbstractSeries*, QVector<ErrorBarData>> errorBarMap;
        for (const QSharedPointer<PlotData> &plotData : m_plotDataList) {
            // Only add error bars for observed data, never for simulated data
            if (plotData && plotData->isObserved && plotData->series && !plotData->errorBars.isEmpty()) {
                errorBarMap[plotData->series] = plotData->errorBars;
            }
        }
        m_chartView->setErrorBarData(errorBarMap);
    } else if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
    }
    
    // Update legend
    updateLegend(plotDataList);
}

QScatterSeries::MarkerShape PlotWidget::getMarkerShape(const QString &symbol) const
{
    // All 6 Qt Charts MarkerShape enum values should work with OpenGL disabled
    if (symbol == "o") return QScatterSeries::MarkerShapeCircle;           // 0 - Circle
    if (symbol == "s") return QScatterSeries::MarkerShapeRectangle;        // 1 - Rectangle
    if (symbol == "d") return QScatterSeries::MarkerShapeRotatedRectangle; // 2 - Diamond
    if (symbol == "t") return QScatterSeries::MarkerShapeTriangle;         // 3 - Triangle
    if (symbol == "v") return QScatterSeries::MarkerShapeRectangle;        // Placeholder for custom rendering
    
    return QScatterSeries::MarkerShapeCircle;
    return QScatterSeries::MarkerShapeCircle; // Default
}

QString PlotWidget::getActualRenderedSymbol(const QString &originalSymbol) const
{
    // Return symbols that match the Qt Charts MarkerShape mapping
    if (originalSymbol == "o") return "o";
    if (originalSymbol == "s") return "s";
    if (originalSymbol == "d") return "d";
    if (originalSymbol == "t") return "t";
    if (originalSymbol == "v") return "v";
    
    return "o"; // Default to circle
}

int PlotWidget::getMarkerIndexForVariable(const QString &variable)
{
    // Get variable info from CDE file
    QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(variable);
    QString description = varInfo.second.toLower();
    
    // Use hash of description to assign consistent markers
    if (!description.isEmpty()) {
        uint hash = qHash(description);
        return hash % m_markerSymbols.size();
    }
    
    // Fallback: use hash of variable name
    uint hash = qHash(variable);
    return hash % m_markerSymbols.size();
}

int PlotWidget::getMarkerIndexForTreatment(const QString &treatment)
{
    // Use treatment-based marker differentiation
    uint hash = qHash(treatment);
    return hash % m_markerSymbols.size();
}

QString PlotWidget::getVariableGroup(const QString &variable)
{
    // Get variable description from CDE file to determine group
    QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(variable);
    return varInfo.second; // Return the full description from CDE
}

void PlotWidget::addSeriesToPlot(const QVector<PlotData> &plotDataList)
{
    qDebug() << "addSeriesToPlot: Called with" << plotDataList.size() << "plot data items";
    if (!m_chart) {
        qDebug() << "addSeriesToPlot: No chart available!";
        return;
    }
    
    // Debug: Print details of each plot data item
    for (int i = 0; i < plotDataList.size(); ++i) {
        const PlotData &plotData = plotDataList[i];
        qDebug() << "addSeriesToPlot: Item" << i << ":";
        qDebug() << "  Treatment:" << plotData.treatment;
        qDebug() << "  TreatmentName:" << plotData.treatmentName;
        qDebug() << "  Variable:" << plotData.variable;
        qDebug() << "  IsObserved:" << plotData.isObserved;
        qDebug() << "  Experiment:" << plotData.experiment;
        qDebug() << "  Points:" << plotData.points.size();
    }
    
    // Clear previous series mappings and plot data
    m_seriesToPlotData.clear();
    m_plotDataList.clear();
    
    for (int i = 0; i < plotDataList.size(); ++i) {
        const PlotData &plotData = plotDataList[i]; // Use const reference
        if (plotData.points.isEmpty()) {
            qDebug() << "addSeriesToPlot: Including empty data for legend:" << plotData.treatment 
                     << "experiment:" << plotData.experiment << "crop:" << plotData.crop;
            // Still add to plot data list for legend, but don't create series
            QSharedPointer<PlotData> sharedPlotData = QSharedPointer<PlotData>::create(plotData);
            m_plotDataList.append(sharedPlotData);
            continue;
        }
        
        // Store plot data (copy)
        QSharedPointer<PlotData> sharedPlotData = QSharedPointer<PlotData>::create(plotData);
        m_plotDataList.append(sharedPlotData);
        
        QAbstractSeries* series = nullptr;
        
        if (sharedPlotData->isObserved || m_currentPlotType == "Scatter") {
            // Use scatter series for observed data or scatter plot type
            QScatterSeries *scatterSeries = new QScatterSeries();
            
            // Disable OpenGL to enable all marker shapes (QTBUG-59881)
            scatterSeries->setUseOpenGL(false);
            // Include crop in name if it's not the default XX
            QString seriesName;
            QString dataType = sharedPlotData->isObserved ? "Observed" : "Simulated";
            if (sharedPlotData->crop != "XX") {
                // For same experiment, same treatment, different crops: use TNAME - CROP format
                QString cropName = getCropNameFromCode(sharedPlotData->crop);
                seriesName = QString("%1 - %2 (%3-%4)")
                           .arg(cropName)  // Use crop name instead of treatment name
                           .arg(sharedPlotData->variable)
                           .arg(sharedPlotData->crop)
                           .arg(dataType);
            } else {
                seriesName = QString("%1 - %2 (%3)")
                           .arg(sharedPlotData->treatmentName)
                           .arg(sharedPlotData->variable)
                           .arg(dataType);
            }
            scatterSeries->setName(seriesName);
            
            scatterSeries->setColor(sharedPlotData->color);
            scatterSeries->setMarkerSize(6.0);
            
            // Apply symbol based on symbolIndex - 5 shapes × 2 fill modes = 10 distinct variants.
            // Variants 0-4: filled (solid color), variants 5-9: hollow (white fill, color border).
            QStringList uniqueShapes = {"o", "s", "d", "t", "v"}; // 5 Qt marker shapes (v is custom)
            int shapeIndex = sharedPlotData->symbolIndex % uniqueShapes.size();
            bool isHollow = (sharedPlotData->symbolIndex / uniqueShapes.size()) % 2 == 1;
            QString originalSymbol = uniqueShapes[shapeIndex];
            QString actualSymbol = getActualRenderedSymbol(originalSymbol);

            QPen symbolPen(sharedPlotData->color, 2, Qt::SolidLine);
            QBrush symbolBrush = isHollow ? QBrush(Qt::white) : QBrush(sharedPlotData->color);

            if (originalSymbol == "v") {
                // For custom inverted triangle shape
                scatterSeries->setMarkerShape(QScatterSeries::MarkerShapeRectangle);
                scatterSeries->setPen(Qt::NoPen);
                scatterSeries->setBrush(Qt::NoBrush);
                scatterSeries->setProperty("custom_shape", "v");
                scatterSeries->setProperty("custom_pen", QVariant::fromValue(symbolPen));
                scatterSeries->setProperty("custom_brush", QVariant::fromValue(symbolBrush));
                scatterSeries->setProperty("custom_size", 6.0);
            } else {
                scatterSeries->setMarkerShape(getMarkerShape(originalSymbol));
                scatterSeries->setPen(symbolPen);
                scatterSeries->setBrush(symbolBrush);
            }
            
            for (const QPointF &point : sharedPlotData->points) {
                scatterSeries->append(point);
            }
            
            series = scatterSeries;
            
            // Store pen and brush info
            sharedPlotData->pen = symbolPen;
            sharedPlotData->brush = symbolBrush;
            sharedPlotData->symbol = actualSymbol;  // Store the actual rendered symbol
            
        } else {
            // Use line series for simulated data
            QLineSeries *lineSeries = new QLineSeries();
            
            // Disable OpenGL to enable all line styles (QTBUG-59881)
            lineSeries->setUseOpenGL(false);
            QString seriesName;
            // If treatmentName is a real word (not just a number), use it as the primary label.
            // This covers sequence OSU where TNAME is "Bean"/"Fallow"/"Soybean" etc.
            bool ok;
            sharedPlotData->treatmentName.toInt(&ok);
            bool trtNameMeaningful = !sharedPlotData->treatmentName.isEmpty() && !ok;
            if (trtNameMeaningful) {
                seriesName = QString("%1 - %2 (Simulated)")
                           .arg(sharedPlotData->treatmentName)
                           .arg(sharedPlotData->variable);
            } else if (sharedPlotData->crop != "XX") {
                // Multiple crops, no meaningful treatment name: label by crop code
                QString cropName = getCropNameFromCode(sharedPlotData->crop);
                seriesName = QString("%1 - %2 (%3-Simulated)")
                           .arg(cropName)
                           .arg(sharedPlotData->variable)
                           .arg(sharedPlotData->crop);
            } else {
                seriesName = QString("%1 - %2 (Simulated)")
                           .arg(sharedPlotData->treatmentName)
                           .arg(sharedPlotData->variable);
            }
            lineSeries->setName(seriesName);
            
            lineSeries->setColor(sharedPlotData->color);

            // 8 distinct line styles using custom dash patterns (requires setUseOpenGL(false)).
            // Patterns are QVector<qreal> in pen-width units: {dash, gap, dash, gap, ...}
            // Cycle lengths are kept short so 2+ full cycles fit in the 50px legend sample,
            // making the dot count clearly visible even at legend size.
            QPen linePen(sharedPlotData->color, 2.0);
            switch (sharedPlotData->lineStyleIndex % 8) {
                case 0: // Solid ──────────────
                    linePen.setStyle(Qt::SolidLine);
                    break;
                case 1: // Long dash  ━━  ━━  ━━      cycle=18px → 2.8× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({6.0, 3.0});
                    break;
                case 2: // Short dash  ─ ─ ─ ─ ─      cycle=12px → 4.2× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({3.0, 3.0});
                    break;
                case 3: // Fine dot  · · · · · ·       cycle=8px  → 6.3× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({1.0, 3.0});
                    break;
                case 4: // Dash-dot  ─·─·─·            cycle=20px → 2.5× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({5.0, 2.0, 1.0, 2.0});
                    break;
                case 5: // Dash-dot-dot  ─··─··         cycle=24px → 2.1× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({5.0, 2.0, 1.0, 2.0, 1.0, 2.0});
                    break;
                case 6: // Dot-dot-dash  ··─··─         cycle=24px → 2.1× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({1.0, 2.0, 1.0, 2.0, 5.0, 2.0});
                    break;
                case 7: // Long-short dash  ━─━─        cycle=24px → 2.1× in 50px
                    linePen.setStyle(Qt::CustomDashLine);
                    linePen.setDashPattern({5.0, 2.0, 2.0, 2.0});
                    break;
            }
            lineSeries->setPen(linePen);
            
            for (const QPointF &point : sharedPlotData->points) {
                lineSeries->append(point);
            }
            
            series = lineSeries;
            
            // Store pen info
            sharedPlotData->pen = linePen;
            sharedPlotData->brush = QBrush();
            sharedPlotData->symbol = "";
        }
        
        if (series) {
            sharedPlotData->series = series;
            m_seriesToPlotData[series] = sharedPlotData;
            m_chart->addSeries(series);

            // Attach series to existing axes (don't create default axes)
            auto axes = m_chart->axes();
            if (axes.size() >= 2) {
                QAbstractAxis *xAxis = nullptr;
                QAbstractAxis *yAxis = nullptr;
                for (auto axis : axes) {
                    if (axis->orientation() == Qt::Horizontal) {
                        xAxis = axis;
                    } else if (axis->orientation() == Qt::Vertical) {
                        yAxis = axis;
                    }
                }
                if (xAxis && yAxis) {
                    series->attachAxis(xAxis);
                    series->attachAxis(yAxis);
                    qDebug() << "addSeriesToPlot: Attached series to custom axes";
                }
            }
        }
    }
    
    qDebug() << "addSeriesToPlot: Chart has" << m_chart->series().count() << "series and" << m_chart->axes().count() << "axes";
    
    // Schedule auto-fit using timer (consolidates multiple auto-fit calls)
    if (m_autoFitTimer && !m_autoFitPending) {
        m_autoFitPending = true;
        m_autoFitTimer->start();
    }
    
    
    // Style axes
    auto axes = m_chart->axes();
    qDebug() << "addSeriesToPlot: Chart has" << axes.count() << "axes";
    for (auto axis : axes) {
        axis->setGridLineVisible(m_showGrid);
        if (auto valueAxis = qobject_cast<QValueAxis*>(axis))
            qDebug() << "addSeriesToPlot: ValueAxis range:" << valueAxis->min() << "to" << valueAxis->max();
        else if (auto dateAxis = qobject_cast<QDateTimeAxis*>(axis))
            qDebug() << "addSeriesToPlot: DateTimeAxis range:" << dateAxis->min() << "to" << dateAxis->max();
    }

    // Qt theme resets axis pens on every addAxis/attachAxis — defer to ensure we run last
    QTimer::singleShot(50, this, &PlotWidget::enforceAxisColors);
}

void PlotWidget::updateLegend(const QVector<PlotData> &plotDataList)
{
    qDebug() << "updateLegend: m_plotDataList has" << m_plotDataList.size() << "items";
    
    clearLegend();
    
    // Create advanced legend structure
    QMap<QString, QMap<QString, QVector<QSharedPointer<PlotData>>>> legendEntries;
    
    // Organize data by variable and treatment
    for (const QSharedPointer<PlotData>& plotData : m_plotDataList) { // Iterate directly over m_plotDataList
        QString category = plotData->isObserved ? "Observed" : "Simulated";
        
        qDebug() << "updateLegend: Processing" << category << "data for variable" << plotData->variable 
                 << "treatment" << plotData->treatment << "name" << plotData->treatmentName;
        
        // For scatter mode, allow empty treatment (we group by variable only)
        // For other modes, require both variable and treatment
        if (plotData->variable.isEmpty()) {
            qDebug() << "updateLegend: Skipping invalid plot data - variable is empty";
            continue;
        }
        
        if (!m_isScatterMode && plotData->treatment.isEmpty()) {
            qDebug() << "updateLegend: Skipping invalid plot data - treatment is empty (non-scatter mode)";
            continue;
        }
        
        if (!legendEntries[category].contains(plotData->variable)) {
            legendEntries[category][plotData->variable] = QVector<QSharedPointer<PlotData>>();
        }
        legendEntries[category][plotData->variable].append(plotData);
    }
    
    qDebug() << "updateLegend: Legend entries organized:";
    for (auto catIt = legendEntries.begin(); catIt != legendEntries.end(); ++catIt) {
        qDebug() << "  Category:" << catIt.key() << "has" << catIt.value().size() << "variables";
        for (auto varIt = catIt.value().begin(); varIt != catIt.value().end(); ++varIt) {
            qDebug() << "    Variable:" << varIt.key() << "has" << varIt.value().size() << "treatments";
        }
    }
    
    updateLegendAdvanced(legendEntries);
}

void PlotWidget::calculateMetrics()
{
    qDebug() << "PlotWidget::calculateMetrics() - ENTRY";
    qDebug() << "PlotWidget: Sim data rows:" << m_simData.rowCount;
    qDebug() << "PlotWidget: Obs data rows:" << m_obsData.rowCount;
    
    if (m_simData.rowCount == 0 || m_obsData.rowCount == 0) {
        qDebug() << "PlotWidget: No data available for metrics calculation";
        return;
    }
    
    QVector<QMap<QString, QVariant>> metrics;
    
    // Calculate metrics for each Y variable and treatment combination
    for (const QString &yVar : m_currentYVars) {
        qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Processing Y variable:" << yVar;

        const DataColumn *simYColumn = m_simData.getColumn(yVar);
        const DataColumn *obsYColumn = m_obsData.getColumn(yVar);
        const DataColumn *simTrtColumn = m_simData.getColumn("TRT");
        const DataColumn *obsTrtColumn = m_obsData.getColumn("TRT");
        
        if (!simYColumn || !obsYColumn || !simTrtColumn || !obsTrtColumn) {
            qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Skip" << yVar << "(missing sim/obs Y or TRT column)";
            continue;
        }
        
        // Get required columns for matching
        const DataColumn *simDateColumn = m_simData.getColumn("DATE");
        const DataColumn *obsDateColumn = m_obsData.getColumn("DATE");
        const DataColumn *simExpColumn = m_simData.getColumn("EXPERIMENT");
        const DataColumn *obsExpColumn = m_obsData.getColumn("EXPERIMENT");
        const DataColumn *simCropColumn = m_simData.getColumn("CROP");
        const DataColumn *obsCropColumn = m_obsData.getColumn("CROP");
        const DataColumn *simRunColumn = m_simData.getColumn("RUN");
        
        if (!simDateColumn || !obsDateColumn) {
            qDebug() << "PlotWidget: Missing DATE column for metrics calculation";
            continue;
        }
        
        // Create key for matching: treatment_experiment_crop_date. If Sequence (SQ), ignore TRT.
        auto createMatchKey = [](const QString& trt, const QString& exp, const QString& crop, const QString& date) {
            if (crop == "SQ") {
                return QString("SQ_ALL_%1_%2").arg(exp, date);
            }
            return QString("%1_%2_%3_%4").arg(trt, exp, crop, date);
        };
        
        // Collect simulated data with match keys, split by RUN if present
        // Map: baseKey (trt_exp_crop_date or SQ_ALL_exp_date) -> (runId -> sim value)
        QMap<QString, QMap<QString, double>> simDataByBaseKeyToRuns;
        // Keep track of which actual sim TRT corresponds to the matchKey for SQ
        QMap<QString, QString> matchKeyToSimTrt;
        
        for (int row = 0; row < m_simData.rowCount; ++row) {
            if (row >= simYColumn->data.size() || row >= simTrtColumn->data.size() || row >= simDateColumn->data.size()) continue;
            
            QString trt = simTrtColumn->data[row].toString();
            QString date = simDateColumn->data[row].toString();
            QString exp = simExpColumn && row < simExpColumn->data.size() ? simExpColumn->data[row].toString() : "";
            QString crop = simCropColumn && row < simCropColumn->data.size() ? simCropColumn->data[row].toString() : "";
            QString runId;
            if (simRunColumn && row < simRunColumn->data.size()) {
                QString rv = simRunColumn->data[row].toString();
                if (!rv.isEmpty()) {
                    runId = QString("RUN%1").arg(rv);
                }
            }
            QVariant yVal = simYColumn->data[row];
            
            if (!DataProcessor::isMissingValue(yVal)) {
                QString baseKey = createMatchKey(trt, exp, crop, date);
                auto &runMap = simDataByBaseKeyToRuns[baseKey];
                runMap[runId] = yVal.toDouble(); // empty runId means no RUN column
                
                // Store the actual simulated TRT for this match key (useful for sequences to recover the sim TRT)
                if (crop == "SQ") {
                    matchKeyToSimTrt[baseKey] = trt;
                }
            }
        }

        int nSimKeys = simDataByBaseKeyToRuns.size();
        QStringList sampleSimKeys = simDataByBaseKeyToRuns.keys().mid(0, 5);
        qDebug() << "[DEBUG] PlotWidget::calculateMetrics - simDataByBaseKeyToRuns:"
                 << nSimKeys << "match keys (trt_exp_crop_date). Sample:" << sampleSimKeys;
        
        // Collect observed data with match keys and create matched pairs grouped by treatment+variable+experiment+crop(+run)
        QMap<QString, QVector<double>> simByTreatmentVarExpCrop;
        QMap<QString, QVector<double>> obsByTreatmentVarExpCrop;
        QMap<QString, QString> treatmentVarExpCropToExp; // experiment per group
        QMap<QString, QString> treatmentVarExpCropToCrop; // crop per group
        QMap<QString, QString> treatmentVarExpCropToTrt; // treatment per group
        QMap<QString, QString> treatmentVarExpCropToRun; // runId per group (may be empty)
        // Track run counts per base (no-run) group for labeling decisions
        QMap<QString, QSet<QString>> baseGroupToRuns;
        
        int matchedPairs = 0;
        int obsRowsWithVal = 0;
        int obsRowsMatched = 0;
        int obsRowsNoSim = 0;
        QStringList sampleNoSimKeys;
        for (int row = 0; row < m_obsData.rowCount; ++row) {
            if (row >= obsYColumn->data.size() || row >= obsTrtColumn->data.size() || row >= obsDateColumn->data.size()) continue;
            
            QString obsTrt = obsTrtColumn->data[row].toString();
            QString date = obsDateColumn->data[row].toString();
            QString exp = obsExpColumn && row < obsExpColumn->data.size() ? obsExpColumn->data[row].toString() : "";
            QString crop = obsCropColumn && row < obsCropColumn->data.size() ? obsCropColumn->data[row].toString() : "";
            QVariant obsVal = obsYColumn->data[row];
            
            if (!DataProcessor::isMissingValue(obsVal)) {
                obsRowsWithVal++;
                QString matchKey = createMatchKey(obsTrt, exp, crop, date);
                
                // Only add if we have matching simulated data for the same key
                if (simDataByBaseKeyToRuns.contains(matchKey)) {
                    obsRowsMatched++;
                    const auto &runMap = simDataByBaseKeyToRuns[matchKey];
                    
                    // For sequence, observed TRT is "1", but we want to group by the actual Sim TRT
                    // so it appears in the metrics table under the correct Simulated treatment
                    QString effectiveTrt = (crop == "SQ" && matchKeyToSimTrt.contains(matchKey)) ? matchKeyToSimTrt[matchKey] : obsTrt;
                    
                    QString baseGroupKey = QString("%1_%2_%3_%4").arg(effectiveTrt, yVar, exp, crop);
                    if (runMap.isEmpty()) {
                        // No run separation
                        QString groupKey = baseGroupKey;
                        simByTreatmentVarExpCrop[groupKey].append(0.0); // placeholder; will be overwritten next line
                        simByTreatmentVarExpCrop[groupKey].back() = 0.0; // ensure vector exists
                        obsByTreatmentVarExpCrop[groupKey].append(obsVal.toDouble());
                        treatmentVarExpCropToExp[groupKey] = exp;
                        treatmentVarExpCropToCrop[groupKey] = crop;
                        treatmentVarExpCropToTrt[groupKey] = effectiveTrt;
                        matchedPairs++;
                    } else {
                        for (auto itRun = runMap.constBegin(); itRun != runMap.constEnd(); ++itRun) {
                            QString runId = itRun.key();
                            double simVal = itRun.value();
                            QString groupKey = runId.isEmpty() ? baseGroupKey : QString("%1_%2").arg(baseGroupKey, runId);
                            simByTreatmentVarExpCrop[groupKey].append(simVal);
                            obsByTreatmentVarExpCrop[groupKey].append(obsVal.toDouble());
                            treatmentVarExpCropToExp[groupKey] = exp;
                            treatmentVarExpCropToCrop[groupKey] = crop;
                            treatmentVarExpCropToTrt[groupKey] = effectiveTrt;
                            treatmentVarExpCropToRun[groupKey] = runId;
                            baseGroupToRuns[baseGroupKey].insert(runId);
                            matchedPairs++;
                        }
                    }
                } else {
                    obsRowsNoSim++;
                    if (sampleNoSimKeys.size() < 5) sampleNoSimKeys << matchKey;
                }
            }
        }

        qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Obs data flow for" << yVar
                 << ": obs rows with valid" << yVar << "=" << obsRowsWithVal
                 << ", matched (sim exists for matchKey)=" << obsRowsMatched
                 << ", skipped (no sim for matchKey)=" << obsRowsNoSim
                 << ", matchedPairs added=" << matchedPairs;
        if (!sampleNoSimKeys.isEmpty())
            qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Sample matchKeys with no sim:" << sampleNoSimKeys;
        qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Group keys (trt_var_exp_crop):" << simByTreatmentVarExpCrop.size();
        if (!simByTreatmentVarExpCrop.isEmpty()) {
            auto gk = simByTreatmentVarExpCrop.keys();
            qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Sample group keys:" << gk.mid(0, qMin(8, gk.size()));
        }
        
        // Calculate metrics for each treatment+variable+experiment+crop(+run) combination
        for (auto it = simByTreatmentVarExpCrop.begin(); it != simByTreatmentVarExpCrop.end(); ++it) {
            QString groupKey = it.key();
            QStringList keyParts = groupKey.split("_");
            
            if (keyParts.size() < 4) continue;
            
            QString trt = treatmentVarExpCropToTrt[groupKey];
            QString variable = keyParts[1]; // Should be yVar
            QString experimentName = treatmentVarExpCropToExp[groupKey];
            QString cropName = treatmentVarExpCropToCrop[groupKey];
            QString runId = treatmentVarExpCropToRun.value(groupKey);
            
            // Check if this treatment should be processed (empty = show all)
            if (!m_currentTreatments.isEmpty() && !m_currentTreatments.contains("All")
                && !m_currentTreatments.contains(trt)
                && !m_currentTreatments.contains(experimentName + "::" + trt)) {
                continue;
            }
            // Per-variable filter
            if (m_plotSettings.excludedSeriesKeys.contains(variable + "::" + experimentName + "::" + trt)) {
                continue;
            }

            QVector<double> simValues = it.value();
            QVector<double> obsValues = obsByTreatmentVarExpCrop[groupKey];
            
            if (simValues.isEmpty() || obsValues.isEmpty()) {
                continue;
            }
            
            qDebug() << "[DEBUG] PlotWidget::calculateMetrics - Calling MetricsCalculator: groupKey=" << groupKey
                     << "trt=" << trt << "variable=" << variable << "n_sim=" << simValues.size()
                     << "n_obs=" << obsValues.size();
            // Use MetricsCalculator
            QVariantMap result = MetricsCalculator::calculateMetrics(simValues, obsValues, trt.toInt());
            
            if (!result.isEmpty()) {
                result["Variable"] = variable;
                // Get variable display name from CDE file
                QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(variable);
                QString varDisplayName = varInfo.first.isEmpty() ? variable : varInfo.first;
                result["VariableName"] = varDisplayName;
                
                result["Treatment"] = trt;
                // Build TreatmentName and append run only if multiple runs exist for this base group
                QString treatmentName = getTreatmentDisplayName(trt, experimentName, cropName);
                QString baseGroupKey = QString("%1_%2_%3_%4").arg(trt, variable, experimentName, cropName);
                if (!runId.isEmpty() && baseGroupToRuns.value(baseGroupKey).size() > 1) {
                    treatmentName += QString(" (%1)").arg(runId);
                }
                result["TreatmentName"] = treatmentName;
                result["Experiment"] = experimentName;
                result["Crop"] = cropName;
                // Get crop display name from crop code
                QString cropDisplayName = getCropNameFromCode(cropName);
                result["CropName"] = cropDisplayName;
                if (!runId.isEmpty()) {
                    result["Run"] = runId;
                }
                
                metrics.append(result);
            }
        }
    }
    
    qDebug() << "PlotWidget::calculateMetrics() - Calculated" << metrics.size() << "metrics";
    
    // Sort metrics by Treatment (numerically), then by Variable, Experiment, and Crop
    if (!metrics.isEmpty()) {
        std::sort(metrics.begin(), metrics.end(), [](const QMap<QString, QVariant>& a, const QMap<QString, QVariant>& b) {
            // Extract Treatment values and convert to integers for numerical sorting
            QString trtA = a.value("Treatment").toString();
            QString trtB = b.value("Treatment").toString();
            
            bool okA, okB;
            int trtNumA = trtA.toInt(&okA);
            int trtNumB = trtB.toInt(&okB);
            
            // If both are valid integers, sort numerically
            if (okA && okB) {
                if (trtNumA != trtNumB) {
                    return trtNumA < trtNumB;
                }
            } else {
                // If not both integers, sort as strings
                if (trtA != trtB) {
                    return trtA < trtB;
                }
            }
            
            // If treatments are equal, sort by Variable
            QString varA = a.value("Variable").toString();
            QString varB = b.value("Variable").toString();
            if (varA != varB) {
                return varA < varB;
            }
            
            // If variables are equal, sort by Experiment
            QString expA = a.value("Experiment").toString();
            QString expB = b.value("Experiment").toString();
            if (expA != expB) {
                return expA < expB;
            }
            
            // If experiments are equal, sort by Crop
            QString cropA = a.value("Crop").toString();
            QString cropB = b.value("Crop").toString();
            return cropA < cropB;
        });
        
        qDebug() << "PlotWidget: Emitting metricsCalculated signal with" << metrics.size() << "metrics (sorted by Treatment)";
        emit metricsCalculated(metrics);
    } else {
        qDebug() << "PlotWidget: No metrics to emit - metrics vector is empty";
    }
}

// Implementation of remaining methods...
QString PlotWidget::getTreatmentNameFromData(const QString &treatment, const QString &experiment, const QString &crop)
{
    // Iterate through m_simData to find the TNAME for the given treatment, experiment, and crop
    // This assumes TNAME is a column in m_simData
    const DataColumn *trtColumn = m_simData.getColumn("TRT");
    const DataColumn *expColumn = m_simData.getColumn("EXPERIMENT");
    const DataColumn *cropColumn = m_simData.getColumn("CROP");
    const DataColumn *tnameColumn = m_simData.getColumn("TNAME");

    if (!trtColumn || !expColumn || !cropColumn || !tnameColumn) {
        return QString();
    }

    for (int i = 0; i < m_simData.rowCount; ++i) {
        if (i < trtColumn->data.size() && i < expColumn->data.size() &&
            i < cropColumn->data.size() && i < tnameColumn->data.size()) {

            QString currentTrt = trtColumn->data[i].toString();
            QString currentExp = expColumn->data[i].toString();
            QString currentCrop = cropColumn->data[i].toString();
            QString currentTname = tnameColumn->data[i].toString();

            if (currentTrt == treatment && currentExp == experiment && currentCrop == crop) {
                if (!currentTname.isEmpty() && currentTname != "NoName") {
                    return currentTname;
                }
            }
        }
    }
    return QString();
}

void PlotWidget::setData(const DataTable &data)
{
    m_simData = data;
}

void PlotWidget::updatePlot(const QString &xVariable, const QString &yVariable, 
                           const QString &treatment, const QString &plotType)
{
    m_currentPlotType = plotType;
    m_currentXVar = xVariable;
    m_currentYVars = QStringList() << yVariable;
    
    QStringList treatments = treatment.isEmpty() ? QStringList() : QStringList() << treatment;
    
    plotDatasets(m_simData, m_obsData, xVariable, QStringList() << yVariable, treatments, m_selectedExperiment);
}

void PlotWidget::clearChart()
{
    if (m_chart) {
        m_chart->removeAllSeries();
        
        // Remove all existing axes to prevent accumulation
        auto existingAxes = m_chart->axes();
        for (auto axis : existingAxes) {
            m_chart->removeAxis(axis);
        }
    }
    clearLegend();
    m_scalingLabel->clear();
    m_plotDataList.clear(); // Clear unique_ptrs
    
    // Clear error bars and axis breaks when chart is cleared
    if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
        m_chartView->clearBoxPlotMedians();
        m_chartView->clearAxisBreaks();
    }
    m_axisBreaks.clear();
    m_axisSegments.clear();
    m_virtualAxisMax = 0.0;

    // Clear axis labels when chart is cleared
    setAxisTitles("", "");
}

void PlotWidget::clear()
{
    clearChart();
    m_simData.clear();
    m_obsData.clear();
    m_scaleFactors.clear();
    
    // Clear date cache when starting new plot
    m_dateCache.clear();
    m_autoFitPending = false;
    if (m_autoFitTimer) {
        m_autoFitTimer->stop();
    }
    
    // Reset scatter mode and show buttons (they'll be hidden again if scatter mode is set)
    m_isScatterMode = false;
    setXAxisButtonsVisible(true);

    // Hide scatter panel area and restore normal chart view
    if (m_scatterScrollArea) m_scatterScrollArea->setVisible(false);
    if (m_scatterPanelContainer) m_scatterPanelContainer->setVisible(false);
    if (m_chartView) m_chartView->setVisible(true);
    if (m_bottomContainer) m_bottomContainer->setVisible(true);

    // Destroy old scatter panel chart views
    for (QChartView *cv : m_scatterPanelViews) cv->deleteLater();
    m_scatterPanelViews.clear();

    // Show treatment selection panel in legend area when plot is cleared
    if (m_legendStack && m_preplotPanelEnabled) m_legendStack->setCurrentIndex(0);
}

// Optimization: Cached date parsing to avoid re-parsing same dates
bool PlotWidget::parseDateCached(const QString &dateStr, double &timestamp, bool isObserved)
{
    // Check cache first
    if (m_dateCache.contains(dateStr)) {
        timestamp = m_dateCache[dateStr];
        return true;
    }
    
    // Parse the date
    QDateTime dateTime = QDateTime::fromString(dateStr, "yyyy-MM-dd");
    if (!dateTime.isValid()) {
        // Try alternative formats
        dateTime = QDateTime::fromString(dateStr, "yyyyMMdd");
        if (!dateTime.isValid()) {
            dateTime = QDateTime::fromString(dateStr, Qt::ISODate);
            if (!dateTime.isValid()) {
                // Try DSSAT format (might be YYYY-DOY)
                dateTime = QDateTime::fromString(dateStr, "yyyy-DDD");
            }
        }
    }
    
    if (dateTime.isValid()) {
        timestamp = dateTime.toMSecsSinceEpoch();
        // Cache the result
        m_dateCache[dateStr] = timestamp;
        return true;
    }
    
    // Only log errors, not every parse attempt
    if (!isObserved) {
        qDebug() << "PlotWidget: Failed to parse DATE string:" << dateStr;
    } else {
        qDebug() << "PlotWidget: Failed to parse observed DATE string:" << dateStr;
    }
    return false;
}

void PlotWidget::computeAxisBreaks(const QVector<PlotData> &plotDataList)
{
    m_axisBreaks.clear();
    m_axisSegments.clear();
    m_virtualAxisMax = 0.0;

    if (m_currentXVar != "DATE") return;

    // Gather all unique real x-values across all series
    QVector<double> allX;
    for (const PlotData &pd : plotDataList) {
        for (const QPointF &pt : pd.points)
            allX.append(pt.x());
    }
    if (allX.isEmpty()) return;

    std::sort(allX.begin(), allX.end());
    allX.erase(std::unique(allX.begin(), allX.end()), allX.end());

    const double ONE_YEAR_MSEC = 365.25 * 24.0 * 3600.0 * 1000.0;

    // Find gaps > 3 years between consecutive unique x values
    QVector<QPair<double,double>> gaps;
    for (int i = 1; i < allX.size(); ++i) {
        double gap = allX[i] - allX[i-1];
        if (gap > 3.0 * ONE_YEAR_MSEC)
            gaps.append({allX[i-1], allX[i]});
    }

    if (gaps.isEmpty()) return; // no breaks needed

    // Build segments: the real data segments separated by breaks
    double segStart = allX.first();
    double virtualCursor = 0.0;

    for (const auto &gap : gaps) {
        // Segment ends at gap start
        AxisSegment seg;
        seg.start = segStart;
        seg.end = gap.first;
        seg.virtualStart = virtualCursor;
        double segLen = seg.end - seg.start;
        m_axisSegments.append(seg);
        virtualCursor += segLen;
        virtualCursor += BREAK_VIRTUAL_WIDTH; // gap takes fixed virtual width

        // Record the break
        AxisBreak br;
        br.gapStart = gap.first;
        br.gapEnd   = gap.second;
        m_axisBreaks.append(br);

        segStart = gap.second;
    }
    // Final segment
    AxisSegment lastSeg;
    lastSeg.start = segStart;
    lastSeg.end = allX.last();
    lastSeg.virtualStart = virtualCursor;
    m_axisSegments.append(lastSeg);
    virtualCursor += lastSeg.end - lastSeg.start;
    m_virtualAxisMax = virtualCursor;
}

double PlotWidget::remapX(double realMsec) const
{
    if (m_axisSegments.isEmpty()) return realMsec;

    for (int i = 0; i < m_axisSegments.size(); ++i) {
        const AxisSegment &seg = m_axisSegments[i];
        // Last segment: include the right endpoint
        if (i == m_axisSegments.size() - 1) {
            if (realMsec >= seg.start - 1e6 && realMsec <= seg.end + 1e6)
                return seg.virtualStart + (realMsec - seg.start);
        } else {
            if (realMsec >= seg.start - 1e6 && realMsec < m_axisBreaks[i].gapStart + 1e6)
                return seg.virtualStart + (realMsec - seg.start);
        }
    }
    // Fallback: clamp to last segment
    const AxisSegment &last = m_axisSegments.last();
    return last.virtualStart + (realMsec - last.start);
}

double PlotWidget::unremapX(double virtualX) const
{
    if (m_axisSegments.isEmpty()) return virtualX;

    for (int i = 0; i < m_axisSegments.size(); ++i) {
        const AxisSegment &seg = m_axisSegments[i];
        double segVirtualLen = seg.end - seg.start;
        double vEnd = seg.virtualStart + segVirtualLen;
        if (virtualX <= vEnd || i == m_axisSegments.size() - 1)
            return seg.start + (virtualX - seg.virtualStart);
    }
    return virtualX;
}

QString PlotWidget::getPlotCSV() const
{
    if (m_plotDataList.isEmpty())
        return {};

    // Collect all unique x-values (in order) across all series
    QVector<double> xValues;
    QSet<double> xSeen;
    for (const auto &pd : m_plotDataList) {
        for (const QPointF &pt : pd->points) {
            if (!xSeen.contains(pt.x())) {
                xSeen.insert(pt.x());
                xValues.append(pt.x());
            }
        }
    }
    std::sort(xValues.begin(), xValues.end());

    // Build column names: XVAR, CROP, EXPERIMENT, TREATMENT, <var>, <var>_OBS, ...
    // Group series by (crop, experiment, treatment) key
    // Columns: xVar | crop | experiment | treatment | var1_SIM | var1_OBS | var2_SIM | ...
    // We collect unique variables from sim and obs series separately
    QStringList simVars, obsVars;
    for (const auto &pd : m_plotDataList) {
        if (pd->isObserved) {
            if (!obsVars.contains(pd->variable)) obsVars << pd->variable;
        } else {
            if (!simVars.contains(pd->variable)) simVars << pd->variable;
        }
    }

    // Build header
    QStringList header;
    header << m_currentXVar << "CROP" << "EXPERIMENT" << "TREATMENT";
    for (const QString &v : simVars) {
        header << v;
        if (obsVars.contains(v))
            header << v + "_OBS";
    }
    // Append any obs-only variables
    for (const QString &v : obsVars) {
        if (!simVars.contains(v))
            header << v + "_OBS";
    }

    // Collect all unique (crop, experiment, treatment) groups
    QVector<std::tuple<QString,QString,QString>> groups;
    QSet<QString> groupSeen;
    for (const auto &pd : m_plotDataList) {
        QString key = pd->crop + "|" + pd->experiment + "|" + pd->treatment;
        if (!groupSeen.contains(key)) {
            groupSeen.insert(key);
            groups.append({pd->crop, pd->experiment, pd->treatment});
        }
    }

    // Index: (crop|exp|trt|var|isObs) -> QMap<double x, double y>
    QMap<QString, QMap<double,double>> index;
    for (const auto &pd : m_plotDataList) {
        QString key = pd->crop + "|" + pd->experiment + "|" + pd->treatment
                      + "|" + pd->variable + "|" + (pd->isObserved ? "1" : "0");
        for (const QPointF &pt : pd->points)
            index[key][pt.x()] = pt.y();
    }

    QString csv;
    QTextStream out(&csv);
    out << header.join(",") << "\n";

    for (const auto &[crop, exp, trt] : groups) {
        for (double x : xValues) {
            // Check if this group has any data at this x
            bool hasAny = false;
            auto checkKey = [&](const QString &v, bool obs) {
                QString k = crop+"|"+exp+"|"+trt+"|"+v+"|"+(obs?"1":"0");
                return index.contains(k) && index[k].contains(x);
            };
            for (const QString &v : simVars) if (checkKey(v,false)||checkKey(v,true)) { hasAny=true; break; }
            if (!hasAny) for (const QString &v : obsVars) if (checkKey(v,true)) { hasAny=true; break; }
            if (!hasAny) continue;

            QStringList row;
            QString xStr;
            if (m_currentXVar == "DATE") {
                double realX = m_axisBreaks.isEmpty() ? x : unremapX(x);
                xStr = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(realX)).toString("yyyy-MM-dd");
            } else {
                xStr = QString::number(x);
            }
            row << xStr << crop << exp << trt;

            for (const QString &v : simVars) {
                QString sk = crop+"|"+exp+"|"+trt+"|"+v+"|0";
                row << (index.contains(sk) && index[sk].contains(x)
                        ? QString::number(index[sk][x]) : "");
                if (obsVars.contains(v)) {
                    QString ok = crop+"|"+exp+"|"+trt+"|"+v+"|1";
                    row << (index.contains(ok) && index[ok].contains(x)
                            ? QString::number(index[ok][x]) : "");
                }
            }
            for (const QString &v : obsVars) {
                if (!simVars.contains(v)) {
                    QString ok = crop+"|"+exp+"|"+trt+"|"+v+"|1";
                    row << (index.contains(ok) && index[ok].contains(x)
                            ? QString::number(index[ok][x]) : "");
                }
            }
            out << row.join(",") << "\n";
        }
    }

    return csv;
}

QString PlotWidget::getPlotRCode() const
{
    QString csv = getPlotCSV();
    if (csv.isEmpty())
        return {};

    // Parse the CSV header to identify sim/obs columns and the x-variable
    QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) return {};

    QStringList header = lines[0].split(',');
    QString xVar = m_currentXVar;
    bool isDate = (xVar == "DATE");

    // Fixed metadata columns
    QStringList idCols = { xVar, "CROP", "EXPERIMENT", "TREATMENT" };

    // Separate simulated vs observed variables
    QStringList simVars, obsVars;
    for (const QString &col : header) {
        if (idCols.contains(col)) continue;
        if (col.endsWith("_OBS"))
            obsVars << col;
        else
            simVars << col;
    }

    // Collect unique treatment display names from plotDataList
    QMap<QString, QString> trtDisplayNames; // treatment id -> display name
    for (const auto &pd : m_plotDataList) {
        if (!pd->treatmentName.isEmpty() && !trtDisplayNames.contains(pd->treatment))
            trtDisplayNames[pd->treatment] = pd->treatmentName;
    }

    // Escape CSV for embedding in R read.csv(text=...)
    QString csvEscaped = csv;
    csvEscaped.replace("\\", "\\\\");
    csvEscaped.replace("\"", "\\\"");

    // Determine axis labels from variable info
    auto labelFor = [&](const QString &v) -> QString {
        QPair<QString, QString> info = DataProcessor::getVariableInfo(v);
        if (!info.first.isEmpty())
            return QString("%1 (%2)").arg(info.first, v);
        return v;
    };

    QString yLabel = m_currentYVars.isEmpty() ? "Value"
        : m_currentYVars.size() == 1 ? labelFor(m_currentYVars[0]) : "Value";

    // Build treatment-name mapping block
    QString trtMapBlock;
    if (!trtDisplayNames.isEmpty()) {
        QStringList entries;
        for (auto it = trtDisplayNames.constBegin(); it != trtDisplayNames.constEnd(); ++it) {
            QString safe = it.value();
            safe.replace("\\", "\\\\").replace("\"", "\\\"");
            entries << QString("  \"%1\" = \"%2\"").arg(it.key(), safe);
        }
        trtMapBlock = QString(
            "# Treatment display name lookup\n"
            "trt_labels <- c(\n%1\n)\n"
            "long$TreatmentLabel <- trt_labels[as.character(long$TREATMENT)]\n"
            "long$TreatmentLabel[is.na(long$TreatmentLabel)] <- as.character(long$TREATMENT[is.na(long$TreatmentLabel)])\n"
        ).arg(entries.join(",\n"));
    } else {
        trtMapBlock = "long$TreatmentLabel <- long$TREATMENT\n";
    }

    // Simulated columns vector
    QString simColsR, obsColsR;
    {
        QStringList q;
        for (const QString &v : simVars) q << QString("\"%1\"").arg(v);
        simColsR = "c(" + q.join(", ") + ")";
    }
    {
        QStringList q;
        for (const QString &v : obsVars) q << QString("\"%1\"").arg(v);
        obsColsR = "c(" + q.join(", ") + ")";
    }

    // Y facet vs single panel
    bool multiVar = m_currentYVars.size() > 1;
    QString facetLine = multiVar
        ? "  facet_wrap(~ Variable, scales = \"free_y\", ncol = 1) +"
        : "";

    QString xConvert = isDate
        ? "df[[xvar]] <- as.Date(df[[xvar]])\n"
        : "";
    QString xScaleLine = isDate
        ? "  scale_x_date(date_labels = \"%b %d, %Y\") +"
        : "";

    QString code = QString(
        "# ============================================================\n"
        "# GB2 Time-Series Plot — ggplot2 R script\n"
        "# Generated by GB2 DSSAT Visualization Tool\n"
        "# ============================================================\n\n"
        "library(ggplot2)\n"
        "library(tidyr)\n"
        "library(dplyr)\n\n"
        "# --- Embedded plot data ---\n"
        "raw_csv <- \"%1\"\n\n"
        "df <- read.csv(text = raw_csv, stringsAsFactors = FALSE, check.names = FALSE)\n\n"
        "xvar  <- \"%2\"\n"
        "id_cols  <- c(xvar, \"CROP\", \"EXPERIMENT\", \"TREATMENT\")\n"
        "sim_cols <- %3\n"
        "obs_cols <- %4\n\n"
        "%5"  // xConvert
        "# --- Reshape to long format ---\n"
        "long <- NULL\n\n"
        "if (length(sim_cols) > 0 && any(sim_cols %in% colnames(df))) {\n"
        "  sim_long <- df %>%\n"
        "    select(all_of(c(id_cols, intersect(sim_cols, colnames(df))))) %>%\n"
        "    pivot_longer(cols = -all_of(id_cols), names_to = \"Variable\", values_to = \"Value\") %>%\n"
        "    mutate(Type = \"Simulated\") %>%\n"
        "    filter(!is.na(Value))\n"
        "  long <- bind_rows(long, sim_long)\n"
        "}\n\n"
        "if (length(obs_cols) > 0 && any(obs_cols %in% colnames(df))) {\n"
        "  obs_long <- df %>%\n"
        "    select(all_of(c(id_cols, intersect(obs_cols, colnames(df))))) %>%\n"
        "    pivot_longer(cols = -all_of(id_cols), names_to = \"Variable\", values_to = \"Value\") %>%\n"
        "    mutate(Variable = sub(\"_OBS$\", \"\", Variable), Type = \"Observed\") %>%\n"
        "    filter(!is.na(Value))\n"
        "  long <- bind_rows(long, obs_long)\n"
        "}\n\n"
        "long$Type <- factor(long$Type, levels = c(\"Simulated\", \"Observed\"))\n\n"
        "# --- Treatment display names ---\n"
        "%6"  // trtMapBlock
        "\n"
        "long$Series <- paste(long$TreatmentLabel, long$Variable, sep = \" — \")\n\n"
        "# --- Plot ---\n"
        "p <- ggplot(long, aes(x = .data[[xvar]], y = Value, color = Series)) +\n"
        "  geom_line(data = subset(long, Type == \"Simulated\"), linewidth = 0.9) +\n"
        "  geom_point(data = subset(long, Type == \"Observed\"), size = 3.5, shape = 21, fill = \"white\", stroke = 1.5) +\n"
        "%7"  // facetLine
        "%8"  // xScaleLine
        "  labs(\n"
        "    x    = \"%2\",\n"
        "    y    = \"%9\",\n"
        "    color = \"Treatment\",\n"
        "    title = \"GB2 Time-Series Plot\"\n"
        "  ) +\n"
        "  theme_bw(base_size = 12) +\n"
        "  theme(\n"
        "    legend.position  = \"right\",\n"
        "    legend.key.width = unit(1.5, \"cm\")\n"
        "  )\n\n"
        "print(p)\n"
    )
    .arg(csvEscaped)        // %1
    .arg(xVar)              // %2
    .arg(simColsR)          // %3
    .arg(obsColsR)          // %4
    .arg(xConvert)          // %5
    .arg(trtMapBlock)       // %6
    .arg(facetLine.isEmpty() ? "" : facetLine + "\n")  // %7
    .arg(xScaleLine.isEmpty() ? "" : xScaleLine + "\n") // %8
    .arg(yLabel);           // %9

    return code;
}

QString PlotWidget::getScatterCSV() const
{
    if (m_scatterExportData.isEmpty())
        return {};

    QString csv = "VARIABLE,EXPERIMENT,SIMULATED,MEASURED\n";
    for (auto it = m_scatterExportData.begin(); it != m_scatterExportData.end(); ++it) {
        QStringList parts = it.key().split("::");
        QString varName = parts.value(0);
        QString expName = parts.value(1);
        for (const QPointF &pt : it.value()) {
            csv += QString("%1,%2,%3,%4\n")
                .arg(varName).arg(expName)
                .arg(pt.x(), 0, 'g', 6)
                .arg(pt.y(), 0, 'g', 6);
        }
    }
    return csv;
}

// xPct/yPct: top-left corner of legend as % of plot area (0=left/top, 100=right/bottom)
static void overlayLegend(QPixmap &pixmap, QWidget *legendWidget, const QString &position,
                          QRectF plotArea = QRectF(), double xPct = 80.0, double yPct = 5.0)
{
    if (!legendWidget || position == "outside-right") return;
    // Constrain width so the grabbed pixmap reflects only legend content, not the full panel width
    int savedMaxW = legendWidget->maximumWidth();
    legendWidget->setMaximumWidth(200);
    legendWidget->adjustSize();
    QPixmap legendPx = legendWidget->grab();
    legendWidget->setMaximumWidth(savedMaxW);
    if (legendPx.isNull()) return;
    int lw = legendPx.width();
    int lh = legendPx.height();

    // Use plot area if provided, otherwise full pixmap
    double areaLeft   = plotArea.isValid() ? plotArea.left()   : 0;
    double areaTop    = plotArea.isValid() ? plotArea.top()    : 0;
    double areaWidth  = plotArea.isValid() ? plotArea.width()  : pixmap.width();
    double areaHeight = plotArea.isValid() ? plotArea.height() : pixmap.height();

    // Place legend top-left corner at (xPct%, yPct%) of the plot area
    int x = qRound(areaLeft + (xPct / 100.0) * areaWidth);
    int y = qRound(areaTop  + (yPct / 100.0) * areaHeight);

    // Clamp so legend stays within plot area
    x = qBound(qRound(areaLeft), x, qRound(areaLeft + areaWidth  - lw));
    y = qBound(qRound(areaTop),  y, qRound(areaTop  + areaHeight - lh));

    QPainter p(&pixmap);
    p.fillRect(x - 4, y - 4, lw + 8, lh + 8, QColor(255, 255, 255, 230));
    p.setPen(QColor(180, 180, 180));
    p.drawRect(x - 4, y - 4, lw + 7, lh + 7);
    p.drawPixmap(x, y, legendPx);
    p.end();
}

void PlotWidget::exportPlot(const QString &filePath, const QString &format)
{
    if (!this) return;

    this->show();
    this->update();
    if (m_chartView) { m_chartView->update(); m_chartView->repaint(); }
    QApplication::processEvents();

    bool insideLegend = !m_isScatterMode && m_showLegend
                        && m_plotSettings.legendPosition != "outside-right";
    bool legendStackWasVisible = m_legendStack && m_legendStack->isVisible();
    if (insideLegend && m_legendStack) m_legendStack->setVisible(false);
    bool bottomWasVisible = m_bottomContainer && m_bottomContainer->isVisible();
    if (m_bottomContainer) m_bottomContainer->setVisible(false);
    QApplication::processEvents();

    QPixmap pixmap = QPixmap(this->size());
    pixmap.fill(Qt::white);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        this->render(&painter);
    }

    if (insideLegend) {
        if (m_legendStack) m_legendStack->setVisible(legendStackWasVisible);
        QApplication::processEvents();
        QRectF plotArea;
        if (m_chart && m_chartView) {
            QRectF pa = m_chart->plotArea();
            QPointF offset = m_chartView->mapTo(this, QPoint(0, 0));
            plotArea = pa.translated(offset);
        }
        overlayLegend(pixmap, m_legendWidget, m_plotSettings.legendPosition,
                      plotArea, m_plotSettings.legendX, m_plotSettings.legendY);
    }

    if (m_legendStack && !insideLegend) m_legendStack->setVisible(legendStackWasVisible);
    if (m_bottomContainer) m_bottomContainer->setVisible(bottomWasVisible);

    qDebug() << "exportPlot: filePath=" << filePath << "format=" << format << "pixmap=" << pixmap.size();
    bool ok = false;
    if (filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
        // Verify the path is writable before handing to QPdfWriter
        QFile testFile(filePath);
        if (!testFile.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(nullptr, "Export Failed",
                QString("Cannot write to:\n%1\n\n%2").arg(filePath, testFile.errorString()));
            return;
        }
        testFile.close();
        testFile.remove();

        if (pixmap.isNull() || pixmap.width() <= 0 || pixmap.height() <= 0) {
            QMessageBox::critical(nullptr, "Export Failed", "Nothing to export — plot pixmap is empty.");
            return;
        }
        QPdfWriter writer(filePath);
        // Use 150 DPI — gives a clean A4-ish page for typical plot sizes
        const int pdfDpi = 150;
        QSizeF sizeMm(pixmap.width() * 25.4 / pdfDpi, pixmap.height() * 25.4 / pdfDpi);
        writer.setPageSize(QPageSize(sizeMm, QPageSize::Millimeter));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(pdfDpi);
        QPainter pdfPainter(&writer);
        ok = pdfPainter.isActive();
        qDebug() << "exportPlot PDF: painter active=" << ok << "writer size=" << writer.width() << "x" << writer.height();
        if (ok) {
            pdfPainter.drawPixmap(0, 0, writer.width(), writer.height(), pixmap);
            pdfPainter.end();
        }
    } else {
        ok = pixmap.save(filePath, format.toUtf8().constData());
    }
    if (!ok)
        QMessageBox::critical(nullptr, "Export Failed",
            QString("Could not save plot to:\n%1\n\nCheck the path is writable.").arg(filePath));
}

// Composite legend widget onto a pixmap at the position specified in m_plotSettings.legendPosition
// Render scatter panels at a given panel size, composite with legend, return pixmap
QPixmap PlotWidget::grabScatterAtSize(int panelSide)
{
    if (m_scatterPanelViews.isEmpty())
        return QPixmap();

    int nCols   = m_scatterNCols;
    int spacing = (m_scatterPanelGrid ? m_scatterPanelGrid->spacing() : 6);
    int pad     = 6;

    // Resize each panel to target size, grab individually, then restore
    QVector<QSize> savedSizes;
    for (QChartView *cv : m_scatterPanelViews) {
        savedSizes.append(cv->size());
        cv->setFixedSize(panelSide, panelSide);
    }
    QApplication::processEvents();

    int nRows = (m_scatterPanelViews.size() + nCols - 1) / nCols;
    int gridW = nCols * panelSide + (nCols - 1) * spacing + 2 * pad;
    int gridH = nRows * panelSide + (nRows - 1) * spacing + 2 * pad;

    // Only include legend if it's visible (hidden for single-experiment scatter)
    bool legendVisible = m_legendWidget && m_legendPanel && m_legendPanel->isVisible();
    QPixmap legendPixmap = legendVisible ? m_legendWidget->grab() : QPixmap();

    int resultW = gridW + (legendVisible ? legendPixmap.width() + 12 : 0);
    int resultH = legendVisible ? qMax(gridH, legendPixmap.height()) : gridH;

    QPixmap result(resultW, resultH);
    result.fill(Qt::white);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    for (int i = 0; i < m_scatterPanelViews.size(); ++i) {
        int row = i / nCols;
        int col = i % nCols;
        int x   = pad + col * (panelSide + spacing);
        int y   = pad + row * (panelSide + spacing);
        QPixmap px = m_scatterPanelViews[i]->grab();
        p.drawPixmap(x, y, px.scaled(panelSide, panelSide,
                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    if (legendVisible)
        p.drawPixmap(gridW + 12, (resultH - legendPixmap.height()) / 2, legendPixmap);
    p.end();

    // Restore panel sizes
    for (int i = 0; i < m_scatterPanelViews.size(); ++i)
        m_scatterPanelViews[i]->setFixedSize(savedSizes[i]);
    resizeScatterPanels();

    return result;
}

void PlotWidget::copyPlotToClipboard()
{
    if (!this) return;

    this->show();
    this->update();
    QApplication::processEvents();

    bool insideLegend = !m_isScatterMode && m_showLegend
                        && m_plotSettings.legendPosition != "outside-right";
    bool legendStackWasVisible = m_legendStack && m_legendStack->isVisible();
    if (insideLegend && m_legendStack) m_legendStack->setVisible(false);
    bool bottomWasVisible = m_bottomContainer && m_bottomContainer->isVisible();
    if (m_bottomContainer) m_bottomContainer->setVisible(false);
    QApplication::processEvents();

    QPixmap pixmap = this->grab();

    if (insideLegend) {
        if (m_legendStack) m_legendStack->setVisible(legendStackWasVisible);
        QApplication::processEvents();
        QRectF plotArea;
        if (m_chart && m_chartView) {
            QRectF pa = m_chart->plotArea();
            QPointF offset = m_chartView->mapTo(this, QPoint(0, 0));
            plotArea = pa.translated(offset);
        }
        overlayLegend(pixmap, m_legendWidget, m_plotSettings.legendPosition,
                      plotArea, m_plotSettings.legendX, m_plotSettings.legendY);
    }
    if (m_legendStack && !insideLegend) m_legendStack->setVisible(legendStackWasVisible);
    if (m_bottomContainer) m_bottomContainer->setVisible(bottomWasVisible);

    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard)
        clipboard->setPixmap(pixmap);
}

void PlotWidget::exportPlot(const QString &filePath, const QString &format, int width, int height, int dpi)
{
    if (!this) return;
    
    // Ensure the widget is properly laid out before export; force chart view repaint so error bars are painted
    this->update();
    if (m_chartView) {
        m_chartView->update();
        m_chartView->repaint();
    }
    this->repaint();
    QApplication::processEvents();
    
    // Force layout update to ensure all widgets are properly positioned
    if (m_mainLayout) {
        m_mainLayout->update();
        m_mainLayout->activate();
    }
    
    // Get the actual widget size
    QSize actualSize = this->size();
    qDebug() << "Export: PlotWidget size is" << actualSize;
    qDebug() << "Export: Left container size is" << (m_leftContainer ? m_leftContainer->size() : QSize());
    qDebug() << "Export: Legend area size is" << (m_legendScrollArea ? m_legendScrollArea->size() : QSize());
    
    // Create a high-resolution pixmap
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::white);
    
    // Set device pixel ratio for high DPI
    double devicePixelRatio = dpi / 96.0;
    pixmap.setDevicePixelRatio(devicePixelRatio);
    
    // Create painter with high quality settings
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scaling to fit the target dimensions
    if (actualSize.width() > 0 && actualSize.height() > 0) {
        double scaleX = (double)width / actualSize.width();
        double scaleY = (double)height / actualSize.height();
        
        // Use the smaller scale to maintain aspect ratio
        double scale = qMin(scaleX, scaleY);
        painter.scale(scale, scale);
        
        qDebug() << "Export: Using scale factor" << scale << "(scaleX:" << scaleX << "scaleY:" << scaleY << ")";
    }
    
    // Hide bottom button bar so it doesn't appear in the exported image
    bool bottomWasVisible = m_bottomContainer && m_bottomContainer->isVisible();
    if (m_bottomContainer) m_bottomContainer->setVisible(false);

    bool insideLegend = m_showLegend && m_plotSettings.legendPosition != "outside-right";
    bool legendStackWasVisible = m_legendStack && m_legendStack->isVisible();
    if (insideLegend && m_legendStack) m_legendStack->setVisible(false);

    QApplication::processEvents();

    // Render the main widget with all its children
    this->render(&painter, QPoint(), this->rect(), QWidget::DrawChildren);

    if (m_bottomContainer) m_bottomContainer->setVisible(bottomWasVisible);
    if (insideLegend && m_legendStack) m_legendStack->setVisible(legendStackWasVisible);

    painter.end();

    // Composite legend overlay after scaling
    if (insideLegend) {
        QRectF plotArea;
        if (m_chart && m_chartView) {
            QSizeF actualSize = this->size();
            double scaleX = (actualSize.width()  > 0) ? (double)width  / actualSize.width()  : 1.0;
            double scaleY = (actualSize.height() > 0) ? (double)height / actualSize.height() : 1.0;
            double scale  = qMin(scaleX, scaleY);
            QRectF pa = m_chart->plotArea();
            QPointF offset = m_chartView->mapTo(this, QPoint(0, 0));
            QRectF widgetPlotArea = pa.translated(offset);
            plotArea = QRectF(widgetPlotArea.left()  * scale, widgetPlotArea.top()    * scale,
                              widgetPlotArea.width() * scale, widgetPlotArea.height() * scale);
        }
        overlayLegend(pixmap, m_legendWidget, m_plotSettings.legendPosition, plotArea, m_plotSettings.legendX, m_plotSettings.legendY);
    }
    
    // Save the pixmap
    bool saved = pixmap.save(filePath, format.toUtf8().constData());
    
    qDebug() << "Export: Saved plot to" << filePath << "with dimensions" << width << "x" << height << "- Success:" << saved;
}

QPixmap PlotWidget::cropToContent(const QPixmap &source)
{
    if (source.isNull()) return source;
    
    QImage img = source.toImage();
    int width = img.width();
    int height = img.height();
    
    // Find content bounds by scanning for non-white pixels
    int left = width, right = 0, top = height, bottom = 0;
    QRgb white = QColor(Qt::white).rgb();
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            QRgb pixel = img.pixel(x, y);
            // Consider pixel as content if it's not pure white or very light gray
            if (pixel != white && qGray(pixel) < 250) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    }
    
    // Add small padding around content
    int padding = 10;
    left = qMax(0, left - padding);
    top = qMax(0, top - padding);
    right = qMin(width - 1, right + padding);
    bottom = qMin(height - 1, bottom + padding);
    
    // Crop to content bounds
    if (left < right && top < bottom) {
        QRect contentRect(left, top, right - left + 1, bottom - top + 1);
        return source.copy(contentRect);
    }
    
    return source; // Return original if no content found
}

void PlotWidget::exportPlotComposite(const QString &filePath, const QString &format, int width, int height, int dpi)
{
    if (!m_chartView || !m_legendWidget) return;

    // Resize to target dimensions before rendering so the source pixmap is full resolution,
    // not captured from the small default window size and then upscaled.
    QSize originalSize = this->size();
    if (width > 0 && height > 0) {
        this->resize(width, height);
    }

    // Ensure everything is visible and updated; force chart view repaint so error bars are painted
    this->show();
    this->update();
    m_chartView->update();
    m_chartView->repaint();
    // Limit processEvents to 500 ms max — prevents stalling when Qt Charts keeps
    // queuing repaint events for large datasets (many series/data points).
    QApplication::processEvents(QEventLoop::AllEvents, 500);
    
    // Render chart to pixmap (QChartView::render() does not include our custom error bars from paintEvent)
    QPixmap chartPixmap(m_chartView->size());
    chartPixmap.fill(Qt::white);
    QPainter chartPainter(&chartPixmap);
    m_chartView->render(&chartPainter);
    chartPainter.end();

    // Draw axis border lines, error bars, and box plots on top (bypassed when using render() directly)
    chartPainter.begin(&chartPixmap);
    m_chartView->paintAxisBorder(&chartPainter, m_chartView->viewport()->pos());
    m_chartView->paintErrorBars(&chartPainter, m_chartView->viewport()->pos());
    m_chartView->paintBoxPlotMedians(&chartPainter);
    chartPainter.end();

    QPixmap legendPixmap;
    if (m_showLegend) {
        legendPixmap = QPixmap(m_legendWidget->size());
        legendPixmap.fill(Qt::white);
        QPainter legendPainter(&legendPixmap);
        m_legendWidget->render(&legendPainter);
        legendPainter.end();
    }
    
    // Auto-crop both pixmaps to remove blank borders
    QPixmap croppedChart = cropToContent(chartPixmap);
    QPixmap croppedLegend = m_showLegend ? cropToContent(legendPixmap) : QPixmap();
    
    // Calculate total dimensions
    int totalWidth = m_showLegend ? (croppedChart.width() + croppedLegend.width()) : croppedChart.width();
    int maxHeight = m_showLegend ? qMax(croppedChart.height(), croppedLegend.height()) : croppedChart.height();
    
    // Create final composite image
    QPixmap finalPixmap(totalWidth, maxHeight);
    finalPixmap.fill(Qt::white);
    
    QPainter painter(&finalPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // Draw chart on the left
    painter.drawPixmap(0, 0, croppedChart);
    
    // Draw legend on the right
    if (m_showLegend && !croppedLegend.isNull()) {
        painter.drawPixmap(croppedChart.width(), 0, croppedLegend);
    }
    
    painter.end();

    // No upscaling needed — already rendered at target size. Only scale if composite
    // dimensions still differ from target (e.g. legend made it wider/taller).
    QPixmap outputPixmap;
    if (width > 0 && height > 0 && (totalWidth != width || maxHeight != height)) {
        outputPixmap = finalPixmap.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        outputPixmap = finalPixmap;
    }

    // Save the result
    bool saved = outputPixmap.save(filePath, format.toUtf8().constData());
    qDebug() << "Simple composite export: Final size" << outputPixmap.size() << "- Success:" << saved;

    // Restore original window size
    if (width > 0 && height > 0) {
        this->resize(originalSize);
    }
}

void PlotWidget::renderLegendContent(QPainter *painter, const QRect &rect)
{
    if (m_plotDataList.isEmpty()) return;
    
    painter->save();
    
    // Legend styling
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSize(12);
    
    QFont itemFont = painter->font();
    itemFont.setPointSize(10);
    
    int y = rect.top() + 10;
    int lineHeight = 20;
    int symbolSize = 12;
    int margin = 10;
    
    // Draw legend title
    painter->setFont(titleFont);
    painter->setPen(Qt::black);
    painter->drawText(rect.left() + margin, y, "Legend");
    y += lineHeight + 5;
    
    // Group plot data by variable
    QMap<QString, QVector<QSharedPointer<PlotData>>> legendEntries;
    for (auto plotData : m_plotDataList) {
        if (plotData) {
            legendEntries[plotData->variable].append(plotData);
        }
    }
    
    painter->setFont(itemFont);
    
    // Render each variable group
    for (auto it = legendEntries.begin(); it != legendEntries.end(); ++it) {
        const QString &variable = it.key();
        const auto &dataList = it.value();
        
        // Draw variable name
        painter->setPen(Qt::black);
        painter->drawText(rect.left() + margin, y, variable);
        y += lineHeight;
        
        // Draw each treatment for this variable
        for (auto plotData : dataList) {
            if (!plotData) continue;
            
            int x = rect.left() + margin + 15; // Indent treatments
            
            // Draw symbol/line sample
            painter->setPen(plotData->pen);
            painter->setBrush(plotData->brush);
            
            if (plotData->isObserved) {
                // Draw correct symbol for observed data
                QPointF center(x + symbolSize/2.0, y);
                if (plotData->symbol == "o") {
                    painter->drawEllipse(center, symbolSize/2.0, symbolSize/2.0);
                } else if (plotData->symbol == "s") {
                    painter->drawRect(x, y - symbolSize/2.0, symbolSize, symbolSize);
                } else if (plotData->symbol == "t") {
                    QPolygonF triangle;
                    triangle << QPointF(center.x(), center.y() - symbolSize/2.0)
                             << QPointF(center.x() + symbolSize/2.0, center.y() + symbolSize/2.0)
                             << QPointF(center.x() - symbolSize/2.0, center.y() + symbolSize/2.0);
                    painter->drawPolygon(triangle);
                } else if (plotData->symbol == "d" || plotData->symbol == "diamond") {
                    QPolygonF diamond;
                    diamond << QPointF(center.x(), center.y() - symbolSize/2.0)
                            << QPointF(center.x() + symbolSize/2.0, center.y())
                            << QPointF(center.x(), center.y() + symbolSize/2.0)
                            << QPointF(center.x() - symbolSize/2.0, center.y());
                    painter->drawPolygon(diamond);
                } else if (plotData->symbol == "v") {
                    QPolygonF invertedTriangle;
                    invertedTriangle << QPointF(center.x() - symbolSize/2.0, center.y() - symbolSize/2.0)
                                     << QPointF(center.x() + symbolSize/2.0, center.y() - symbolSize/2.0)
                                     << QPointF(center.x(), center.y() + symbolSize/2.0);
                    painter->drawPolygon(invertedTriangle);
                } else {
                    painter->drawEllipse(center, symbolSize/2.0, symbolSize/2.0);
                }
            } else {
                // Draw line for simulated data
                painter->drawLine(x, y, x + symbolSize * 2, y);
            }
            
            // Draw treatment name
            painter->setPen(Qt::black);
            QString treatmentText = plotData->treatmentName.isEmpty() ? 
                                  plotData->treatment : plotData->treatmentName;
            if (plotData->isObserved) {
                treatmentText += " (Obs)";
            }
            
            painter->drawText(x + symbolSize * 3, y + 4, treatmentText);
            y += lineHeight;
            
            // Check bounds
            if (y > rect.bottom() - lineHeight) break;
        }
        
        y += 5; // Extra space between variables
        if (y > rect.bottom() - lineHeight) break;
    }
    
    painter->restore();
}

void PlotWidget::setShowLegend(bool show)
{
    m_showLegend = show;
    // In scatter mode: legend is only useful when there are multiple experiments.
    // plotScatter() already sets visibility correctly; don't override it here.
    if (!m_isScatterMode)
        m_legendScrollArea->setVisible(show);
}

void PlotWidget::setShowGrid(bool show)
{
    m_showGrid = show;
    if (m_chart) {
        auto axes = m_chart->axes();
        for (auto axis : axes) {
            axis->setGridLineVisible(show);
        }
    }
}

void PlotWidget::setXAxisButtonsVisible(bool visible)
{
    if (m_dasButton) m_dasButton->setVisible(visible);
    if (m_dapButton) m_dapButton->setVisible(visible);
    if (m_dateButton) m_dateButton->setVisible(visible);
    // Box plot button is only for OSU summary files; hide whenever x-buttons are hidden
    if (!visible && m_boxPlotButton) m_boxPlotButton->setVisible(false);
}

void PlotWidget::setBoxPlotButtonVisible(bool visible)
{
    if (m_boxPlotButton) m_boxPlotButton->setVisible(visible);
    if (!visible && m_isBoxPlotMode) {
        m_isBoxPlotMode = false;
        if (m_boxPlotButton) {
            m_boxPlotButton->setChecked(false);
            m_boxPlotButton->setText("Box Plot");
        }
    }
}

void PlotWidget::setPreplotPanelVisible(bool visible)
{
    m_preplotPanelEnabled = visible;
    if (m_treatmentsButton) m_treatmentsButton->setVisible(visible);
    // Only hide the stack when disabling the preplot panel AND we're on the preplot page.
    // Once a plot is drawn (page 1 = legend), the stack must remain visible.
    if (m_legendStack) {
        if (!visible && m_legendStack->currentIndex() == 0)
            m_legendStack->setVisible(false);
        else if (visible)
            m_legendStack->setVisible(true);
        // if !visible but currentIndex == 1 (legend page), leave it visible
    }
}

void PlotWidget::setPlotTitle(const QString &title)
{
    if (m_chart) {
        m_chart->setTitle(title);
    }
}

void PlotWidget::setAxisTitles(const QString &xTitle, const QString &yTitle)
{
    if (!m_chart) return;
    
    auto axes = m_chart->axes();
    for (auto axis : axes) {
        if (axis->orientation() == Qt::Horizontal) {
            axis->setTitleText(xTitle);
        } else {
            axis->setTitleText(yTitle);
        }
    }
}

void PlotWidget::updateScalingLabel(const QStringList &yVars)
{
    qDebug() << "PlotWidget: updateScalingLabel called with variables:" << yVars;
    qDebug() << "PlotWidget: m_appliedScalingFactors keys:" << m_appliedScalingFactors.keys();
    QStringList scalingInfo;

    // Use the simpler applied scaling factors storage
    for (const QString &yVar : yVars) {
        qDebug() << "PlotWidget: Checking variable:" << yVar << "in applied scaling factors";
        
        if (m_appliedScalingFactors.contains(yVar)) {
            double scaleFactor = m_appliedScalingFactors[yVar];
            qDebug() << "PlotWidget: Variable" << yVar << "has applied scale factor:" << scaleFactor;
            
            if (qAbs(scaleFactor - 1.0) > 0.001) {  // More precise check for scaling
                // Get full variable name
                QPair<QString, QString> yVarInfo = DataProcessor::getVariableInfo(yVar);
                QString displayName = yVarInfo.first.isEmpty() ? yVar : yVarInfo.first;
                
                QString scaleText;
                if (scaleFactor < 1.0) {
                    scaleText = QString("%1: ÷%2").arg(displayName).arg(1.0/scaleFactor, 0, 'g', 3);
                } else {
                    scaleText = QString("%1: ×%2").arg(displayName).arg(scaleFactor, 0, 'g', 3);
                }
                scalingInfo.append(scaleText);
                qDebug() << "PlotWidget: Added to scaling info:" << scaleText;
            } else {
                qDebug() << "PlotWidget: Variable" << yVar << "has scale factor 1.0 (no scaling)";
            }
        } else {
            qDebug() << "PlotWidget: Variable" << yVar << "not found in applied scaling factors";
        }
    }
    
    QString labelText;
    if (!scalingInfo.isEmpty()) {
        labelText = "Scaling applied: " + scalingInfo.join(", ");
        qDebug() << "PlotWidget: Setting scaling label:" << labelText;
    } else {
        // Don't show any text when no scaling is applied
        labelText = ""; // Hide when no scaling is needed
        qDebug() << "PlotWidget: No scaling applied, hiding scaling label";
    }
    
    m_scalingLabel->setText(labelText);
    m_scalingLabel->setVisible(!labelText.isEmpty());
    qDebug() << "PlotWidget: Scaling label visibility set to:" << !labelText.isEmpty() << "with text:" << labelText;
}

// Removed complex legend controls - keeping simple legend like Python

// Legend methods -> see PlotWidget_Legend.cpp

QColor PlotWidget::getColorForTreatment(const QString &treatment, int index)
{
    // Check if this treatment already has a color assigned
    if (m_treatmentColorMap.contains(treatment)) {
        return m_treatmentColorMap[treatment];
    }
    
    // Assign a new color based on the number of treatments already mapped
    int colorIndex = m_treatmentColorMap.size() % m_plotColors.size();
    QColor assignedColor = m_plotColors[colorIndex];
    m_treatmentColorMap[treatment] = assignedColor;
    
    return assignedColor;
}

bool PlotWidget::hasVariable(const QString &varName, const DataTable &data)
{
    return data.columnNames.contains(varName);
}


void PlotWidget::changeXAxis(const QString &newXVar)
{
    if (m_currentXVar == newXVar) return;
    
    m_currentXVar = newXVar;
    
    // Re-plot with new X variable if data is available
    if (m_simData.rowCount > 0) {
        updatePlotWithScaling();
    }
}

void PlotWidget::updatePlotWithScaling()
{
    qDebug() << "PlotWidget::updatePlotWithScaling() - ENTRY POINT";
    qDebug() << "PlotWidget: Sim data rows:" << m_simData.rowCount;
    qDebug() << "PlotWidget: Current Y vars count:" << m_currentYVars.size();
    qDebug() << "PlotWidget: Current Y vars list:" << m_currentYVars;
    qDebug() << "PlotWidget: Current X var:" << m_currentXVar;
    
    if (m_simData.rowCount == 0) {
        qDebug() << "PlotWidget::updatePlotWithScaling() - NO DATA, returning";
        return;
    }

    // In multi-panel mode each variable has its own Y axis — scaling is not needed
    bool isMultiPanel = m_plotSettings.multiPanelTimeSeries && m_currentYVars.size() >= 2
                        && !m_isScatterMode && !m_isBoxPlotMode;

    DataTable scaledSimData = m_simData;
    DataTable scaledObsData = m_obsData;

    if (!isMultiPanel) {
        // Re-calculate scaling factors based on current Y-vars
        m_scaleFactors = calculateScalingFactors(m_simData, m_obsData, m_currentYVars);
        scaledSimData = applyScaling(scaledSimData, m_currentYVars);
        if (scaledObsData.rowCount > 0)
            scaledObsData = applyScaling(scaledObsData, m_currentYVars);
    } else {
        m_scaleFactors.clear();
    }

    // Update the plot with scaled data
    qDebug() << "PlotWidget::updatePlotWithScaling() - About to plot datasets";
    qDebug() << "PlotWidget::updatePlotWithScaling() - Plotting with SCALED data";
    
    // Clear the chart first to ensure fresh plotting
    if (m_chart) {
        m_chart->removeAllSeries();
        // Also remove axes so box plot â†” line plot switching doesn't stack old axes
        for (auto *axis : m_chart->axes())
            m_chart->removeAxis(axis);
    }

    // Clear error bars and median markers when updating plot
    if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
        m_chartView->clearBoxPlotMedians();
    }
    
    if (m_isBoxPlotMode) {
        plotOsuBoxPlot(scaledSimData, m_currentYVars, m_currentTreatments, m_selectedExperiment);
    } else {
        plotDatasets(scaledSimData, scaledObsData, m_currentXVar, m_currentYVars, m_currentTreatments, m_selectedExperiment);
    }
    qDebug() << "PlotWidget::updatePlotWithScaling() - Datasets plotted";

    // Update the scaling label (hidden in multi-panel mode — no scaling applied)
    if (isMultiPanel) {
        if (m_scalingLabel) m_scalingLabel->setVisible(false);
    } else {
        updateScalingLabel(m_currentYVars);
    }
    
    // Schedule auto-fit using timer (consolidates multiple auto-fit calls)
    if (m_autoFitTimer && !m_autoFitPending) {
        m_autoFitPending = true;
        m_autoFitTimer->start();
    }
    
    qDebug() << "PlotWidget::updatePlotWithScaling() - COMPLETED";

    // Switch between single-chart and multi-panel layout based on settings
    if (!m_isScatterMode && !m_isBoxPlotMode)
        plotTimeSeriesMultiPanel();
}

void PlotWidget::onPlotSettingsChanged()
{
    // Handle plot settings changes
}

void PlotWidget::onXAxisButtonClicked()
{
    // Handle X-axis button clicks
}

void PlotWidget::addTestData()
{
    
    if (!m_chart) {
        return;
    }
    
    // Clear any existing series first
    m_chart->removeAllSeries();
    m_plotDataList.clear();
    
    // Create test plot data for legend
    QSharedPointer<PlotData> simData1 = QSharedPointer<PlotData>::create();
    simData1->treatment = "1";
    simData1->treatmentName = "Treatment 1";
    simData1->variable = "HWAM";
    simData1->isObserved = false;
    simData1->experiment = "TEST";
    simData1->color = QColor("#1f77b4");
    simData1->pen = QPen(simData1->color, 2);
    simData1->brush = QBrush();
    simData1->symbol = "";
    
    QSharedPointer<PlotData> obsData1 = QSharedPointer<PlotData>::create();
    obsData1->treatment = "1";
    obsData1->treatmentName = "Treatment 1";
    obsData1->variable = "HWAM";
    obsData1->isObserved = true;
    obsData1->experiment = "TEST";
    obsData1->color = QColor("#1f77b4");
    obsData1->pen = QPen(obsData1->color, 2);
    obsData1->brush = QBrush(obsData1->color);
    obsData1->symbol = "o";
    
    QSharedPointer<PlotData> simData2 = QSharedPointer<PlotData>::create();
    simData2->treatment = "2";
    simData2->treatmentName = "Treatment 2";
    simData2->variable = "HWAM";
    simData2->isObserved = false;
    simData2->experiment = "TEST";
    simData2->color = QColor("#ff7f0e");
    simData2->pen = QPen(simData2->color, 2);
    simData2->brush = QBrush();
    simData2->symbol = "";
    
    // Add to plot data list
    m_plotDataList.append(simData1);
    m_plotDataList.append(obsData1);
    m_plotDataList.append(simData2);
    
    // Create a simple scatter series first (easier to see)
    QScatterSeries *scatterSeries = new QScatterSeries();
    scatterSeries->setName("TEST POINTS");
    scatterSeries->setColor(QColor("#FF0000")); // Bright red
    scatterSeries->setMarkerSize(15); // Large points
    
    // Add clear, spaced out points
    scatterSeries->append(1, 10);
    scatterSeries->append(2, 20);
    scatterSeries->append(3, 30);
    scatterSeries->append(4, 25);
    
    
    // Add to chart
    m_chart->addSeries(scatterSeries);
    
    // Create axes
    m_chart->createDefaultAxes();
    m_chart->setTitle("TEST CHART - Should See Red Dots");
    
    // Configure axes
    auto axes = m_chart->axes();
    
    for (auto axis : axes) {
        axis->setGridLineVisible(true);
        if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
            valueAxis->setLabelFormat("%.0f");
            if (axis->orientation() == Qt::Horizontal) {
                valueAxis->setRange(0, 5);
            } else {
                valueAxis->setRange(0, 35);
            }
        }
    }
    
    // Remove test label - keeping legend clean
    
    // Update legend with test data
    QVector<PlotData> testPlotDataList;
    for (const auto& plotData : m_plotDataList) {
        testPlotDataList.append(*plotData);
    }
    updateLegend(testPlotDataList);
    
}

bool PlotWidget::eventFilter(QObject* obj, QEvent* event)
{
    // Mouse events arrive on the viewport child widget, not on the QChartView itself.
    // Accept events from either so zoom/pan/hit-testing all work.
    const bool isChartObj = m_chartView &&
                            (obj == m_chartView || obj == m_chartView->viewport());

    if (isChartObj) {
        if (event->type() == QEvent::Wheel) {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            const double scaleFactor = 1.15;
            if (wheelEvent->angleDelta().y() > 0)
                m_chartView->chart()->zoom(scaleFactor);
            else
                m_chartView->chart()->zoom(1.0 / scaleFactor);
            m_isZoomed = true;
            return true;
        }
        else if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                if (m_isZoomed) {
                    m_chartView->chart()->zoomReset();
                    autoFitAxes();
                    m_isZoomed = false;
                } else {
                    // Zoom in 2x centred on the cursor
                    QPoint vpos = (obj == m_chartView->viewport())
                                  ? mouseEvent->pos()
                                  : m_chartView->viewport()->mapFromGlobal(mouseEvent->globalPos());
                    QPointF scenePos = m_chartView->mapToScene(vpos);
                    QPointF chartPos = m_chart->mapFromScene(scenePos);
                    QRectF plotArea = m_chart->plotArea();
                    double w = plotArea.width()  / 2.0;
                    double h = plotArea.height() / 2.0;
                    QRectF zoomRect(chartPos.x() - w / 2.0, chartPos.y() - h / 2.0, w, h);
                    // Clamp rect to plot area so we don't zoom outside data range
                    zoomRect = zoomRect.intersected(plotArea);
                    m_chart->zoomIn(zoomRect);
                    m_isZoomed = true;
                }
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::MiddleButton) {
                m_chartView->chart()->zoomReset();
                autoFitAxes();
                m_isZoomed = false;
                return true;
            }
            else if (mouseEvent->button() == Qt::RightButton) {
                m_chartView->setDragMode(QGraphicsView::ScrollHandDrag);
                return false;
            }
            else if (mouseEvent->button() == Qt::LeftButton) {
                QPoint vpos = (obj == m_chartView->viewport())
                              ? mouseEvent->pos()
                              : m_chartView->viewport()->mapFromGlobal(mouseEvent->globalPos());
                m_chartClickPressPos = vpos;
                // Ctrl held → potential image drag, block rubber-band
                if (mouseEvent->modifiers() & Qt::ControlModifier) {
                    m_ctrlDragPending = true;
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                m_chartView->setDragMode(QGraphicsView::RubberBandDrag);
                return false;
            }
            else if (mouseEvent->button() == Qt::LeftButton) {
                if (m_ctrlDragPending) {
                    m_ctrlDragPending = false;
                    return true;
                }
                QPoint vpos = (obj == m_chartView->viewport())
                              ? mouseEvent->pos()
                              : m_chartView->viewport()->mapFromGlobal(mouseEvent->globalPos());
                QPoint delta = vpos - m_chartClickPressPos;
                if (delta.manhattanLength() < 6) {
                    QAbstractSeries* hit = findSeriesNearPoint(vpos);
                    if (hit) {
                        selectLegendRowForSeries(hit);
                    }
                }
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint vpos = (obj == m_chartView->viewport())
                          ? mouseEvent->pos()
                          : m_chartView->viewport()->mapFromGlobal(mouseEvent->globalPos());

            // Ctrl+drag → export chart as drag image
            if (m_ctrlDragPending && (mouseEvent->buttons() & Qt::LeftButton)) {
                QPoint delta = vpos - m_chartClickPressPos;
                if (delta.manhattanLength() >= 6) {
                    m_ctrlDragPending = false;
                    // Grab the whole PlotWidget (chart + legend at their actual positions)
                    // Hide UI chrome (bottom toolbar) exactly as clipboard export does
                    bool bottomWasVisible = m_bottomContainer && m_bottomContainer->isVisible();
                    if (m_bottomContainer) m_bottomContainer->setVisible(false);
                    QApplication::processEvents();
                    QPixmap chartPixmap = this->grab();
                    if (m_bottomContainer) m_bottomContainer->setVisible(bottomWasVisible);

                    // Save to a temp file so apps that need a file URI (Word, web uploads) work
                    QString tempPath = QDir::temp().filePath("gb2_chart.png");
                    chartPixmap.save(tempPath, "PNG");

                    QByteArray pngData;
                    QBuffer buf(&pngData);
                    buf.open(QIODevice::WriteOnly);
                    chartPixmap.save(&buf, "PNG");
                    buf.close();

                    QMimeData* mimeData = new QMimeData();
                    mimeData->setData("image/png", pngData);
                    mimeData->setImageData(chartPixmap.toImage());
                    mimeData->setUrls({ QUrl::fromLocalFile(tempPath) });

                    QDrag* drag = new QDrag(this);
                    drag->setMimeData(mimeData);
                    drag->setPixmap(chartPixmap.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    drag->setHotSpot(QPoint(8, 8));
                    drag->exec(Qt::CopyAction);
                    return true;
                }
            }

            // Always use viewport coordinates for hit-testing
            QAbstractSeries* hit = findSeriesNearPoint(vpos);
            if (hit != m_hoveredSeries) {
                if (m_hoveredSeries) highlightLegendRowForSeries(m_hoveredSeries, false);
                m_hoveredSeries = hit;
                if (m_hoveredSeries) highlightLegendRowForSeries(m_hoveredSeries, true);
            }
            if (m_plotSettings.showHoverTooltip) {
                if (hit) {
                    QPointF nearestPt = findNearestDataPoint(hit, vpos);
                    showHoverTooltip(hit, nearestPt, vpos);
                } else {
                    hideHoverTooltip();
                }
            }
        }
    }
    
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget* widget = qobject_cast<QWidget*>(obj);
        if (widget && widget->property("seriesToHighlight").isValid()) {
            createToggleHandler(widget);
            return true;
        }
    }

    // ── Legend width resize (left strip) ────────────────────────────────────
    if (obj == m_legendResizeStrip) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_legendResizing = true;
                m_legendResizeStartGlobalX = m_legendResizeStrip->mapToGlobal(me->pos()).x();
                m_legendResizeStartWidth = m_legendPanel->width();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            if (m_legendResizing) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                int delta = m_legendResizeStartGlobalX - m_legendResizeStrip->mapToGlobal(me->pos()).x();
                m_legendUserWidth = qBound(100, m_legendResizeStartWidth + delta, 600);
                m_legendPanel->setFixedWidth(m_legendUserWidth);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            m_legendResizing = false;
            return true;
        }
    }

    // ── Legend height resize (bottom strip, floating only) ──────────────────
    if (obj == m_legendResizeBottom) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_legendResizingH = true;
                m_legendResizeStartGlobalY = m_legendResizeBottom->mapToGlobal(me->pos()).y();
                m_legendResizeStartHeight = m_legendPanel->height();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            if (m_legendResizingH) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                int delta = m_legendResizeBottom->mapToGlobal(me->pos()).y() - m_legendResizeStartGlobalY;
                int newHeight = qBound(80, m_legendResizeStartHeight + delta, 900);
                m_legendPanel->setFixedHeight(newHeight);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            m_legendResizingH = false;
            return true;
        }
    }

    // ── Legend handle drag ──────────────────────────────────────────────────
    if (obj == m_legendHandle) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_legendDragging = true;
                QPoint globalClick = m_legendHandle->mapToGlobal(me->pos());
                m_legendDragOffset = globalClick - m_legendPanel->mapToGlobal(QPoint(0, 0));
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                if (m_legendFloating) dockLegend();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (m_legendDragging) {
                QPoint globalCursor = m_legendHandle->mapToGlobal(me->pos());
                QPoint newPos = mapFromGlobal(globalCursor - m_legendDragOffset);
                if (!m_legendFloating) floatLegend();
                newPos.setX(qBound(0, newPos.x(), width()  - m_legendPanel->width()));
                newPos.setY(qBound(0, newPos.y(), height() - m_legendPanel->height()));
                m_legendPanel->move(newPos);
                m_legendPanel->raise();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            m_legendDragging = false;
            return false;
        }
    }

    return QWidget::eventFilter(obj, event);
}


void PlotWidget::createToggleHandler(QWidget* rowWidget)
{
    // Matching Python toggle_highlight function
    bool currentState = rowWidget->property("highlighted").toBool();
    
    // Reset all items first (matching Python _reset_all_highlighted_items)
    resetAllHighlightedItems();
    
    // If it was already highlighted, toggle off
    if (currentState) {
        rowWidget->setProperty("highlighted", false);
        return;
    }
    
    // Otherwise highlight this row
    rowWidget->setProperty("highlighted", true);
    QString originalStyle = rowWidget->property("originalStyle").toString();
    rowWidget->setStyleSheet(originalStyle + "background-color: #e6f2ff; border: 1px solid #99ccff;");
    
    // Get plot items for this row
    QVector<QAbstractSeries*> plotItems = 
        rowWidget->property("seriesToHighlight").value<QVector<QAbstractSeries*>>();
    
    // Highlight/dim items (matching Python logic)
    for (int i = 0; i < m_chart->series().count(); ++i) {
        QAbstractSeries* series = m_chart->series().at(i);
        
        if (plotItems.contains(series)) {
            // Highlight selected items
            highlightSeries(series, true);
        } else {
            // Dim other items
            highlightSeries(series, false);
        }
    }
}

void PlotWidget::highlightSeries(QAbstractSeries* series, bool highlight)
{
    if (!series) return;
    
    if (QLineSeries* lineSeries = qobject_cast<QLineSeries*>(series)) {
        QPen currentPen = lineSeries->pen();
        
        if (highlight) {
            // Highlight: make thicker and more opaque
            if (!lineSeries->property("originalPen").isValid()) {
                lineSeries->setProperty("originalPen", QVariant::fromValue(currentPen));
            }
            QPen highlightPen = currentPen;
            highlightPen.setWidth(currentPen.width() * 2);
            lineSeries->setPen(highlightPen);
        } else {
            // Dim: make semi-transparent
            if (!lineSeries->property("originalPen").isValid()) {
                lineSeries->setProperty("originalPen", QVariant::fromValue(currentPen));
            }
            QPen dimPen = currentPen;
            QColor dimColor = dimPen.color();
            dimColor.setAlpha(50);
            dimPen.setColor(dimColor);
            lineSeries->setPen(dimPen);
        }
    } else if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
        if (highlight) {
            // Highlight: make larger
            if (!scatterSeries->property("originalSize").isValid()) {
                scatterSeries->setProperty("originalSize", scatterSeries->markerSize());
            }
            scatterSeries->setMarkerSize(scatterSeries->markerSize() * 1.5);
        } else {
            // Dim: make semi-transparent
            if (!scatterSeries->property("originalBrush").isValid()) {
                scatterSeries->setProperty("originalBrush", QVariant::fromValue(scatterSeries->brush()));
            }
            QBrush dimBrush = scatterSeries->brush();
            QColor dimColor = dimBrush.color();
            dimColor.setAlpha(50);
            dimBrush.setColor(dimColor);
            scatterSeries->setBrush(dimBrush);
        }
    }
}

QString PlotWidget::getTreatmentDisplayName(const QString &trtId, const QString &experimentId, const QString &cropId)
{
    // Matching Python _get_treatment_display_name logic
    QString tname;
    
    
    // First try exact match with experiment key
    if (m_treatmentNames.contains(experimentId)) {
        tname = m_treatmentNames[experimentId].value(trtId);
    }
    
    // If not found and the key contains underscore (experiment_crop format),
    // try just the experiment part (matching Python logic)
    if (tname.isEmpty() && experimentId.contains('_')) {
        QString baseExpOnly = experimentId.split('_').first();
        if (m_treatmentNames.contains(baseExpOnly)) {
            tname = m_treatmentNames[baseExpOnly].value(trtId);
        }
    }
    
    // Fallback to default key
    if (tname.isEmpty() && m_treatmentNames.contains("default")) {
        tname = m_treatmentNames["default"].value(trtId);
    }
    
    // If no name found, use treatment number only
    if (tname.isEmpty()) {
        tname = trtId;
    }
    
    // Detect multiple crops by checking current plot data
    QSet<QString> uniqueCrops;
    for (const QSharedPointer<PlotData> &plotData : m_plotDataList) {
        if (plotData && !plotData->crop.isEmpty() && plotData->crop != "XX") {
            uniqueCrops.insert(plotData->crop);
        }
    }
    
    // If we have a specific crop for this treatment, always show it when there are potential conflicts
    // This handles cases where we're building legend entries and need to distinguish crops
    bool hasMultipleCrops = (uniqueCrops.size() > 1) || 
                           (!cropId.isEmpty() && cropId != "XX" && uniqueCrops.size() >= 1 && !uniqueCrops.contains(cropId));
    
    // Add crop info if meaningful and we have multiple crops
    QString cropName;
    if (!cropId.isEmpty() && cropId != "XX" && hasMultipleCrops) {
        cropName = getCropNameFromCode(cropId);
    }
    
    // Build final display name with experiment and crop info
    QString result = tname;
    QStringList suffixes;
    
    // Add experiment info if meaningful and we have multiple experiments
    if (!experimentId.isEmpty() && 
        experimentId != "simulation" && experimentId != "observed" && experimentId != "default" &&
        m_treatmentNames.size() > 1) {
        suffixes.append(experimentId);
    }
    
    // Add crop info if we have multiple crops
    if (!cropName.isEmpty()) {
        suffixes.append(cropName);
    }
    
    if (!suffixes.isEmpty()) {
        result = QString("%1 (%2)").arg(tname).arg(suffixes.join(", "));
    }
    
    return result;
}

void PlotWidget::onBoxPlotButtonClicked()
{
    m_isBoxPlotMode = m_boxPlotButton ? m_boxPlotButton->isChecked() : false;
    if (m_boxPlotButton)
        m_boxPlotButton->setText(m_isBoxPlotMode ? "Line Plot" : "Box Plot");
    if (m_chartView) m_chartView->clearBoxPlotMedians();
    updatePlotWithScaling();
}

void PlotWidget::setBoxPlotMode(bool enabled)
{
    m_isBoxPlotMode = enabled;
    if (m_boxPlotButton) {
        m_boxPlotButton->setChecked(enabled);
        m_boxPlotButton->setText(enabled ? "Line Plot" : "Box Plot");
    }
    if (m_chartView) m_chartView->clearBoxPlotMedians();
}

// ---------------------------------------------------------------------------
// OSU seasonal box plot
// ---------------------------------------------------------------------------
// plotOsuBoxPlot → see PlotWidget_BoxPlot.cpp
// X-axis button handlers
void PlotWidget::onDasButtonClicked()
{
    qDebug() << "PlotWidget: DAS button clicked";
    setXAxisVariable("DAS");
}

void PlotWidget::onDapButtonClicked()
{
    qDebug() << "PlotWidget: DAP button clicked";
    setXAxisVariable("DAP");
}

void PlotWidget::onDateButtonClicked()
{
    qDebug() << "PlotWidget: DATE button clicked";
    setXAxisVariable("DATE");
}

void PlotWidget::setupPreplotPanel()
{
    m_preplotPanel = new QWidget();
    m_preplotPanel->setStyleSheet("background-color: white;");

    QVBoxLayout *layout = new QVBoxLayout(m_preplotPanel);
    layout->setContentsMargins(5, 6, 5, 6);
    layout->setSpacing(4);

    // Header — matches legend section header style
    QLabel *titleLabel = new QLabel("Treatments");
    titleLabel->setStyleSheet(
        "font-size: 10px; font-weight: bold; color: #555555; "
        "padding: 2px 0px; border-bottom: 1px solid #dddddd;"
    );
    titleLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(titleLabel);

    // All / None buttons — compact, like legend action buttons
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);
    btnRow->setContentsMargins(0, 2, 0, 2);
    QPushButton *allBtn  = new QPushButton("All");
    QPushButton *noneBtn = new QPushButton("None");
    QString btnStyle =
        "QPushButton { padding: 2px 8px; border: 1px solid #cccccc; "
        "border-radius: 3px; background: #f5f5f5; font-size: 10px; color: #333333; }"
        "QPushButton:hover { background: #e0e0e0; }";
    allBtn->setStyleSheet(btnStyle);
    noneBtn->setStyleSheet(btnStyle);
    allBtn->setFixedHeight(20);
    noneBtn->setFixedHeight(20);
    btnRow->addWidget(allBtn);
    btnRow->addWidget(noneBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // Treatment checklist — matches legend list style
    m_treatmentSelectList = new QListWidget();
    m_treatmentSelectList->setFrameShape(QFrame::NoFrame);
    m_treatmentSelectList->setStyleSheet(
        "QListWidget { background: white; border: none; }"
        "QListWidget::item { padding: 3px 2px; font-size: 10px; color: #333333; }"
        "QListWidget::item:hover { background: #f0f4ff; }"
    );
    m_treatmentSelectList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_treatmentSelectList, 1);

    // Hint at bottom
    QLabel *hintLabel = new QLabel("Click Refresh Plot to apply.");
    hintLabel->setStyleSheet("font-size: 9px; color: #999999; padding: 2px 0px;");
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    connect(allBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_treatmentSelectList->count(); ++i)
            m_treatmentSelectList->item(i)->setCheckState(Qt::Checked);
    });
    connect(noneBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_treatmentSelectList->count(); ++i)
            m_treatmentSelectList->item(i)->setCheckState(Qt::Unchecked);
    });
}

void PlotWidget::setAvailableTreatments(const QStringList &treatments,
    const QMap<QString, QMap<QString, QString>> &treatmentNames)
{
    if (!m_treatmentSelectList) return;

    m_treatmentSelectList->blockSignals(true);
    m_treatmentSelectList->clear();

    for (const QString &key : treatments) {
        // Parse compound keys:
        //   "R#::slot::cropName"  — sequence OSU slot
        //   "exp::trt"            — multi-experiment regular file
        //   "trt"                 — single experiment
        QString expPrefix, trtId, displayName;
        QStringList parts = key.split("::");
        if (parts.size() >= 2 && parts[0] == "R#") {
            // Sequence OSU: "R#::slot" or "R#::slot::cropName"
            trtId       = parts[1];
            displayName = parts.size() >= 3 ? parts.mid(2).join("::") : QString();
        } else if (parts.size() == 2) {
            expPrefix = parts[0];
            trtId     = parts[1];
        } else {
            trtId = key;
        }

        // For regular files, look up display name from treatmentNames map
        if (displayName.isEmpty()) {
            if (!expPrefix.isEmpty() && treatmentNames.contains(expPrefix))
                displayName = treatmentNames[expPrefix].value(trtId);
            if (displayName.isEmpty()) {
                for (auto it = treatmentNames.constBegin(); it != treatmentNames.constEnd(); ++it) {
                    if (it.value().contains(trtId)) {
                        displayName = it.value().value(trtId);
                        break;
                    }
                }
            }
        }

        QString label;
        if (!expPrefix.isEmpty()) {
            label = displayName.isEmpty()
                ? QString("%1 · %2").arg(trtId, expPrefix)
                : QString("%1 - %2 · %3").arg(trtId, displayName, expPrefix);
        } else {
            label = displayName.isEmpty() ? trtId : QString("%1 - %2").arg(trtId, displayName);
        }

        QListWidgetItem *item = new QListWidgetItem(label);
        // Store only the "R#::slot" part as the functional key (crop name is display-only)
        QString storeKey = (parts.size() >= 3 && parts[0] == "R#") ? ("R#::" + trtId) : key;
        item->setData(Qt::UserRole, storeKey);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        m_treatmentSelectList->addItem(item);
    }

    m_treatmentSelectList->blockSignals(false);

    // Switch legend area to show treatment pre-selection
    if (m_legendStack && m_preplotPanelEnabled)
        m_legendStack->setCurrentIndex(0);
}

QStringList PlotWidget::getSelectedTreatments() const
{
    if (!m_treatmentSelectList) return QStringList();

    QStringList selected;
    int total = m_treatmentSelectList->count();
    for (int i = 0; i < total; ++i) {
        QListWidgetItem *item = m_treatmentSelectList->item(i);
        if (item && item->checkState() == Qt::Checked)
            selected.append(item->data(Qt::UserRole).toString());
    }

    // If list empty or all checked, return empty = show all
    if (total == 0 || selected.count() == total)
        return QStringList();
    // If none checked, return sentinel — filter will match nothing
    if (selected.isEmpty())
        return QStringList() << "__NO_SELECTION__";
    return selected;
}

void PlotWidget::showTreatmentSelection()
{
    // Only switch if there are treatments to show
    if (m_legendStack && m_preplotPanelEnabled && m_treatmentSelectList && m_treatmentSelectList->count() > 0)
        m_legendStack->setCurrentIndex(0);
}

// onSettingsButtonClicked / applyPlotSettings / saveSettings / loadSettings / setXAxisVariable → see PlotWidget_Settings.cpp

// plotScatter -> see PlotWidget_Scatter.cpp

void PlotWidget::floatLegend()
{
    if (m_legendFloating || !m_legendPanel) return;
    m_mainLayout->removeWidget(m_legendPanel);
    m_legendPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    m_legendPanel->setMinimumHeight(0);
    m_legendStack->setMaximumHeight(QWIDGETSIZE_MAX);
    m_legendStack->setMinimumHeight(0);
    m_legendPanel->adjustSize();
    m_legendPanel->setAttribute(Qt::WA_TranslucentBackground);
    {
        QColor bg = m_plotSettings.legendBackgroundColor;
        if (bg.alpha() == 0) bg = QColor(255, 255, 255, 245);
        m_legendPanel->setStyleSheet(
            QString("#legendPanel { background: rgba(%1,%2,%3,%4); border: 1px solid #4a6fa5; border-radius: 4px; }")
                .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha()));
    }
    m_legendPanel->setObjectName("legendPanel");
    m_legendPanel->setFixedWidth(m_legendUserWidth);
    if (m_legendResizeBottom) m_legendResizeBottom->show();
    if (auto* btn = m_legendHandle->findChild<QPushButton*>("legendDockBtn"))
        btn->show();
    m_legendPanel->show();
    m_legendPanel->raise();
    m_legendFloating = true;
}

void PlotWidget::dockLegend()
{
    if (!m_legendFloating || !m_legendPanel) return;
    if (auto* btn = m_legendHandle->findChild<QPushButton*>("legendDockBtn"))
        btn->hide();
    if (m_legendResizeBottom) m_legendResizeBottom->hide();
    m_legendPanel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_legendPanel->setObjectName("");
    {
        QColor bg = m_plotSettings.legendBackgroundColor;
        if (bg.alpha() > 0) {
            m_legendPanel->setStyleSheet(
                QString("background-color: rgba(%1,%2,%3,%4);")
                    .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha()));
        } else {
            m_legendPanel->setStyleSheet("");
        }
    }
    // Clear fixed height so panel fills full window height when docked
    m_legendPanel->setMinimumHeight(0);
    m_legendPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    m_legendPanel->setFixedWidth(m_legendUserWidth);
    m_mainLayout->addWidget(m_legendPanel);
    m_legendPanel->show();
    m_legendFloating = false;
    m_legendDragging = false;
    m_legendResizingH = false;
}
