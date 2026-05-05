#include "PlotWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QDateTime>
#include <QtCharts/QDateTimeAxis>
#include <cmath>

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

void ErrorBarChartView::setBoxPlotYBounds(double yMin, double yMax)
{
    m_bpYMin = yMin;
    m_bpYMax = yMax;
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

    // Number of treatment categories = unique labels (first varIndex==0 entries)
    int nCats = 0;
    for (const BoxPlotStats &s : m_boxStats)
        if (s.varIndex == 0) ++nCats;
    if (nCats == 0) nCats = m_boxStats.size();
    int nVars = m_boxStats.isEmpty() ? 1 : m_boxStats.first().varCount;
    if (nVars < 1) nVars = 1;

    double slotW   = plotArea.width() / nCats;
    // Each variable sub-box occupies (0.8/nVars) of the slot
    double subW    = slotW * 0.8 / nVars;
    double boxHalf = subW * 0.5;
    double capHalf = subW * 0.4;

    auto yToPixel = [&](double val) -> double {
        double ratio = (val - m_bpYMin) / yRange;
        return plotArea.top() + (1.0 - ratio) * plotArea.height();
    };

    const QColor whiskerColor(60, 60, 60);
    const QColor medianColor(255, 255, 255);
    const QColor outlierColor(220, 60, 60);

    // Track which category index each entry belongs to (based on varIndex==0 entries)
    int catIdx = -1;
    for (int i = 0; i < m_boxStats.size(); ++i) {
        const BoxPlotStats &s = m_boxStats[i];
        if (s.varIndex == 0) ++catIdx;

        // Center of slot, then offset for this variable's sub-box
        double slotCenter = plotArea.left() + (catIdx + 0.5) * slotW;
        double subOffset  = (s.varIndex - (nVars - 1) * 0.5) * subW;
        double cx = slotCenter + subOffset;

        double yQ0 = yToPixel(s.q0);
        double yQ1 = yToPixel(s.q1);
        double yQ2 = yToPixel(s.q2);
        double yQ3 = yToPixel(s.q3);
        double yQ4 = yToPixel(s.q4);

        QColor fill   = s.color.isValid() ? s.color : QColor(70, 130, 180, 180);
        QColor border = fill.darker(150);

        // --- Whisker ---
        QPen whiskerPen(whiskerColor, 1.5, Qt::SolidLine, Qt::FlatCap);
        painter->setPen(whiskerPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(QPointF(cx, yQ1), QPointF(cx, yQ0));
        painter->drawLine(QPointF(cx - capHalf, yQ0), QPointF(cx + capHalf, yQ0));
        painter->drawLine(QPointF(cx, yQ3), QPointF(cx, yQ4));
        painter->drawLine(QPointF(cx - capHalf, yQ4), QPointF(cx + capHalf, yQ4));

        // --- IQR Box ---
        QRectF boxRect(cx - boxHalf, yQ3, boxHalf * 2.0, yQ1 - yQ3);
        painter->setPen(QPen(border, 1.5));
        painter->setBrush(QBrush(fill));
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

        // --- X category label below the plot area (draw once per category, centered on slot) ---
        if (!s.label.isEmpty() && s.varIndex == 0) {
            double slotCenter = plotArea.left() + (catIdx + 0.5) * slotW;
            painter->setPen(QPen(QColor(60, 60, 60)));
            painter->setFont(m_catLabelFont);
            if (nCats > 6) {
                painter->save();
                painter->translate(slotCenter, plotArea.bottom() + 6);
                painter->rotate(45);
                painter->drawText(QRectF(0, 0, slotW * 2, 20), Qt::AlignLeft | Qt::AlignVCenter, s.label);
                painter->restore();
            } else {
                QRectF labelRect(slotCenter - slotW * 0.5, plotArea.bottom() + 4, slotW, 32);
                painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, s.label);
            }
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

void ErrorBarChartView::paintAxisBorder(QPainter *painter, const QPoint &viewportOffset)
{
    if (!chart()) return;
    QRectF pa = chart()->plotArea().translated(-viewportOffset);
    QColor axisCol = m_axisLineColor.isValid() ? m_axisLineColor : Qt::black;
    painter->save();
    painter->setPen(QPen(axisCol, 1));
    painter->drawLine(pa.bottomLeft(), pa.bottomRight()); // X axis
    painter->drawLine(pa.bottomLeft(), pa.topLeft());     // Y axis
    painter->restore();
}

void ErrorBarChartView::paintEvent(QPaintEvent *event)
{
    QChartView::paintEvent(event);

    if (!chart()) return;

    QPainter painter(this->viewport());

    // Draw axis border lines in the user-chosen color (Qt theme always draws them gray)
    paintAxisBorder(&painter);

    if (m_errorBars.isEmpty() && m_axisBreaks.isEmpty() && m_boxStats.isEmpty()) return;

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
    if (!m_axisBreaks.isEmpty()) {
        paintAxisBreaks(&painter);
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

void ErrorBarChartView::paintAxisBreaks(QPainter *painter)
{
    if (!chart() || m_axisBreaks.isEmpty()) return;

    QRectF plotArea = chart()->plotArea();

    QValueAxis *xAxis = nullptr;
    for (QAbstractAxis *axis : chart()->axes(Qt::Horizontal)) {
        if ((xAxis = qobject_cast<QValueAxis*>(axis))) break;
    }
    if (!xAxis) return;

    double vMin = xAxis->min();
    double vMax = xAxis->max();
    if (vMax <= vMin) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QColor bgColor = chart()->backgroundBrush().color();

    QFont labelFont = painter->font();
    labelFont.setPointSize(8);
    painter->setFont(labelFont);
    QFontMetrics fm(labelFont);

    const double tickLen       = 6.0;
    const double breakHalfWidth = 10.0;
    const double slashHalfH    = 10.0;
    const double slashOffset   =  4.0;
    double py = plotArea.bottom();

    // Helper: convert virtual x → pixel x
    auto vToPixel = [&](double vx) {
        return plotArea.left() + (vx - vMin) / (vMax - vMin) * plotArea.width();
    };

    // ── 1. Draw tick marks + date labels for each segment ──────────────────
    const double msecPerDay   = 86400000.0;
    const double msecPerMonth = 30.44  * msecPerDay;
    const double msecPerYear  = 365.25 * msecPerDay;
    const double minLabelPx   = 70.0;  // minimum pixel spacing between labels

    for (const SegmentInfo &seg : m_axisSegments) {
        double realSpanMsec = seg.realEnd - seg.realStart;
        if (realSpanMsec <= 0) continue;

        // Pixel width this segment occupies on screen
        double segPxWidth = vToPixel(seg.virtualEnd) - vToPixel(seg.virtualStart);
        if (segPxWidth <= 0) continue;

        // How many ticks fit in this pixel width?
        int maxTicks = qMax(2, static_cast<int>(segPxWidth / minLabelPx));

        // Choose a "nice" tick interval in real time that produces <= maxTicks ticks
        // Candidate intervals in ascending order
        struct Candidate { double msec; int months; int years; QString fmt; };
        QVector<Candidate> candidates = {
            { 7  * msecPerDay,   0, 0, "MMM dd"   },
            { 14 * msecPerDay,   0, 0, "MMM dd"   },
            { msecPerMonth,      1, 0, "MMM yyyy" },
            { 2  * msecPerMonth, 2, 0, "MMM yyyy" },
            { 3  * msecPerMonth, 3, 0, "MMM yyyy" },
            { 6  * msecPerMonth, 6, 0, "MMM yyyy" },
            { msecPerYear,       0, 1, "MMM yyyy" },
            { 2  * msecPerYear,  0, 2, "yyyy"     },
            { 5  * msecPerYear,  0, 5, "yyyy"     },
            { 10 * msecPerYear,  0,10, "yyyy"     },
            { 20 * msecPerYear,  0,20, "yyyy"     },
        };

        Candidate chosen = candidates.last();
        for (const Candidate &c : candidates) {
            int approxTicks = static_cast<int>(realSpanMsec / c.msec) + 1;
            if (approxTicks <= maxTicks) {
                chosen = c;
                break;
            }
        }

        QDateTime dtStart = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(seg.realStart));
        QDateTime dtEnd   = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(seg.realEnd));

        // Snap first tick to a clean boundary
        QDateTime tick;
        if (chosen.years >= 1) {
            int year = dtStart.date().year();
            year = static_cast<int>(std::ceil(static_cast<double>(year) / chosen.years)) * chosen.years;
            tick = QDateTime(QDate(year, 1, 1), QTime(0, 0));
        } else if (chosen.months >= 1) {
            int year  = dtStart.date().year();
            int month = dtStart.date().month();
            tick = QDateTime(QDate(year, month, 1), QTime(0, 0));
            if (tick < dtStart) tick = tick.addMonths(chosen.months);
            // Snap to multiple of interval from Jan
            int monthOffset = (tick.date().month() - 1) % chosen.months;
            if (monthOffset) tick = tick.addMonths(chosen.months - monthOffset);
        } else {
            tick = dtStart;
        }

        while (tick <= dtEnd) {
            double realMsec = static_cast<double>(tick.toMSecsSinceEpoch());
            double vx = seg.virtualStart + (realMsec - seg.realStart);
            if (vx >= vMin - 1e6 && vx <= vMax + 1e6) {
                double px = vToPixel(vx);
                painter->setPen(QPen(Qt::black, 1));
                painter->drawLine(QPointF(px, py), QPointF(px, py + tickLen));
                QString label = tick.toString(chosen.fmt);
                int lw = fm.horizontalAdvance(label);
                painter->setPen(Qt::black);
                painter->drawText(QPointF(px - lw / 2.0, py + tickLen + fm.ascent() + 2), label);
            }
            if (chosen.years >= 1)        tick = tick.addYears(chosen.years);
            else if (chosen.months >= 1)  tick = tick.addMonths(chosen.months);
            else                          tick = tick.addMSecs(static_cast<qint64>(chosen.msec));
        }
    }

    // ── 2. Draw // break symbols ───────────────────────────────────────────
    for (const BreakInfo &br : m_axisBreaks) {
        double px = vToPixel(br.virtualX);

        // Erase axis line under break
        painter->setPen(Qt::NoPen);
        painter->setBrush(bgColor);
        painter->drawRect(QRectF(px - breakHalfWidth, py - 2, breakHalfWidth * 2, tickLen + 4));

        // Draw //
        QPen slashPen(Qt::black, 1.5);
        painter->setPen(slashPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(QPointF(px - slashOffset - 2, py + slashHalfH),
                          QPointF(px - slashOffset + 2, py - slashHalfH));
        painter->drawLine(QPointF(px + slashOffset - 2, py + slashHalfH),
                          QPointF(px + slashOffset + 2, py - slashHalfH));
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
