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
    update();
}

void ErrorBarChartView::setBoxPlotData(const QVector<BoxPlotStats> &stats, double yMin, double yMax)
{
    m_boxStats = stats;
    m_bpYMin   = yMin;
    m_bpYMax   = yMax;
    update();
}

void ErrorBarChartView::clearBoxPlotMedians()
{
    m_boxStats.clear();
    update();
}

void ErrorBarChartView::paintBoxPlotMedians(QPainter *painter)
{
    if (m_boxStats.isEmpty() || !chart()) return;
    double yRange = m_bpYMax - m_bpYMin;
    if (qFuzzyIsNull(yRange)) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF plotArea = chart()->plotArea();
    int nCats = m_boxStats.size();

    // Each category occupies an equal slot; box takes 50% of slot width
    double slotW  = plotArea.width() / nCats;
    double boxHalf = slotW * 0.25;   // box half-width = 50% of slot
    double capHalf = slotW * 0.12;   // whisker cap half-width

    auto yToPixel = [&](double val) -> double {
        double ratio = (val - m_bpYMin) / yRange;
        return plotArea.top() + (1.0 - ratio) * plotArea.height();
    };

    const QColor boxFill(70, 130, 180, 180);     // steel blue, semi-transparent
    const QColor boxBorder(45, 90, 130);
    const QColor whiskerColor(60, 60, 60);
    const QColor medianColor(255, 255, 255);
    const QColor outlierColor(220, 60, 60);

    for (int i = 0; i < nCats; ++i) {
        const BoxPlotStats &s = m_boxStats[i];
        double cx   = plotArea.left() + (i + 0.5) * slotW;  // center x of category
        double yQ0  = yToPixel(s.q0);
        double yQ1  = yToPixel(s.q1);
        double yQ2  = yToPixel(s.q2);
        double yQ3  = yToPixel(s.q3);
        double yQ4  = yToPixel(s.q4);

        // --- Whisker: vertical spine Q0 → Q1 (lower) and Q3 → Q4 (upper) ---
        QPen whiskerPen(whiskerColor, 1.5, Qt::SolidLine, Qt::FlatCap);
        painter->setPen(whiskerPen);
        painter->setBrush(Qt::NoBrush);

        // Lower whisker (Q1 down to Q0)
        painter->drawLine(QPointF(cx, yQ1), QPointF(cx, yQ0));
        // Lower cap
        painter->drawLine(QPointF(cx - capHalf, yQ0), QPointF(cx + capHalf, yQ0));

        // Upper whisker (Q3 up to Q4)
        painter->drawLine(QPointF(cx, yQ3), QPointF(cx, yQ4));
        // Upper cap
        painter->drawLine(QPointF(cx - capHalf, yQ4), QPointF(cx + capHalf, yQ4));

        // --- IQR Box (Q1 to Q3) ---
        QRectF boxRect(cx - boxHalf, yQ3, boxHalf * 2.0, yQ1 - yQ3);
        painter->setPen(QPen(boxBorder, 1.5));
        painter->setBrush(QBrush(boxFill));
        painter->drawRect(boxRect);

        // --- Median line ---
        painter->setPen(QPen(medianColor, 2.5, Qt::SolidLine, Qt::FlatCap));
        painter->drawLine(QPointF(cx - boxHalf, yQ2), QPointF(cx + boxHalf, yQ2));

        // --- Outlier dot: if n=1 (all quartiles equal), show as single point ---
        if (qFuzzyCompare(s.q0, s.q4)) {
            painter->setPen(QPen(outlierColor, 1.0));
            painter->setBrush(QBrush(outlierColor));
            painter->drawEllipse(QPointF(cx, yQ2), 4.0, 4.0);
        }

        // --- X category label centered below the plot area ---
        if (!s.label.isEmpty()) {
            painter->setPen(QPen(QColor(60, 60, 60)));
            painter->setFont(QFont("Arial", 9));
            QRectF labelRect(cx - slotW * 0.5, plotArea.bottom() + 4, slotW, 32);
            painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, s.label);
        }
    }

    painter->restore();
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

    if (m_errorBars.isEmpty() && !chart()) return;

    QPainter painter(this->viewport());
    
    // Draw tick marks
    paintTickMarks(&painter);

    // Draw custom markers (e.g., inverted triangle)
    paintCustomMarkers(&painter);

    if (!m_errorBars.isEmpty()) {
        paintErrorBars(&painter, QPoint(0, 0));
    }
    if (!m_boxStats.isEmpty()) {
        paintBoxPlotMedians(&painter);
    }
}

void ErrorBarChartView::paintTickMarks(QPainter *painter)
{
    if (!chart()) return;
    
    painter->save();
    
    QRectF plotArea = chart()->plotArea();
    QList<QAbstractAxis*> axes = chart()->axes();
    
    const int tickLength = 6;
    const int minorTickLength = 3;
    
    for (QAbstractAxis *axis : axes) {
        if (!axis->isVisible()) continue;
        
        QColor axisColor = Qt::black; // default
        if (auto pAxis = qobject_cast<QValueAxis*>(axis)) {
            axisColor = pAxis->linePenColor();
            if (!pAxis->isLineVisible()) continue;
        } else if (auto dAxis = qobject_cast<QDateTimeAxis*>(axis)) {
            axisColor = dAxis->linePenColor();
            if (!dAxis->isLineVisible()) continue;
        }
        
        painter->setPen(QPen(axisColor, 1));
        
        if (axis->alignment() == Qt::AlignBottom) {
            double min = 0, max = 0;
            int tickCount = 0;
            int minorTickCount = 0;
            if (auto valAxis = qobject_cast<QValueAxis*>(axis)) {
                min = valAxis->min();
                max = valAxis->max();
                tickCount = valAxis->tickCount();
                minorTickCount = valAxis->minorTickCount();
            } else if (auto dateAxis = qobject_cast<QDateTimeAxis*>(axis)) {
                min = dateAxis->min().toMSecsSinceEpoch();
                max = dateAxis->max().toMSecsSinceEpoch();
                tickCount = dateAxis->tickCount();
            }
            if (tickCount > 1) {
                double y = plotArea.bottom();
                for (int i = 0; i < tickCount; ++i) {
                    double ratio = (double)i / (tickCount - 1);
                    double x = plotArea.left() + ratio * plotArea.width();
                    painter->drawLine(QPointF(x, y), QPointF(x, y + tickLength));
                    
                    if (i < tickCount - 1 && minorTickCount > 0) {
                        for (int j = 1; j <= minorTickCount; ++j) {
                            double minorRatio = ratio + ((double)j / (minorTickCount + 1)) / (tickCount - 1);
                            double minorX = plotArea.left() + minorRatio * plotArea.width();
                            painter->drawLine(QPointF(minorX, y), QPointF(minorX, y + minorTickLength));
                        }
                    }
                }
            }
        } else if (axis->alignment() == Qt::AlignLeft) {
            double min = 0, max = 0;
            int tickCount = 0;
            int minorTickCount = 0;
            if (auto valAxis = qobject_cast<QValueAxis*>(axis)) {
                min = valAxis->min();
                max = valAxis->max();
                tickCount = valAxis->tickCount();
                minorTickCount = valAxis->minorTickCount();
            }
            if (tickCount > 1) {
                double x = plotArea.left();
                for (int i = 0; i < tickCount; ++i) {
                    double ratio = (double)i / (tickCount - 1);
                    double y = plotArea.bottom() - ratio * plotArea.height();
                    painter->drawLine(QPointF(x - tickLength, y), QPointF(x, y));
                    
                    if (i < tickCount - 1 && minorTickCount > 0) {
                        for (int j = 1; j <= minorTickCount; ++j) {
                            double minorRatio = ratio + ((double)j / (minorTickCount + 1)) / (tickCount - 1);
                            double minorY = plotArea.bottom() - minorRatio * plotArea.height();
                            painter->drawLine(QPointF(x - minorTickLength, minorY), QPointF(x, minorY));
                        }
                    }
                }
            }
        }
    }
    
    painter->restore();
}

void ErrorBarChartView::paintCustomMarkers(QPainter *painter)
{
    if (!chart()) return;
    
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    
    QRectF plotArea = chart()->plotArea();
    
    for (QAbstractSeries *series : chart()->series()) {
        if (!series->isVisible()) continue;
        
        QScatterSeries *scatterSeries = qobject_cast<QScatterSeries*>(series);
        if (!scatterSeries) continue;
        
        QString customShape = scatterSeries->property("custom_shape").toString();
        if (customShape != "v") continue; // currently only handling inverted triangle custom shape
        
        QPen seriesPen = scatterSeries->property("custom_pen").value<QPen>();
        QBrush seriesBrush = scatterSeries->property("custom_brush").value<QBrush>();
        double symbolSize = scatterSeries->property("custom_size").toDouble();
        if (symbolSize <= 0) symbolSize = 6.0;
        
        painter->setPen(seriesPen);
        painter->setBrush(seriesBrush);
        
        // Ensure marker size isn't too large
        symbolSize = qMin(symbolSize, 20.0);
        
        for (const QPointF &point : scatterSeries->points()) {
            QPointF p = chart()->mapToPosition(point, series);
            
            // Only draw inside plot area bounds approximately
            if (p.x() >= plotArea.left() - symbolSize && 
                p.x() <= plotArea.right() + symbolSize && 
                p.y() >= plotArea.top() - symbolSize && 
                p.y() <= plotArea.bottom() + symbolSize) {
                
                QPolygonF triangle;
                triangle << QPointF(p.x() - symbolSize/2, p.y() - symbolSize/2)
                         << QPointF(p.x() + symbolSize/2, p.y() - symbolSize/2)
                         << QPointF(p.x(), p.y() + symbolSize/2);
                painter->drawPolygon(triangle);
            }
        }
    }
    
    painter->restore();
}

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
    m_legendScrollArea->setFixedWidth(140);

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
    m_legendStack->setFixedWidth(140);
    m_legendStack->addWidget(m_preplotPanel);       // page 0 — shown before first plot
    m_legendStack->addWidget(m_legendScrollArea);   // page 1 — shown after plotting
    m_legendStack->setCurrentIndex(0);
    m_mainLayout->addWidget(m_legendStack, 20);

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
    xAxis->setLinePen(QPen(Qt::black));
    yAxis->setLinePen(QPen(Qt::black));
    xAxis->setLabelsBrush(QBrush(Qt::black));
    yAxis->setLabelsBrush(QBrush(Qt::black));

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
        valueAxis->setMinorGridLineVisible(true);
        
        xAxis = valueAxis;
        qDebug() << "PlotWidget::setupAxes() - Created QValueAxis for" << xVar;
    }
    
    // Create Y-axis (always value axis)
    QValueAxis *yAxis = new QValueAxis();
    
    yAxis->setTitleText("");
    
    // Add minor ticks to Y axis
    yAxis->setMinorTickCount(4);
    yAxis->setMinorGridLineVisible(true);
    
    // Configure axis appearance
    xAxis->setGridLineVisible(m_showGrid);
    yAxis->setGridLineVisible(m_showGrid);
    xAxis->setLabelsVisible(true);
    yAxis->setLabelsVisible(true);
    xAxis->setLinePen(QPen(Qt::black));
    yAxis->setLinePen(QPen(Qt::black));
    xAxis->setLabelsBrush(QBrush(Qt::black));
    yAxis->setLabelsBrush(QBrush(Qt::black));

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

void PlotWidget::resizeScatterPanels()
{
    if (!m_scatterPanelContainer || !m_scatterPanelGrid) return;
    if (m_scatterPanelGrid->count() == 0) return;

    int nCols   = m_scatterNCols;
    int nRows   = m_scatterNRows;
    if (nCols < 1) nCols = 1;
    int spacing = m_scatterPanelGrid->spacing();
    int margins = 12;

    int availW = m_scatterScrollArea ? m_scatterScrollArea->viewport()->width()
                                     : m_leftContainer->width();
    int availH = m_scatterScrollArea ? m_scatterScrollArea->viewport()->height()
                                     : m_leftContainer->height();
    if (availW < 50) availW = width() - 150;
    if (availH < 50) availH = height() - 20;
    if (availW < 200) availW = 700;
    if (availH < 200) availH = 600;

    int sideW = (availW - margins - spacing * (nCols - 1)) / nCols;
    int sideH = (availH - margins - spacing * (nRows - 1)) / nRows;
    // Use the smaller of the two so panels fit both axes without scrolling
    int side  = qMin(sideW, sideH);
    if (side < 180) side = 180;

    int statsFontPt = qBound(8, side / 28, 13);
    if (m_plotSettings.axisTickFontSize != 9)
        statsFontPt = qBound(7, m_plotSettings.axisTickFontSize, 14);

    for (int i = 0; i < m_scatterPanelGrid->count(); ++i) {
        QLayoutItem *item = m_scatterPanelGrid->itemAt(i);
        if (!item || !item->widget()) continue;
        item->widget()->setFixedSize(side, side);
        // Update stats label font to match new panel size
        for (QChartView *cv : item->widget()->findChildren<QChartView*>()) {
            for (QLabel *lbl : cv->findChildren<QLabel*>()) {
                if (lbl->parent() == cv) {
                    lbl->setStyleSheet(QString(
                        "QLabel { background: rgba(255,255,255,210); font-size: %1pt; "
                        "padding: 2px 4px; border: none; }").arg(statsFontPt));
                    lbl->adjustSize();
                }
            }
        }
    }
    int cW = side * nCols + spacing * (nCols - 1) + margins;
    int cH = side * nRows + spacing * (nRows - 1) + margins;
    m_scatterPanelContainer->setFixedSize(cW, cH);
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

        for (int row = 0; row < simData.rowCount; ++row) {
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

            // Treatment filter: match plain trt or compound exp::trt key
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
            // For summary OSU files each row is one year; don't split by RUN or every
            // year becomes its own single-point series and no line is drawn.
            QString expTrtKey = (runStr.isEmpty() || isSummaryOsu)
                ? QString("%1__%2__%3").arg(crop).arg(experiment).arg(trt)
                : QString("%1__%2__%3__%4").arg(crop).arg(experiment).arg(trt).arg(runStr);
            
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
            // Always use crop__experiment__treatment as color key so observed and simulated
            // data for the same treatment share the same color
            QString treatmentId = QString("%1__%2__%3").arg(crop).arg(experiment).arg(treatment);
            plotData.color = getColorForTreatment(treatmentId, colorIndex);
            // Line style based on variable index, not treatment index
            plotData.lineStyleIndex = yVars.indexOf(yVar); // modulo applied at render time
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
    
    // Clear error bars when chart is cleared
    if (m_chartView) {
        m_chartView->setErrorBarData(QMap<QAbstractSeries*, QVector<ErrorBarData>>());
        m_chartView->clearBoxPlotMedians();
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
            row << QString::number(x) << crop << exp << trt;

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

void PlotWidget::exportPlot(const QString &filePath, const QString &format)
{
    if (!this) return;

    this->show();
    this->update();
    if (m_chartView) { m_chartView->update(); m_chartView->repaint(); }
    QApplication::processEvents();

    QPixmap pixmap;

    if (m_isScatterMode && m_scatterPanelContainer && m_legendWidget) {
        pixmap = grabScatterAtSize(400);
    } else {
        if (m_legendScrollArea) {
            m_legendScrollArea->setVisible(m_showLegend);
            if (m_showLegend) m_legendScrollArea->update();
        }
        pixmap = QPixmap(this->size());
        pixmap.fill(Qt::white);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        this->render(&painter);
        painter.end();
    }

    pixmap.save(filePath, format.toUtf8().constData());
}

// Render scatter panels at a given panel size, composite with legend, return pixmap
QPixmap PlotWidget::grabScatterAtSize(int panelSide)
{
    if (!m_scatterPanelContainer || !m_scatterPanelGrid || !m_legendWidget)
        return QPixmap();

    int nCols   = m_scatterNCols;
    int nRows   = m_scatterNRows;
    int spacing = m_scatterPanelGrid->spacing();
    int margins = 12;

    // Resize panels to target size
    for (int i = 0; i < m_scatterPanelGrid->count(); ++i) {
        QLayoutItem *item = m_scatterPanelGrid->itemAt(i);
        if (item && item->widget())
            item->widget()->setFixedSize(panelSide, panelSide);
    }
    int cW = panelSide * nCols + spacing * (nCols - 1) + margins;
    int cH = panelSide * nRows + spacing * (nRows - 1) + margins;
    m_scatterPanelContainer->setFixedSize(cW, cH);
    m_scatterPanelContainer->update();
    QApplication::processEvents();

    QPixmap panelsPixmap = m_scatterPanelContainer->grab();
    QPixmap legendPixmap = m_legendWidget->grab();

    int totalW = panelsPixmap.width() + legendPixmap.width() + 12;
    int totalH = qMax(panelsPixmap.height(), legendPixmap.height());
    QPixmap result(totalW, totalH);
    result.fill(Qt::white);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(0, (totalH - panelsPixmap.height()) / 2, panelsPixmap);
    p.drawPixmap(panelsPixmap.width() + 12, (totalH - legendPixmap.height()) / 2, legendPixmap);
    p.end();

    // Restore to screen size
    resizeScatterPanels();

    return result;
}

void PlotWidget::copyPlotToClipboard()
{
    if (!this) return;

    this->show();
    this->update();
    QApplication::processEvents();

    QPixmap pixmap;

    if (m_isScatterMode && m_scatterPanelContainer && m_legendWidget) {
        pixmap = grabScatterAtSize(400); // 400px per panel for clipboard
    } else {
        if (m_legendScrollArea)
            m_legendScrollArea->setVisible(m_showLegend);
        pixmap = this->grab();
    }

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

    setAxisTitles(xLabel, "");
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
        // Also remove axes so box plot ↔ line plot switching doesn't stack old axes
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
    
    // Organize data by variable first, in the same order as the Y variable list.
    // Use m_currentYVars to preserve the user's selection order, then append any
    // remaining variables not in that list (e.g. scatter mode) at the end.
    QSet<QString> seen;
    QStringList variables;
    for (const QString& yVar : m_currentYVars) {
        bool exists = (legendEntries.contains("Simulated") && legendEntries["Simulated"].contains(yVar))
                   || (legendEntries.contains("Observed")  && legendEntries["Observed"].contains(yVar));
        if (exists) {
            variables.append(yVar);
            seen.insert(yVar);
        }
    }
    // Append any variables present in data but not in m_currentYVars
    for (const QString& category : {"Simulated", "Observed"}) {
        if (legendEntries.contains(category)) {
            for (const QString& varName : legendEntries[category].keys()) {
                if (!seen.contains(varName)) {
                    variables.append(varName);
                    seen.insert(varName);
                }
            }
        }
    }

    qDebug() << "updateLegendAdvanced: Found" << variables.size() << "variables:" << variables;

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
        
        // Custom sort: sort by trt_id (treatment number) numerically
        std::sort(sortedKeys.begin(), sortedKeys.end(), [&varTreatments](const QString& a, const QString& b) {
            QMap<QString, QVariant> dataA = varTreatments[a];
            QMap<QString, QVariant> dataB = varTreatments[b];

            bool okA, okB;
            int trtA = dataA["trt_id"].toString().toInt(&okA);
            int trtB = dataB["trt_id"].toString().toInt(&okB);

            if (okA && okB) return trtA < trtB;
            if (okA) return true;
            if (okB) return false;
            return dataA["trt_id"].toString() < dataB["trt_id"].toString();
        });
        
        for (const QString& uniqueKey : sortedKeys) {
            createLegendRowFromData(varTreatments[uniqueKey], varName, displayName);
        }
        
        // Add separator after each variable except the last (matching Python)
        if (varName != variables.last()) {
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
    setFixedSize(50, 15);
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
        } else if (m_symbol == "diamond" || m_symbol == "d") {
            // Diamond - matches QScatterSeries::MarkerShapeRotatedRectangle
            QPolygonF diamond;
            diamond << QPointF(center.x(), center.y() - symbolSize/2)
                    << QPointF(center.x() + symbolSize/2, center.y())
                    << QPointF(center.x(), center.y() + symbolSize/2)
                    << QPointF(center.x() - symbolSize/2, center.y());
            painter.drawPolygon(diamond);
        } else if (m_symbol == "v") {
            // Inverted Triangle
            QPolygonF invertedTriangle;
            invertedTriangle << QPointF(center.x() - symbolSize/2, center.y() - symbolSize/2)
                             << QPointF(center.x() + symbolSize/2, center.y() - symbolSize/2)
                             << QPointF(center.x(), center.y() + symbolSize/2);
            painter.drawPolygon(invertedTriangle);
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

// ---------------------------------------------------------------------------
// OSU seasonal box plot
// ---------------------------------------------------------------------------
void PlotWidget::plotOsuBoxPlot(const DataTable &simData, const QStringList &yVars,
                                 const QStringList &treatments, const QString &selectedExperiment)
{
    if (!m_chart || yVars.isEmpty()) return;

    // Only one Y variable at a time for box plots (first selected)
    const QString &yVar = yVars.first();

    const DataColumn *trtColumn  = simData.getColumn("TRT");
    const DataColumn *yColumn    = simData.getColumn(yVar);
    const DataColumn *expColumn  = simData.getColumn("EXPERIMENT");
    const DataColumn *tnameCol   = simData.getColumn("TNAME");

    if (!trtColumn || !yColumn) {
        qWarning() << "PlotOsuBoxPlot: missing TRT or" << yVar << "column";
        return;
    }

    // Determine if multiple experiments or multiple crops are present
    // (sequence OSU files have TRT=1 always but vary by CROP code)
    const DataColumn *cropColumn = simData.getColumn("CROP");

    QSet<QString> expSet, cropSet;
    for (int row = 0; row < simData.rowCount; ++row) {
        if (expColumn && row < expColumn->data.size()) {
            QString e = expColumn->data[row].toString();
            if (!e.isEmpty()) expSet.insert(e);
        }
        if (cropColumn && row < cropColumn->data.size()) {
            QString c = cropColumn->data[row].toString();
            if (!c.isEmpty()) cropSet.insert(c);
        }
    }
    if (expSet.isEmpty()) expSet.insert(selectedExperiment);
    bool multiExp  = expSet.size() > 1;
    bool multiCrop = cropSet.size() > 1;   // sequence file: crop varies, trt is always 1

    // Collect Y values keyed by a compound identifier that mirrors plotDatasets:
    //   sequence:  crop::exp::trt   (crop is the primary differentiator)
    //   multi-exp: exp::trt
    //   single:    trt
    QMap<QString, QVector<double>> trtValues;
    QMap<QString, QString>         keyToLabel;

    for (int row = 0; row < simData.rowCount; ++row) {
        if (row >= trtColumn->data.size() || row >= yColumn->data.size()) continue;

        QString trt = trtColumn->data[row].toString();

        QString experiment = selectedExperiment;
        if (expColumn && row < expColumn->data.size()) {
            QString e = expColumn->data[row].toString();
            if (!e.isEmpty()) experiment = e;
        }

        QString crop = "XX";
        if (cropColumn && row < cropColumn->data.size()) {
            QString c = cropColumn->data[row].toString();
            if (!c.isEmpty()) crop = c;
        }

        // Treatment filter: accept plain trt OR compound exp::trt
        // For sequence (multiCrop) the treatment panel shows trt=1 entries, so this still works
        if (!treatments.isEmpty() && !treatments.contains("All")
            && !treatments.contains(trt)
            && !treatments.contains(experiment + "::" + trt)) {
            continue;
        }

        QVariant yVal = yColumn->data[row];
        if (DataProcessor::isMissingValue(yVal)) continue;
        bool ok;
        double y = yVal.toDouble(&ok);
        if (!ok) continue;

        // Build group key matching plotDatasets logic
        QString key;
        if (multiCrop)
            key = crop + "::" + experiment + "::" + trt;   // sequence
        else if (multiExp)
            key = experiment + "::" + trt;                  // multi-experiment seasonal
        else
            key = trt;                                       // single experiment seasonal

        trtValues[key].append(y);

        if (!keyToLabel.contains(key)) {
            QString tname;
            if (tnameCol && row < tnameCol->data.size())
                tname = tnameCol->data[row].toString();

            if (multiCrop) {
                // For sequence: label is the crop code (and exp if multi-exp)
                keyToLabel[key] = multiExp
                    ? QString("%1·%2").arg(crop, experiment)
                    : crop;
            } else if (multiExp) {
                keyToLabel[key] = tname.isEmpty()
                    ? QString("%1·%2").arg(trt, experiment)
                    : QString("%1·%2").arg(trt, tname);
            } else {
                keyToLabel[key] = tname.isEmpty() ? trt
                    : QString("%1-%2").arg(trt, tname);
            }
        }
    }

    if (trtValues.isEmpty()) return;

    // Sort keys: crop→exp→trt for sequence, exp→trt for multi-exp, numeric trt for single
    QStringList trtKeys = trtValues.keys();
    std::sort(trtKeys.begin(), trtKeys.end(), [&](const QString &a, const QString &b) {
        QStringList pa = a.split("::"), pb = b.split("::");
        // Compare segment by segment; numeric segments compared as integers
        int n = qMax(pa.size(), pb.size());
        for (int i = 0; i < n; ++i) {
            QString sa = (i < pa.size()) ? pa[i] : QString();
            QString sb = (i < pb.size()) ? pb[i] : QString();
            if (sa == sb) continue;
            bool aOk, bOk;
            int ai = sa.toInt(&aOk), bi = sb.toInt(&bOk);
            return (aOk && bOk) ? ai < bi : sa < sb;
        }
        return false;
    });

    // Category labels for the bar chart x-axis
    QStringList categories;
    for (const QString &k : trtKeys)
        categories.append(keyToLabel.value(k, k));

    // Quantile helper
    auto quantile = [](QVector<double> &sorted, double p) -> double {
        int n = sorted.size();
        if (n == 0) return 0.0;
        if (n == 1) return sorted[0];
        double pos = p * (n - 1);
        int lo = static_cast<int>(pos);
        int hi = lo + 1;
        if (hi >= n) return sorted[n - 1];
        return sorted[lo] + (pos - lo) * (sorted[hi] - sorted[lo]);
    };

    // Compute quartiles
    struct BoxStats { double q0, q1, q2, q3, q4; };
    QVector<BoxStats> stats;
    double globalMin =  std::numeric_limits<double>::max();
    double globalMax = -std::numeric_limits<double>::max();

    for (int i = 0; i < trtKeys.size(); ++i) {
        QVector<double> vals = trtValues[trtKeys[i]];
        std::sort(vals.begin(), vals.end());
        BoxStats s;
        s.q0 = vals.first();
        s.q1 = quantile(vals, 0.25);
        s.q2 = quantile(vals, 0.50);
        s.q3 = quantile(vals, 0.75);
        s.q4 = vals.last();
        stats.append(s);
        globalMin = std::min(globalMin, s.q0);
        globalMax = std::max(globalMax, s.q4);
    }

    m_chart->removeAllSeries();
    for (auto *axis : m_chart->axes())
        m_chart->removeAxis(axis);

    // Use an invisible scatter series as axis anchor (Qt Charts needs ≥1 series on axes)
    auto *dummySeries = new QScatterSeries();
    dummySeries->setOpacity(0.0);
    // X range: 0..nCats so each category occupies exactly one unit, no padding
    dummySeries->append(0, globalMin);
    dummySeries->append(trtKeys.size(), globalMax);
    m_chart->addSeries(dummySeries);

    // X axis — value axis so we control slot positions precisely (no QBarCategoryAxis padding)
    auto *xAxis = new QValueAxis();
    xAxis->setRange(0, trtKeys.size());
    xAxis->setTickCount(trtKeys.size() + 1);
    xAxis->setLabelsVisible(false);     // hide numeric labels — painter draws category names
    xAxis->setGridLineVisible(false);
    xAxis->setMinorGridLineVisible(false);
    xAxis->setTitleText("Treatment");
    xAxis->setLinePen(QPen(QColor(180, 180, 180)));
    xAxis->setLabelsFont(QFont("Arial", 9));
    xAxis->setTitleFont(QFont("Arial", 9, QFont::Bold));
    m_chart->addAxis(xAxis, Qt::AlignBottom);

    // Y axis
    double yPad = (globalMax - globalMin) * 0.08;
    double yMin = (globalMin >= 0.0) ? 0.0 : globalMin - yPad;
    double yMax = globalMax + yPad;

    double tickInterval = calculateNiceYInterval(yMax);
    double alignedMax   = std::ceil(yMax / tickInterval) * tickInterval;
    if (alignedMax <= globalMax) alignedMax += tickInterval;

    auto *yAxis = new QValueAxis();
    yAxis->setMin(yMin);
    yAxis->setMax(alignedMax);
    yAxis->setTickInterval(tickInterval);
    int tickCount = qRound(alignedMax / tickInterval) + 1;
    if (tickCount < 8) tickCount = 8;
    yAxis->setTickCount(tickCount);
    yAxis->setLabelFormat("%.0f");
    yAxis->setGridLineColor(QColor(220, 220, 220));
    yAxis->setGridLineVisible(true);
    yAxis->setMinorGridLineVisible(false);
    yAxis->setLinePen(QPen(QColor(180, 180, 180)));
    yAxis->setLabelsFont(QFont("Arial", 9));
    m_chart->addAxis(yAxis, Qt::AlignLeft);

    dummySeries->attachAxis(xAxis);
    dummySeries->attachAxis(yAxis);

    // Chart title
    QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(yVar);
    QString yLabel = varInfo.first.isEmpty() ? yVar : varInfo.first;
    if (!varInfo.second.isEmpty()) yLabel += " (" + varInfo.second + ")";
    m_chart->setTitle("");
    // Extra bottom margin so painter-drawn category labels aren't clipped by the widget edge
    m_chart->setMargins(QMargins(0, 0, 0, 20));

    // Build painter stats vector
    QVector<ErrorBarChartView::BoxPlotStats> bpStats;
    for (int i = 0; i < trtKeys.size(); ++i) {
        ErrorBarChartView::BoxPlotStats bp;
        bp.q0    = stats[i].q0;
        bp.q1    = stats[i].q1;
        bp.q2    = stats[i].q2;
        bp.q3    = stats[i].q3;
        bp.q4    = stats[i].q4;
        bp.label = categories[i];
        bpStats.append(bp);
    }
    if (m_chartView)
        m_chartView->setBoxPlotData(bpStats, yMin, alignedMax);

    // Legend panel
    clearLegend();
    QLabel *legHeader = new QLabel("Box Plot Key");
    legHeader->setStyleSheet("font-size: 10px; font-weight: bold; color: #444; "
                             "padding: 4px 0px 4px 4px; border-bottom: 1px solid #ddd;");
    m_legendLayout->addWidget(legHeader);

    struct LegRow { QString css; QString label; };
    const QColor boxFill(70, 130, 180, 180);
    const QColor boxBorder(45, 90, 130);
    const QVector<LegRow> rows = {
        { QString("background:#3c3c3c; border:none; min-height:2px; max-height:2px;"), "Whisker (Min/Max)" },
        { QString("background-color:rgba(%1,%2,%3,%4); border:1px solid %5;")
              .arg(boxFill.red()).arg(boxFill.green()).arg(boxFill.blue()).arg(boxFill.alpha())
              .arg(boxBorder.name()),                                                   "IQR Box (25–75%)" },
        { QString("background:white; border:2px solid #2d5a82; min-height:2px; max-height:2px;"), "Median" },
        { QString("background:#dc3c3c; border-radius:3px; min-width:6px; max-width:6px; "
                  "min-height:6px; max-height:6px;"),                                  "Single data point" },
    };
    for (const LegRow &r : rows) {
        QWidget *row = new QWidget();
        QHBoxLayout *rl = new QHBoxLayout(row);
        rl->setContentsMargins(4, 4, 4, 4);
        rl->setSpacing(8);
        QLabel *sw = new QLabel();
        sw->setFixedSize(22, 14);
        sw->setStyleSheet(r.css);
        rl->addWidget(sw);
        QLabel *lbl = new QLabel(r.label);
        lbl->setFont(QFont("Arial", 9));
        lbl->setStyleSheet("color:#333;");
        rl->addWidget(lbl);
        rl->addStretch();
        m_legendLayout->addWidget(row);
    }
    m_legendLayout->addStretch();

    if (m_legendStack) m_legendStack->setCurrentIndex(1);
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
        // Parse compound "exp::trt" keys (used when multiple experiments are loaded)
        QString expPrefix, trtId;
        if (key.contains("::")) {
            expPrefix = key.section("::", 0, 0);
            trtId     = key.section("::", 1);
        } else {
            trtId = key;
        }

        // Find display name: prefer exact experiment match, then any experiment
        QString displayName;
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

        QString label;
        if (!expPrefix.isEmpty())
            label = displayName.isEmpty()
                ? QString("%1 · %2").arg(trtId, expPrefix)
                : QString("%1 - %2 · %3").arg(trtId, displayName, expPrefix);
        else
            label = displayName.isEmpty() ? trtId : QString("%1 - %2").arg(trtId, displayName);

        QListWidgetItem *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, key);
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

void PlotWidget::onSettingsButtonClicked()
{
    qDebug() << "PlotWidget: Settings button clicked";

    // Populate available experiments and treatments from current data
    QStringList availableExperiments;
    QMap<QString, QStringList> experimentTreatments;
    QMap<QString, QString> treatmentDisplayNames;
    const DataColumn *trtColumn = m_simData.getColumn("TRT");
    // For scatter mode (EVALUATE.OUT), prefer EXCODE over the metadata EXPERIMENT column
    const DataColumn *expColumn = nullptr;
    if (m_isScatterMode) {
        expColumn = m_simData.getColumn("EXCODE");
    }
    if (!expColumn) expColumn = m_simData.getColumn("EXPERIMENT");
    if (trtColumn) {
        QSet<QString> seenPairs;
        QSet<QString> seenExps;
        for (int i = 0; i < m_simData.rowCount; ++i) {
            QString trtId = trtColumn->data.value(i).toString();
            if (trtId.isEmpty()) continue;
            QString expId = m_selectedExperiment;
            if (expColumn && i < expColumn->data.size()) {
                QString e = expColumn->data[i].toString();
                if (!e.isEmpty()) expId = e;
            }
            QString pairKey = expId + "::" + trtId;
            if (!seenPairs.contains(pairKey)) {
                seenPairs.insert(pairKey);
                if (!seenExps.contains(expId)) {
                    seenExps.insert(expId);
                    availableExperiments.append(expId);
                }
                experimentTreatments[expId].append(trtId);
                treatmentDisplayNames[pairKey] = getTreatmentDisplayName(trtId, expId);
            }
        }
    }
    m_plotSettings.availableExperiments = availableExperiments;
    m_plotSettings.experimentTreatments = experimentTreatments;
    m_plotSettings.treatmentDisplayNames = treatmentDisplayNames;
    m_plotSettings.availableYVars = m_currentYVars;
    // Build Y variable display names
    for (const QString &yVar : m_currentYVars) {
        if (!m_plotSettings.yVarDisplayNames.contains(yVar)) {
            QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(yVar);
            // For EVALUATE.OUT variables (e.g. HWAMS, HWAMM), strip S/M suffix for CDE lookup
            if (varInfo.first.isEmpty() && yVar.length() > 1) {
                QString baseName = yVar.left(yVar.length() - 1);
                varInfo = DataProcessor::getVariableInfo(baseName);
            }
            m_plotSettings.yVarDisplayNames[yVar] = varInfo.first.isEmpty() ? yVar : varInfo.first;
        }
    }

    PlotSettingsDialog dialog(m_plotSettings, this, this);
    if (dialog.exec() == QDialog::Accepted) {
        PlotSettings newSettings = dialog.getSettings();

        // Check what changed BEFORE applyPlotSettings overwrites m_plotSettings
        bool filterChanged = (m_plotSettings.excludedSeriesKeys != newSettings.excludedSeriesKeys);
        bool errorBarChanged = (m_plotSettings.showErrorBars != newSettings.showErrorBars) ||
                               (m_plotSettings.errorBarType != newSettings.errorBarType);

        applyPlotSettings(newSettings);
        m_plotSettings = newSettings;
        saveSettings();

        // Re-plot if filter or error bar settings changed
        bool needsReplot = (filterChanged || errorBarChanged) && m_simData.rowCount > 0;
        if (needsReplot) {
            if (m_isScatterMode) {
                if (!m_currentYVars.isEmpty()) {
                    DataTable dataCopy = m_simData;
                    plotScatter(dataCopy, m_currentYVars);
                }
            } else {
                updatePlotWithScaling();
                if (m_obsData.rowCount > 0) {
                    calculateMetrics();
                }
            }
        }
    }
}

void PlotWidget::applyPlotSettings(const PlotSettings &settings)
{
    qDebug() << "PlotWidget: Applying plot settings";
    
    // Apply grid settings
    setShowGrid(settings.showGrid);
    
    // Apply legend settings (scatter mode always keeps legend visible)
    if (!m_isScatterMode)
        setShowLegend(settings.showLegend);
    
    // Apply axis titles (skip for scatter mode — it has its own measured/simulated titles)
    if (!m_isScatterMode) {
        QString xTitle = settings.xAxisTitle.isEmpty() ? m_currentXVar : settings.xAxisTitle;

        QString yTitle = settings.yAxisTitle;
        setAxisTitles(xTitle, yTitle);
    }
    
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
    auto hAxes = m_chart->axes(Qt::Horizontal);
    for (auto axis : axes) {
        // Apply axis tick label font
        QFont tickFont(settings.fontFamily, settings.axisTickFontSize);
        axis->setLabelsFont(tickFont);

        // Apply axis title font
        QFont labelFont(settings.fontFamily, settings.axisLabelFontSize);
        labelFont.setBold(settings.boldAxisLabels);
        axis->setTitleFont(labelFont);

        // Axis line, ticks, and labels are always black
        axis->setLinePen(QPen(Qt::black));
        axis->setLabelsBrush(QBrush(Qt::black));

        if (auto valueAxis = qobject_cast<QValueAxis*>(axis)) {
            valueAxis->setMinorTickCount(settings.minorTickCount);
            valueAxis->setMinorGridLineVisible(settings.showMinorGrid);
            valueAxis->setLabelsVisible(settings.showAxisLabels);

            // Apply X-axis tick customization only to X-axis (skip for scatter)
            if (!m_isScatterMode && !hAxes.isEmpty() && axis == hAxes.first()) {
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
            if (!hAxes.isEmpty() && axis == hAxes.first()) {
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

    // Apply settings live to all scatter panel charts (no full replot needed for style-only changes)
    if (m_isScatterMode) {
        QFont axFont(settings.fontFamily, settings.axisTickFontSize);
        QFont axTitleFont(settings.fontFamily, settings.axisLabelFontSize);
        axTitleFont.setBold(settings.boldAxisLabels);
        int tickCount = (settings.xAxisTickCount > 0) ? settings.xAxisTickCount : 6;

        for (QChartView *cv : m_scatterPanelViews) {
            if (!cv || !cv->chart()) continue;
            QChart *ch = cv->chart();
            ch->setBackgroundBrush(QBrush(settings.backgroundColor));
            ch->setPlotAreaBackgroundBrush(QBrush(settings.plotAreaColor));

            for (QAbstractAxis *axis : ch->axes()) {
                axis->setLabelsFont(axFont);
                axis->setTitleFont(axTitleFont);
                if (auto va = qobject_cast<QValueAxis*>(axis)) {
                    va->setLabelsVisible(settings.showAxisLabels);
                    va->setGridLineVisible(settings.showGrid);
                    va->setMinorGridLineVisible(settings.showMinorGrid);
                    va->setMinorTickCount(settings.showMinorGrid ? settings.minorTickCount : 0);
                    va->setTickCount(qMin(tickCount, va->tickCount() > 0 ? 10 : 6));
                }
                if (ch->axes(Qt::Horizontal).contains(axis))
                    axis->setTitleText(settings.showAxisTitles ? "Simulated" : "");
                else
                    axis->setTitleText(settings.showAxisTitles ? "Measured" : "");
            }

            for (QAbstractSeries *s : ch->series()) {
                if (auto ss = qobject_cast<QScatterSeries*>(s))
                    ss->setMarkerSize(settings.markerSize);
            }

            // Update strip label and stats label fonts
            int stripFontPx = qMax(8, settings.titleFontSize);
            int panelW = cv->width();
            int statsFontPx = qBound(8, panelW / 28, 13);
            if (settings.axisTickFontSize != 9)
                statsFontPx = qBound(7, settings.axisTickFontSize, 14);
            if (cv->parentWidget()) {
                for (QLabel *lbl : cv->parentWidget()->findChildren<QLabel*>()) {
                    if (lbl->parent() == cv->parentWidget()) { // strip label
                        lbl->setStyleSheet(QString(
                            "QLabel { background-color: #e8e8e8; border: 1px solid #cccccc; "
                            "font-weight: bold; font-size: %1px; padding: 3px 0px; }")
                            .arg(stripFontPx));
                    }
                }
            }
            // Stats label is a child of cv
            for (QLabel *lbl : cv->findChildren<QLabel*>()) {
                if (lbl->parent() == cv) {
                    lbl->setStyleSheet(QString(
                        "QLabel { background: rgba(255,255,255,210); font-size: %1pt; "
                        "padding: 2px 4px; border: none; }").arg(statsFontPx));
                    lbl->adjustSize();
                }
            }
        }

        // Update experiment legend font
        if (m_legendWidget) {
            QFont legendFont(settings.fontFamily, settings.legendFontSize);
            for (QLabel *lbl : m_legendWidget->findChildren<QLabel*>())
                lbl->setFont(legendFont);
        }
    }

    // Update internal settings
    m_showGrid = settings.showGrid;
    m_showLegend = settings.showLegend;

    // Update internal plot settings
    // Note: error bar change detection is handled in onSettingsButtonClicked()
    // before m_plotSettings is overwritten
    m_plotSettings = settings;

    qDebug() << "PlotWidget: Plot settings applied successfully";
}

void PlotWidget::saveSettings() const
{
    QSettings s("DSSAT", "GB2");
    s.beginGroup("PlotSettings");

    // Grid
    s.setValue("showGrid",       m_plotSettings.showGrid);
    s.setValue("showMinorGrid",  m_plotSettings.showMinorGrid);
    s.setValue("minorTickCount", m_plotSettings.minorTickCount);

    // Legend
    s.setValue("showLegend", m_plotSettings.showLegend);

    // Error bars
    s.setValue("showErrorBars", m_plotSettings.showErrorBars);
    s.setValue("errorBarType",  m_plotSettings.errorBarType);

    // Lines & markers
    s.setValue("lineWidth",  m_plotSettings.lineWidth);
    s.setValue("markerSize", m_plotSettings.markerSize);

    // Axes
    s.setValue("showAxisLabels",   m_plotSettings.showAxisLabels);
    s.setValue("showAxisTitles",   m_plotSettings.showAxisTitles);
    s.setValue("xAxisTitle",       m_plotSettings.xAxisTitle);
    s.setValue("yAxisTitle",       m_plotSettings.yAxisTitle);
    s.setValue("xAxisTickCount",   m_plotSettings.xAxisTickCount);
    s.setValue("xAxisTickSpacing", m_plotSettings.xAxisTickSpacing);

    // Appearance
    s.setValue("plotTitle",        m_plotSettings.plotTitle);
    s.setValue("backgroundColor",  m_plotSettings.backgroundColor.name());
    s.setValue("plotAreaColor",    m_plotSettings.plotAreaColor.name());

    // Export
    s.setValue("exportWidth",  m_plotSettings.exportWidth);
    s.setValue("exportHeight", m_plotSettings.exportHeight);
    s.setValue("exportDpi",    m_plotSettings.exportDpi);

    // Fonts
    s.setValue("fontFamily",        m_plotSettings.fontFamily);
    s.setValue("titleFontSize",     m_plotSettings.titleFontSize);
    s.setValue("axisLabelFontSize", m_plotSettings.axisLabelFontSize);
    s.setValue("axisTickFontSize",  m_plotSettings.axisTickFontSize);
    s.setValue("legendFontSize",    m_plotSettings.legendFontSize);
    s.setValue("boldTitle",         m_plotSettings.boldTitle);
    s.setValue("boldAxisLabels",    m_plotSettings.boldAxisLabels);

    s.endGroup();
}

void PlotWidget::loadSettings()
{
    QSettings s("DSSAT", "GB2");
    if (!s.childGroups().contains("PlotSettings")) return;   // nothing saved yet

    s.beginGroup("PlotSettings");

    m_plotSettings.showGrid       = s.value("showGrid",       m_plotSettings.showGrid).toBool();
    m_plotSettings.showMinorGrid  = s.value("showMinorGrid",  m_plotSettings.showMinorGrid).toBool();
    m_plotSettings.minorTickCount = s.value("minorTickCount", m_plotSettings.minorTickCount).toInt();

    m_plotSettings.showLegend = s.value("showLegend", m_plotSettings.showLegend).toBool();

    m_plotSettings.showErrorBars = s.value("showErrorBars", m_plotSettings.showErrorBars).toBool();
    m_plotSettings.errorBarType  = s.value("errorBarType",  m_plotSettings.errorBarType).toString();

    m_plotSettings.lineWidth  = s.value("lineWidth",  m_plotSettings.lineWidth).toInt();
    m_plotSettings.markerSize = s.value("markerSize", m_plotSettings.markerSize).toInt();

    m_plotSettings.showAxisLabels   = s.value("showAxisLabels",   m_plotSettings.showAxisLabels).toBool();
    m_plotSettings.showAxisTitles   = s.value("showAxisTitles",   m_plotSettings.showAxisTitles).toBool();
    m_plotSettings.xAxisTitle       = s.value("xAxisTitle",       m_plotSettings.xAxisTitle).toString();
    m_plotSettings.yAxisTitle       = s.value("yAxisTitle",       m_plotSettings.yAxisTitle).toString();
    m_plotSettings.xAxisTickCount   = s.value("xAxisTickCount",   m_plotSettings.xAxisTickCount).toInt();
    m_plotSettings.xAxisTickSpacing = s.value("xAxisTickSpacing", m_plotSettings.xAxisTickSpacing).toDouble();

    m_plotSettings.plotTitle     = s.value("plotTitle",     m_plotSettings.plotTitle).toString();
    m_plotSettings.backgroundColor = QColor(s.value("backgroundColor", m_plotSettings.backgroundColor.name()).toString());
    m_plotSettings.plotAreaColor   = QColor(s.value("plotAreaColor",   m_plotSettings.plotAreaColor.name()).toString());

    m_plotSettings.exportWidth  = s.value("exportWidth",  m_plotSettings.exportWidth).toInt();
    m_plotSettings.exportHeight = s.value("exportHeight", m_plotSettings.exportHeight).toInt();
    m_plotSettings.exportDpi    = s.value("exportDpi",    m_plotSettings.exportDpi).toInt();

    m_plotSettings.fontFamily        = s.value("fontFamily",        m_plotSettings.fontFamily).toString();
    m_plotSettings.titleFontSize     = s.value("titleFontSize",     m_plotSettings.titleFontSize).toInt();
    m_plotSettings.axisLabelFontSize = s.value("axisLabelFontSize", m_plotSettings.axisLabelFontSize).toInt();
    m_plotSettings.axisTickFontSize  = s.value("axisTickFontSize",  m_plotSettings.axisTickFontSize).toInt();
    m_plotSettings.legendFontSize    = s.value("legendFontSize",    m_plotSettings.legendFontSize).toInt();
    m_plotSettings.boldTitle         = s.value("boldTitle",         m_plotSettings.boldTitle).toBool();
    m_plotSettings.boldAxisLabels    = s.value("boldAxisLabels",    m_plotSettings.boldAxisLabels).toBool();

    s.endGroup();

    // Apply loaded settings to the chart appearance
    applyPlotSettings(m_plotSettings);
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
    const QStringList &varNames)
{
    qDebug() << "PlotWidget::plotScatter() - ENTRY, vars:" << varNames;
    m_isScatterMode = true;
    m_scatterExportData.clear();
    setXAxisButtonsVisible(false);

    // Cap at 9 variables
    QStringList vars = varNames.mid(0, 9);
    if (vars.isEmpty()) {
        qWarning() << "PlotWidget::plotScatter() - No variables provided";
        return;
    }
    if (varNames.size() > 9)
        emit errorOccurred(QString("Showing first 9 of %1 selected variables (maximum is 9).")
                           .arg(varNames.size()));

    if (evaluateData.rowCount == 0) {
        qWarning() << "PlotWidget::plotScatter() - No data";
        return;
    }

    // --- Gather experiment colors from EXCODE (last 4 chars) ---
    const DataColumn *excodeCol = evaluateData.getColumn("EXCODE");
    const DataColumn *crCol     = evaluateData.getColumn("CR");

    // Build experiment label map: full EXCODE -> display label (use full EXCODE directly)
    QMap<QString, QString> fullToShort; // full EXCODE -> display label
    {
        for (int i = 0; i < evaluateData.rowCount; ++i) {
            QString raw = excodeCol ? excodeCol->data.value(i).toString().trimmed() : QString();
            if (raw.isEmpty()) raw = "?";
            fullToShort[raw] = raw;
        }
    }

    QStringList expOrder;
    QMap<int, QString> rowExp; // row index -> display label
    for (int i = 0; i < evaluateData.rowCount; ++i) {
        QString raw = excodeCol ? excodeCol->data.value(i).toString().trimmed() : QString();
        if (raw.isEmpty()) raw = "?";
        QString label = fullToShort.value(raw, raw);
        rowExp[i] = label;
        if (!expOrder.contains(label)) expOrder.append(label);
    }
    std::sort(expOrder.begin(), expOrder.end(), [](const QString &a, const QString &b) {
        bool okA, okB;
        int na = a.toInt(&okA), nb = b.toInt(&okB);
        if (okA && okB) return na < nb;
        return a < b;
    });

    // Okabe-Ito colorblind-safe palette (high contrast, distinct shapes of color)
    static const QVector<QColor> kPalette = {
        QColor("#E69F00"),  // orange
        QColor("#56B4E9"),  // sky blue
        QColor("#009E73"),  // bluish green
        QColor("#CC79A7"),  // reddish purple
        QColor("#0072B2"),  // blue
        QColor("#D55E00"),  // vermillion
        QColor("#F0E442"),  // yellow
        QColor("#000000"),  // black
    };
    QMap<QString, QColor> expColor;
    for (int ei = 0; ei < expOrder.size(); ++ei)
        expColor[expOrder[ei]] = kPalette[ei % kPalette.size()];

    // --- Nice-axis helper (reused per panel) ---
    auto niceAxis = [](double dataMin, double dataMax,
                       double &outMin, double &outMax, int &outTicks, QString &outFmt) {
        double range = dataMax - dataMin;
        double pad = (range > 0) ? range * 0.05 : 1.0;
        if (range < 1.0) pad = qMax(pad, 0.5 * range + 0.5);
        dataMin -= pad;  dataMax += pad;
        if (dataMax <= dataMin) dataMax = dataMin + 1.0;
        double rawRange = dataMax - dataMin;
        double roughStep = rawRange / 5.0;
        if (roughStep <= 0) roughStep = 1.0;
        double mag  = qPow(10.0, qFloor(std::log10(roughStep)));
        double norm = roughStep / mag;
        double step = (norm < 7.5) ? 5.0 * mag : 10.0 * mag;
        if (step < 1e-10) step = 1.0;
        outMin  = qFloor(dataMin / step) * step;
        outMax  = qCeil (dataMax / step) * step;
        if (outMax <= outMin) outMax = outMin + step;
        outTicks = qRound((outMax - outMin) / step) + 1;
        outFmt   = (step < 1.0) ? QString("%.2g") : QString("%.0f");
    };

    // --- Build / reset scatter panel area ---
    // Hide the regular chart view; show scatter panels instead
    if (m_chartView) m_chartView->setVisible(false);
    if (m_bottomContainer) m_bottomContainer->setVisible(false);

    // Destroy old panels
    for (QChartView *cv : m_scatterPanelViews) { cv->deleteLater(); }
    m_scatterPanelViews.clear();

    if (!m_scatterScrollArea) {
        m_scatterScrollArea = new QScrollArea();
        m_scatterScrollArea->setWidgetResizable(false);
        m_scatterScrollArea->setFrameShape(QFrame::NoFrame);
        m_scatterScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scatterScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scatterScrollArea->setWidgetResizable(true);

        // Centering wrapper: scroll area shows this; panel container is centered inside it
        QWidget *centerWrapper = new QWidget();
        QVBoxLayout *wrapV = new QVBoxLayout(centerWrapper);
        wrapV->setContentsMargins(0, 0, 0, 0);
        QHBoxLayout *wrapH = new QHBoxLayout();
        wrapH->setContentsMargins(0, 0, 0, 0);

        m_scatterPanelContainer = new QWidget();
        m_scatterPanelContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_scatterPanelGrid = new QGridLayout(m_scatterPanelContainer);
        m_scatterPanelGrid->setSpacing(6);
        m_scatterPanelGrid->setContentsMargins(6, 6, 6, 6);

        wrapH->addStretch(1);
        wrapH->addWidget(m_scatterPanelContainer);
        wrapH->addStretch(1);
        wrapV->addStretch(1);
        wrapV->addLayout(wrapH);
        wrapV->addStretch(1);

        m_scatterScrollArea->setWidget(centerWrapper);
        m_leftLayout->insertWidget(0, m_scatterScrollArea, 1);
    }

    // Remove old items from grid
    while (m_scatterPanelGrid->count()) {
        QLayoutItem *item = m_scatterPanelGrid->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (m_scatterScrollArea) m_scatterScrollArea->setVisible(true);
    m_scatterPanelContainer->setVisible(true);

    // --- Determine grid layout ---
    int n     = vars.size();
    int nCols, nRows;
    if      (n == 1) { nCols = 1; nRows = 1; }
    else if (n == 2) { nCols = 2; nRows = 1; }
    else if (n == 3) { nCols = 2; nRows = 2; } // 2×2 with one empty cell
    else if (n == 4) { nCols = 2; nRows = 2; }
    else if (n <= 6) { nCols = 3; nRows = 2; }
    else             { nCols = 3; nRows = 3; }

    m_scatterNCols = nCols;
    m_scatterNRows = nRows;

    // Equal column stretch only — height controlled explicitly
    for (int c = 0; c < nCols; ++c) m_scatterPanelGrid->setColumnStretch(c, 1);

    // Initial size: use a reasonable default; resizeScatterPanels() will correct it
    // once the widget is actually laid out (deferred via QTimer below)
    int panelSize = 300;

    // --- Build one panel per variable ---
    QVector<QMap<QString, QVariant>> allMetrics;

    for (int vi = 0; vi < vars.size(); ++vi) {
        QString baseVar = vars[vi].toUpper();
        QString simVar  = baseVar + "S";
        QString measVar = baseVar + "M";

        // Case-insensitive column lookup
        auto findCol = [&](const QString &name) -> const DataColumn* {
            for (const QString &cn : evaluateData.columnNames)
                if (cn.compare(name, Qt::CaseInsensitive) == 0)
                    return evaluateData.getColumn(cn);
            return nullptr;
        };
        const DataColumn *simCol  = findCol(simVar);
        const DataColumn *measCol = findCol(measVar);
        if (!simCol || !measCol) {
            qDebug() << "plotScatter: skipping" << baseVar << "- missing columns";
            continue;
        }

        // Collect points per experiment
        // key = experiment label; value = list of (sim, meas) points
        // X = simulated, Y = measured  (matching R plot)
        QMap<QString, QVector<QPointF>> expPoints;
        for (int i = 0; i < evaluateData.rowCount; ++i) {
            QVariant sv = simCol->data.value(i);
            QVariant mv = measCol->data.value(i);
            if (DataProcessor::isMissingValue(sv) || DataProcessor::isMissingValue(mv)) continue;
            bool okS, okM;
            double s = DataProcessor::toDouble(sv, &okS);
            double m = DataProcessor::toDouble(mv, &okM);
            if (!okS || !okM) continue;
            if (s < 0 || m < 0) continue; // treat negatives as missing (like R code)
            expPoints[rowExp[i]].append(QPointF(s, m));
        }
        if (expPoints.isEmpty()) continue;

        // Store for CSV export
        for (auto it = expPoints.begin(); it != expPoints.end(); ++it)
            m_scatterExportData[baseVar + "::" + it.key()] = it.value();

        // Compute overall range across all points for shared axis
        double xMin = std::numeric_limits<double>::max();
        double xMax = std::numeric_limits<double>::lowest();
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        int totalPts = 0;
        for (const auto &pts : expPoints) {
            for (const QPointF &p : pts) {
                xMin = qMin(xMin, p.x()); xMax = qMax(xMax, p.x());
                yMin = qMin(yMin, p.y()); yMax = qMax(yMax, p.y());
                ++totalPts;
            }
        }
        if (totalPts == 0) continue;

        // Use combined range for both axes (free per variable, but X=Y scale for 1:1 line)
        double combinedMin = qMin(xMin, yMin);
        double combinedMax = qMax(xMax, yMax);
        double axMin, axMax;
        int    axTicks;
        QString axFmt;
        niceAxis(combinedMin, combinedMax, axMin, axMax, axTicks, axFmt);

        // Compute full metrics over all points (all experiments combined)
        QVector<double> simVals, measVals;
        for (const auto &pts : expPoints)
            for (const QPointF &p : pts) { simVals.append(p.x()); measVals.append(p.y()); }
        QVariantMap fullMetrics = MetricsCalculator::calculateMetrics(simVals, measVals, 1);
        double rmse = fullMetrics.value("RMSE", 0.0).toDouble();
        double r2   = MetricsCalculator::rSquared(simVals, measVals);

        // Build panel chart — honour m_plotSettings where applicable
        QChart *chart = new QChart();
        chart->setBackgroundBrush(QBrush(m_plotSettings.backgroundColor));
        chart->setPlotAreaBackgroundBrush(QBrush(m_plotSettings.plotAreaColor));
        chart->setPlotAreaBackgroundVisible(true);
        chart->legend()->setVisible(false);
        chart->setMargins(QMargins(2, 0, 2, 2)); // top=0: no gap below strip label
        chart->setTitle(""); // empty title so Qt Charts doesn't reserve title height

        // 1:1 reference line
        QLineSeries *refLine = new QLineSeries();
        QPen refPen(QColor("#666666"), 1, Qt::DashLine);
        refLine->setPen(refPen);
        refLine->append(axMin, axMin);
        refLine->append(axMax, axMax);
        chart->addSeries(refLine);

        // One scatter series per experiment
        for (const QString &expLabel : expOrder) {
            if (!expPoints.contains(expLabel)) continue;
            QScatterSeries *ss = new QScatterSeries();
            ss->setName(expLabel);
            QColor c = expColor.value(expLabel, Qt::gray);
            ss->setColor(c);
            ss->setBorderColor(c.darker(120));
            ss->setMarkerSize(m_plotSettings.markerSize);
            ss->setMarkerShape(QScatterSeries::MarkerShapeCircle);
            ss->setUseOpenGL(false);
            for (const QPointF &p : expPoints[expLabel]) ss->append(p);
            chart->addSeries(ss);
        }

        // Axes — use settings for fonts, grid, tick count
        QValueAxis *xAx = new QValueAxis();
        QValueAxis *yAx = new QValueAxis();
        int tickCount = (m_plotSettings.xAxisTickCount > 0)
            ? qMin(m_plotSettings.xAxisTickCount, axTicks)
            : qMin(axTicks, 6);
        xAx->setRange(axMin, axMax); xAx->setTickCount(tickCount);
        yAx->setRange(axMin, axMax); yAx->setTickCount(tickCount);
        xAx->setLabelFormat(axFmt);  yAx->setLabelFormat(axFmt);
        xAx->setTitleText(m_plotSettings.showAxisTitles ? "Simulated" : "");
        yAx->setTitleText(m_plotSettings.showAxisTitles ? "Measured"  : "");
        QFont axFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
        xAx->setLabelsFont(axFont);  yAx->setLabelsFont(axFont);
        xAx->setLabelsVisible(m_plotSettings.showAxisLabels);
        yAx->setLabelsVisible(m_plotSettings.showAxisLabels);
        QFont axTitleFont(m_plotSettings.fontFamily, m_plotSettings.axisLabelFontSize);
        axTitleFont.setBold(m_plotSettings.boldAxisLabels);
        xAx->setTitleFont(axTitleFont); yAx->setTitleFont(axTitleFont);
        xAx->setGridLineVisible(m_plotSettings.showGrid);
        yAx->setGridLineVisible(m_plotSettings.showGrid);
        xAx->setMinorTickCount(m_plotSettings.showMinorGrid ? m_plotSettings.minorTickCount : 0);
        yAx->setMinorTickCount(m_plotSettings.showMinorGrid ? m_plotSettings.minorTickCount : 0);
        xAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
        yAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
        chart->addAxis(xAx, Qt::AlignBottom);
        chart->addAxis(yAx, Qt::AlignLeft);
        for (QAbstractSeries *s : chart->series()) {
            static_cast<QXYSeries*>(s)->attachAxis(xAx);
            static_cast<QXYSeries*>(s)->attachAxis(yAx);
        }

        // Chart view — wrap in a container with a strip title label at the top
        QWidget *panelWidget = new QWidget();
        panelWidget->setFixedSize(panelSize, panelSize);
        panelWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        QVBoxLayout *panelLayout = new QVBoxLayout(panelWidget);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->setSpacing(0);

        QLabel *stripLabel = new QLabel(baseVar);
        stripLabel->setAlignment(Qt::AlignCenter);
        stripLabel->setStyleSheet(QString(
            "QLabel { background-color: #e8e8e8; border-bottom: 1px solid #cccccc; "
            "font-weight: bold; font-size: %1px; padding: 2px 0px; }")
            .arg(m_plotSettings.titleFontSize > 0 ? m_plotSettings.titleFontSize : 10));
        stripLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        panelLayout->addWidget(stripLabel);

        QChartView *cv = new QChartView(chart);
        cv->setRenderHint(QPainter::Antialiasing);
        cv->setFrameShape(QFrame::NoFrame);  // remove border gap between strip and chart
        cv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        panelLayout->addWidget(cv, 1);

        // RMSE / R² overlay label — repositioned on every resize via event filter
        QString statsText = QString("RMSE = %1\nR² = %2")
            .arg(rmse, 0, 'f', rmse < 1 ? 3 : (rmse < 100 ? 2 : 1))
            .arg(r2,   0, 'f', 2);
        QLabel *statsLabel = new QLabel(statsText, cv);
        // Scale font with panel size: ~9pt at 200px, ~11pt at 300px, ~13pt at 400px
        int statsFontPt = qBound(8, panelSize / 28, 13);
        // Let user's axisTickFontSize override if explicitly set (non-default)
        if (m_plotSettings.axisTickFontSize != 9)
            statsFontPt = qBound(7, m_plotSettings.axisTickFontSize, 14);
        statsLabel->setStyleSheet(QString(
            "QLabel { background: rgba(255,255,255,210); font-size: %1pt; "
            "padding: 2px 4px; border: none; }").arg(statsFontPt));
        statsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        statsLabel->adjustSize();
        statsLabel->raise();
        statsLabel->show();

        // Install event filter on the chart view to reposition label whenever it resizes
        QChartView *cvRef = cv;
        QLabel *labelRef = statsLabel;
        struct Repositioner : public QObject {
            QChartView *view;
            QLabel     *label;
            Repositioner(QChartView *v, QLabel *l, QObject *parent)
                : QObject(parent), view(v), label(l) {}
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Resize || e->type() == QEvent::Paint) {
                    QRectF pa = view->chart()->plotArea();
                    if (pa.isValid() && pa.width() > 10) {
                        label->move(
                            static_cast<int>(pa.left()) + 4,
                            static_cast<int>(pa.top())  + 4);
                        label->raise();
                    }
                }
                return false; // never consume the event
            }
        };
        auto *repo = new Repositioner(cvRef, labelRef, cvRef);
        cvRef->installEventFilter(repo);

        int row = vi / nCols;
        int col = vi % nCols;
        m_scatterPanelGrid->addWidget(panelWidget, row, col);
        m_scatterPanelViews.append(cv);

        // Collect full metrics for dialog
        QMap<QString, QVariant> mmap;
        mmap["Variable"]          = baseVar;
        mmap["N"]                 = totalPts;
        mmap["R²"]                = r2;
        mmap["RMSE"]              = rmse;
        mmap["Willmott's d-stat"] = fullMetrics.value("Willmott's d-stat");
        mmap["BIAS"]              = fullMetrics.value("BIAS");
        // Store MSEs/MSEu as proportion of total MSE (0–1) so display is unitless
        double mseTotal = rmse * rmse;
        double mseSraw  = fullMetrics.value("MSEs", 0.0).toDouble();
        double mseUraw  = fullMetrics.value("MSEu", 0.0).toDouble();
        mmap["MSEs"] = (mseTotal > 0) ? mseSraw / mseTotal : QVariant(0.0);
        mmap["MSEu"] = (mseTotal > 0) ? mseUraw / mseTotal : QVariant(0.0);
        // Crop from CR column (first non-empty value for this variable's rows)
        QString cropCode;
        if (crCol) {
            for (int i = 0; i < evaluateData.rowCount; ++i) {
                QString cr = crCol->data.value(i).toString().trimmed();
                if (!cr.isEmpty()) { cropCode = cr; break; }
            }
        }
        mmap["Crop"]       = cropCode.isEmpty() ? QVariant("NA") : QVariant(cropCode);
        mmap["Experiment"] = expOrder.size() == 1 ? QVariant(expOrder.first()) : QVariant("All");
        allMetrics.append(mmap);
    }

    // Defer panel resize until after the layout is complete and widget has real dimensions
    QTimer::singleShot(0, this, [this]() { resizeScatterPanels(); });

    // --- Build legend in right panel (hide if only one experiment — redundant) ---
    clearLegend();
    bool showScatterLegend = (expOrder.size() > 1);
    if (m_legendStack) {
        m_legendStack->setCurrentIndex(1);
        m_legendStack->setVisible(showScatterLegend);
    }
    if (m_legendScrollArea) m_legendScrollArea->setVisible(showScatterLegend);

    emit metricsCalculated(allMetrics);
    emit plotUpdated();

    if (!showScatterLegend) return; // single experiment — legend not needed

    // Title
    QLabel *legendTitle = new QLabel("Experiment");
    legendTitle->setStyleSheet("font-weight: bold; font-size: 10px; padding: 4px 0px 2px 0px;");
    m_legendLayout->addWidget(legendTitle);

    for (const QString &expLabel : expOrder) {
        QWidget *row = new QWidget();
        QHBoxLayout *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 1, 0, 1);
        hl->setSpacing(4);

        QLabel *swatch = new QLabel();
        swatch->setFixedSize(12, 12);
        QColor c = expColor.value(expLabel, Qt::gray);
        swatch->setStyleSheet(QString("background-color: %1; border-radius: 6px;").arg(c.name()));
        QLabel *txt = new QLabel(expLabel);
        txt->setStyleSheet("font-size: 10px;");

        hl->addWidget(swatch);
        hl->addWidget(txt);
        hl->addStretch();
        m_legendLayout->addWidget(row);
    }
}


