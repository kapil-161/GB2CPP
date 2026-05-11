#include "PlotWidget.h"
#include "DataProcessor.h"
#include "PlotSettingsDialog.h"
#include <QSettings>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QChartView>

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

    // Seed axis range spinboxes with the current chart axis values so the user
    // sees the real current limits rather than 0/100 defaults.
    // Also detect axis type mismatch and clear stale X overrides.
    if (m_chart) {
        auto hAxesSeed = m_chart->axes(Qt::Horizontal);
        auto vAxesSeed = m_chart->axes(Qt::Vertical);
        if (!hAxesSeed.isEmpty()) {
            if (auto xAx = qobject_cast<QValueAxis*>(hAxesSeed.first())) {
                // Current axis is numeric — clear overrides if stored values look like epoch ms
                if (m_plotSettings.xAxisMin > 1e10 || m_plotSettings.xAxisMax > 1e10) {
                    m_plotSettings.useCustomXMin = false;
                    m_plotSettings.useCustomXMax = false;
                }
                m_plotSettings.xAxisMin = xAx->min();
                m_plotSettings.xAxisMax = xAx->max();
            } else if (auto xDtAx = qobject_cast<QDateTimeAxis*>(hAxesSeed.first())) {
                m_plotSettings.xAxisMin = static_cast<double>(xDtAx->min().toMSecsSinceEpoch());
                m_plotSettings.xAxisMax = static_cast<double>(xDtAx->max().toMSecsSinceEpoch());
            }
        }
        if (!vAxesSeed.isEmpty()) {
            if (auto yAx = qobject_cast<QValueAxis*>(vAxesSeed.first())) {
                m_plotSettings.yAxisMin = yAx->min();
                m_plotSettings.yAxisMax = yAx->max();
            }
        }
    }

    PlotSettingsDialog dialog(m_plotSettings, this, this);
    if (dialog.exec() == QDialog::Accepted) {
        PlotSettings newSettings = dialog.getSettings();

        // Check what changed BEFORE applyPlotSettings overwrites m_plotSettings
        bool filterChanged = (m_plotSettings.excludedSeriesKeys != newSettings.excludedSeriesKeys);
        bool errorBarChanged = (m_plotSettings.showErrorBars != newSettings.showErrorBars) ||
                               (m_plotSettings.errorBarType != newSettings.errorBarType);
        bool axisRangeChanged = (m_plotSettings.useCustomXMin != newSettings.useCustomXMin) ||
                                (m_plotSettings.useCustomXMax != newSettings.useCustomXMax) ||
                                (m_plotSettings.useCustomYMin != newSettings.useCustomYMin) ||
                                (m_plotSettings.useCustomYMax != newSettings.useCustomYMax) ||
                                (m_plotSettings.xAxisMin != newSettings.xAxisMin) ||
                                (m_plotSettings.xAxisMax != newSettings.xAxisMax) ||
                                (m_plotSettings.yAxisMin != newSettings.yAxisMin) ||
                                (m_plotSettings.yAxisMax != newSettings.yAxisMax) ||
                                (m_plotSettings.yAxisTickSpacing != newSettings.yAxisTickSpacing) ||
                                (m_plotSettings.xAxisTickSpacing != newSettings.xAxisTickSpacing);

        applyPlotSettings(newSettings);
        m_plotSettings = newSettings;
        saveSettings();

        if (m_isScatterMode && m_simData.rowCount > 0 && !m_currentYVars.isEmpty()) {
            // Always replot scatter — metrics selection, appearance, etc. all require rebuild
            DataTable dataCopy = m_simData;
            plotScatter(dataCopy, m_currentYVars);
        } else if (m_isBoxPlotMode && m_simData.rowCount > 0) {
            // Box plot: tick spacing or decimals change requires a full replot; range change goes via autoFitAxes
            bool boxReplot = (m_plotSettings.yAxisTickSpacing != newSettings.yAxisTickSpacing) ||
                             (m_plotSettings.yAxisDecimals    != newSettings.yAxisDecimals);
            if (boxReplot || filterChanged) {
                updatePlotWithScaling();
            } else if (axisRangeChanged) {
                autoFitAxes();
            }
        } else if (!m_isScatterMode && m_simData.rowCount > 0) {
            if (filterChanged || errorBarChanged) {
                updatePlotWithScaling();
                if (m_obsData.rowCount > 0)
                    calculateMetrics();
            } else if (axisRangeChanged) {
                autoFitAxes();
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
            valueAxis->setMinorGridLineVisible(settings.showMinorGrid);
            valueAxis->setLabelsVisible(settings.showAxisLabels);

            bool isXAxis = !hAxes.isEmpty() && axis == hAxes.first();

            if (!m_isScatterMode && isXAxis) {
                // X-axis tick customization
                valueAxis->setMinorTickCount(settings.xAxisMinorTickCount);
                if (settings.xAxisTickSpacing > 0.0) {
                    valueAxis->setTickInterval(settings.xAxisTickSpacing);
                } else if (settings.xAxisTickCount > 0) {
                    valueAxis->setTickCount(settings.xAxisTickCount);
                }
                // Label format
                if (settings.xAxisDecimals >= 0) {
                    valueAxis->setLabelFormat(QString("%.%1f").arg(settings.xAxisDecimals));
                }
            } else if (!m_isScatterMode && !isXAxis) {
                // Y-axis tick customization
                valueAxis->setMinorTickCount(settings.yAxisMinorTickCount);
                if (settings.yAxisTickSpacing > 0.0) {
                    valueAxis->setTickInterval(settings.yAxisTickSpacing);
                }
                // Don't call setTickCount when auto — autoFitAxes already set clean round ticks
                // Label format
                if (settings.yAxisDecimals >= 0) {
                    valueAxis->setLabelFormat(QString("%.%1f").arg(settings.yAxisDecimals));
                }
            } else {
                // Scatter mode — keep existing minor tick count
                valueAxis->setMinorTickCount(settings.xAxisMinorTickCount);
            }
        }
        else if (auto dateTimeAxis = qobject_cast<QDateTimeAxis*>(axis)) {
            dateTimeAxis->setLinePen(QPen(Qt::black));
            dateTimeAxis->setLabelsBrush(QBrush(Qt::black));
            dateTimeAxis->setLabelsVisible(settings.showAxisLabels);

            if (!hAxes.isEmpty() && axis == hAxes.first()) {
                if (settings.xAxisTickCount > 0) {
                    dateTimeAxis->setTickCount(settings.xAxisTickCount);
                }
            }
        }
    }

    // Apply user axis range overrides (after auto-fit has already run)
    if (!m_isScatterMode) {
        auto hAxes2 = m_chart->axes(Qt::Horizontal);
        auto vAxes2 = m_chart->axes(Qt::Vertical);
        if (!hAxes2.isEmpty()) {
            if (auto xAx = qobject_cast<QValueAxis*>(hAxes2.first())) {
                double lo = xAx->min(), hi = xAx->max();
                if (settings.useCustomXMin) lo = settings.xAxisMin;
                if (settings.useCustomXMax) hi = settings.xAxisMax;
                if (settings.useCustomXMin || settings.useCustomXMax)
                    xAx->setRange(lo, hi);
            } else if (auto xDtAx = qobject_cast<QDateTimeAxis*>(hAxes2.first())) {
                // Date axis — treat the values as milliseconds-since-epoch (same encoding as the rest of the code)
                QDateTime lo = xDtAx->min(), hi = xDtAx->max();
                if (settings.useCustomXMin) lo = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(settings.xAxisMin));
                if (settings.useCustomXMax) hi = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(settings.xAxisMax));
                if (settings.useCustomXMin || settings.useCustomXMax)
                    xDtAx->setRange(lo, hi);
            }
        }
        if (!vAxes2.isEmpty()) {
            if (auto yAx = qobject_cast<QValueAxis*>(vAxes2.first())) {
                double lo = yAx->min(), hi = yAx->max();
                if (settings.useCustomYMin) lo = settings.yAxisMin;
                if (settings.useCustomYMax) hi = settings.yAxisMax;
                if (settings.useCustomYMin || settings.useCustomYMax)
                    yAx->setRange(lo, hi);
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

    // Apply legend background color
    if (m_legendScrollArea) {
        QColor bg = settings.legendBackgroundColor;
        QString ss = QString("background-color: rgba(%1,%2,%3,%4);")
                         .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha());
        m_legendScrollArea->setStyleSheet(ss);
        if (m_legendScrollArea->widget())
            m_legendScrollArea->widget()->setStyleSheet(ss);
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

    // Box plot live-update: category label font and painter Y bounds
    if (m_isBoxPlotMode && m_chartView && !m_chartView->boxPlotStats().isEmpty()) {
        QFont tickFont(settings.fontFamily, settings.axisTickFontSize);
        m_chartView->setCategoryLabelFont(tickFont);

        // Sync Y bounds from current axis (range override may have changed them)
        auto vAxesBp = m_chart->axes(Qt::Vertical);
        if (!vAxesBp.isEmpty()) {
            if (auto yAx = qobject_cast<QValueAxis*>(vAxesBp.first()))
                m_chartView->setBoxPlotYBounds(yAx->min(), yAx->max());
        }

        // Axis line color on box plot axes
        QColor axCol = settings.axisLineColor.isValid() ? settings.axisLineColor : Qt::black;
        for (auto *axis : m_chart->axes()) {
            axis->setLinePen(QPen(axCol));
            axis->setLabelsBrush(QBrush(axCol));
        }

        // X title respects showAxisTitles (hide when many categories)
        auto hAxesBp = m_chart->axes(Qt::Horizontal);
        if (!hAxesBp.isEmpty()) {
            QString xTitle = settings.showAxisTitles
                             ? (settings.xAxisTitle.isEmpty() ? "Treatment" : settings.xAxisTitle)
                             : "";
            int nCats = m_chartView->boxPlotStats().isEmpty() ? 0
                        : [&]() { int n = 0; for (auto &s : m_chartView->boxPlotStats()) if (s.varIndex == 0) ++n; return n; }();
            hAxesBp.first()->setTitleText(nCats > 6 ? "" : xTitle);
        }

        // Y title
        auto vAxesBp2 = m_chart->axes(Qt::Vertical);
        if (!vAxesBp2.isEmpty())
            vAxesBp2.first()->setTitleText(settings.showAxisTitles ? settings.yAxisTitle : "");
    }

    // Update internal settings
    m_showGrid = settings.showGrid;
    m_showLegend = settings.showLegend;

    // Update internal plot settings
    // Note: error bar change detection is handled in onSettingsButtonClicked()
    // before m_plotSettings is overwritten
    m_plotSettings = settings;

    QTimer::singleShot(50, this, &PlotWidget::enforceAxisColors);

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
    s.setValue("showLegend",      m_plotSettings.showLegend);
    s.setValue("legendPosition",  m_plotSettings.legendPosition);
    s.setValue("legendX",         m_plotSettings.legendX);
    s.setValue("legendY",         m_plotSettings.legendY);

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
    s.setValue("axisLineColor",          m_plotSettings.axisLineColor.name());
    s.setValue("legendBackgroundColor",  m_plotSettings.legendBackgroundColor.name(QColor::HexArgb));

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

    m_plotSettings.showLegend     = s.value("showLegend",     m_plotSettings.showLegend).toBool();
    m_plotSettings.legendPosition = s.value("legendPosition", m_plotSettings.legendPosition).toString();
    m_plotSettings.legendX        = s.value("legendX",        m_plotSettings.legendX).toDouble();
    m_plotSettings.legendY        = s.value("legendY",        m_plotSettings.legendY).toDouble();

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
    m_plotSettings.axisLineColor         = QColor(s.value("axisLineColor",         m_plotSettings.axisLineColor.name()).toString());
    m_plotSettings.legendBackgroundColor = QColor(s.value("legendBackgroundColor", m_plotSettings.legendBackgroundColor.name(QColor::HexArgb)).toString());

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
