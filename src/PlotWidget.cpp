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
#include <QEnterEvent>
#include <QContextMenuEvent>
#include <algorithm>
#include <cmath>

// ErrorBarChartView implementation
ErrorBarChartView::ErrorBarChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent)
{
}

void ErrorBarChartView::setErrorBarData(const QMap<QAbstractSeries*, QVector<ErrorBarData>> &errorBars)
{
    m_errorBars = errorBars;
    update();  // Trigger repaint
}

void ErrorBarChartView::paintErrorBars(QPainter *painter, const QPoint &viewportOffset)
{
    if (!painter || m_errorBars.isEmpty() || !chart()) return;

    painter->save();
    if (!viewportOffset.isNull()) {
        painter->translate(viewportOffset);
    }
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF plotArea = chart()->plotArea();
    QList<QAbstractAxis*> axes = chart()->axes();
    QAbstractAxis *xAxis = nullptr;
    QAbstractAxis *yAxis = nullptr;

    for (QAbstractAxis *axis : axes) {
        if (axis->alignment() == Qt::AlignBottom || axis->alignment() == Qt::AlignTop) {
            xAxis = axis;
        } else if (axis->alignment() == Qt::AlignLeft || axis->alignment() == Qt::AlignRight) {
            yAxis = axis;
        }
    }

    if (!xAxis || !yAxis) {
        painter->restore();
        return;
    }

    for (auto it = m_errorBars.begin(); it != m_errorBars.end(); ++it) {
        QAbstractSeries *series = it.key();
        const QVector<ErrorBarData> &errorBars = it.value();

        if (errorBars.isEmpty()) continue;

        QColor color = Qt::black;
        if (auto lineSeries = qobject_cast<QLineSeries*>(series)) {
            color = lineSeries->pen().color();
        } else if (auto scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            QColor penColor = scatterSeries->pen().color();
            color = penColor.isValid() ? penColor : scatterSeries->brush().color();
        }
        painter->setPen(QPen(color, 1.5));

        double xMin, xMax, yMin, yMax;
        if (QValueAxis *valueXAxis = qobject_cast<QValueAxis*>(xAxis)) {
            xMin = valueXAxis->min();
            xMax = valueXAxis->max();
        } else if (QDateTimeAxis *dateXAxis = qobject_cast<QDateTimeAxis*>(xAxis)) {
            xMin = dateXAxis->min().toMSecsSinceEpoch();
            xMax = dateXAxis->max().toMSecsSinceEpoch();
        } else {
            continue;
        }

        if (QValueAxis *valueYAxis = qobject_cast<QValueAxis*>(yAxis)) {
            yMin = valueYAxis->min();
            yMax = valueYAxis->max();
        } else {
            continue;
        }

        for (const ErrorBarData &errorBar : errorBars) {
            double xRatio = (errorBar.meanX - xMin) / (xMax - xMin);
            double yRatio = (errorBar.meanY - yMin) / (yMax - yMin);

            double x = plotArea.left() + xRatio * plotArea.width();
            double y = plotArea.top() + (1.0 - yRatio) * plotArea.height();

            double errorHeight = (errorBar.errorValue / (yMax - yMin)) * plotArea.height();
            double topY = y - errorHeight;
            double bottomY = y + errorHeight;

            painter->drawLine(QPointF(x, topY), QPointF(x, bottomY));
            const double capWidth = 5.0;
            painter->drawLine(QPointF(x - capWidth, topY), QPointF(x + capWidth, topY));
            painter->drawLine(QPointF(x - capWidth, bottomY), QPointF(x + capWidth, bottomY));
        }
    }
    painter->restore();
}

void ErrorBarChartView::paintEvent(QPaintEvent *event)
{
    QChartView::paintEvent(event);

    if (m_errorBars.isEmpty() || !chart()) return;

    QPainter painter(this->viewport());
    paintErrorBars(&painter, QPoint(0, 0));
}

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_leftContainer(nullptr)
    , m_leftLayout(nullptr)
    , m_chart(nullptr)
    , m_chartView(nullptr)
    , m_bottomContainer(nullptr)
    , m_bottomLayout(nullptr)
    , m_dateButton(nullptr)
    , m_dasButton(nullptr)
    , m_dapButton(nullptr)
    , m_settingsButton(nullptr)
    , m_scalingLabel(nullptr)
    , m_legendScrollArea(nullptr)
    , m_legendWidget(nullptr)
    , m_legendLayout(nullptr)
    , m_showLegend(true)
    , m_showGrid(true)
    , m_currentPlotType("Line")
    , m_dataProcessor(new DataProcessor(this))
    , m_currentXVar("DAP")
    , m_isScatterMode(false)
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

    m_markerSymbols = {"o", "s", "d", "t", "+", "x", "p", "h", "star"};
    
    // Initialize optimization variables
    m_autoFitPending = false;
    m_autoFitTimer = new QTimer(this);
    m_autoFitTimer->setSingleShot(true);
    m_autoFitTimer->setInterval(100);  // 100ms delay for auto-fit
    connect(m_autoFitTimer, &QTimer::timeout, this, &PlotWidget::autoFitAxes);
    
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    
    // Add some space before settings button
    m_bottomLayout->addSpacing(20);
    m_bottomLayout->addWidget(m_settingsButton);
    
    // Connect button signals
    connect(m_dasButton, &QPushButton::clicked, this, &PlotWidget::onDasButtonClicked);
    connect(m_dapButton, &QPushButton::clicked, this, &PlotWidget::onDapButtonClicked);
    connect(m_dateButton, &QPushButton::clicked, this, &PlotWidget::onDateButtonClicked);
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
    
    
    // Add a placeholder widget for the chart (will be replaced in setupChart)
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
    m_legendScrollArea->setFixedWidth(200);
    
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
    m_mainLayout->addWidget(m_legendScrollArea, 20);
}

void PlotWidget::setupChart()
{
    if (m_chart) {
        delete m_chart;
    }
    
    m_chart = new QChart();
    m_chartView = new ErrorBarChartView(m_chart);
    m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Enable zoom and pan functionality
    m_chartView->setDragMode(QGraphicsView::RubberBandDrag);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    
    // Enable pan with right mouse button by setting drag mode programmatically
    // Pan will be handled in eventFilter for right mouse button
    
    // Install event filter for mouse wheel zoom and middle-click reset
    m_chartView->installEventFilter(this);
    
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
    xAxis->setMinorGridLineVisible(true);
    yAxis->setMinorGridLineVisible(true);
    
    m_chart->addAxis(xAxis, Qt::AlignBottom);
    m_chart->addAxis(yAxis, Qt::AlignLeft);
    
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
    if (xVar == "DATE") {
        QDateTimeAxis *dateAxis = new QDateTimeAxis();
        
        // Use comprehensive format that shows year
        // For agricultural data, this is important as growing seasons span years
        dateAxis->setFormat("MMM dd, yyyy");
        dateAxis->setTitleText("Date");
        
        // Set dynamic tick count based on plot size
        int optimalTicks = calculateOptimalDateTickCount();
        dateAxis->setTickCount(optimalTicks);
        
        xAxis = dateAxis;
        qDebug() << "PlotWidget::setupAxes() - Created QDateTimeAxis for DATE variable with format: MMM dd, yyyy and dynamic tickCount:" << optimalTicks;
    } else {
        QValueAxis *valueAxis = new QValueAxis();
        
        // Set appropriate title based on variable info from CDE file
        QPair<QString, QString> xVarInfo = DataProcessor::getVariableInfo(xVar);
        QString xTitle = xVarInfo.first.isEmpty() ? xVar : xVarInfo.first;
        valueAxis->setTitleText(xTitle);
        
        // Add minor ticks to X value axis
        valueAxis->setMinorTickCount(4);
        valueAxis->setMinorGridLineVisible(true);
        
        xAxis = valueAxis;
        qDebug() << "PlotWidget::setupAxes() - Created QValueAxis for" << xVar;
    }
    
    // Create Y-axis (always value axis)
    QValueAxis *yAxis = new QValueAxis();
    
    // Set Y-axis title based on current Y variables
    QString yTitle = "Y Variable";
    if (!m_currentYVars.isEmpty()) {
        QStringList yLabels;
        for (const QString &yVar : m_currentYVars) {
            QPair<QString, QString> yVarInfo = DataProcessor::getVariableInfo(yVar);
            QString baseLabel = yVarInfo.first.isEmpty() ? yVar : yVarInfo.first;
            // Include scaling factor if available
            if (m_scaleFactors.contains("default") && m_scaleFactors["default"].contains(yVar)) {
                const auto& info = m_scaleFactors["default"][yVar];
                if (info.scaleFactor != 1.0) {
                    baseLabel += QString(" (x%1)").arg(info.scaleFactor, 0, 'g', 3);
                }
            }
            yLabels.append(baseLabel);
        }
        yTitle = yLabels.join(", ");
    }
    yAxis->setTitleText(yTitle);
    
    // Add minor ticks to Y axis
    yAxis->setMinorTickCount(4);
    yAxis->setMinorGridLineVisible(true);
    
    // Configure axis appearance
    xAxis->setGridLineVisible(m_showGrid);
    yAxis->setGridLineVisible(m_showGrid);
    xAxis->setLabelsVisible(true);
    yAxis->setLabelsVisible(true);
    
    // Add axes to chart
    m_chart->addAxis(xAxis, Qt::AlignBottom);
    m_chart->addAxis(yAxis, Qt::AlignLeft);
    
    qDebug() << "PlotWidget::setupAxes() - Added axes for X variable:" << xVar;
}

void PlotWidget::autoFitAxes()
{
    // Reset pending flag when auto-fit executes
    m_autoFitPending = false;
    
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
                valueAxis->setRange(minX - xLeftPadding, maxX + xRightPadding);
                
                // Check if this is a day-based variable (DAS, DAP, etc.)
                QString xVarName = m_currentXVar.toUpper();
                if (xVarName.contains("DAS") || xVarName.contains("DAP") || 
                    xVarName.contains("DAY") || xVarName.contains("DATE")) {
                    // For day variables, use clean round intervals
                    double dataRange = maxX - minX;
                    double tickInterval = calculateNiceXInterval(dataRange);
                    
                    // Align range to clean multiples of the interval
                    double cleanMinX = std::floor(minX / tickInterval) * tickInterval;
                    double cleanMaxX = std::ceil(maxX / tickInterval) * tickInterval;
                    
                    // Set clean range and interval
                    valueAxis->setRange(cleanMinX, cleanMaxX);
                    valueAxis->setTickInterval(tickInterval);
                    valueAxis->setLabelFormat("%.0f"); // No decimals for days
                    
                    // Force tick count for more ticks
                    int tickCount = qRound((cleanMaxX - cleanMinX) / tickInterval) + 1;
                    if (tickCount < 8) tickCount = 8;
                    valueAxis->setTickCount(tickCount);
                    
                    qDebug() << "PlotWidget::autoFitAxes() - Set X day-based axis: range" << cleanMinX << "to" << cleanMaxX << "interval:" << tickInterval << "ticks:" << tickCount;
                } else {
                    // For other variables, use default formatting
                    valueAxis->setLabelFormat("%.2f");
                }
                
                valueAxis->setMinorTickCount(4);
                valueAxis->setMinorGridLineVisible(true);
                qDebug() << "PlotWidget::autoFitAxes() - Set X ValueAxis range:" << (minX - xLeftPadding) << "to" << (maxX + xRightPadding);
            } else if (auto dateAxis = qobject_cast<QDateTimeAxis*>(axis)) {
                QDateTime minDateTime = QDateTime::fromMSecsSinceEpoch(minX - xLeftPadding);
                QDateTime maxDateTime = QDateTime::fromMSecsSinceEpoch(maxX + xRightPadding);
                dateAxis->setRange(minDateTime, maxDateTime);
                
                // Set dynamic tick count based on plot size
                int optimalTicks = calculateOptimalDateTickCount();
                dateAxis->setTickCount(optimalTicks);
                
                qDebug() << "PlotWidget::autoFitAxes() - Set X DateTimeAxis range:" << minDateTime.toString() << "to" << maxDateTime.toString() << "with dynamic tickCount:" << optimalTicks;
            }
        } else if (axis->orientation() == Qt::Vertical) {
            if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
                // Start with data maximum and minimal padding (5%)
                double dataMax = maxY;
                double minimalPadding = dataMax * 0.05;  // Just 5% padding
                double targetMax = dataMax + minimalPadding;
                
                // Find clean interval based on data range only
                double tickInterval = calculateNiceYInterval(targetMax);
                
                // Find the next clean tick above the target
                double alignedMax = std::ceil(targetMax / tickInterval) * tickInterval;
                
                // Make sure we have at least one tick above data
                if (alignedMax <= dataMax) {
                    alignedMax += tickInterval;
                }
                
                // Set clean range and interval
                valueAxis->setRange(0, alignedMax);
                valueAxis->setTickInterval(tickInterval);
                valueAxis->setMinorTickCount(4);
                valueAxis->setMinorGridLineVisible(true);
                
                // Force tick count for better readability
                int tickCount = qRound(alignedMax / tickInterval) + 1;
                if (tickCount < 8) tickCount = 8; // Minimum 8 ticks
                valueAxis->setTickCount(tickCount);
                
                qDebug() << "PlotWidget::autoFitAxes() - Y-axis: data max=" << dataMax << "target=" << targetMax << "aligned=" << alignedMax << "interval=" << tickInterval << "ticks=" << tickCount;
            }
        }
    }
    
    qDebug() << "PlotWidget::autoFitAxes() - Completed with data bounds: X(" << minX << "," << maxX << ") Y(" << minY << "," << maxY << ")";
    
    // Force chart to repaint with new axis settings
    if (m_chart) {
        m_chart->update();
    }
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
    QVector<double> cleanIntervals = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000};
    
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
    
    // Base calculation: aim for ~60-80 pixels per tick for good readability
    double pixelsPerTick = 70.0; // Target pixels between ticks
    int baseTicks = qMax(4, qRound(plotWidth / pixelsPerTick));
    
    // Apply size-based scaling
    int optimalTicks;
    if (plotWidth < 400) {
        // Small plot: fewer ticks to avoid crowding
        optimalTicks = qBound(4, baseTicks - 2, 8);
    } else if (plotWidth < 800) {
        // Medium plot: standard tick count
        optimalTicks = qBound(6, baseTicks, 12);
    } else if (plotWidth < 1200) {
        // Large plot: more ticks for detail
        optimalTicks = qBound(8, baseTicks, 16);
    } else {
        // Very large plot: maximum detail
        optimalTicks = qBound(10, baseTicks, 20);
    }
    
    qDebug() << "PlotWidget::calculateOptimalDateTickCount() - Width:" << plotWidth 
             << "Base ticks:" << baseTicks << "Optimal ticks:" << optimalTicks;
    
    return optimalTicks;
}

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
                qDebug() << "PlotWidget::resizeEvent() - Updated date axis tick count to:" << newTickCount;
                break; // Only update the first date axis
            }
        }
    }
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
    
    // Show DAS, DAP, DATE buttons for time series plots (they're applicable)
    setXAxisButtonsVisible(true);
    
    try {
        
        clear();

        m_simData = simData;
        m_obsData = obsData;
        
        // Debug data assignment
        
        m_selectedFolder = selectedFolder;
        m_selectedExperiment = selectedExperiment;
        m_currentTreatments = selectedTreatments;
        m_currentXVar = xVar;
        m_currentYVars = yVars;
        m_treatmentNames = treatmentNames;

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
        
        for (int row = 0; row < simData.rowCount; ++row) {
            if (row >= xColumn->data.size() || row >= yColumn->data.size() || row >= trtColumn->data.size()) {
                continue;
            }
            
            QString trt = trtColumn->data[row].toString();
            if (!treatments.isEmpty() && !treatments.contains("All") && !treatments.contains(trt)) {
                continue;
            }
            
            // Get experiment for this row, or use selectedExperiment as fallback
            QString experiment = selectedExperiment;
            if (expColumn && row < expColumn->data.size()) {
                QString expFromData = expColumn->data[row].toString();
                if (!expFromData.isEmpty()) {
                    experiment = expFromData;
                }
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
            QString expTrtKey = runStr.isEmpty()
                ? QString("%1__%2__%3").arg(crop).arg(experiment).arg(trt)
                : QString("%1__%2__%3__%4").arg(crop).arg(experiment).arg(trt).arg(runStr);
            
            QVariant xVal = xColumn->data[row];
            QVariant yVal = yColumn->data[row];
            
            if (DataProcessor::isMissingValue(xVal) || DataProcessor::isMissingValue(yVal)) {
                continue;
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
            // Removed excessive POINT ADDED logging (was generating 333+ log entries per plot)
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
            // Always use getTreatmentDisplayName for consistency with observed data
            // This will add experiment name when multiple experiments exist and crop name when multiple crops exist
            plotData.treatmentName = getTreatmentDisplayName(treatment, experiment, crop);
            // Only append RUN if there are multiple runs under the same crop+experiment+treatment
            QString baseKey = QString("%1__%2__%3").arg(crop).arg(experiment).arg(treatment);
            if (!runPart.isEmpty() && baseKeyToRunCount.value(baseKey, 0) > 1) {
                plotData.treatmentName += QString(" (%1)").arg(runPart);
            }
            plotData.variable = yVar;
            plotData.points = it.value();
            // Use run number as primary treatment ID when present, otherwise use crop__experiment__treatment
            QString treatmentId = runPart.isEmpty()
                ? QString("%1__%2__%3").arg(crop).arg(experiment).arg(treatment)
                : runPart; // Use run number as primary ID when run is present
            plotData.color = getColorForTreatment(treatmentId, colorIndex);
            // Line style based on variable index, not treatment index
            plotData.lineStyleIndex = yVars.indexOf(yVar) % 4;
            // Marker based on variable index to ensure each variable gets a different marker
            plotData.symbolIndex = yVars.indexOf(yVar);
            colorIndex++;
            plotData.isObserved = false;
            
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
                if (!treatments.isEmpty() && !treatments.contains("All") && !treatments.contains(trt)) {
                    continue;
                }
                
                // Get experiment for this row, or use selectedExperiment as fallback
                QString experiment = selectedExperiment;
                if (expColumn && row < expColumn->data.size()) {
                    QString expFromData = expColumn->data[row].toString();
                    if (!expFromData.isEmpty()) {
                        experiment = expFromData;
                    }
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
                plotData.lineStyleIndex = yVars.indexOf(yVar) % 4;
                // Marker based on variable index to ensure each variable gets a different marker
                plotData.symbolIndex = yVars.indexOf(yVar);
                colorIndex++;
                plotData.isObserved = true;
                
                plotDataList.append(plotData);
            }
        }
    }
    
    qDebug() << "plotDatasets: Generated" << plotDataList.size() << "plot data items before adding to chart";
    
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
    if (symbol == "+") return QScatterSeries::MarkerShapeStar;             // 4 - Star
    if (symbol == "x") return QScatterSeries::MarkerShapePentagon;         // 5 - Pentagon
    if (symbol == "p") return QScatterSeries::MarkerShapePentagon;         // 5 - Pentagon
    if (symbol == "h") return QScatterSeries::MarkerShapeCircle;           // 0 - Circle (cycle)
    if (symbol == "star") return QScatterSeries::MarkerShapeStar;          // 4 - Star
    return QScatterSeries::MarkerShapeCircle; // Default
}

QString PlotWidget::getActualRenderedSymbol(const QString &originalSymbol) const
{
    // Return symbols that match the Qt Charts MarkerShape mapping
    if (originalSymbol == "o") return "o";      // Circle
    if (originalSymbol == "s") return "s";      // Rectangle
    if (originalSymbol == "d") return "d";      // RotatedRectangle (use diamond in legend)
    if (originalSymbol == "t") return "t";      // Triangle
    if (originalSymbol == "+") return "star";   // Star (use star symbol in legend)
    if (originalSymbol == "x") return "p";      // Pentagon
    if (originalSymbol == "p") return "p";      // Pentagon
    if (originalSymbol == "h") return "o";      // Circle (cycles back)
    if (originalSymbol == "star") return "star"; // Star
    
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
            scatterSeries->setMarkerSize(sharedPlotData->isObserved ? 8.0 : 6.0);
            
            // Apply symbol based on symbolIndex - use only unique visual shapes
            // Map to 6 unique shapes: circle, rectangle, diamond, triangle, star, pentagon
            // Use variable index directly to ensure each variable gets a different visual marker
            QStringList uniqueShapes = {"o", "s", "d", "t", "star", "p"}; // 6 unique visual shapes
            int shapeIndex = sharedPlotData->symbolIndex % uniqueShapes.size();
            QString originalSymbol = uniqueShapes[shapeIndex];
            QString actualSymbol = getActualRenderedSymbol(originalSymbol);
            scatterSeries->setMarkerShape(getMarkerShape(originalSymbol));

            // Apply pen for symbol outline
            QPen symbolPen(sharedPlotData->color, 2);
            if (sharedPlotData->symbolIndex % 2 == 0) { // Alternate pen style for symbols
                symbolPen.setStyle(Qt::SolidLine);
            } else {
                symbolPen.setStyle(Qt::NoPen);
            }
            scatterSeries->setPen(symbolPen);

            // Apply brush for symbol fill
            scatterSeries->setBrush(sharedPlotData->color);
            
            for (const QPointF &point : sharedPlotData->points) {
                scatterSeries->append(point);
            }
            
            series = scatterSeries;
            
            // Store pen and brush info
            sharedPlotData->pen = symbolPen;
            sharedPlotData->brush = QBrush(sharedPlotData->color);
            sharedPlotData->symbol = actualSymbol;  // Store the actual rendered symbol
            
        } else {
            // Use line series for simulated data
            QLineSeries *lineSeries = new QLineSeries();
            
            // Disable OpenGL to enable all line styles (QTBUG-59881)
            lineSeries->setUseOpenGL(false);
            // Include crop in name if it's not the default XX
            QString seriesName;
            if (sharedPlotData->crop != "XX") {
                // For same experiment, same treatment, different crops: use TNAME - CROP format
                QString cropName = getCropNameFromCode(sharedPlotData->crop);
                seriesName = QString("%1 - %2 (%3-Simulated)")
                           .arg(cropName)  // Use crop name instead of treatment name
                           .arg(sharedPlotData->variable)
                           .arg(sharedPlotData->crop);
            } else {
                seriesName = QString("%1 - %2 (Simulated)")
                           .arg(sharedPlotData->treatmentName)
                           .arg(sharedPlotData->variable);
            }
            lineSeries->setName(seriesName);
            
            lineSeries->setColor(sharedPlotData->color);

            // Apply line style based on lineStyleIndex
            QPen linePen(sharedPlotData->color, 2);
            switch (sharedPlotData->lineStyleIndex % 4) {
                case 0: linePen.setStyle(Qt::SolidLine); break;
                case 1: linePen.setStyle(Qt::DashLine); break;
                case 2: linePen.setStyle(Qt::DotLine); break;
                case 3: linePen.setStyle(Qt::DashDotLine); break;
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
        if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
            // Don't override label format - it's set in autoFitAxes based on variable type
            qDebug() << "addSeriesToPlot: ValueAxis range:" << valueAxis->min() << "to" << valueAxis->max();
        } else if (auto dateAxis = qobject_cast<QDateTimeAxis*>(axis)) {
            qDebug() << "addSeriesToPlot: DateTimeAxis range:" << dateAxis->min() << "to" << dateAxis->max();
        }
    }
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
        
        // Create key for matching: treatment_experiment_crop_date
        auto createMatchKey = [](const QString& trt, const QString& exp, const QString& crop, const QString& date) {
            return QString("%1_%2_%3_%4").arg(trt, exp, crop, date);
        };
        
        // Collect simulated data with match keys, split by RUN if present
        // Map: baseKey (trt_exp_crop_date) -> (runId -> sim value)
        QMap<QString, QMap<QString, double>> simDataByBaseKeyToRuns;
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
            
            QString trt = obsTrtColumn->data[row].toString();
            QString date = obsDateColumn->data[row].toString();
            QString exp = obsExpColumn && row < obsExpColumn->data.size() ? obsExpColumn->data[row].toString() : "";
            QString crop = obsCropColumn && row < obsCropColumn->data.size() ? obsCropColumn->data[row].toString() : "";
            QVariant obsVal = obsYColumn->data[row];
            
            if (!DataProcessor::isMissingValue(obsVal)) {
                obsRowsWithVal++;
                QString matchKey = createMatchKey(trt, exp, crop, date);
                
                // Only add if we have matching simulated data for the same key
                if (simDataByBaseKeyToRuns.contains(matchKey)) {
                    obsRowsMatched++;
                    const auto &runMap = simDataByBaseKeyToRuns[matchKey];
                    QString baseGroupKey = QString("%1_%2_%3_%4").arg(trt, yVar, exp, crop);
                    if (runMap.isEmpty()) {
                        // No run separation
                        QString groupKey = baseGroupKey;
                        simByTreatmentVarExpCrop[groupKey].append(0.0); // placeholder; will be overwritten next line
                        simByTreatmentVarExpCrop[groupKey].back() = 0.0; // ensure vector exists
                        obsByTreatmentVarExpCrop[groupKey].append(obsVal.toDouble());
                        treatmentVarExpCropToExp[groupKey] = exp;
                        treatmentVarExpCropToCrop[groupKey] = crop;
                        treatmentVarExpCropToTrt[groupKey] = trt;
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
                            treatmentVarExpCropToTrt[groupKey] = trt;
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
            
            // Check if this treatment should be processed
            if (!m_currentTreatments.contains("All") && !m_currentTreatments.contains(trt)) {
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
    
    // Clear error bars when chart is cleared
    if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
    }
    
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

void PlotWidget::exportPlot(const QString &filePath, const QString &format)
{
    if (!this) return;
    
    // Ensure everything is visible and updated
    this->show();
    this->update();
    // Force chart view to repaint so error bars (drawn in paintEvent) are in the buffer
    if (m_chartView) {
        m_chartView->update();
        m_chartView->repaint();
    }
    QApplication::processEvents();
    
    // Respect legend visibility setting
    if (m_legendScrollArea) {
        m_legendScrollArea->setVisible(m_showLegend);
        if (m_showLegend) {
            m_legendScrollArea->update();
        }
    }
    
    // Use render() instead of grab() so custom-painted content (error bars in ErrorBarChartView::paintEvent)
    // is included. grab() can miss custom paint on some platforms.
    QPixmap pixmap(this->size());
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    this->render(&painter);
    painter.end();
    
    qDebug() << "Quick export: Widget size" << this->size() << "pixmap size" << pixmap.size();
    
    pixmap.save(filePath, format.toUtf8().constData());
}

void PlotWidget::copyPlotToClipboard()
{
    if (!this) return;
    
    // Ensure everything is visible and updated
    this->show();
    this->update();
    QApplication::processEvents();
    
    // Respect legend visibility setting
    if (m_legendScrollArea) {
        m_legendScrollArea->setVisible(m_showLegend);
        if (m_showLegend) {
            m_legendScrollArea->update();
        }
    }
    
    // Grab the plot as a pixmap
    QPixmap pixmap = this->grab();
    
    // Copy to clipboard
    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setPixmap(pixmap);
        qDebug() << "Plot copied to clipboard. Size:" << pixmap.size();
    }
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
    
    // Render the main widget with all its children
    // This should include both the chart area and legend area
    this->render(&painter, QPoint(), this->rect(), QWidget::DrawChildren);
    
    painter.end();
    
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
    
    // Ensure everything is visible and updated; force chart view repaint so error bars are painted
    this->show();
    this->update();
    m_chartView->update();
    m_chartView->repaint();
    QApplication::processEvents();
    
    // Render chart to pixmap (QChartView::render() does not include our custom error bars from paintEvent)
    QPixmap chartPixmap(m_chartView->size());
    chartPixmap.fill(Qt::white);
    QPainter chartPainter(&chartPixmap);
    m_chartView->render(&chartPainter);
    chartPainter.end();

    // Draw error bars on top of the chart pixmap so composite export includes them
    chartPainter.begin(&chartPixmap);
    m_chartView->paintErrorBars(&chartPainter, m_chartView->viewport()->pos());
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
    
    // Save the result
    bool saved = finalPixmap.save(filePath, format.toUtf8().constData());
    qDebug() << "Simple composite export: Final size" << finalPixmap.size() << "- Success:" << saved;
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
                // Draw symbol for observed data
                QRect symbolRect(x, y - symbolSize/2, symbolSize, symbolSize);
                painter->drawEllipse(symbolRect);
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

void PlotWidget::clearLegend()
{
    // Simple clear (matching Python)
    while (QLayoutItem* item = m_legendLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void PlotWidget::setAxisLabels(const QString &xVar, const QStringList &yVars, const DataTable &data)
{
    QPair<QString, QString> xVarInfo = DataProcessor::getVariableInfo(xVar);
    QString xLabel = xVarInfo.first.isEmpty() ? xVar : xVarInfo.first;

    QStringList yLabels;
    for (const QString &yVar : yVars) {
        QPair<QString, QString> yVarInfo = DataProcessor::getVariableInfo(yVar);
        QString baseLabel = yVarInfo.first.isEmpty() ? yVar : yVarInfo.first;

        // Append scaling factor to label if scaled
        if (m_scaleFactors.contains("default") && m_scaleFactors["default"].contains(yVar)) {
            const auto& info = m_scaleFactors["default"][yVar];
            if (info.scaleFactor != 1.0) {
                baseLabel += QString(" (x%1)").arg(info.scaleFactor, 0, 'g', 3);
            }
        }
        yLabels.append(baseLabel);
    }
    QString yLabel = yLabels.join(", ");
    
    setAxisTitles(xLabel, yLabel);
}

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

    // Re-calculate scaling factors based on current Y-vars
    qDebug() << "PlotWidget::updatePlotWithScaling() - About to calculate scaling factors";
    m_scaleFactors = calculateScalingFactors(m_simData, m_obsData, m_currentYVars);
    qDebug() << "PlotWidget::updatePlotWithScaling() - Scaling factors calculated";

    // Create temporary copies for scaling
    DataTable scaledSimData = m_simData;
    DataTable scaledObsData = m_obsData;

    // Apply scaling
    qDebug() << "PlotWidget::updatePlotWithScaling() - BEFORE scaling, sample values:";
    for (const QString &var : m_currentYVars) {
        const DataColumn *col = scaledSimData.getColumn(var);
        if (col && !col->data.isEmpty()) {
            qDebug() << "  " << var << " first value BEFORE scaling:" << col->data[0];
        }
    }
    
    scaledSimData = applyScaling(scaledSimData, m_currentYVars);

    qDebug() << "PlotWidget::updatePlotWithScaling() - AFTER scaling, sample values:";
    for (const QString &var : m_currentYVars) {
        const DataColumn *col = scaledSimData.getColumn(var);
        if (col && !col->data.isEmpty()) {
            // Find first non-zero value
            QString firstNonZero = "all zero";
            for (int i = 0; i < qMin(10, col->data.size()); ++i) {
                double val = col->data[i].toDouble();
                if (qAbs(val) > 0.0001) {
                    firstNonZero = col->data[i].toString();
                    break;
                }
            }
            qDebug() << "  " << var << " first non-zero AFTER scaling:" << firstNonZero;
        }
    }

    if (scaledObsData.rowCount > 0) {
        scaledObsData = applyScaling(scaledObsData, m_currentYVars);
    }

    // Update the plot with scaled data
    qDebug() << "PlotWidget::updatePlotWithScaling() - About to plot datasets";
    qDebug() << "PlotWidget::updatePlotWithScaling() - Plotting with SCALED data";
    
    // Clear the chart first to ensure fresh plotting
    if (m_chart) {
        m_chart->removeAllSeries();
    }
    
    // Clear error bars when updating plot
    if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
    }
    
    plotDatasets(scaledSimData, scaledObsData, m_currentXVar, m_currentYVars, m_currentTreatments, m_selectedExperiment);
    qDebug() << "PlotWidget::updatePlotWithScaling() - Datasets plotted";

    // Update the scaling label
    qDebug() << "PlotWidget::updatePlotWithScaling() - About to update scaling label";
    updateScalingLabel(m_currentYVars);
    
    // Schedule auto-fit using timer (consolidates multiple auto-fit calls)
    if (m_autoFitTimer && !m_autoFitPending) {
        m_autoFitPending = true;
        m_autoFitTimer->start();
    }
    
    qDebug() << "PlotWidget::updatePlotWithScaling() - COMPLETED";
}

void PlotWidget::onPlotSettingsChanged()
{
    // Handle plot settings changes
}

void PlotWidget::onXAxisButtonClicked()
{
    // Handle X-axis button clicks
}

// ============================================================================
// COMPREHENSIVE LEGEND IMPLEMENTATION
// ============================================================================

void PlotWidget::updateLegendAdvanced(const QMap<QString, QMap<QString, QVector<QSharedPointer<PlotData>>>>& legendEntries)
{
    
    clearLegend();
    
    // Add legend title (matching Python)
    QLabel* legendTitle = new QLabel("<b>Legend</b>");
    legendTitle->setAlignment(Qt::AlignCenter);
    m_legendLayout->addWidget(legendTitle);
    
    // Create header row (matching Python)
    if (m_isScatterMode) {
        QWidget* headerWidget = new QWidget();
        QHBoxLayout* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 2, 0, 2);
        headerLayout->setSpacing(5);
        headerWidget->setLayout(headerLayout);
        
        QLabel* varHeader = new QLabel("<b>Variable</b>");
        varHeader->setAlignment(Qt::AlignLeft);
        headerLayout->addWidget(varHeader, 1);
        m_legendLayout->addWidget(headerWidget);
    } else {
        QWidget* headerWidget = new QWidget();
        QHBoxLayout* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 2, 0, 2);
        headerLayout->setSpacing(5);
        headerWidget->setLayout(headerLayout);
        
        QLabel* obsHeader = new QLabel("<b>Obs.</b>");
        obsHeader->setAlignment(Qt::AlignCenter);
        obsHeader->setFixedWidth(30);
        
        QLabel* simHeader = new QLabel("<b>Sim.</b>");
        simHeader->setAlignment(Qt::AlignCenter);
        simHeader->setFixedWidth(30);
        
        QLabel* trtHeader = new QLabel("<b>Treatment</b>");
        trtHeader->setAlignment(Qt::AlignLeft);
        
        headerLayout->addWidget(obsHeader);
        headerLayout->addWidget(simHeader);
        headerLayout->addWidget(trtHeader, 1);
        
        m_legendLayout->addWidget(headerWidget);
    }
    
    // Horizontal separator (matching Python)
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    m_legendLayout->addWidget(separator);
    
    // Organize data by variable first (matching Python)
    QSet<QString> variables;
    for (const QString& category : {"Simulated", "Observed"}) {
        if (legendEntries.contains(category)) {
            for (const QString& varName : legendEntries[category].keys()) {
                variables.insert(varName);
            }
        }
    }
    
    qDebug() << "updateLegendAdvanced: Found" << variables.size() << "variables:" << variables.values();
    
    // If no data, show a placeholder message
    if (variables.isEmpty()) {
        QLabel* noDataLabel = new QLabel("<i>No data to display in legend</i>");
        noDataLabel->setAlignment(Qt::AlignCenter);
        noDataLabel->setStyleSheet("color: #888888; padding: 10px;");
        m_legendLayout->addWidget(noDataLabel);
        return;
    }
    
    // Create entries organized by variable and treatment (matching Python)
    for (const QString& varName : variables) {
        QMap<QString, QMap<QString, QVariant>> varTreatments;
        
        // Process simulated data
        if (legendEntries.contains("Simulated") && legendEntries.value("Simulated").contains(varName)) {
            const auto& simVector = legendEntries.value("Simulated").value(varName);
            for (const QSharedPointer<PlotData>& plotData : simVector) {
                if (plotData.isNull()) {
                    continue;
                }
                QString uniqueKey;
                if (m_isScatterMode) {
                    // For scatter plots, use variable name as key (no treatment grouping)
                    uniqueKey = varName;
                } else {
                    // Use experiment_id + trt for truly unique identification (matching Python)
                    QString expId = plotData->experiment.isEmpty() ? "default" : plotData->experiment;
                    QString cropId = plotData->crop.isEmpty() ? "XX" : plotData->crop;
                    uniqueKey = QString("%1__TRT%2__EXP%3__CROP%4")
                                       .arg(plotData->treatmentName)
                                       .arg(plotData->treatment)
                                       .arg(expId)
                                       .arg(cropId);
                }
                
                
                if (!varTreatments.contains(uniqueKey)) {
                    QMap<QString, QVariant> treatmentData;
                    treatmentData["name"] = plotData->treatmentName;
                    treatmentData["trt_id"] = plotData->treatment;
                    treatmentData["experiment_id"] = plotData->experiment.isEmpty() ? "default" : plotData->experiment;
                    // Extract treatmentId (run number when present) by finding which key in m_treatmentColorMap matches this color
                    QString treatmentId;
                    for (auto it = m_treatmentColorMap.begin(); it != m_treatmentColorMap.end(); ++it) {
                        if (it.value() == plotData->color) {
                            treatmentId = it.key();
                            break;
                        }
                    }
                    treatmentData["treatment_id"] = treatmentId; // Store treatmentId for sorting
                    treatmentData["sim"] = QVariant::fromValue(plotData);
                    treatmentData["obs"] = QVariant();
                    varTreatments[uniqueKey] = treatmentData;
                } else {
                    varTreatments[uniqueKey]["sim"] = QVariant::fromValue(plotData);
                }
            }
        }
        
        // Process observed data
        if (legendEntries.contains("Observed") && legendEntries.value("Observed").contains(varName)) {
            const auto& obsVector = legendEntries.value("Observed").value(varName);
            for (const QSharedPointer<PlotData>& plotData : obsVector) {
                QString uniqueKey;
                if (m_isScatterMode) {
                    // For scatter plots, use variable name as key (no treatment grouping)
                    uniqueKey = varName;
                } else {
                    // Use experiment_id + trt for truly unique identification (matching Python)
                    QString expId = plotData->experiment.isEmpty() ? "default" : plotData->experiment;
                    QString cropId = plotData->crop.isEmpty() ? "XX" : plotData->crop;
                    uniqueKey = QString("%1__TRT%2__EXP%3__CROP%4")
                                       .arg(plotData->treatmentName)
                                       .arg(plotData->treatment)
                                       .arg(expId)
                                       .arg(cropId);
                }
                
                
                if (!varTreatments.contains(uniqueKey)) {
                    QMap<QString, QVariant> treatmentData;
                    treatmentData["name"] = plotData->treatmentName;
                    treatmentData["trt_id"] = plotData->treatment;
                    treatmentData["experiment_id"] = plotData->experiment.isEmpty() ? "default" : plotData->experiment;
                    // Extract treatmentId (run number when present) by finding which key in m_treatmentColorMap matches this color
                    QString treatmentId;
                    for (auto it = m_treatmentColorMap.begin(); it != m_treatmentColorMap.end(); ++it) {
                        if (it.value() == plotData->color) {
                            treatmentId = it.key();
                            break;
                        }
                    }
                    treatmentData["treatment_id"] = treatmentId; // Store treatmentId for sorting
                    treatmentData["sim"] = QVariant();
                    treatmentData["obs"] = QVariant::fromValue(plotData);
                    varTreatments[uniqueKey] = treatmentData;
                } else {
                    varTreatments[uniqueKey]["obs"] = QVariant::fromValue(plotData);
                    // Update treatment_id if not set (shouldn't happen, but just in case)
                    if (!varTreatments[uniqueKey].contains("treatment_id")) {
                        QString treatmentId;
                        for (auto it = m_treatmentColorMap.begin(); it != m_treatmentColorMap.end(); ++it) {
                            if (it.value() == plotData->color) {
                                treatmentId = it.key();
                                break;
                            }
                        }
                        varTreatments[uniqueKey]["treatment_id"] = treatmentId;
                    }
                }
            }
        }
        
        // Filter out treatments that have no actual data points for this variable
        for (auto it = varTreatments.begin(); it != varTreatments.end(); ) {
            bool hasSimData = false, hasObsData = false;
            
            // Check if sim data has points
            if (it.value().contains("sim") && it.value()["sim"].isValid()) {
                auto simData = it.value()["sim"].value<QSharedPointer<PlotData>>();
                hasSimData = simData && !simData->points.isEmpty();
            }
            
            // Check if obs data has points  
            if (it.value().contains("obs") && it.value()["obs"].isValid()) {
                auto obsData = it.value()["obs"].value<QSharedPointer<PlotData>>();
                hasObsData = obsData && !obsData->points.isEmpty();
            }
            
            // Remove if no actual data points for this variable
            if (!hasSimData && !hasObsData) {
                qDebug() << "updateLegendAdvanced: Removing treatment with no data points:" << it.key();
                it = varTreatments.erase(it);
            } else {
                ++it;
            }
        }
        
        // For scatter plots, extract base variable name to get full name (used in row, not header)
        QString displayName;
        if (m_isScatterMode) {
            // Extract base variable name (remove trailing 's' or 'm')
            QString baseVarName = varName;
            if (baseVarName.endsWith("s", Qt::CaseInsensitive) || baseVarName.endsWith("m", Qt::CaseInsensitive)) {
                baseVarName.chop(1); // Remove the last character ('s' or 'm')
            }
            
            // Try to get full name: first try base name (uppercase), then original variable name
            QPair<QString, QString> baseVarInfo = DataProcessor::getVariableInfo(baseVarName.toUpper());
            if (!baseVarInfo.first.isEmpty()) {
                displayName = baseVarInfo.first;
            } else {
                // Try original variable name (uppercase)
                QPair<QString, QString> origVarInfo = DataProcessor::getVariableInfo(varName.toUpper());
                if (!origVarInfo.first.isEmpty()) {
                    displayName = origVarInfo.first;
                } else {
                    // Fallback to base name if no info found
                    displayName = baseVarName;
                }
            }
        } else {
            // For non-scatter plots, use original logic
            QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(varName);
            displayName = varInfo.first.isEmpty() ? varName : varInfo.first;
        }
        
        // Variable header (skip for scatter plots since we show variable name in row)
        if (!m_isScatterMode) {
            QWidget* varWidget = new QWidget();
            QHBoxLayout* varLayout = new QHBoxLayout();
            varLayout->setContentsMargins(0, 5, 0, 2);
            varLayout->setSpacing(5);
            varWidget->setLayout(varLayout);
            
            varLayout->addSpacing(65); // Space for columns
            
            QLabel* varLabelWidget = new QLabel(QString("<b>%1</b>").arg(displayName));
            varLabelWidget->setAlignment(Qt::AlignLeft);
            varLayout->addWidget(varLabelWidget, 1);
            
            m_legendLayout->addWidget(varWidget);
        }
        
        // Create rows for each treatment (now properly unique) (matching Python)
        // For scatter plots, this will show the variable name with marker (no duplicate header)
        QStringList sortedKeys = varTreatments.keys();
        
        // Custom sort: sort by treatment ID (run number when present, numeric) otherwise alphabetically
        std::sort(sortedKeys.begin(), sortedKeys.end(), [&varTreatments](const QString& a, const QString& b) {
            QMap<QString, QVariant> dataA = varTreatments[a];
            QMap<QString, QVariant> dataB = varTreatments[b];
            
            QString treatmentIdA = dataA["treatment_id"].toString();
            QString treatmentIdB = dataB["treatment_id"].toString();
            
            // Extract run number from treatmentId (e.g., "RUN1" -> 1, "RUN2" -> 2)
            auto extractRunNumber = [](const QString& treatmentId) -> int {
                if (treatmentId.startsWith("RUN")) {
                    QString numStr = treatmentId.mid(3); // After "RUN"
                    bool ok;
                    int runNum = numStr.toInt(&ok);
                    if (ok) {
                        return runNum;
                    }
                }
                return -1; // Not a run number
            };
            
            int runA = extractRunNumber(treatmentIdA);
            int runB = extractRunNumber(treatmentIdB);
            
            // If both have run numbers, sort numerically
            if (runA >= 0 && runB >= 0) {
                return runA < runB;
            }
            // If only A has run number, A comes first
            if (runA >= 0 && runB < 0) {
                return true;
            }
            // If only B has run number, B comes first
            if (runA < 0 && runB >= 0) {
                return false;
            }
            // If neither has run number, sort alphabetically by treatment ID
            return treatmentIdA < treatmentIdB;
        });
        
        for (const QString& uniqueKey : sortedKeys) {
            createLegendRowFromData(varTreatments[uniqueKey], varName, displayName);
        }
        
        // Add separator after each variable except the last (matching Python)
        QStringList sortedVariables = variables.values();
        sortedVariables.sort();
        if (varName != sortedVariables.last()) {
            QFrame* separator = new QFrame();
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Plain);
            separator->setStyleSheet("color: #EEEEEE;");
            m_legendLayout->addWidget(separator);
        }
    }
}

void PlotWidget::createLegendRowFromData(const QMap<QString, QVariant>& treatmentData, const QString& varName, const QString& displayName)
{
    // Create a single legend row for a treatment (matching Python _create_legend_row)
    QWidget* rowWidget = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(5);
    rowWidget->setLayout(rowLayout);
    rowWidget->setCursor(Qt::PointingHandCursor);

    if (m_isScatterMode) {
        // Scatter legend: single column with marker and variable name (no treatment)
        QSharedPointer<PlotData> scatterData;
        if (treatmentData.contains("sim") && treatmentData["sim"].isValid()) {
            scatterData = treatmentData["sim"].value<QSharedPointer<PlotData>>();
        } else if (treatmentData.contains("obs") && treatmentData["obs"].isValid()) {
            scatterData = treatmentData["obs"].value<QSharedPointer<PlotData>>();
        }

        QWidget* symbolWidget = new QWidget();
        symbolWidget->setFixedWidth(30);
        QHBoxLayout* symbolLayout = new QHBoxLayout();
        symbolLayout->setContentsMargins(0, 0, 0, 0);
        symbolLayout->setAlignment(Qt::AlignCenter);
        symbolWidget->setLayout(symbolLayout);

        if (scatterData) {
            QString tooltip = QString("Variable: %1").arg(displayName);
            LegendSampleWidget* sample = new LegendSampleWidget(
                true,
                scatterData->pen,
                scatterData->symbol,
                scatterData->brush,
                tooltip
            );
            symbolLayout->addWidget(sample);
        }

        // Show variable name instead of treatment name
        QLabel* varLabel = new QLabel(displayName);
        varLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        rowLayout->addWidget(symbolWidget);
        rowLayout->addWidget(varLabel, 1);

        QVector<QAbstractSeries*> toggleItems;
        if (scatterData && scatterData->series) {
            toggleItems.append(scatterData->series);
        }

        rowWidget->setProperty("seriesToHighlight", QVariant::fromValue(toggleItems));
        rowWidget->setProperty("varName", varName);
        rowWidget->setProperty("trtId", ""); // No treatment ID for scatter plots
        rowWidget->installEventFilter(this);

        m_legendLayout->addWidget(rowWidget);
        return;
    }
    
    // Observed column
    QWidget* obsWidget = new QWidget();
    obsWidget->setFixedWidth(30);
    QHBoxLayout* obsLayout = new QHBoxLayout();
    obsLayout->setContentsMargins(0, 0, 0, 0);
    obsLayout->setAlignment(Qt::AlignCenter);
    obsWidget->setLayout(obsLayout);
    
    QSharedPointer<PlotData> obsData;
    if (treatmentData.contains("obs") && treatmentData["obs"].isValid()) {
        obsData = treatmentData["obs"].value<QSharedPointer<PlotData>>();
    }
    
    if (obsData) {
        QString obsTooltip = QString("Observed\nVariable: %1\nTreatment: %2")
                            .arg(displayName)
                            .arg(treatmentData["name"].toString());
        
        LegendSampleWidget* obsSample = new LegendSampleWidget(
            true,  // has symbol
            obsData->pen,
            obsData->symbol,
            obsData->brush,
            obsTooltip
        );
        obsLayout->addWidget(obsSample);
    } else {
        QLabel* placeholder = new QLabel("-");
        placeholder->setStyleSheet("color: #CCCCCC;");
        placeholder->setAlignment(Qt::AlignCenter);
        obsLayout->addWidget(placeholder);
    }
    
    // Simulated column
    QWidget* simWidget = new QWidget();
    simWidget->setFixedWidth(30);
    QHBoxLayout* simLayout = new QHBoxLayout();
    simLayout->setContentsMargins(0, 0, 0, 0);
    simLayout->setAlignment(Qt::AlignCenter);
    simWidget->setLayout(simLayout);
    
    QSharedPointer<PlotData> simData;
    if (treatmentData.contains("sim") && treatmentData["sim"].isValid()) {
        simData = treatmentData["sim"].value<QSharedPointer<PlotData>>();
    }
    
    if (simData) {
        QString simTooltip = QString("Simulated\nVariable: %1\nTreatment: %2")
                            .arg(displayName)
                            .arg(treatmentData["name"].toString());
        
        LegendSampleWidget* simSample = new LegendSampleWidget(
            false,  // no symbol (line)
            simData->pen,
            "",
            QBrush(),
            simTooltip
        );
        simLayout->addWidget(simSample);
    } else {
        QLabel* placeholder = new QLabel("-");
        placeholder->setStyleSheet("color: #CCCCCC;");
        placeholder->setAlignment(Qt::AlignCenter);
        simLayout->addWidget(placeholder);
    }
    
    // Treatment name column - regenerate name to ensure crop info is included
    QString currentCrop;
    QString baseTreatmentName;
    
    // Get crop and base treatment name from either sim or obs data
    QSharedPointer<PlotData> simPlotData;
    if (treatmentData.contains("sim") && treatmentData["sim"].isValid()) {
        simPlotData = treatmentData["sim"].value<QSharedPointer<PlotData>>();
        if (simPlotData) {
            currentCrop = simPlotData->crop;
            baseTreatmentName = treatmentData["name"].toString();
        }
    }
    if (currentCrop.isEmpty()) {
        QSharedPointer<PlotData> obsData;
        if (treatmentData.contains("obs") && treatmentData["obs"].isValid()) {
            obsData = treatmentData["obs"].value<QSharedPointer<PlotData>>();
            if (obsData) {
                currentCrop = obsData->crop;
                baseTreatmentName = treatmentData["name"].toString();
            }
        }
    }
    
    // Check if this same treatment/experiment combination exists with different crops in current plot data
    QString currentTrtId = treatmentData["trt_id"].toString();
    QString currentExpId = treatmentData["experiment_id"].toString();
    bool needsCropDisplay = false;
    
    if (!currentCrop.isEmpty() && currentCrop != "XX") {
        for (const QSharedPointer<PlotData> &plotData : m_plotDataList) {
            if (plotData && 
                plotData->treatment == currentTrtId && 
                plotData->experiment == currentExpId &&
                !plotData->crop.isEmpty() && 
                plotData->crop != "XX" && 
                plotData->crop != currentCrop) {
                needsCropDisplay = true;
                break;
            }
        }
    }
    
    QString legendDisplayName = baseTreatmentName;
    if (needsCropDisplay) {
        QString cropName = getCropNameFromCode(currentCrop);
        legendDisplayName = QString("%1 (%2)").arg(baseTreatmentName).arg(cropName);
    }
    
    QLabel* trtLabel = new QLabel(legendDisplayName);
    trtLabel->setAlignment(Qt::AlignLeft);
    trtLabel->setToolTip(QString("Treatment: %1\nVariable: %2")
                        .arg(legendDisplayName)
                        .arg(displayName));
    
    rowLayout->addWidget(obsWidget);
    rowLayout->addWidget(simWidget);
    rowLayout->addWidget(trtLabel, 1);
    
    // Store plot items for this row (matching Python)
    QVector<QAbstractSeries*> toggleItems;
    if (obsData && obsData->series) {
        toggleItems.append(obsData->series);
    }
    if (simData && simData->series) {
        toggleItems.append(simData->series);
    }
    
    // Store data for click handling
    rowWidget->setProperty("seriesToHighlight", QVariant::fromValue(toggleItems));
    rowWidget->setProperty("varName", varName);
    rowWidget->setProperty("trtId", treatmentData["trt_id"].toString());
    
    // Install event filter for click handling
    rowWidget->installEventFilter(this);
    
    m_legendLayout->addWidget(rowWidget);
}

QString PlotWidget::getCropNameFromCode(const QString& cropCode) const
{
    if (cropCode.isEmpty() || cropCode == "XX") {
        return cropCode; // Return as-is for default/empty codes
    }
    
    // Use DataProcessor to get crop details from DETAIL.CDE file
    if (m_dataProcessor) {
        QVector<CropDetails> cropDetails = m_dataProcessor->getCropDetails();
        for (const CropDetails& crop : cropDetails) {
            if (crop.cropCode.toUpper() == cropCode.toUpper()) {
                return crop.cropName;
            }
        }
    }
    
    // Return the code itself if no mapping found in CDE file
    return cropCode;
}

void PlotWidget::createSimpleLegendRow(const LegendTreatmentData& treatmentData, const QString& varName, const QString& displayName)
{
    // Legacy function - convert to new format
    QMap<QString, QVariant> data;
    data["name"] = treatmentData.name;
    data["trt_id"] = treatmentData.trtId;
    data["experiment_id"] = treatmentData.experimentId;
    if (treatmentData.simData) {
        data["sim"] = QVariant::fromValue(treatmentData.simData);
    }
    if (treatmentData.obsData) {
        data["obs"] = QVariant::fromValue(treatmentData.obsData);
    }
    createLegendRowFromData(data, varName, displayName);
}

void PlotWidget::createLegendHeader()
{
    // Legend title
    QLabel* legendTitle = new QLabel("<b>Legend</b>");
    legendTitle->setAlignment(Qt::AlignCenter);
    m_legendLayout->addWidget(legendTitle);
    
    // Header row
    QWidget* headerWidget = new QWidget();
    QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 2, 0, 2);
    headerLayout->setSpacing(5);
    
    QLabel* obsHeader = new QLabel("<b>Obs.</b>");
    obsHeader->setAlignment(Qt::AlignCenter);
    obsHeader->setFixedWidth(30);
    
    QLabel* simHeader = new QLabel("<b>Sim.</b>");
    simHeader->setAlignment(Qt::AlignCenter);
    simHeader->setFixedWidth(30);
    
    QLabel* trtHeader = new QLabel("<b>Treatment</b>");
    trtHeader->setAlignment(Qt::AlignLeft);
    
    headerLayout->addWidget(obsHeader);
    headerLayout->addWidget(simHeader);
    headerLayout->addWidget(trtHeader, 1);
    
    m_legendLayout->addWidget(headerWidget);
    
    // Separator
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    m_legendLayout->addWidget(separator);
}

void PlotWidget::createVariableSection(const QString& varName, const QString& displayName)
{
    QWidget* varWidget = new QWidget();
    QHBoxLayout* varLayout = new QHBoxLayout(varWidget);
    varLayout->setContentsMargins(0, 5, 0, 2);
    varLayout->setSpacing(5);
    
    varLayout->addSpacing(65);  // Space for columns
    
    // Use DataProcessor::getVariableInfo to get the label
    QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(varName);
    QString displayLabel = varInfo.first.isEmpty() ? varName : varInfo.first;

    QLabel* varLabel = new QLabel(QString("<b>%1</b>").arg(displayLabel));
    varLabel->setAlignment(Qt::AlignLeft);
    varLayout->addWidget(varLabel, 1);
    
    m_legendLayout->addWidget(varWidget);
}

void PlotWidget::createLegendRow(const LegendTreatmentData& treatmentData, const QString& varName, const QString& displayName)
{
    // Use simple legend row creation instead
    createSimpleLegendRow(treatmentData, varName, displayName);
}

void PlotWidget::resetAllHighlightedItems()
{
    // Reset all legend rows (matching Python _reset_all_highlighted_items)
    for (int i = 0; i < m_legendLayout->count(); ++i) {
        QLayoutItem* item = m_legendLayout->itemAt(i);
        if (item && item->widget()) {
            QWidget* widget = item->widget();
            if (widget->property("highlighted").isValid()) {
                widget->setProperty("highlighted", false);
                widget->setStyleSheet("");
            }
        }
    }
    
    // Reset all plot items (matching Python logic)
    for (int i = 0; i < m_chart->series().count(); ++i) {
        QAbstractSeries* series = m_chart->series().at(i);
        
        if (QLineSeries* lineSeries = qobject_cast<QLineSeries*>(series)) {
            if (lineSeries->property("originalPen").isValid()) {
                QPen originalPen = lineSeries->property("originalPen").value<QPen>();
                lineSeries->setPen(originalPen);
            }
        } else if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            if (scatterSeries->property("originalSize").isValid()) {
                qreal originalSize = scatterSeries->property("originalSize").toReal();
                scatterSeries->setMarkerSize(originalSize);
            }
            if (scatterSeries->property("originalBrush").isValid()) {
                QBrush originalBrush = scatterSeries->property("originalBrush").value<QBrush>();
                scatterSeries->setBrush(originalBrush);
            }
        }
    }
}

void PlotWidget::highlightPlotItems(const QVector<QAbstractSeries*>& seriesToHighlight)
{
    for (QAbstractSeries* series : m_chart->series()) {
        QPen pen;
        QColor color;
        if (QLineSeries* lineSeries = qobject_cast<QLineSeries*>(series)) {
            pen = lineSeries->pen();
            color = pen.color();
        } else if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            pen = scatterSeries->pen();
            color = pen.color();
        } else {
            // Handle other series types if necessary, or provide a default
            continue;
        }
        
        if (seriesToHighlight.contains(series)) {
            // Highlight: make opaque and thicker
            color.setAlphaF(1.0); // Fully opaque
            pen.setColor(color);
            pen.setWidth(3); // Thicker line
        } else {
            // Dim: make semi-transparent
            color.setAlphaF(0.2); // Semi-transparent
            pen.setColor(color);
            pen.setWidth(2); // Normal thickness
        }
        if (QLineSeries* lineSeries = qobject_cast<QLineSeries*>(series)) {
            lineSeries->setPen(pen);
        } else if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            scatterSeries->setPen(pen);
        }
        
        // For scatter series, also adjust brush
        if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            QBrush brush = scatterSeries->brush();
            color = brush.color();
            if (seriesToHighlight.contains(series)) {
                color.setAlphaF(1.0);
            } else {
                color.setAlphaF(0.2);
            }
            brush.setColor(color);
            scatterSeries->setBrush(brush);
        }
    }
}

void PlotWidget::resetPlotItemHighlights()
{
    for (QAbstractSeries* series : m_chart->series()) {
        QPen pen;
        QColor color;
        if (QLineSeries* lineSeries = qobject_cast<QLineSeries*>(series)) {
            pen = lineSeries->pen();
            color = pen.color();
        } else if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            pen = scatterSeries->pen();
            color = pen.color();
        } else {
            // Handle other series types if necessary, or provide a default
            continue;
        }
        
        // Reset to full opacity and default width
        color.setAlphaF(1.0);
        pen.setColor(color);
        pen.setWidth(2);
        if (QLineSeries* lineSeries = qobject_cast<QLineSeries*>(series)) {
            lineSeries->setPen(pen);
        } else if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            scatterSeries->setPen(pen);
        }
        
        if (QScatterSeries* scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            QBrush brush = scatterSeries->brush();
            color = brush.color();
            color.setAlphaF(1.0);
            brush.setColor(color);
            scatterSeries->setBrush(brush);
        }
    }
}

// Simplified legend implementation (matching Python)

// ============================================================================
// LEGEND SAMPLE WIDGET IMPLEMENTATION
// ============================================================================

LegendSampleWidget::LegendSampleWidget(bool hasSymbol, const QPen& pen, const QString& symbol, 
                                       const QBrush& brush, const QString& tooltip, QWidget* parent)
    : QWidget(parent)
    , m_hasSymbol(hasSymbol)
    , m_pen(pen)
    , m_symbol(symbol)
    , m_brush(brush)
{
    setFixedSize(20, 15);
    setToolTip(tooltip);
}

void LegendSampleWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    if (m_hasSymbol && !m_symbol.isEmpty()) {
        // Draw symbol (scatter point)
        painter.setPen(m_pen);
        painter.setBrush(m_brush);
        
        QPointF center(width() / 2.0, height() / 2.0);
        int symbolSize = 7;
        
        // Draw different marker shapes to match Qt Charts exactly
        if (m_symbol == "o") {
            // Circle - matches QScatterSeries::MarkerShapeCircle
            painter.drawEllipse(center, symbolSize/2, symbolSize/2);
        } else if (m_symbol == "s") {
            // Square mapped to Rectangle in Qt Charts - draw rectangle
            QRectF rect(center.x() - symbolSize/2, center.y() - symbolSize/2, symbolSize, symbolSize);
            painter.drawRect(rect);
        } else if (m_symbol == "t") {
            // Triangle - matches QScatterSeries::MarkerShapeTriangle
            QPolygonF triangle;
            triangle << QPointF(center.x(), center.y() - symbolSize/2)
                     << QPointF(center.x() + symbolSize/2, center.y() + symbolSize/2)
                     << QPointF(center.x() - symbolSize/2, center.y() + symbolSize/2);
            painter.drawPolygon(triangle);
        } else if (m_symbol == "d") {
            // Diamond - matches QScatterSeries::MarkerShapeRotatedRectangle
            QPolygonF diamond;
            diamond << QPointF(center.x(), center.y() - symbolSize/2)
                    << QPointF(center.x() + symbolSize/2, center.y())
                    << QPointF(center.x(), center.y() + symbolSize/2)
                    << QPointF(center.x() - symbolSize/2, center.y());
            painter.drawPolygon(diamond);
        } else if (m_symbol == "p") {
            // Pentagon - matches QScatterSeries::MarkerShapePentagon
            QPolygonF pentagon;
            for (int i = 0; i < 5; ++i) {
                double angle = 2 * M_PI * i / 5 - M_PI / 2; // Start from top
                pentagon << QPointF(center.x() + symbolSize/2 * cos(angle), 
                                  center.y() + symbolSize/2 * sin(angle));
            }
            painter.drawPolygon(pentagon);
        } else if (m_symbol == "star") {
            // Star - matches QScatterSeries::MarkerShapeStar
            QPolygonF star;
            for (int i = 0; i < 10; ++i) {
                double angle = 2 * M_PI * i / 10 - M_PI / 2; // Start from top
                double radius = (i % 2 == 0) ? symbolSize/2 : symbolSize/4;
                star << QPointF(center.x() + radius * cos(angle), 
                              center.y() + radius * sin(angle));
            }
            painter.drawPolygon(star);
        } else {
            // Default circle for unknown symbols
            painter.drawEllipse(center, symbolSize/2, symbolSize/2);
        }
    } else {
        // Draw line
        if (m_pen.style() != Qt::NoPen) {
            painter.setPen(m_pen);
            int y = height() / 2;
            painter.drawLine(0, y, width(), y);
        }
    }
}

// Simple legend implementation (no complex row widgets)

// Removed complex LegendRowWidget - using simple widgets instead

// Removed all complex LegendRowWidget methods - using simple approach

// Simple legend functionality (matching Python)

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
    // Handle chart view events for zoom and pan
    if (obj == m_chartView && m_chartView) {
        if (event->type() == QEvent::Wheel) {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            const double scaleFactor = 1.15;
            
            if (wheelEvent->angleDelta().y() > 0) {
                // Zoom in
                m_chartView->chart()->zoom(scaleFactor);
            } else {
                // Zoom out
                m_chartView->chart()->zoom(1.0 / scaleFactor);
            }
            return true;
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::MiddleButton) {
                // Reset zoom on middle click
                m_chartView->chart()->zoomReset();
                return true;
            }
            else if (mouseEvent->button() == Qt::RightButton) {
                // Switch to pan mode for right mouse button
                m_chartView->setDragMode(QGraphicsView::ScrollHandDrag);
                return false; // Let the chart view handle the drag
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                // Switch back to zoom mode when right mouse button is released
                m_chartView->setDragMode(QGraphicsView::RubberBandDrag);
                return false;
            }
        }
    }
    
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget* widget = qobject_cast<QWidget*>(obj);
        if (widget && widget->property("seriesToHighlight").isValid()) {
            // Create click handler (matching Python _create_toggle_handler)
            createToggleHandler(widget);
            return true;
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
    
    // Final fallback
    if (tname.isEmpty()) {
        tname = QString("Treatment %1").arg(trtId);
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

void PlotWidget::onSettingsButtonClicked()
{
    qDebug() << "PlotWidget: Settings button clicked";
    
    PlotSettingsDialog dialog(m_plotSettings, this, this);
    if (dialog.exec() == QDialog::Accepted) {
        PlotSettings newSettings = dialog.getSettings();
        applyPlotSettings(newSettings);
        m_plotSettings = newSettings;
    }
}

void PlotWidget::applyPlotSettings(const PlotSettings &settings)
{
    qDebug() << "PlotWidget: Applying plot settings";
    
    // Apply grid settings
    setShowGrid(settings.showGrid);
    
    // Apply legend settings
    setShowLegend(settings.showLegend);
    
    // Apply axis settings - always apply, using current defaults if empty
    QString xTitle = settings.xAxisTitle.isEmpty() ? m_currentXVar : settings.xAxisTitle;
    
    // Create default Y title from current Y variables
    QString defaultYTitle = "Y Variable";
    if (!m_currentYVars.isEmpty()) {
        QStringList yLabels;
        for (const QString &yVar : m_currentYVars) {
            QPair<QString, QString> yVarInfo = DataProcessor::getVariableInfo(yVar);
            QString baseLabel = yVarInfo.first.isEmpty() ? yVar : yVarInfo.first;
            // Include scaling factor if available
            if (m_scaleFactors.contains("default") && m_scaleFactors["default"].contains(yVar)) {
                const auto& info = m_scaleFactors["default"][yVar];
                if (info.scaleFactor != 1.0) {
                    baseLabel += QString(" (x%1)").arg(info.scaleFactor, 0, 'g', 3);
                }
            }
            yLabels.append(baseLabel);
        }
        defaultYTitle = yLabels.join(", ");
    }
    
    QString yTitle = settings.yAxisTitle.isEmpty() ? defaultYTitle : settings.yAxisTitle;
    setAxisTitles(xTitle, yTitle);
    
    // Apply plot title - always apply, can be empty to clear title
    setPlotTitle(settings.plotTitle);
    
    // Apply background colors
    if (m_chart) {
        m_chart->setBackgroundBrush(QBrush(settings.backgroundColor));
        m_chart->setPlotAreaBackgroundBrush(QBrush(settings.plotAreaColor));

        // Apply title font
        QFont titleFont(settings.fontFamily, settings.titleFontSize);
        titleFont.setBold(settings.boldTitle);
        m_chart->setTitleFont(titleFont);
    }
    
    // Apply axis settings to all axes
    auto axes = m_chart->axes();
    for (auto axis : axes) {
        // Apply axis tick label font
        QFont tickFont(settings.fontFamily, settings.axisTickFontSize);
        axis->setLabelsFont(tickFont);

        // Apply axis title font
        QFont labelFont(settings.fontFamily, settings.axisLabelFontSize);
        labelFont.setBold(settings.boldAxisLabels);
        axis->setTitleFont(labelFont);

        if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
            valueAxis->setMinorTickCount(settings.minorTickCount);
            valueAxis->setMinorGridLineVisible(settings.showMinorGrid);
            valueAxis->setLabelsVisible(settings.showAxisLabels);

            // Apply X-axis tick customization only to X-axis
            if (axis == m_chart->axes(Qt::Horizontal).first()) {
                if (settings.xAxisTickCount > 0) {
                    valueAxis->setTickCount(settings.xAxisTickCount);
                }

                // If custom spacing is set (> 0), calculate and set tick interval
                if (settings.xAxisTickSpacing > 0.0) {
                    valueAxis->setTickInterval(settings.xAxisTickSpacing);
                }
            }
        }
        else if (auto dateTimeAxis = qobject_cast<QDateTimeAxis*>(axis)) {
            dateTimeAxis->setLabelsVisible(settings.showAxisLabels);

            // Apply X-axis tick customization only to X-axis (for DATE variables)
            if (axis == m_chart->axes(Qt::Horizontal).first()) {
                if (settings.xAxisTickCount > 0) {
                    dateTimeAxis->setTickCount(settings.xAxisTickCount);
                }

                // Note: QDateTimeAxis doesn't support setTickInterval
                // Tick spacing is controlled through setTickCount only
            }
        }
    }
    
    // Update line and marker settings for existing series
    auto seriesList = m_chart->series();
    for (auto series : seriesList) {
        if (auto lineSeries = qobject_cast<QLineSeries*>(series)) {
            QPen pen = lineSeries->pen();
            pen.setWidth(settings.lineWidth);
            lineSeries->setPen(pen);
        }
        
        if (auto scatterSeries = qobject_cast<QScatterSeries*>(series)) {
            scatterSeries->setMarkerSize(settings.markerSize);
            
            // Update pen width for scatter series too
            QPen pen = scatterSeries->pen();
            pen.setWidth(settings.lineWidth);
            scatterSeries->setPen(pen);
        }
    }
    
    // Apply legend font (to custom legend widget, not chart legend)
    if (m_legendWidget) {
        QFont legendFont(settings.fontFamily, settings.legendFontSize);
        m_legendWidget->setFont(legendFont);

        // Apply font to all existing labels in the legend
        QList<QLabel*> labels = m_legendWidget->findChildren<QLabel*>();
        for (QLabel* label : labels) {
            label->setFont(legendFont);
        }
    }

    // Update internal settings
    m_showGrid = settings.showGrid;
    m_showLegend = settings.showLegend;

    // Update plot settings (including error bar settings)
    m_plotSettings = settings;
    
    // If error bar settings changed, replot to apply changes
    bool errorBarChanged = (m_plotSettings.showErrorBars != settings.showErrorBars) ||
                          (m_plotSettings.errorBarType != settings.errorBarType);
    
    if (errorBarChanged && m_simData.rowCount > 0 && !m_currentYVars.isEmpty()) {
        qDebug() << "PlotWidget: Error bar settings changed, replotting...";
        updatePlotWithScaling();
    }
    
    qDebug() << "PlotWidget: Plot settings applied successfully";
}

void PlotWidget::setXAxisVariable(const QString &xVar)
{
    qDebug() << "PlotWidget: Setting X-axis variable to" << xVar;
    
    // Update button states
    m_dasButton->setChecked(xVar == "DAS");
    m_dapButton->setChecked(xVar == "DAP");
    m_dateButton->setChecked(xVar == "DATE");
    
    // Store current X variable
    m_currentXVar = xVar;
    
    // Emit signal to notify MainWindow
    emit xVariableChanged(xVar);
    
    // Re-plot with new X variable if we have data
    if (m_simData.rowCount > 0 && !m_currentYVars.isEmpty()) {
        qDebug() << "PlotWidget: Re-plotting with new X variable:" << xVar;
        updatePlotWithScaling();
    } else {
        qDebug() << "PlotWidget: No data to re-plot with new X variable";
    }
}

void PlotWidget::plotScatter(
    const DataTable &evaluateData,
    const QString &xVar,
    const QString &yVar,
    const QStringList &selectedTreatments,
    const QMap<QString, QMap<QString, QString>> &treatmentNames)
{
        qDebug() << "PlotWidget::plotScatter() - ENTRY";
        m_isScatterMode = true;
        
        // Hide DAS, DAP, DATE buttons for scatter plots (not applicable)
        setXAxisButtonsVisible(false);
        qDebug() << "  X Variable:" << xVar;
        qDebug() << "  Y Variable:" << yVar;
        qDebug() << "  Treatments:" << selectedTreatments;
        qDebug() << "  Data row count:" << evaluateData.rowCount;
        qDebug() << "  Available columns:" << evaluateData.columnNames;
        
        try {
            clear();
            // Re-set scatter mode after clear() (which resets it to false)
            m_isScatterMode = true;

            // Reset chart margins for scatter plots
            if (m_chart) {
                m_chart->setMargins(QMargins(0, 0, 0, 0));  // Use default margins
            }

            if (evaluateData.rowCount == 0) {
                qWarning() << "PlotWidget: No data available for scatter plot";
                return;
            }
            
            const DataColumn *xCol = evaluateData.getColumn(xVar);
            const DataColumn *yCol = evaluateData.getColumn(yVar);
            
            qDebug() << "PlotWidget::plotScatter() - X column found:" << (xCol != nullptr);
            qDebug() << "PlotWidget::plotScatter() - Y column found:" << (yCol != nullptr);
            
            if (!xCol || !yCol) {
                qWarning() << "PlotWidget: X or Y column not found";
                qWarning() << "  X column name:" << xVar << "exists:" << evaluateData.columnNames.contains(xVar);
                qWarning() << "  Y column name:" << yVar << "exists:" << evaluateData.columnNames.contains(yVar);
                return;
            }
        
        // Get experiment code column (EXCODE in EVALUATE.OUT) for metrics
        const DataColumn *excodeCol = evaluateData.getColumn("EXCODE");
        const DataColumn *crCol = evaluateData.getColumn("CR");  // Crop code
        
        // Collect all data points (not grouped by treatment) and by experiment (for metrics)
        QVector<QPointF> allPoints;
        QMap<QString, QVector<QPointF>> experimentPoints;
        QMap<QString, QString> experimentCropCodes;
        
        int validPointCount = 0;
        for (int i = 0; i < evaluateData.rowCount; ++i) {
            QVariant xVal = xCol->data.value(i);
            QVariant yVal = yCol->data.value(i);
            
            // Skip missing values
            if (DataProcessor::isMissingValue(xVal) || DataProcessor::isMissingValue(yVal)) {
                continue;
            }
            
            bool okX, okY;
            double x = DataProcessor::toDouble(xVal, &okX);
            double y = DataProcessor::toDouble(yVal, &okY);
            
            if (!okX || !okY) {
                qDebug() << "PlotWidget::plotScatter() - Row" << i << "failed conversion: x=" << xVal << "y=" << yVal;
                continue;
            }
            
            validPointCount++;
            
            // Get experiment code and crop for metrics
            QString expCode = "";
            QString cropCode = "";
            if (excodeCol && i < excodeCol->data.size()) {
                QVariant excodeVal = excodeCol->data.value(i);
                if (!DataProcessor::isMissingValue(excodeVal)) {
                    expCode = excodeVal.toString();
                }
            }
            if (crCol && i < crCol->data.size()) {
                QVariant crVal = crCol->data.value(i);
                if (!DataProcessor::isMissingValue(crVal)) {
                    cropCode = crVal.toString();
                }
            }
            
            // Create experiment key (use experiment code when available)
            QString experimentKey = expCode.isEmpty() ? QString("Experiment") : expCode;

            // Add point to all points (no treatment filtering)
            allPoints.append(QPointF(x, y));

            experimentPoints[experimentKey].append(QPointF(x, y));
            if (!cropCode.isEmpty()) {
                experimentCropCodes[experimentKey] = cropCode;
            }
        }
        
        qDebug() << "PlotWidget::plotScatter() - Valid points found:" << validPointCount;
        
        if (allPoints.isEmpty()) {
            qWarning() << "PlotWidget: No valid data points for scatter plot";
            qWarning() << "  Total rows processed:" << evaluateData.rowCount;
            qWarning() << "  Valid points found:" << validPointCount;
            return;
        }
        
        // Chart is already cleared by clear() call at the beginning of this function
        // Just ensure plotDataList is cleared
        m_plotDataList.clear();
        
        // Create a single scatter series for all points with one shape marker
        // Extract base variable name (remove trailing 's' or 'm') to get full name
        QString baseVarName = yVar;
        if (baseVarName.endsWith("s", Qt::CaseInsensitive) || baseVarName.endsWith("m", Qt::CaseInsensitive)) {
            baseVarName.chop(1); // Remove the last character ('s' or 'm')
        }
        
        // Try to get full name: first try base name (uppercase), then original variable name
        QPair<QString, QString> baseVarInfo = DataProcessor::getVariableInfo(baseVarName.toUpper());
        QString yTitle;
        if (!baseVarInfo.first.isEmpty()) {
            yTitle = baseVarInfo.first;
        } else {
            // Try original variable name (uppercase)
            QPair<QString, QString> origVarInfo = DataProcessor::getVariableInfo(yVar.toUpper());
            if (!origVarInfo.first.isEmpty()) {
                yTitle = origVarInfo.first;
            } else {
                // Fallback to base name if no info found
                yTitle = baseVarName;
            }
        }
        
        // Use a default color (blue) for all points
        QColor pointColor = QColor("#1f77b4"); // Default blue color
        
        QScatterSeries *scatterSeries = new QScatterSeries();
        scatterSeries->setUseOpenGL(false);
        scatterSeries->setName(yTitle); // Use base variable full name instead of treatment
        scatterSeries->setColor(pointColor);
        scatterSeries->setMarkerSize(8.0);
        scatterSeries->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        scatterSeries->setPen(QPen(pointColor, 2));
        scatterSeries->setBrush(QBrush(pointColor));
        
        for (const QPointF &point : allPoints) {
            scatterSeries->append(point);
        }
        
        m_chart->addSeries(scatterSeries);
        
        // Store plot data for legend (variable-based, not treatment-based)
        QSharedPointer<PlotData> plotData = QSharedPointer<PlotData>::create();
        plotData->treatment = ""; // No treatment grouping
        plotData->treatmentName = yTitle; // Use variable name
        plotData->variable = yVar;
        plotData->points = allPoints;
        plotData->color = pointColor;
        plotData->isObserved = false;
        plotData->series = scatterSeries;
        plotData->pen = QPen(pointColor, 2);
        plotData->brush = QBrush(pointColor);
        plotData->symbol = "o";
        m_plotDataList.append(plotData);
        
        // Add 1:1 reference line
        QLineSeries *referenceLine = new QLineSeries();
        referenceLine->setName("1:1 Line");
        referenceLine->setColor(Qt::black);
        QPen refPen(Qt::black, 2, Qt::DashLine);
        referenceLine->setPen(refPen);
        
        // Find min and max values for reference line
        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::lowest();
        
        for (const QPointF &point : allPoints) {
            minVal = qMin(minVal, qMin(point.x(), point.y()));
            maxVal = qMax(maxVal, qMax(point.x(), point.y()));
        }
        
        // Handle case where all values are the same (or very close)
        double range = maxVal - minVal;
        double padding;
        if (range < 1e-10) {
            // All points are at the same location - create a visible range around the value
            double center = minVal; // minVal == maxVal in this case
            if (qAbs(center) < 1e-10) {
                // Value is near zero, use a small range around zero
                minVal = -1.0;
                maxVal = 1.0;
            } else {
                // Use 5% of the absolute value as padding (smaller padding for better visibility)
                padding = qAbs(center) * 0.05;
                if (padding < 1.0) {
                    padding = 1.0; // Minimum 1 unit padding
                }
                minVal = center - padding;
                maxVal = center + padding;
            }
        } else {
            // Normal case - add padding
            padding = range * 0.1;
            minVal -= padding;
            maxVal += padding;
        }
        
        referenceLine->append(minVal, minVal);
        referenceLine->append(maxVal, maxVal);
        m_chart->addSeries(referenceLine);
        
        qDebug() << "PlotWidget::plotScatter() - Axis range: min=" << minVal << "max=" << maxVal;
        
        // Setup axes
        QValueAxis *xAxis = new QValueAxis();
        QValueAxis *yAxis = new QValueAxis();
        
        // Extract base variable name for X axis (remove trailing 'm')
        QString baseXVarName = xVar;
        if (baseXVarName.endsWith("m", Qt::CaseInsensitive)) {
            baseXVarName.chop(1); // Remove the last character ('m')
        }
        
        // Try to get full name: first try base name (uppercase), then original variable name
        QPair<QString, QString> baseXVarInfo = DataProcessor::getVariableInfo(baseXVarName.toUpper());
        QString xTitle;
        if (!baseXVarInfo.first.isEmpty()) {
            xTitle = baseXVarInfo.first;
        } else {
            // Try original variable name (uppercase)
            QPair<QString, QString> origXVarInfo = DataProcessor::getVariableInfo(xVar.toUpper());
            if (!origXVarInfo.first.isEmpty()) {
                xTitle = origXVarInfo.first;
            } else {
                // Fallback to base name if no info found
                xTitle = baseXVarName;
            }
        }
        // yTitle already declared above (using base variable name)
        
        // Add "(measured)" to X-axis and "(simulated)" to Y-axis for scatter plots
        xAxis->setTitleText(xTitle + " (measured)");
        yAxis->setTitleText(yTitle + " (simulated)");
        
        // Set axis ranges
        xAxis->setRange(minVal, maxVal);
        yAxis->setRange(minVal, maxVal);
        
        // Add axes to chart (using non-deprecated method)
        m_chart->addAxis(xAxis, Qt::AlignBottom);
        m_chart->addAxis(yAxis, Qt::AlignLeft);
        
        // Attach series to axes
        for (QAbstractSeries *series : m_chart->series()) {
            if (series != referenceLine) {
                static_cast<QXYSeries*>(series)->attachAxis(xAxis);
                static_cast<QXYSeries*>(series)->attachAxis(yAxis);
            }
        }
        referenceLine->attachAxis(xAxis);
        referenceLine->attachAxis(yAxis);
        
        // Calculate metrics for each experiment (since scatter data has one point per treatment)
        QVector<QMap<QString, QVariant>> metrics;
        for (auto it = experimentPoints.begin(); it != experimentPoints.end(); ++it) {
            QString experimentKey = it.key();
            QVector<QPointF> points = it.value();
            
            QVector<double> xValues, yValues;
            for (const QPointF &point : points) {
                xValues.append(point.x());
                yValues.append(point.y());
            }
            
            // Calculate metrics (X = measured, Y = simulated)
            QVariantMap metricResult = MetricsCalculator::calculateMetrics(yValues, xValues, 0);
            
            // Calculate R² for scatter plots
            double rSquared = MetricsCalculator::rSquared(xValues, yValues);
            metricResult["R²"] = rSquared;
            
            QMap<QString, QVariant> metricMap;
            for (auto it2 = metricResult.begin(); it2 != metricResult.end(); ++it2) {
                metricMap[it2.key()] = it2.value();
            }
            metricMap["Treatment"] = experimentKey;
            metricMap["TreatmentName"] = experimentKey;
            metricMap["Experiment"] = experimentKey;
            metricMap["ExperimentName"] = experimentKey;
            metricMap["Variable"] = baseVarName; // Use base variable name (without 's' or 'm')
            metricMap["VariableName"] = yTitle; // Use full name from variable info

            QString cropCodeForExp = experimentCropCodes.value(experimentKey);
            if (!cropCodeForExp.isEmpty()) {
                metricMap["Crop"] = cropCodeForExp;
                QString cropDisplay = getCropNameFromCode(cropCodeForExp);
                if (!cropDisplay.isEmpty()) {
                    metricMap["CropName"] = cropDisplay;
                }
            }
            metrics.append(metricMap);
        }
        
        // Sort scatter plot metrics by Experiment (which serves as treatment identifier), then by Variable and Crop
        if (!metrics.isEmpty()) {
            std::sort(metrics.begin(), metrics.end(), [](const QMap<QString, QVariant>& a, const QMap<QString, QVariant>& b) {
                // Sort by Experiment (treatment identifier in scatter plots)
                QString expA = a.value("Experiment").toString();
                QString expB = b.value("Experiment").toString();
                
                // Try to extract numeric part for numerical sorting
                bool okA, okB;
                int expNumA = expA.toInt(&okA);
                int expNumB = expB.toInt(&okB);
                
                if (okA && okB) {
                    if (expNumA != expNumB) {
                        return expNumA < expNumB;
                    }
                } else {
                    if (expA != expB) {
                        return expA < expB;
                    }
                }
                
                // If experiments are equal, sort by Variable
                QString varA = a.value("Variable").toString();
                QString varB = b.value("Variable").toString();
                if (varA != varB) {
                    return varA < varB;
                }
                
                // If variables are equal, sort by Crop
                QString cropA = a.value("Crop").toString();
                QString cropB = b.value("Crop").toString();
                return cropA < cropB;
            });
        }
        
        // Emit metrics signal
        emit metricsCalculated(metrics);
        
        // Update legend (updateLegend uses m_plotDataList internally, parameter is unused)
        QVector<PlotData> emptyPlotDataList;
        updateLegend(emptyPlotDataList);
        
        emit plotUpdated();
        
    } catch (const std::exception& e) {
        QString error = QString("Error in plotScatter: %1").arg(e.what());
        qWarning() << error;
        emit errorOccurred(error);
    }
}


