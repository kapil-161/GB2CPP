#include "PlotWidget.h"
#include "DataProcessor.h"
#include "MetricsCalculator.h"
#include <QTimer>
#include <QtCharts/QValueAxis>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLineSeries>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

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


void PlotWidget::plotScatter(
    const DataTable &evaluateData,
    const QStringList &varNames)
{
    m_isScatterMode = true;
    m_simData = evaluateData;      // store so settings changes can trigger replot
    m_currentYVars = varNames;     // store so replot from settings dialog works
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

    static const QVector<QScatterSeries::MarkerShape> kShapes = {
        QScatterSeries::MarkerShapeCircle,
        QScatterSeries::MarkerShapeRectangle,
        QScatterSeries::MarkerShapeRotatedRectangle,
        QScatterSeries::MarkerShapeTriangle,
    };
    QMap<QString, QScatterSeries::MarkerShape> expShape;
    for (int ei = 0; ei < expOrder.size(); ++ei)
        expShape[expOrder[ei]] = kShapes[ei % kShapes.size()];

    // --- Nice-axis helper (reused per panel) ---
    auto niceAxis = [](double dataMin, double dataMax,
                       double &outMin, double &outMax, int &outTicks, QString &outFmt) {
        double range = dataMax - dataMin;
        // For zero or near-zero range (single point), use 10% of value magnitude as pad
        double pad = (range > 0) ? range * 0.1
                                 : qMax(1.0, std::abs(dataMin) * 0.1);
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
        // Pick format: avoid scientific notation — use decimal places based on step
        if (step >= 1.0)       outFmt = "%.0f";
        else if (step >= 0.1)  outFmt = "%.1f";
        else                   outFmt = "%.2f";
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
    QMap<QString, QVector<QAbstractSeries*>> expSeriesMap; // expLabel -> series across all panels

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
            ss->setMarkerShape(expShape.value(expLabel, QScatterSeries::MarkerShapeCircle));
            ss->setUseOpenGL(false);
            for (const QPointF &p : expPoints[expLabel]) ss->append(p);
            chart->addSeries(ss);
            expSeriesMap[expLabel].append(ss);
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

        QPair<QString, QString> stripVarInfo = DataProcessor::getVariableInfo(baseVar);
        QString stripDisplayName = stripVarInfo.first.isEmpty() ? baseVar : stripVarInfo.first;
        QString stripStyle = QString(
            "background-color: #e8e8e8; border-bottom: 1px solid #cccccc; "
            "font-weight: bold; font-size: %1px; padding: 2px 0px;")
            .arg(m_plotSettings.titleFontSize > 0 ? m_plotSettings.titleFontSize : 10);

        // Editable strip title: QStackedWidget with label (page 0) and line edit (page 1)
        QStackedWidget *titleStack = new QStackedWidget();
        titleStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QLabel *stripLabel = new QLabel(stripDisplayName);
        stripLabel->setAlignment(Qt::AlignCenter);
        stripLabel->setStyleSheet(QString("QLabel { %1 }").arg(stripStyle));
        stripLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        stripLabel->setToolTip("Double-click to edit title");
        stripLabel->setCursor(Qt::IBeamCursor);

        QLineEdit *stripEdit = new QLineEdit(stripDisplayName);
        stripEdit->setAlignment(Qt::AlignCenter);
        stripEdit->setStyleSheet(QString("QLineEdit { %1 border: 1px solid #99ccff; }").arg(stripStyle));
        stripEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        titleStack->addWidget(stripLabel);  // page 0 — display
        titleStack->addWidget(stripEdit);   // page 1 — edit

        // Double-click label → switch to edit mode
        stripLabel->installEventFilter(this);
        stripLabel->setProperty("titleStack", QVariant::fromValue(titleStack));
        stripLabel->setProperty("titleEdit",  QVariant::fromValue(stripEdit));
        stripLabel->setProperty("isScatterTitleLabel", true);

        // Commit on Enter or focus loss
        auto commit = [titleStack, stripLabel, stripEdit]() {
            QString t = stripEdit->text().trimmed();
            if (t.isEmpty()) t = stripLabel->text();
            stripLabel->setText(t);
            titleStack->setCurrentIndex(0);
        };
        connect(stripEdit, &QLineEdit::returnPressed, this, commit);
        connect(stripEdit, &QLineEdit::editingFinished, this, commit);

        panelLayout->addWidget(titleStack);

        QChartView *cv = new QChartView(chart);
        cv->setRenderHint(QPainter::Antialiasing);
        cv->setFrameShape(QFrame::NoFrame);  // remove border gap between strip and chart
        cv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        panelLayout->addWidget(cv, 1);

        // Build overlay stats text from user-selected metrics
        // Put R², N, and normalized MSEs/MSEu into the map so we can look them up uniformly
        fullMetrics["R²"] = r2;
        fullMetrics["N"]  = totalPts;
        {
            double mseTotal = rmse * rmse;
            double mseSraw  = fullMetrics.value("MSEs", 0.0).toDouble();
            double mseUraw  = fullMetrics.value("MSEu", 0.0).toDouble();
            fullMetrics["MSEs/MSE"] = (mseTotal > 0) ? mseSraw / mseTotal : 0.0;
            fullMetrics["MSEu/MSE"] = (mseTotal > 0) ? mseUraw / mseTotal : 0.0;
        }

        // Ordered list so display is consistent regardless of QSet iteration order
        const QStringList kMetricOrder = {"N", "RMSE", "R²", "d-stat", "BIAS", "MSEs/MSE", "MSEu/MSE"};

        QStringList statsLines;
        for (const QString &mkey : kMetricOrder) {
            if (!m_plotSettings.scatterMetrics.contains(mkey)) continue;
            // Map display key → fullMetrics map key
            QString fkey = mkey;
            if (mkey == "d-stat") fkey = "Willmott's d-stat";
            else if (mkey == "R²") fkey = "R²";
            QVariant val = fullMetrics.value(fkey);
            if (!val.isValid()) continue;
            bool ok = false;
            double dval = val.toDouble(&ok);
            if (!ok) continue;
            QString fmt;
            if (mkey == "N")          fmt = QString("N = %1").arg(static_cast<int>(dval));
            else if (mkey == "RMSE")  fmt = QString("RMSE = %1").arg(dval, 0, 'f', dval < 1 ? 3 : (dval < 100 ? 2 : 1));
            else if (mkey == "R²")    fmt = QString("R² = %1").arg(dval, 0, 'f', 2);
            else if (mkey == "d-stat") fmt = QString("d = %1").arg(dval, 0, 'f', 3);
            else if (mkey == "BIAS")  fmt = QString("BIAS = %1").arg(dval, 0, 'f', 3);
            else if (mkey == "MSEs/MSE") fmt = QString("MSEs/MSE = %1").arg(dval, 0, 'f', 2);
            else if (mkey == "MSEu/MSE") fmt = QString("MSEu/MSE = %1").arg(dval, 0, 'f', 2);
            statsLines << fmt;
        }
        QString statsText = statsLines.join("\n");
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
    if (m_legendPanel) m_legendPanel->setVisible(showScatterLegend);
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
    legendTitle->setStyleSheet("font-weight: bold; font-size: 12px; padding: 4px 0px 2px 0px;");
    m_legendLayout->addWidget(legendTitle);

    for (const QString &expLabel : expOrder) {
        QWidget *row = new QWidget();
        QHBoxLayout *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 2, 0, 2);
        hl->setSpacing(6);
        row->setCursor(Qt::PointingHandCursor);

        QColor c = expColor.value(expLabel, Qt::gray);
        QScatterSeries::MarkerShape shape = expShape.value(expLabel, QScatterSeries::MarkerShapeCircle);
        QString symbol;
        switch (shape) {
            case QScatterSeries::MarkerShapeCircle:           symbol = "o"; break;
            case QScatterSeries::MarkerShapeRectangle:        symbol = "s"; break;
            case QScatterSeries::MarkerShapeRotatedRectangle: symbol = "d"; break;
            case QScatterSeries::MarkerShapeTriangle:         symbol = "t"; break;
            default:                                          symbol = "o"; break;
        }
        QPen pen(c.darker(120)); pen.setWidth(1);
        LegendSampleWidget *swatch = new LegendSampleWidget(true, pen, symbol, QBrush(c), QString());

        QLabel *txt = new QLabel(expLabel);
        txt->setStyleSheet("font-size: 12px;");

        hl->addWidget(swatch);
        hl->addWidget(txt);
        hl->addStretch();

        // Wire up single-click highlight and double-click show/hide
        QVector<QAbstractSeries*> seriesList = expSeriesMap.value(expLabel);
        row->setProperty("seriesToHighlight", QVariant::fromValue(seriesList));
        row->setProperty("varName", expLabel);
        row->setProperty("trtId", "");
        row->installEventFilter(this);

        m_legendLayout->addWidget(row);
    }
}
