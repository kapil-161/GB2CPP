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

    // Landscape panels: divide available space by grid dims
    int panelW = (availW - margins - spacing * (nCols - 1)) / nCols;
    int panelH = (availH - margins - spacing * (nRows - 1)) / nRows;

    // Enforce a minimum and a 3:2 ratio (width:height)
    panelW = qMax(panelW, 260);
    panelH = qMax(panelH, 140);

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

        // Y range for this variable
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        for (const auto &pd : varData) {
            for (const QPointF &pt : pd->points) {
                yMin = qMin(yMin, pt.y());
                yMax = qMax(yMax, pt.y());
            }
        }
        if (yMin >= yMax) { yMax = yMin + 1.0; }
        double yPad = (yMax - yMin) * 0.08;
        yMin = qMax(0.0, yMin - yPad);
        yMax += yPad;

        // --- Build QChart ---
        QChart *chart = new QChart();
        chart->setBackgroundBrush(QBrush(m_plotSettings.backgroundColor));
        chart->setPlotAreaBackgroundBrush(QBrush(m_plotSettings.plotAreaColor));
        chart->setPlotAreaBackgroundVisible(true);
        chart->legend()->setVisible(false);
        chart->setMargins(QMargins(2, 0, 2, 2));
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
                ls->setPen(pd->pen);
                for (const QPointF &pt : pd->points) ls->append(pt);
                chart->addSeries(ls);
            }
        }

        // --- X axis ---
        QAbstractAxis *xAxis = nullptr;
        if (isDateAxis) {
            QDateTimeAxis *dtAx = new QDateTimeAxis();
            dtAx->setFormat(m_plotSettings.xAxisTickCount <= 6 ? "MMM dd\nyyyy" : "MMM dd");
            dtAx->setMin(QDateTime::fromMSecsSinceEpoch((qint64)globalXMin));
            dtAx->setMax(QDateTime::fromMSecsSinceEpoch((qint64)globalXMax));
            dtAx->setTickCount(qBound(2, m_plotSettings.xAxisTickCount, 10));
            QFont axFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
            dtAx->setLabelsFont(axFont);
            dtAx->setLabelsVisible(m_plotSettings.showAxisLabels);
            dtAx->setGridLineVisible(m_plotSettings.showGrid);
            dtAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
            chart->addAxis(dtAx, Qt::AlignBottom);
            xAxis = dtAx;
        } else {
            QValueAxis *vAx = new QValueAxis();
            vAx->setRange(globalXMin, globalXMax);
            vAx->setTickCount(qBound(2, m_plotSettings.xAxisTickCount, 15));
            QFont axFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
            vAx->setLabelsFont(axFont);
            vAx->setLabelsVisible(m_plotSettings.showAxisLabels);
            vAx->setGridLineVisible(m_plotSettings.showGrid);
            vAx->setMinorTickCount(m_plotSettings.showMinorGrid ? m_plotSettings.xAxisMinorTickCount : 0);
            vAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
            if (!m_plotSettings.xAxisTitle.isEmpty() && m_plotSettings.showAxisTitles)
                vAx->setTitleText(m_plotSettings.xAxisTitle);
            else if (m_plotSettings.showAxisTitles)
                vAx->setTitleText(m_currentXVar);
            QFont titleFont(m_plotSettings.fontFamily, m_plotSettings.axisLabelFontSize);
            titleFont.setBold(m_plotSettings.boldAxisLabels);
            vAx->setTitleFont(titleFont);
            chart->addAxis(vAx, Qt::AlignBottom);
            xAxis = vAx;
        }

        // --- Y axis ---
        QValueAxis *yAx = new QValueAxis();
        yAx->setRange(yMin, yMax);
        yAx->setTickCount(qBound(2, m_plotSettings.yAxisTickCount, 15));
        QFont axFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
        yAx->setLabelsFont(axFont);
        yAx->setLabelsVisible(m_plotSettings.showAxisLabels);
        yAx->setGridLineVisible(m_plotSettings.showGrid);
        yAx->setMinorTickCount(m_plotSettings.showMinorGrid ? m_plotSettings.yAxisMinorTickCount : 0);
        yAx->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
        QFont yTitleFont(m_plotSettings.fontFamily, m_plotSettings.axisLabelFontSize);
        yTitleFont.setBold(m_plotSettings.boldAxisLabels);
        yAx->setTitleFont(yTitleFont);
        chart->addAxis(yAx, Qt::AlignLeft);

        // Attach all series to both axes
        for (QAbstractSeries *s : chart->series()) {
            if (auto *xy = qobject_cast<QXYSeries*>(s)) {
                xy->attachAxis(xAxis);
                xy->attachAxis(yAx);
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
        QString displayName = varInfo.first.isEmpty() ? varCode : varInfo.first;
        QString unit = varInfo.second;
        QString labelText = unit.isEmpty() ? displayName : QString("%1 (%2)").arg(displayName, unit);

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
}
