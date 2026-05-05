#include "PlotWidget.h"
#include "DataProcessor.h"
#include <QtCharts/QValueAxis>
#include <QtCharts/QScatterSeries>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <limits>

void PlotWidget::plotOsuBoxPlot(const DataTable &simData, const QStringList &yVars,
                                 const QStringList &treatments, const QString &selectedExperiment)
{
    if (!m_chart || yVars.isEmpty()) return;

    const DataColumn *trtColumn  = simData.getColumn("TRT");
    const DataColumn *expColumn  = simData.getColumn("EXPERIMENT");
    const DataColumn *tnameCol   = simData.getColumn("TNAME");
    const DataColumn *cropColumn = simData.getColumn("CROP");
    const DataColumn *mdatCol    = simData.getColumn("MDAT");

    if (!trtColumn) { qWarning() << "PlotOsuBoxPlot: missing TRT column"; return; }

    // Determine if multiple experiments or multiple crops are present
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
    bool multiCrop = cropSet.size() > 1;

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

    // Distinct colors per variable
    const QVector<QColor> varColors = {
        QColor(70,  130, 180, 180),   // steel blue
        QColor(210, 105,  30, 180),   // chocolate
        QColor( 60, 160,  60, 180),   // green
        QColor(180,  60,  60, 180),   // red
        QColor(130,  60, 180, 180),   // purple
        QColor(180, 160,  40, 180),   // olive
    };

    // Collect per-variable stats and shared category keys
    struct BoxStats { double q0, q1, q2, q3, q4; };
    // perVarStats[vi] = vector of BoxStats (one per trtKey)
    QVector<QVector<BoxStats>> perVarStats;
    QStringList trtKeys;
    QMap<QString, QString> keyToLabel;
    double globalMin =  std::numeric_limits<double>::max();
    double globalMax = -std::numeric_limits<double>::max();

    // Filter yVars to those present in data
    QStringList validYVars;
    for (const QString &yv : yVars)
        if (simData.getColumn(yv)) validYVars.append(yv);
    if (validYVars.isEmpty()) return;

    for (int vi = 0; vi < validYVars.size(); ++vi) {
        const QString &yVar = validYVars[vi];
        const DataColumn *yColumn = simData.getColumn(yVar);

        QMap<QString, QVector<double>> trtValues;

        for (int row = 0; row < simData.rowCount; ++row) {
            if (row >= trtColumn->data.size()) continue;

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

            if (!treatments.isEmpty() && !treatments.contains("All")
                && !treatments.contains(trt)
                && !treatments.contains(experiment + "::" + trt))
                continue;

            if (row >= yColumn->data.size()) continue;
            QVariant yVal = yColumn->data[row];
            if (DataProcessor::isMissingValue(yVal)) continue;
            bool ok;
            double y = yVal.toDouble(&ok);
            if (!ok) continue;

            // Skip crop-failure rows (y==0 and MDAT missing)
            if (qFuzzyIsNull(y) && mdatCol && row < mdatCol->data.size()
                && DataProcessor::isMissingValue(mdatCol->data[row]))
                continue;

            QString key;
            if (multiCrop)      key = crop + "::" + experiment + "::" + trt;
            else if (multiExp)  key = experiment + "::" + trt;
            else                key = trt;

            trtValues[key].append(y);

            if (!keyToLabel.contains(key)) {
                QString tname;
                if (tnameCol && row < tnameCol->data.size())
                    tname = tnameCol->data[row].toString();
                if (multiCrop)
                    keyToLabel[key] = multiExp ? QString("%1·%2").arg(crop, experiment) : crop;
                else if (multiExp)
                    keyToLabel[key] = tname.isEmpty() ? QString("%1·%2").arg(trt, experiment)
                                                       : QString("%1·%2").arg(trt, tname);
                else
                    keyToLabel[key] = tname.isEmpty() ? trt : QString("%1-%2").arg(trt, tname);
            }
        }

        if (trtValues.isEmpty()) continue;

        // Build sorted key list from first valid variable
        if (trtKeys.isEmpty()) {
            trtKeys = trtValues.keys();
            std::sort(trtKeys.begin(), trtKeys.end(), [&](const QString &a, const QString &b) {
                QStringList pa = a.split("::"), pb = b.split("::");
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
        }

        QVector<BoxStats> vstats;
        for (const QString &k : trtKeys) {
            QVector<double> vals = trtValues.value(k);
            std::sort(vals.begin(), vals.end());
            if (vals.isEmpty()) {
                vstats.append({0,0,0,0,0});
                continue;
            }
            BoxStats s;
            s.q0 = vals.first();
            s.q1 = quantile(vals, 0.25);
            s.q2 = quantile(vals, 0.50);
            s.q3 = quantile(vals, 0.75);
            s.q4 = vals.last();
            vstats.append(s);
            globalMin = std::min(globalMin, s.q0);
            globalMax = std::max(globalMax, s.q4);
        }
        perVarStats.append(vstats);
    }

    if (trtKeys.isEmpty() || perVarStats.isEmpty()) return;

    QStringList categories;
    for (const QString &k : trtKeys)
        categories.append(keyToLabel.value(k, k));

    m_chart->removeAllSeries();
    for (auto *axis : m_chart->axes())
        m_chart->removeAxis(axis);

    auto *dummySeries = new QScatterSeries();
    dummySeries->setOpacity(0.0);
    dummySeries->append(0, globalMin);
    dummySeries->append(trtKeys.size(), globalMax);
    m_chart->addSeries(dummySeries);

    QFont tickFont(m_plotSettings.fontFamily, m_plotSettings.axisTickFontSize);
    QFont titleFont(m_plotSettings.fontFamily, m_plotSettings.axisLabelFontSize);
    titleFont.setBold(m_plotSettings.boldAxisLabels);
    QColor axisLineCol = m_plotSettings.axisLineColor.isValid()
                         ? m_plotSettings.axisLineColor : Qt::black;

    // X axis — value axis so we control slot positions precisely (no QBarCategoryAxis padding)
    auto *xAxis = new QValueAxis();
    xAxis->setRange(0, trtKeys.size());
    xAxis->setTickCount(trtKeys.size() + 1);
    xAxis->setLabelsVisible(false);     // hide numeric labels — painter draws category names
    xAxis->setGridLineVisible(false);
    xAxis->setMinorGridLineVisible(false);
    xAxis->setLabelsFont(tickFont);
    xAxis->setTitleFont(titleFont);
    m_chart->addAxis(xAxis, Qt::AlignBottom);
    xAxis->setLinePen(QPen(axisLineCol));
    xAxis->setLabelsBrush(QBrush(axisLineCol));

    // Y axis
    double yPad = (globalMax - globalMin) * 0.08;
    double yMin = (globalMin >= 0.0) ? 0.0 : globalMin - yPad;
    double yMax = globalMax + yPad;

    double tickInterval = m_plotSettings.yAxisTickSpacing > 0.0
                          ? m_plotSettings.yAxisTickSpacing
                          : calculateNiceYInterval(yMax);
    double alignedMax   = std::ceil(yMax / tickInterval) * tickInterval;
    if (alignedMax <= globalMax) alignedMax += tickInterval;

    QString yFmt = (m_plotSettings.yAxisDecimals >= 0)
                   ? QString("%.%1f").arg(m_plotSettings.yAxisDecimals)
                   : "%.0f";

    auto *yAxis = new QValueAxis();
    yAxis->setMin(yMin);
    yAxis->setMax(alignedMax);
    yAxis->setTickInterval(tickInterval);
    int tickCount = qRound(alignedMax / tickInterval) + 1;
    if (tickCount < 8) tickCount = 8;
    yAxis->setTickCount(tickCount);
    yAxis->setLabelFormat(yFmt);
    yAxis->setGridLineColor(QColor(220, 220, 220));
    yAxis->setGridLineVisible(m_plotSettings.showGrid);
    yAxis->setMinorGridLineVisible(m_plotSettings.showMinorGrid);
    yAxis->setMinorTickCount(m_plotSettings.yAxisMinorTickCount);
    yAxis->setLabelsFont(tickFont);
    yAxis->setLabelsVisible(m_plotSettings.showAxisLabels);
    m_chart->addAxis(yAxis, Qt::AlignLeft);
    yAxis->setLinePen(QPen(axisLineCol));
    yAxis->setLabelsBrush(QBrush(axisLineCol));

    if (m_chartView)
        m_chartView->setCategoryLabelFont(tickFont);

    dummySeries->attachAxis(xAxis);
    dummySeries->attachAxis(yAxis);

    m_chart->setTitle(m_plotSettings.plotTitle);
    int bottomMargin = (trtKeys.size() > 6) ? 80 : 20;
    m_chart->setMargins(QMargins(0, 0, 0, bottomMargin));

    QString xTitle = m_plotSettings.showAxisTitles
                     ? (m_plotSettings.xAxisTitle.isEmpty() ? "Treatment" : m_plotSettings.xAxisTitle)
                     : "";
    QString yTitle = m_plotSettings.showAxisTitles ? m_plotSettings.yAxisTitle : "";
    xAxis->setTitleText(trtKeys.size() > 6 ? "" : xTitle);
    yAxis->setTitleText(yTitle);

    // Build interleaved painter stats: for each category, one entry per variable
    int nVars = perVarStats.size();
    QVector<ErrorBarChartView::BoxPlotStats> bpStats;
    for (int ci = 0; ci < trtKeys.size(); ++ci) {
        for (int vi = 0; vi < nVars; ++vi) {
            ErrorBarChartView::BoxPlotStats bp;
            const auto &s = perVarStats[vi][ci];
            bp.q0       = s.q0;  bp.q1 = s.q1;  bp.q2 = s.q2;
            bp.q3       = s.q3;  bp.q4 = s.q4;
            bp.label    = (vi == 0) ? categories[ci] : QString();
            bp.color    = varColors[vi % varColors.size()];
            bp.varIndex = vi;
            bp.varCount = nVars;
            bpStats.append(bp);
        }
    }
    if (m_chartView)
        m_chartView->setBoxPlotData(bpStats, yMin, alignedMax);

    // Legend panel — matches time series style (Sim. | Variable)
    clearLegend();

    // Header row
    QWidget *hdrWidget = new QWidget();
    QHBoxLayout *hdrLayout = new QHBoxLayout(hdrWidget);
    hdrLayout->setContentsMargins(0, 2, 0, 2);
    hdrLayout->setSpacing(5);
    QLabel *simHdr = new QLabel("<b>Sim.</b>");
    simHdr->setAlignment(Qt::AlignCenter);
    simHdr->setFixedWidth(30);
    QLabel *varHdr = new QLabel("<b>Variable</b>");
    varHdr->setAlignment(Qt::AlignLeft);
    hdrLayout->addWidget(simHdr);
    hdrLayout->addWidget(varHdr, 1);
    m_legendLayout->addWidget(hdrWidget);

    QFrame *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Plain);
    m_legendLayout->addWidget(sep);

    // One row per Y variable
    for (int vi = 0; vi < validYVars.size(); ++vi) {
        const QString &yv = validYVars[vi];
        QPair<QString, QString> vinfo = DataProcessor::getVariableInfo(yv);
        QString lbl = vinfo.first.isEmpty() ? yv : vinfo.first;
        QColor boxFill = varColors[vi % varColors.size()];
        QColor boxBorder = boxFill.darker(150);

        QWidget *row = new QWidget();
        QHBoxLayout *rl = new QHBoxLayout(row);
        rl->setContentsMargins(2, 3, 2, 3);
        rl->setSpacing(5);

        // Sim swatch — box color
        QLabel *sw = new QLabel();
        sw->setFixedSize(30, 14);
        sw->setStyleSheet(QString("background-color:rgba(%1,%2,%3,%4); border:1px solid %5;")
            .arg(boxFill.red()).arg(boxFill.green()).arg(boxFill.blue()).arg(boxFill.alpha())
            .arg(boxBorder.name()));
        sw->setAlignment(Qt::AlignCenter);
        rl->addWidget(sw);

        QLabel *name = new QLabel(lbl);
        name->setFont(QFont("Arial", 9));
        name->setWordWrap(true);
        rl->addWidget(name, 1);
        m_legendLayout->addWidget(row);
    }
    m_legendLayout->addStretch();

    if (m_legendStack) m_legendStack->setCurrentIndex(1);
}
