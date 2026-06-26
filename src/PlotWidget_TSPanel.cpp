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
#include <QFontMetrics>
#include <QTextDocument>
#include <limits>
#include "MetricsCalculator.h"

// ---------------------------------------------------------------------------
// DraggableOverlay — makes a metrics QLabel draggable inside its parent
// chart view. Auto-positions at plot-area top-left until the user drags it.
// ---------------------------------------------------------------------------
class DraggableOverlay : public QObject {
public:
    DraggableOverlay(QChartView *view, QLabel *label, QObject *parent)
        : QObject(parent), m_view(view), m_label(label)
    {
        m_label->setCursor(Qt::SizeAllCursor);
        m_label->installEventFilter(this);
        m_view->installEventFilter(this);
    }

    bool eventFilter(QObject *obj, QEvent *e) override {
        if (obj == m_view) {
            // Reposition ONLY on resize. Never on Paint: moving/raising the label
            // schedules another paint, which would re-enter here and loop forever
            // (app hangs "not responding"), especially while the chart's plotArea
            // is still zero-width right after a rebuild.
            if (!m_userPositioned && e->type() == QEvent::Resize) {
                QRectF pa = m_view->chart()->plotArea();
                if (pa.isValid() && pa.width() > 10) {
                    m_label->move(static_cast<int>(pa.left()) + 28,
                                  static_cast<int>(pa.top())  + 8);
                    m_label->raise();
                }
            }
            return false;
        }
        if (obj == m_label) {
            if (e->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent*>(e);
                if (me->button() == Qt::LeftButton) {
                    m_dragging = true;
                    m_dragOffset = me->pos();
                    m_label->raise();
                }
                return true;
            }
            if (e->type() == QEvent::MouseMove && m_dragging) {
                auto *me = static_cast<QMouseEvent*>(e);
                QPoint np = m_label->pos() + me->pos() - m_dragOffset;
                QRect pr  = m_label->parentWidget()->rect();
                np.setX(qBound(0, np.x(), pr.width()  - m_label->width()));
                np.setY(qBound(0, np.y(), pr.height() - m_label->height()));
                m_label->move(np);
                m_userPositioned = true;
                return true;
            }
            if (e->type() == QEvent::MouseButtonRelease) {
                m_dragging = false;
                return true;
            }
        }
        return false;
    }

private:
    QChartView *m_view;
    QLabel     *m_label;
    QPoint      m_dragOffset;
    bool        m_dragging       = false;
    bool        m_userPositioned = false;
};

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

        // Remove stale overlay
        if (m_tsMetricsOverlay) { delete m_tsMetricsOverlay; m_tsMetricsOverlay = nullptr; }

        // Build metrics overlay on the single chart — use cached metrics (same source as table)
        if (!m_plotSettings.tsMetrics.isEmpty() && !m_lastTSMetrics.isEmpty()) {
            QString html = this->buildTSOverlayHtml(m_lastTSMetrics,
                               QSet<QString>(m_plotSettings.tsMetrics.begin(), m_plotSettings.tsMetrics.end()));

            if (m_chartView && !html.isEmpty()) {
                QLabel *label = new QLabel(m_chartView);
                label->setTextFormat(Qt::RichText);
                label->setText(html);
                label->setWordWrap(false);
                int fontPt = qBound(8, m_plotSettings.axisTickFontSize, 13);
                label->setStyleSheet(QString(
                    "QLabel { background: rgba(255,255,255,220); font-size: %1pt; "
                    "padding: 4px 7px; border: 1px solid rgba(0,0,0,40); border-radius: 3px; }").arg(fontPt));
                label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                label->adjustSize();
                // Position inside plot area once chart is laid out
                QRectF pa = m_chartView->chart()->plotArea();
                if (pa.isValid() && pa.width() > 10)
                    label->move(static_cast<int>(pa.left()) + 28, static_cast<int>(pa.top()) + 8);
                else
                    label->move(28, 8); // chart not laid out yet — park at top-left
                label->raise();
                label->show();
                m_tsMetricsOverlay = label;
                // Draggable overlay: auto-positions at plot-area top-left, user can drag
                new DraggableOverlay(m_chartView, label, label);
            }
        }
        return;
    }

    // Remove single-panel overlay when switching to multi-panel
    if (m_tsMetricsOverlay) { delete m_tsMetricsOverlay; m_tsMetricsOverlay = nullptr; }
    m_tsPanelOverlays.clear();

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
    // Extend X range to include snapshot data so panels aren't clipped
    if (m_snapshotActive) {
        for (const auto &pd : m_snapshotDataList) {
            for (const QPointF &pt : pd->points) {
                globalXMin = qMin(globalXMin, pt.x());
                globalXMax = qMax(globalXMax, pt.x());
            }
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
            if (pd->isObserved && !pd->errorBars.isEmpty()) {
                for (const ErrorBarData &eb : pd->errorBars)
                    yDataMax = qMax(yDataMax, eb.meanY + eb.errorValue);
            }
        }
        // Extend Y range to include snapshot data for this variable
        if (m_snapshotActive) {
            for (const auto &snapPD : m_snapshotDataList) {
                if (snapPD->variable != varCode) continue;
                for (const QPointF &pt : snapPD->points)
                    yDataMax = qMax(yDataMax, pt.y());
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
                // Sync pd pen/brush to actual series state so highlight/reset use correct baseline
                pd->pen   = ss->pen();
                pd->brush = ss->brush();
                pd->series = ss;
                panelSeriesMap[ss] = pd;
                if (!pd->errorBars.isEmpty())
                    panelErrorBars[ss] = pd->errorBars;
            } else {
                QLineSeries *ls = new QLineSeries();
                // Force solid line — style variation is only needed in overlay mode
                QPen solidPen(pd->pen);
                solidPen.setStyle(Qt::SolidLine);
                ls->setPen(solidPen);
                for (const QPointF &pt : pd->points) ls->append(pt);
                chart->addSeries(ls);
                // Sync pd->pen to the solid pen so highlight/reset restore solid lines, not dash patterns
                pd->pen = solidPen;
                pd->series = ls;
                panelSeriesMap[ls] = pd;
            }
        }

        // Inject snapshot series for this variable (attached to axes in the batch loop below)
        if (m_snapshotActive) {
            for (const auto &snapPD : m_snapshotDataList) {
                if (snapPD->variable != varCode || snapPD->points.isEmpty()) continue;
                QColor c = snapPD->color;
                c.setAlphaF(0.5);
                if (snapPD->isObserved) {
                    QScatterSeries *ss = new QScatterSeries();
                    ss->setUseOpenGL(false);
                    ss->setColor(c);
                    ss->setBorderColor(c);
                    ss->setMarkerSize(qMax(4.0, m_plotSettings.markerSize * 0.85));
                    ss->setMarkerShape(getMarkerShape(snapPD->symbol));
                    for (const QPointF &pt : snapPD->points) ss->append(pt);
                    chart->addSeries(ss);
                } else {
                    QLineSeries *ls = new QLineSeries();
                    ls->setUseOpenGL(false);
                    QPen p(c, m_plotSettings.lineWidth);
                    p.setStyle(Qt::DashLine);
                    ls->setPen(p);
                    for (const QPointF &pt : snapPD->points) ls->append(pt);
                    chart->addSeries(ls);
                }
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

            double alignedMin = m_plotSettings.useCustomYMin
                                ? m_plotSettings.yAxisMin
                                : 0.0;
            if (m_plotSettings.useCustomYMax) alignedMax = m_plotSettings.yAxisMax;

            QValueAxis *yAx = new QValueAxis();
            yAx->setRange(alignedMin, alignedMax);
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
        cv->setErrorBarData(panelErrorBars);
        cv->setAxisLineColor(m_plotSettings.axisLineColor);
        if (hasBreaks) cv->setAxisBreaks(globalBreakInfos, globalSegInfos);
        else           cv->clearAxisBreaks();
        panelLayout->addWidget(cv, 1);

        // --- Metrics overlay — use same cached metrics as single-chart overlay ---
        if (!m_plotSettings.tsMetrics.isEmpty() && !m_lastTSMetrics.isEmpty()) {
            QStringList statsLines;
            double totalN = 0, sumSS = 0, sumObsMean = 0, sumDStat = 0;
            double pooledDStat = -1.0;
            for (const auto &m : m_lastTSMetrics) {
                if (m.value("Variable").toString() != varCode) continue;
                double n = m.value("n").toDouble();
                if (n <= 0) continue;
                double rmse = m.value("RMSE").toDouble();
                totalN     += n;
                sumSS      += n * rmse * rmse;
                sumObsMean += n * m.value("ObsMean").toDouble();
                sumDStat   += n * m.value("Willmott's d-stat").toDouble();
                if (pooledDStat < 0 && m.contains("PooledDStat"))
                    pooledDStat = m.value("PooledDStat").toDouble();
            }
            if (totalN > 0) {
                double rmse    = std::sqrt(sumSS / totalN);
                double obsMean = sumObsMean / totalN;
                double nrmse   = (obsMean > 0) ? (rmse / obsMean) * 100.0 : 0.0;
                double dStat   = (pooledDStat >= 0) ? pooledDStat : sumDStat / totalN;
                if (m_plotSettings.tsMetrics.contains("N"))
                    statsLines << QString("N = %1").arg((int)totalN);
                if (m_plotSettings.tsMetrics.contains("RMSE"))
                    statsLines << QString("RMSE = %1").arg(rmse, 0, 'f', rmse < 1 ? 3 : (rmse < 100 ? 2 : 1));
                if (m_plotSettings.tsMetrics.contains("NRMSE"))
                    statsLines << QString("NRMSE = %1%").arg(nrmse, 0, 'f', 1);
                if (m_plotSettings.tsMetrics.contains("d-stat"))
                    statsLines << QString("d = %1").arg(dStat, 0, 'f', 3);
            }

            if (!statsLines.isEmpty()) {
                // Parent to panelWidget (not cv) so label is not clipped by chart view
                int statsFontPt = qBound(7, m_plotSettings.axisTickFontSize, 13);
                QLabel *statsLabel = new QLabel(statsLines.join("\n"), panelWidget);
                statsLabel->setStyleSheet(QString(
                    "QLabel { background: rgba(255,255,255,220); font-size: %1pt; "
                    "padding: 4px 7px; border: 1px solid rgba(0,0,0,40); border-radius: 3px; }").arg(statsFontPt));
                statsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                statsLabel->setMaximumWidth(QWIDGETSIZE_MAX);
                statsLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                statsLabel->show();
                statsLabel->adjustSize();
                // Position inside cv's plot area (offset by strip label height)
                int stripH = stripLabel->sizeHint().height();
                statsLabel->move(28, stripH + 8);
                statsLabel->raise();
                new DraggableOverlay(cv, statsLabel, statsLabel);
                m_tsPanelOverlays[varCode] = statsLabel;
            }
        }

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
    auto *obsH = new QLabel(); obsH->setFixedWidth(30); obsH->setAlignment(Qt::AlignCenter);
    obsH->setCursor(Qt::PointingHandCursor);
    obsH->setProperty("legendHeaderType", "obs");
    obsH->installEventFilter(this);
    m_obsHeaderLabel = obsH;

    auto *simH = new QLabel(); simH->setFixedWidth(30); simH->setAlignment(Qt::AlignCenter);
    simH->setCursor(Qt::PointingHandCursor);
    simH->setProperty("legendHeaderType", "sim");
    simH->installEventFilter(this);
    m_simHeaderLabel = simH;

    updateObsSimHeaders();

    QLabel *trtH = new QLabel("<b>Treatment</b>"); trtH->setAlignment(Qt::AlignLeft);
    trtH->setObjectName("legendTrtHeader");
    trtH->setProperty("legendHeaderType", "treatment");
    trtH->setCursor(Qt::PointingHandCursor);
    trtH->installEventFilter(this);
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

        // Collect all series for this treatment across all panels (all variables)
        QVector<QAbstractSeries*> toggleItems;
        for (const auto &pd : m_plotDataList) {
            if (pd && pd->series &&
                pd->treatment   == ref->treatment &&
                pd->experiment  == ref->experiment &&
                pd->crop        == ref->crop)
                toggleItems.append(pd->series.data());
        }
        row->setProperty("seriesToHighlight", QVariant::fromValue(toggleItems));
        row->setProperty("varName", QString());
        row->setProperty("trtId", ref->treatment);
        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(this);

        m_legendLayout->addWidget(row);
    }

    // Update header to singular/plural based on entry count
    if (QLabel *hdr = m_legendScrollArea->findChild<QLabel*>("legendTrtHeader")) {
        QString txt = entryOrder.size() == 1 ? "<b>Treatment</b>" : "<b>Treatments</b>";
        hdr->setText(txt);
    }

    m_legendLayout->addStretch();

    if (!m_obsVisible) setObsSeriesVisible(false);
    if (!m_simVisible) setSimSeriesVisible(false);
}

void PlotWidget::refreshTSMetricsOverlay()
{
    if (m_plotSettings.tsMetrics.isEmpty() || m_lastTSMetrics.isEmpty()) return;

    bool isMultiPanel = m_plotSettings.multiPanelTimeSeries
                        && m_currentYVars.size() >= 2
                        && !m_plotDataList.isEmpty();

    if (isMultiPanel) {
        // Build/update per-panel overlays
        int statsFontPt = qBound(7, m_plotSettings.axisTickFontSize, 13);
        QSet<QString> metricSet(m_plotSettings.tsMetrics.begin(), m_plotSettings.tsMetrics.end());

        for (int vi = 0; vi < m_currentYVars.size(); ++vi) {
            const QString &varCode = m_currentYVars[vi];
            if (vi >= m_tsPanelViews.size()) continue;
            ErrorBarChartView *cv = m_tsPanelViews[vi];
            if (!cv) continue;
            QWidget *panelWidget = cv->parentWidget();
            if (!panelWidget) continue;

            // Compute overall stats — use pooled d-stat if available
            double totalN = 0, sumSS = 0, sumObsMean = 0, sumDStat = 0;
            double pooledDStat = -1.0;
            for (const auto &m : m_lastTSMetrics) {
                if (m.value("Variable").toString() != varCode) continue;
                double n = m.value("n").toDouble();
                if (n <= 0) continue;
                double rmse = m.value("RMSE").toDouble();
                totalN     += n;
                sumSS      += n * rmse * rmse;
                sumObsMean += n * m.value("ObsMean").toDouble();
                sumDStat   += n * m.value("Willmott's d-stat").toDouble();
                if (pooledDStat < 0 && m.contains("PooledDStat"))
                    pooledDStat = m.value("PooledDStat").toDouble();
            }
            if (totalN <= 0) continue;
            double rmse    = std::sqrt(sumSS / totalN);
            double obsMean = sumObsMean / totalN;
            double nrmse   = (obsMean > 0) ? (rmse / obsMean) * 100.0 : 0.0;
            double dStat   = (pooledDStat >= 0) ? pooledDStat : sumDStat / totalN;

            QStringList statsLines;
            if (metricSet.contains("N"))     statsLines << QString("N = %1").arg((int)totalN);
            if (metricSet.contains("RMSE"))  statsLines << QString("RMSE = %1").arg(rmse, 0, 'f', rmse < 1 ? 3 : (rmse < 100 ? 2 : 1));
            if (metricSet.contains("NRMSE")) statsLines << QString("NRMSE = %1%").arg(nrmse, 0, 'f', 1);
            if (metricSet.contains("d-stat"))statsLines << QString("d = %1").arg(dStat, 0, 'f', 3);
            if (statsLines.isEmpty()) continue;

            if (m_tsPanelOverlays.contains(varCode) && m_tsPanelOverlays[varCode]) {
                // Update existing
                m_tsPanelOverlays[varCode]->setText(statsLines.join("\n"));
                m_tsPanelOverlays[varCode]->adjustSize();
                m_tsPanelOverlays[varCode]->show();
            } else {
                // Create new label parented to panelWidget to avoid cv clipping
                QLabel *statsLabel = new QLabel(statsLines.join("\n"), panelWidget);
                statsLabel->setStyleSheet(QString(
                    "QLabel { background: rgba(255,255,255,220); font-size: %1pt; "
                    "padding: 4px 7px; border: 1px solid rgba(0,0,0,40); border-radius: 3px; }").arg(statsFontPt));
                statsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                statsLabel->setMaximumWidth(QWIDGETSIZE_MAX);
                statsLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                statsLabel->show();
                statsLabel->adjustSize();
                // Position inside chart area (below strip label)
                int stripH = panelWidget->layout() ? 20 : 0; // approximate strip height
                statsLabel->move(28, stripH + 8);
                statsLabel->raise();
                new DraggableOverlay(cv, statsLabel, statsLabel);
                m_tsPanelOverlays[varCode] = statsLabel;
            }
        }
        return;
    }

    if (!m_chartView) return;

    QString html = buildTSOverlayHtml(m_lastTSMetrics,
                       QSet<QString>(m_plotSettings.tsMetrics.begin(), m_plotSettings.tsMetrics.end()));
    if (html.isEmpty()) return;

    int fontPt = qBound(8, m_plotSettings.axisTickFontSize, 13);

    if (m_tsMetricsOverlay) {
        m_tsMetricsOverlay->setText(html);
        QTextDocument doc;
        QFont f = m_tsMetricsOverlay->font(); f.setPointSize(fontPt);
        doc.setDefaultFont(f);
        doc.setHtml(html);
        QSize docSize = doc.size().toSize();
        m_tsMetricsOverlay->resize(docSize.width() + 32, docSize.height() + 12);
        m_tsMetricsOverlay->show();
        return;
    }
    QLabel *label = new QLabel(m_chartView);
    label->setTextFormat(Qt::RichText);
    label->setText(html);
    label->setWordWrap(false);
    label->setStyleSheet(QString(
        "QLabel { background: rgba(255,255,255,220); font-size: %1pt; "
        "padding: 4px 7px; border: 1px solid rgba(0,0,0,40); border-radius: 3px; }").arg(fontPt));
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setMaximumWidth(QWIDGETSIZE_MAX);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    // Use QTextDocument to measure HTML content (adjustSize unreliable before polish)
    {
        QTextDocument doc;
        QFont f = label->font(); f.setPointSize(fontPt);
        doc.setDefaultFont(f);
        doc.setHtml(html);
        QSize docSize = doc.size().toSize();
        label->resize(docSize.width() + 32, docSize.height() + 12);
    }
    label->move(28, 8); // temporary; repositioned after layout below
    label->raise();
    label->show();
    m_tsMetricsOverlay = label;
    new DraggableOverlay(m_chartView, label, label);

    // Defer final positioning until Qt has finished laying out the chart
    QTimer::singleShot(0, this, [this]() {
        if (!m_tsMetricsOverlay || !m_chartView) return;
        QRectF pa = m_chartView->chart()->plotArea();
        if (pa.isValid() && pa.width() > 10) {
            m_tsMetricsOverlay->move(static_cast<int>(pa.left()) + 28,
                                     static_cast<int>(pa.top())  + 8);
            m_tsMetricsOverlay->raise();
        }
    });
}
