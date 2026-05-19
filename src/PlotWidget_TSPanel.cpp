#include "PlotWidget.h"
#include "DataProcessor.h"
#include <QTimer>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QDebug>
#include <limits>

// ---------------------------------------------------------------------------
// resizeTimeSeriesPanels — called deferred after layout; sizes panels to fill
// the available scroll-area viewport with a landscape (3:2) aspect ratio.
// ---------------------------------------------------------------------------
void PlotWidget::resizeTimeSeriesPanels()
{
    if (!m_tsPanelContainer || !m_tsPanelGrid) return;
    if (m_tsPanelGrid->count() == 0) return;

    int nCols   = m_tsNCols;
    int nRows   = m_tsNRows;
    if (nCols < 1) nCols = 1;
    int spacing = m_tsPanelGrid->spacing();
    int margins = 12;

    int availW = m_tsScrollArea ? m_tsScrollArea->viewport()->width()
                                : m_leftContainer->width();
    int availH = m_tsScrollArea ? m_tsScrollArea->viewport()->height()
                                : m_leftContainer->height();
    if (availW < 50) availW = width() - 150;
    if (availH < 50) availH = height() - 20;
    if (availW < 300) availW = 800;
    if (availH < 200) availH = 600;

    // Landscape panels: divide available space by visible rows only (max 3 rows shown at once).
    // Extra rows scroll — this prevents panels from shrinking when many variables are selected.
    int visibleRows = qMin(nRows, 3);
    int panelW = (availW - margins - spacing * (nCols - 1)) / nCols;
    int panelH = (availH - margins - spacing * (visibleRows - 1)) / visibleRows;

    // Enforce a minimum and a 3:2 ratio (width:height)
    panelW = qMax(panelW, 260);
    panelH = qMax(panelH, 160);

    // When only one column, don't make panels unnecessarily tall
    if (nCols == 1)
        panelH = qMin(panelH, panelW * 2 / 3);

    for (int i = 0; i < m_tsPanelGrid->count(); ++i) {
        QLayoutItem *item = m_tsPanelGrid->itemAt(i);
        if (!item || !item->widget()) continue;
        item->widget()->setFixedSize(panelW, panelH);
    }
    int cW = panelW * nCols + spacing * (nCols - 1) + margins;
    int cH = panelH * nRows + spacing * (nRows - 1) + margins;
    m_tsPanelContainer->setFixedSize(cW, cH);
}

// ---------------------------------------------------------------------------
// plotTimeSeriesMultiPanel — builds one panel per Y variable from m_plotDataList.
// When the setting is off or fewer than 2 variables are plotted, reverts to the
// normal single-chart view.
// ---------------------------------------------------------------------------
void PlotWidget::plotTimeSeriesMultiPanel()
{
    bool wantPanel = m_plotSettings.multiPanelTimeSeries
                     && m_currentYVars.size() >= 2
                     && !m_plotDataList.isEmpty();

    // --- Revert to single-chart mode ---
    if (!wantPanel) {
        if (m_tsScrollArea)    m_tsScrollArea->setVisible(false);
        if (m_chartView)       m_chartView->setVisible(true);
        if (m_bottomContainer) m_bottomContainer->setVisible(true);
        return;
    }

    // --- Hide single chart, show TS panel area ---
    if (m_chartView)       m_chartView->setVisible(false);
    if (m_bottomContainer) m_bottomContainer->setVisible(true); // keep DAS/DAP/Date buttons

    // Destroy old panel chart views
    for (QChartView *cv : m_tsPanelViews) cv->deleteLater();
    m_tsPanelViews.clear();

    // Create scroll area + container/grid on first use
    if (!m_tsScrollArea) {
        m_tsScrollArea = new QScrollArea();
        m_tsScrollArea->setWidgetResizable(false);
        m_tsScrollArea->setFrameShape(QFrame::NoFrame);
        m_tsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_tsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_tsScrollArea->setWidgetResizable(true);

        QWidget *centerWrapper = new QWidget();
        QVBoxLayout *wrapV = new QVBoxLayout(centerWrapper);
        wrapV->setContentsMargins(0, 0, 0, 0);
        QHBoxLayout *wrapH = new QHBoxLayout();
        wrapH->setContentsMargins(0, 0, 0, 0);

        m_tsPanelContainer = new QWidget();
        m_tsPanelContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_tsPanelGrid = new QGridLayout(m_tsPanelContainer);
        m_tsPanelGrid->setSpacing(6);
        m_tsPanelGrid->setContentsMargins(6, 6, 6, 6);

        wrapH->addStretch(1);
        wrapH->addWidget(m_tsPanelContainer);
        wrapH->addStretch(1);
        wrapV->addStretch(1);
        wrapV->addLayout(wrapH);
        wrapV->addStretch(1);

        m_tsScrollArea->setWidget(centerWrapper);
        m_leftLayout->insertWidget(0, m_tsScrollArea, 1);
    }

    // Clear previous grid items
    while (m_tsPanelGrid->count()) {
        QLayoutItem *item = m_tsPanelGrid->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_tsScrollArea->setVisible(true);
    m_tsPanelContainer->setVisible(true);

    // --- Collect unique variables in plot order ---
    QStringList varOrder;
    for (const auto &pd : m_plotDataList) {
        if (!varOrder.contains(pd->variable))
            varOrder.append(pd->variable);
    }
    int n = varOrder.size();

    // Grid layout: prefer vertical stack for ≤3 vars; 2 columns for 4+
    int nCols, nRows;
    if      (n <= 3) { nCols = 1; nRows = n; }
    else if (n <= 6) { nCols = 2; nRows = (n + 1) / 2; }
    else             { nCols = 2; nRows = (n + 1) / 2; }
    m_tsNCols = nCols;
    m_tsNRows = nRows;

    for (int c = 0; c < nCols; ++c) m_tsPanelGrid->setColumnStretch(c, 1);

    // --- Global X range for consistent initial view across panels ---
    double globalXMin = std::numeric_limits<double>::max();
    double globalXMax = std::numeric_limits<double>::lowest();
    for (const auto &pd : m_plotDataList) {
        for (const QPointF &pt : pd->points) {
            globalXMin = qMin(globalXMin, pt.x());
            globalXMax = qMax(globalXMax, pt.x());
        }
    }
    if (globalXMin >= globalXMax) { globalXMax = globalXMin + 1.0; }
    double dataXMin = globalXMin; // pre-pad, used to avoid negative axis start
    double xPad = (globalXMax - globalXMin) * 0.02;
    globalXMin -= xPad;
    globalXMax += xPad;

    bool isDateAxis = (m_currentXVar == "DATE");

    // --- Build one panel per variable ---
    QVector<QAbstractAxis*> allXAxes; // for cross-panel X-sync

    for (int vi = 0; vi < varOrder.size(); ++vi) {
        const QString &varCode = varOrder[vi];

        // Collect PlotData items for this variable
        QVector<QSharedPointer<PlotData>> varData;
        for (const auto &pd : m_plotDataList) {
            if (pd->variable == varCode) varData.append(pd);
        }
        if (varData.isEmpty()) continue;

        // Y data range for this variable (Y always starts from 0 like main chart)
        double yDataMax = std::numeric_limits<double>::lowest();
        for (const auto &pd : varData) {
            for (const QPointF &pt : pd->points)
                yDataMax = qMax(yDataMax, pt.y());
        }
        if (yDataMax <= 0.0) yDataMax = 1.0;

        // --- Build QChart ---
        QChart *chart = new QChart();
        chart->setBackgroundBrush(QBrush(m_plotSettings.backgroundColor));
        chart->setPlotAreaBackgroundBrush(QBrush(m_plotSettings.plotAreaColor));
        chart->setPlotAreaBackgroundVisible(true);
        chart->legend()->setVisible(false);
        chart->setMargins(QMargins(8, 0, 4, 4));
        chart->setTitle("");

        // Add series from PlotData
        for (const auto &pd : varData) {
            if (pd->isObserved) {
                QScatterSeries *ss = new QScatterSeries();
                ss->setColor(pd->color);
                ss->setBorderColor(pd->color.darker(120));
                ss->setMarkerSize(m_plotSettings.markerSize);
                ss->setMarkerShape(getMarkerShape(pd->symbol));
                ss->setUseOpenGL(false);
                for (const QPointF &pt : pd->points) ss->append(pt);
                chart->addSeries(ss);
            } else {
                QLineSeries *ls = new QLineSeries();
                // Force solid line — style variation is only needed in overlay mode
                QPen solidPen(pd->pen);
                solidPen.setStyle(Qt::SolidLine);
                ls->setPen(solidPen);
                for (const QPointF &pt : pd->points) ls->append(pt);
                chart->addSeries(ls);
            }
        }

        QFont tickFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
        QFont titleFont(m_plotSettings.fontFamily, m_plotSettings.axisLabelFontSize);
        titleFont.setBold(m_plotSettings.boldAxisLabels);

        // --- X axis ---
        QAbstractAxis *xAxis = nullptr;
        if (isDateAxis) {
            QDateTimeAxis *dtAx = new QDateTimeAxis();
            dtAx->setFormat("d MMM");
            dtAx->setMin(QDateTime::fromMSecsSinceEpoch((qint64)globalXMin));
            dtAx->setMax(QDateTime::fromMSecsSinceEpoch((qint64)globalXMax));
            dtAx->setTickCount(calculateOptimalDateTickCount());
            dtAx->setLabelsFont(tickFont);
            dtAx->setLabelsVisible(m_plotSettings.showAxisLabels);
            dtAx->setGridLineVisible(m_plotSettings.showGrid);
            dtAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
            chart->addAxis(dtAx, Qt::AlignBottom);
            // Set colors AFTER addAxis so Qt theme doesn't override
            dtAx->setLinePen(QPen(m_plotSettings.axisLineColor));
            dtAx->setLabelsBrush(QBrush(Qt::black));
            xAxis = dtAx;
        } else {
            QValueAxis *vAx = new QValueAxis();
            // Target ~5 ticks for compact panels — find a clean interval
            double xRange = globalXMax - globalXMin;
            double xInterval = calculateNiceXInterval(xRange);
            // Scale interval up until we get ≤6 ticks
            while (xRange / xInterval > 6.0) xInterval *= 2.0;
            double cleanXMin = std::floor(globalXMin / xInterval) * xInterval;
            // Don't go negative when actual data starts at 0
            if (dataXMin >= 0.0 && cleanXMin < 0.0) cleanXMin = 0.0;
            double cleanXMax = std::ceil(globalXMax / xInterval) * xInterval;
            int xTickCount = qRound((cleanXMax - cleanXMin) / xInterval) + 1;
            if (xTickCount < 3) xTickCount = 3;
            vAx->setRange(cleanXMin, cleanXMax);
            vAx->setTickCount(xTickCount);
            vAx->setLabelFormat("%.0f");
            vAx->setLabelsFont(tickFont);
            vAx->setLabelsVisible(m_plotSettings.showAxisLabels);
            vAx->setGridLineVisible(m_plotSettings.showGrid);
            vAx->setMinorTickCount(m_plotSettings.xAxisMinorTickCount);
            vAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
            QString xTitle = m_plotSettings.xAxisTitle.isEmpty() ? m_currentXVar : m_plotSettings.xAxisTitle;
            vAx->setTitleText(m_plotSettings.showAxisTitles ? xTitle : "");
            vAx->setTitleFont(titleFont);
            chart->addAxis(vAx, Qt::AlignBottom);
            vAx->setLinePen(QPen(m_plotSettings.axisLineColor));
            vAx->setLabelsBrush(QBrush(Qt::black));
            xAxis = vAx;
        }

        // --- Y axis: same nice-interval logic as autoFitAxes ---
        {
            double tickInterval = m_plotSettings.yAxisTickSpacing > 0.0
                                  ? m_plotSettings.yAxisTickSpacing
                                  : calculateNiceYInterval(yDataMax * 1.05);
            double alignedMax = std::ceil(yDataMax * 1.05 / tickInterval) * tickInterval;
            if (alignedMax <= yDataMax) alignedMax += tickInterval;
            int numIntervals = qRound(alignedMax / tickInterval);
            if (numIntervals < 2) numIntervals = 2;
            if (numIntervals > 5) numIntervals = 5; // cap at 5 intervals in compact panels
            int yTickCount = numIntervals + 1;

            // Auto decimal format matching main chart logic
            int autoDecimals = 0;
            if (m_plotSettings.yAxisDecimals >= 0) {
                autoDecimals = m_plotSettings.yAxisDecimals;
            } else if (alignedMax < 100.0) {
                if      (tickInterval < 0.01)  autoDecimals = 4;
                else if (tickInterval < 0.1)   autoDecimals = 3;
                else if (tickInterval < 1.0)   autoDecimals = 2;
                else if (tickInterval < 10.0)  autoDecimals = 1;
            }

            QValueAxis *yAx = new QValueAxis();
            yAx->setRange(0, alignedMax);
            yAx->setTickCount(yTickCount);
            yAx->setMinorTickCount(m_plotSettings.yAxisMinorTickCount);
            yAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
            yAx->setLabelFormat(QString("%.%1f").arg(autoDecimals));
            yAx->setLabelsFont(tickFont);
            yAx->setLabelsVisible(m_plotSettings.showAxisLabels);
            yAx->setGridLineVisible(m_plotSettings.showGrid);
            yAx->setTitleFont(titleFont);
            chart->addAxis(yAx, Qt::AlignLeft);
            yAx->setLinePen(QPen(m_plotSettings.axisLineColor));
            yAx->setLabelsBrush(QBrush(Qt::black));

            // Attach all series to both axes
            for (QAbstractSeries *s : chart->series()) {
                if (auto *xy = qobject_cast<QXYSeries*>(s)) {
                    xy->attachAxis(xAxis);
                    xy->attachAxis(yAx);
                }
            }
        }

        allXAxes.append(xAxis);

        // --- Panel widget: strip label + chart view ---
        QWidget *panelWidget = new QWidget();
        panelWidget->setFixedSize(300, 180); // will be corrected by resize
        panelWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        QVBoxLayout *panelLayout = new QVBoxLayout(panelWidget);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->setSpacing(0);

        // Strip label with variable display name
        QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(varCode);
        QString labelText = varInfo.first.isEmpty() ? varCode : varInfo.first;

        QLabel *stripLabel = new QLabel(labelText);
        stripLabel->setAlignment(Qt::AlignCenter);
        int stripFontPx = m_plotSettings.titleFontSize > 0 ? m_plotSettings.titleFontSize : 10;
        stripLabel->setStyleSheet(QString(
            "QLabel { background-color: #e8e8e8; border-bottom: 1px solid #cccccc; "
            "font-weight: bold; font-size: %1px; padding: 2px 4px; }")
            .arg(stripFontPx));
        stripLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        panelLayout->addWidget(stripLabel);

        QChartView *cv = new QChartView(chart);
        cv->setRenderHint(QPainter::Antialiasing);
        cv->setFrameShape(QFrame::NoFrame);
        cv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        panelLayout->addWidget(cv, 1);

        int row = vi / nCols;
        int col = vi % nCols;
        m_tsPanelGrid->addWidget(panelWidget, row, col);
        m_tsPanelViews.append(cv);
    }

    // --- Cross-panel X-axis sync (DateTimeAxis) ---
    // When user zooms one panel, all others follow the same time range.
    if (isDateAxis && allXAxes.size() > 1) {
        static bool syncing = false;
        for (QAbstractAxis *ax : allXAxes) {
            auto *dtAx = qobject_cast<QDateTimeAxis*>(ax);
            if (!dtAx) continue;
            // Capture the full list for the lambda
            QVector<QAbstractAxis*> others = allXAxes;
            QObject::connect(dtAx, &QDateTimeAxis::rangeChanged,
                             dtAx,
                             [others, dtAx](const QDateTime &min, const QDateTime &max) {
                                 if (syncing) return;
                                 syncing = true;
                                 for (QAbstractAxis *o : others) {
                                     if (o == dtAx) continue;
                                     if (auto *d = qobject_cast<QDateTimeAxis*>(o))
                                         d->setRange(min, max);
                                 }
                                 syncing = false;
                             });
        }
    } else if (!isDateAxis && allXAxes.size() > 1) {
        static bool syncing = false;
        for (QAbstractAxis *ax : allXAxes) {
            auto *vAx = qobject_cast<QValueAxis*>(ax);
            if (!vAx) continue;
            QVector<QAbstractAxis*> others = allXAxes;
            QObject::connect(vAx, &QValueAxis::rangeChanged,
                             vAx,
                             [others, vAx](qreal min, qreal max) {
                                 if (syncing) return;
                                 syncing = true;
                                 for (QAbstractAxis *o : others) {
                                     if (o == vAx) continue;
                                     if (auto *v = qobject_cast<QValueAxis*>(o))
                                         v->setRange(min, max);
                                 }
                                 syncing = false;
                             });
        }
    }

    // Defer sizing until after layout
    QTimer::singleShot(0, this, [this]() { resizeTimeSeriesPanels(); });

    // --- Build flat treatment legend (one entry per treatment, no variable grouping) ---
    buildMultiPanelLegend();
}

void PlotWidget::buildMultiPanelLegend()
{
    if (!m_legendLayout || !m_legendStack || !m_legendScrollArea) return;

    clearLegend();
    m_legendStack->setCurrentIndex(1);
    m_legendStack->setVisible(true);
    m_legendScrollArea->setVisible(true);

    // Header
    QWidget *headerWidget = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 2, 0, 2);
    headerLayout->setSpacing(5);
    QLabel *obsH = new QLabel("<b>Obs.</b>");  obsH->setFixedWidth(30); obsH->setAlignment(Qt::AlignCenter);
    QLabel *simH = new QLabel("<b>Sim.</b>");  simH->setFixedWidth(30); simH->setAlignment(Qt::AlignCenter);
    QLabel *trtH = new QLabel("<b>Treatment</b>"); trtH->setAlignment(Qt::AlignLeft);
    headerLayout->addWidget(obsH);
    headerLayout->addWidget(simH);
    headerLayout->addWidget(trtH, 1);
    m_legendLayout->addWidget(headerWidget);

    QFrame *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Plain);
    m_legendLayout->addWidget(sep);

    // Collect one sim + one obs PlotData per treatment (from first variable that has it)
    // Key: same uniqueKey used in the main legend
    struct TrtEntry { QSharedPointer<PlotData> sim; QSharedPointer<PlotData> obs; };
    QMap<QString, TrtEntry> entries;
    QStringList entryOrder;

    for (const auto &pd : m_plotDataList) {
        QString expId  = pd->experiment.isEmpty() ? "default" : pd->experiment;
        QString cropId = pd->crop.isEmpty() ? "XX" : pd->crop;
        QString key    = QString("%1__TRT%2__EXP%3__CROP%4")
                         .arg(pd->treatmentName).arg(pd->treatment).arg(expId).arg(cropId);
        if (!entries.contains(key)) {
            entries[key] = TrtEntry{};
            entryOrder.append(key);
        }
        if (pd->isObserved)  entries[key].obs = pd;
        else                 entries[key].sim = pd;
    }

    int fontSize = m_plotSettings.legendFontSize > 0 ? m_plotSettings.legendFontSize : 9;

    for (const QString &key : entryOrder) {
        const TrtEntry &e = entries[key];
        QSharedPointer<PlotData> ref = e.sim ? e.sim : e.obs;
        if (!ref) continue;

        QWidget *row = new QWidget();
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(2, 1, 2, 1);
        rowLayout->setSpacing(4);

        // Obs sample (dot)
        auto *obsSample = new LegendSampleWidget(
            e.obs != nullptr,
            e.obs ? e.obs->pen   : QPen(Qt::NoPen),
            e.obs ? e.obs->symbol : QString(),
            e.obs ? QBrush(e.obs->color) : QBrush(Qt::NoBrush));
        obsSample->setFixedSize(30, 16);
        rowLayout->addWidget(obsSample);

        // Sim sample (solid line)
        QPen solidPen = ref->pen;
        solidPen.setStyle(Qt::SolidLine);
        auto *simSample = new LegendSampleWidget(false, solidPen, QString(), QBrush());
        simSample->setFixedSize(30, 16);
        rowLayout->addWidget(simSample);

        // Treatment name
        QLabel *trtLabel = new QLabel(ref->treatmentName);
        trtLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        trtLabel->setWordWrap(true);
        QFont f = trtLabel->font();
        f.setPointSize(fontSize);
        trtLabel->setFont(f);
        rowLayout->addWidget(trtLabel, 1);

        m_legendLayout->addWidget(row);
    }

    m_legendLayout->addStretch();
}
