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
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <limits>

// ---------------------------------------------------------------------------
// PanelEventFilter — per-panel event filter giving each multi-panel chart view
// zoom/reset, wheel zoom, right-drag pan, and hover tooltip.
// No Q_OBJECT needed — only overrides a virtual.
// ---------------------------------------------------------------------------
class PanelEventFilter : public QObject
{
public:
    explicit PanelEventFilter(
        ErrorBarChartView *cv,
        const QMap<QAbstractSeries*, QSharedPointer<PlotData>> &seriesMap,
        const QString &xVar,
        const QVector<ErrorBarChartView::SegmentInfo> &segments,
        bool showTooltip,
        QObject *parent = nullptr)
        : QObject(parent), m_cv(cv), m_seriesMap(seriesMap),
          m_xVar(xVar), m_segments(segments), m_showTooltip(showTooltip) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (!m_cv) return false;
        if (obj != m_cv && obj != m_cv->viewport()) return false;

        if (event->type() == QEvent::Wheel) {
            auto *we = static_cast<QWheelEvent*>(event);
            const double k = 1.15;
            m_cv->chart()->zoom(we->angleDelta().y() > 0 ? k : 1.0 / k);
            m_isZoomed = true;
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                if (m_isZoomed) {
                    m_cv->chart()->zoomReset();
                    m_isZoomed = false;
                } else {
                    QPoint vpos = viewportPos(obj, me);
                    QPointF chartPos = m_cv->chart()->mapFromScene(m_cv->mapToScene(vpos));
                    QRectF plotArea  = m_cv->chart()->plotArea();
                    double w = plotArea.width()  / 2.0;
                    double h = plotArea.height() / 2.0;
                    QRectF zoomRect(chartPos.x() - w / 2.0, chartPos.y() - h / 2.0, w, h);
                    m_cv->chart()->zoomIn(zoomRect.intersected(plotArea));
                    m_isZoomed = true;
                }
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::MiddleButton) {
                m_cv->chart()->zoomReset();
                m_isZoomed = false;
                return true;
            }
            if (me->button() == Qt::RightButton) {
                m_cv->setDragMode(QGraphicsView::ScrollHandDrag);
                return false;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::RightButton) {
                m_cv->setDragMode(QGraphicsView::RubberBandDrag);
                return false;
            }
        }
        if (event->type() == QEvent::MouseMove && m_showTooltip) {
            auto *me = static_cast<QMouseEvent*>(event);
            QPoint vpos = viewportPos(obj, me);
            QAbstractSeries *hit = seriesNear(vpos);
            if (hit) {
                auto pd = m_seriesMap.value(hit);
                if (pd) showTip(pd, nearestPoint(hit, vpos), vpos);
                else    hideTip();
            } else {
                hideTip();
            }
        }
        if (event->type() == QEvent::Leave)
            hideTip();
        return false;
    }

private:
    QPoint viewportPos(QObject *obj, QMouseEvent *me) const
    {
        return (obj == m_cv->viewport())
               ? me->pos()
               : m_cv->viewport()->mapFromGlobal(me->globalPos());
    }

    QAbstractSeries* seriesNear(const QPoint &vpos) const
    {
        QPointF chartPos = m_cv->chart()->mapFromScene(m_cv->mapToScene(vpos));
        const double lineThr    = 20.0;
        const double scatterThr = 14.0;
        QAbstractSeries *nearest = nullptr;
        double minDist = std::numeric_limits<double>::max();
        for (QAbstractSeries *s : m_cv->chart()->series()) {
            if (!s->isVisible()) continue;
            if (auto *ls = qobject_cast<QLineSeries*>(s)) {
                const auto pts = ls->points();
                for (int i = 0; i + 1 < pts.size(); ++i) {
                    QPointF a = m_cv->chart()->mapToPosition(pts[i],   s);
                    QPointF b = m_cv->chart()->mapToPosition(pts[i+1], s);
                    QPointF ab = b - a, ap = chartPos - a;
                    double lenSq = ab.x()*ab.x() + ab.y()*ab.y();
                    double t = (lenSq > 0) ? qBound(0.0, (ap.x()*ab.x()+ap.y()*ab.y())/lenSq, 1.0) : 0.0;
                    double dist = QLineF(chartPos, a + t*ab).length();
                    if (dist < lineThr && dist < minDist) { minDist = dist; nearest = s; }
                }
            } else if (auto *ss = qobject_cast<QScatterSeries*>(s)) {
                double thr = scatterThr + ss->markerSize() / 2.0;
                for (const QPointF &pt : ss->points()) {
                    double dist = QLineF(chartPos, m_cv->chart()->mapToPosition(pt, s)).length();
                    if (dist < thr && dist < minDist) { minDist = dist; nearest = s; }
                }
            }
        }
        return nearest;
    }

    QPointF nearestPoint(QAbstractSeries *series, const QPoint &vpos) const
    {
        QPointF chartPos = m_cv->chart()->mapFromScene(m_cv->mapToScene(vpos));
        QVector<QPointF> pts;
        if (auto *ls = qobject_cast<QLineSeries*>(series))        pts = ls->points();
        else if (auto *ss = qobject_cast<QScatterSeries*>(series)) pts = ss->points();
        if (pts.isEmpty()) return {};
        double minDist = std::numeric_limits<double>::max();
        int idx = 0;
        for (int i = 0; i < pts.size(); ++i) {
            double d = QLineF(chartPos, m_cv->chart()->mapToPosition(pts[i], series)).length();
            if (d < minDist) { minDist = d; idx = i; }
        }
        return pts[idx]; // virtual x in axis-break mode
    }

    // Convert virtual x back to real msec for axis-break DATE display
    double unremapX(double vx) const
    {
        for (int i = 0; i < m_segments.size(); ++i) {
            const auto &s = m_segments[i];
            if (vx <= s.virtualEnd || i == m_segments.size() - 1)
                return s.realStart + (vx - s.virtualStart);
        }
        return vx;
    }

    void showTip(const QSharedPointer<PlotData> &pd, const QPointF &pt, const QPoint &vpos)
    {
        QPair<QString,QString> vi = DataProcessor::getVariableInfo(pd->variable);
        QString varName = vi.first.isEmpty() ? pd->variable : vi.first;
        QString unit    = vi.second;

        QString xStr;
        if (m_xVar == "DATE") {
            double realMsec = m_segments.isEmpty() ? pt.x() : unremapX(pt.x());
            xStr = QDateTime::fromMSecsSinceEpoch((qint64)realMsec).toString("MMM dd, yyyy");
        } else {
            xStr = QString::number(pt.x(), 'f', 1);
        }

        double y = pt.y();
        QString yStr = (qAbs(y) >= 0.01 && qAbs(y) < 1e6)
                       ? QString::number(y, 'f', 2) : QString::number(y, 'g', 4);
        if (!unit.isEmpty()) yStr += " " + unit;

        QString html = QString(
            "<div style='font-family:sans-serif;font-size:11px;'>"
            "<b>%1</b> <span style='color:#aad4ff;'>(%2)</span><br>"
            "<span style='color:#cccccc;'>%3</span><br>"
            "<span style='color:#88aacc;'>%4:</span> %5<br>"
            "<b style='font-size:12px;'>%6</b>"
            "</div>")
            .arg(varName.toHtmlEscaped()).arg(pd->isObserved ? "Obs" : "Sim")
            .arg(pd->treatmentName.toHtmlEscaped())
            .arg(m_xVar.toHtmlEscaped()).arg(xStr.toHtmlEscaped())
            .arg(yStr.toHtmlEscaped());

        if (!m_tooltip) {
            m_tooltip = new QLabel(m_cv);
            m_tooltip->setStyleSheet(
                "background-color: rgba(255,255,255,242); color: #1a1a2e;"
                "border: 1px solid #4a6fa5; border-radius: 4px; padding: 5px 7px;");
            m_tooltip->setTextFormat(Qt::RichText);
            m_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        m_tooltip->setText(html);
        m_tooltip->adjustSize();
        const int ox = 14, oy = -m_tooltip->height() - 4;
        QPoint pos = vpos + QPoint(ox, oy);
        QRect avail = m_cv->rect();
        pos.setX(qBound(2, pos.x(), avail.right()  - m_tooltip->width()  - 2));
        pos.setY(qBound(2, pos.y(), avail.bottom() - m_tooltip->height() - 2));
        m_tooltip->move(pos);
        m_tooltip->show();
        m_tooltip->raise();
    }

    void hideTip() { if (m_tooltip) m_tooltip->hide(); }

    QPointer<ErrorBarChartView> m_cv;
    QMap<QAbstractSeries*, QSharedPointer<PlotData>> m_seriesMap;
    QString m_xVar;
    QVector<ErrorBarChartView::SegmentInfo> m_segments;
    bool m_showTooltip;
    bool m_isZoomed = false;
    QPointer<QLabel> m_tooltip;
};

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
    for (ErrorBarChartView *cv : m_tsPanelViews) cv->deleteLater();
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
    bool hasBreaks  = isDateAxis && !m_axisBreaks.isEmpty();

    // Pre-build break/segment vectors once — same for every panel
    QVector<ErrorBarChartView::BreakInfo>   globalBreakInfos;
    QVector<ErrorBarChartView::SegmentInfo> globalSegInfos;
    if (hasBreaks) {
        for (int i = 0; i < m_axisBreaks.size(); ++i) {
            ErrorBarChartView::BreakInfo bi;
            double segVEnd = m_axisSegments[i].virtualStart
                             + (m_axisSegments[i].end - m_axisSegments[i].start);
            bi.virtualX  = segVEnd + BREAK_VIRTUAL_WIDTH / 2.0;
            bi.realStart = m_axisBreaks[i].gapStart;
            bi.realEnd   = m_axisBreaks[i].gapEnd;
            globalBreakInfos.append(bi);
        }
        for (const AxisSegment &s : m_axisSegments) {
            ErrorBarChartView::SegmentInfo si;
            si.virtualStart = s.virtualStart;
            si.virtualEnd   = s.virtualStart + (s.end - s.start);
            si.realStart    = s.start;
            si.realEnd      = s.end;
            globalSegInfos.append(si);
        }
    }

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
            // Include error bar tops so they don't get clipped
            if (m_plotSettings.showErrorBars && pd->isObserved) {
                for (const ErrorBarData &eb : pd->errorBars)
                    yDataMax = qMax(yDataMax, eb.meanY + eb.errorValue);
            }
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
        QMap<QAbstractSeries*, QVector<ErrorBarData>>      panelErrorBars;
        QMap<QAbstractSeries*, QSharedPointer<PlotData>>   panelSeriesMap;
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
                panelSeriesMap[ss] = pd;
                if (m_plotSettings.showErrorBars && !pd->errorBars.isEmpty())
                    panelErrorBars[ss] = pd->errorBars;
            } else {
                QLineSeries *ls = new QLineSeries();
                // Force solid line — style variation is only needed in overlay mode
                QPen solidPen(pd->pen);
                solidPen.setStyle(Qt::SolidLine);
                ls->setPen(solidPen);
                for (const QPointF &pt : pd->points) ls->append(pt);
                chart->addSeries(ls);
                panelSeriesMap[ls] = pd;
            }
        }

        QFont tickFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
        QFont titleFont(m_plotSettings.fontFamily, m_plotSettings.axisLabelFontSize);
        titleFont.setBold(m_plotSettings.boldAxisLabels);

        // --- X axis ---
        QAbstractAxis *xAxis = nullptr;
        if (isDateAxis && !hasBreaks) {
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
        } else if (isDateAxis && hasBreaks) {
            // Axis-break mode: virtual QValueAxis — custom tick labels painted by ErrorBarChartView
            QValueAxis *vAx = new QValueAxis();
            vAx->setRange(globalXMin, globalXMax);
            vAx->setTickCount(2);
            vAx->setLabelsVisible(false);
            vAx->setMinorTickCount(0);
            vAx->setMinorGridLineVisible(false);
            vAx->setGridLineVisible(m_plotSettings.showGrid);
            chart->addAxis(vAx, Qt::AlignBottom);
            vAx->setLinePen(QPen(m_plotSettings.axisLineColor));
            xAxis = vAx;
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

        ErrorBarChartView *cv = new ErrorBarChartView(chart);
        cv->setRenderHint(QPainter::Antialiasing);
        cv->setFrameShape(QFrame::NoFrame);
        cv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cv->setDragMode(QGraphicsView::RubberBandDrag);
        cv->setRubberBand(QChartView::RectangleRubberBand);
        cv->setMouseTracking(true);
        cv->viewport()->setMouseTracking(true);
        auto *panelFilter = new PanelEventFilter(
            cv, panelSeriesMap, m_currentXVar, globalSegInfos,
            m_plotSettings.showHoverTooltip, cv); // parented to cv — auto-deleted
        cv->installEventFilter(panelFilter);
        cv->viewport()->installEventFilter(panelFilter);
        if (m_plotSettings.showErrorBars)
            cv->setErrorBarData(panelErrorBars);
        cv->setAxisLineColor(m_plotSettings.axisLineColor);
        if (hasBreaks) cv->setAxisBreaks(globalBreakInfos, globalSegInfos);
        else           cv->clearAxisBreaks();
        panelLayout->addWidget(cv, 1);

        int row = vi / nCols;
        int col = vi % nCols;
        m_tsPanelGrid->addWidget(panelWidget, row, col);
        m_tsPanelViews.append(cv);
    }

    // --- Cross-panel X-axis sync ---
    // When user zooms one panel, all others follow the same time range.
    // Axis-break DATE mode uses QValueAxis (virtual coords), same sync path as numeric axes.
    if (isDateAxis && !hasBreaks && allXAxes.size() > 1) {
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
    } else if ((!isDateAxis || hasBreaks) && allXAxes.size() > 1) {
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
