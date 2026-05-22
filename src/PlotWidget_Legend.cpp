#include "PlotWidget.h"
#include "DataProcessor.h"
#include <QPainter>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QDebug>
#include <QDateTime>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <algorithm>

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

// ============================================================================
// COMPREHENSIVE LEGEND IMPLEMENTATION
// ============================================================================

void PlotWidget::updateLegendAdvanced(const QMap<QString, QMap<QString, QVector<QSharedPointer<PlotData>>>>& legendEntries)
{
    clearLegend();

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

        auto* obsHeader = new QLabel();
        obsHeader->setAlignment(Qt::AlignCenter);
        obsHeader->setFixedWidth(30);
        obsHeader->setCursor(Qt::PointingHandCursor);
        obsHeader->setProperty("legendHeaderType", "obs");
        obsHeader->installEventFilter(this);
        m_obsHeaderLabel = obsHeader;

        auto* simHeader = new QLabel();
        simHeader->setAlignment(Qt::AlignCenter);
        simHeader->setFixedWidth(30);
        simHeader->setCursor(Qt::PointingHandCursor);
        simHeader->setProperty("legendHeaderType", "sim");
        simHeader->installEventFilter(this);
        m_simHeaderLabel = simHeader;

        updateObsSimHeaders();  // apply current toggle appearance

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
            QFrame* sep = new QFrame();
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Plain);
            sep->setStyleSheet("color: #EEEEEE;");
            m_legendLayout->addWidget(sep);
        }
    }

    // Re-apply toggle visibility in case this is a legend rebuild (e.g. axis change)
    // after the user had already toggled obs or sim off.
    if (!m_obsVisible) setObsSeriesVisible(false);
    if (!m_simVisible) setSimSeriesVisible(false);
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
            LegendSampleWidget* sample = new LegendSampleWidget(
                true,
                scatterData->pen,
                scatterData->symbol,
                scatterData->brush,
                QString()
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
        LegendSampleWidget* obsSample = new LegendSampleWidget(
            true,  // has symbol
            obsData->pen,
            obsData->symbol,
            obsData->brush,
            QString()
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
        LegendSampleWidget* simSample = new LegendSampleWidget(
            false,  // no symbol (line)
            simData->pen,
            "",
            QBrush(),
            QString()
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
        QSharedPointer<PlotData> obsPlotData;
        if (treatmentData.contains("obs") && treatmentData["obs"].isValid()) {
            obsPlotData = treatmentData["obs"].value<QSharedPointer<PlotData>>();
            if (obsPlotData) {
                currentCrop = obsPlotData->crop;
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
    trtLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    trtLabel->setWordWrap(true);

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
    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Plain);
    m_legendLayout->addWidget(sep);
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

// ============================================================================
// PLOT → LEGEND REVERSE COMMUNICATION
// ============================================================================

QWidget* PlotWidget::findLegendRowForSeries(QAbstractSeries* series) const
{
    for (int i = 0; i < m_legendLayout->count(); ++i) {
        QLayoutItem* item = m_legendLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        QWidget* widget = item->widget();
        if (!widget->property("seriesToHighlight").isValid()) continue;
        const auto seriesList = widget->property("seriesToHighlight").value<QVector<QAbstractSeries*>>();
        if (seriesList.contains(series))
            return widget;
    }
    return nullptr;
}

void PlotWidget::highlightLegendRowForSeries(QAbstractSeries* series, bool hoverOn)
{
    QWidget* row = findLegendRowForSeries(series);
    if (!row) return;
    // Don't override a persistent click-highlight
    if (row->property("highlighted").toBool()) return;
    row->setStyleSheet(hoverOn ? "background-color: #f0f7ff;" : "");
}

void PlotWidget::selectLegendRowForSeries(QAbstractSeries* series)
{
    QWidget* row = findLegendRowForSeries(series);
    if (row)
        createToggleHandler(row);
}

QAbstractSeries* PlotWidget::findSeriesNearPoint(const QPoint& viewPos) const
{
    // viewPos is in QChartView viewport coordinates.
    // QGraphicsView::mapToScene() expects viewport coordinates directly — no
    // intermediate widget mapping needed.
    // mapToPosition() returns coordinates in QChart's local item space, which is the
    // same space as mapFromScene(), so the comparison is valid.
    QPointF scenePos = m_chartView->mapToScene(viewPos);
    QPointF chartPos = m_chart->mapFromScene(scenePos);

    const double lineThreshold    = 20.0;  // lines are thin, needs generous hit area
    const double scatterThreshold = 14.0;

    QAbstractSeries* nearest = nullptr;
    double minDist = std::numeric_limits<double>::max();

    for (QAbstractSeries* series : m_chart->series()) {
        if (!series->isVisible()) continue;

        if (auto* ls = qobject_cast<QLineSeries*>(series)) {
            const auto pts = ls->points();
            for (int i = 0; i + 1 < pts.size(); ++i) {
                QPointF a = m_chart->mapToPosition(pts[i],   series);
                QPointF b = m_chart->mapToPosition(pts[i+1], series);
                // Distance from chartPos to segment AB
                QPointF ab = b - a;
                QPointF ap = chartPos - a;
                double lenSq = ab.x()*ab.x() + ab.y()*ab.y();
                double t = (lenSq > 0) ? qBound(0.0, (ap.x()*ab.x() + ap.y()*ab.y()) / lenSq, 1.0) : 0.0;
                QPointF proj = a + t * ab;
                double dist = QLineF(chartPos, proj).length();
                if (dist < lineThreshold && dist < minDist) {
                    minDist = dist;
                    nearest = series;
                }
            }
        } else if (auto* ss = qobject_cast<QScatterSeries*>(series)) {
            double threshold = scatterThreshold + ss->markerSize() / 2.0;
            for (const QPointF& pt : ss->points()) {
                QPointF mapped = m_chart->mapToPosition(pt, series);
                double dist = QLineF(chartPos, mapped).length();
                if (dist < threshold && dist < minDist) {
                    minDist = dist;
                    nearest = series;
                }
            }
        }
    }
    return nearest;
}

// ============================================================================
// HOVER TOOLTIP
// ============================================================================

QPointF PlotWidget::findNearestDataPoint(QAbstractSeries* series, const QPoint& viewPos) const
{
    QPointF scenePos = m_chartView->mapToScene(viewPos);
    QPointF chartPos = m_chart->mapFromScene(scenePos);

    // Chart series points (may use virtual x in axis-break mode)
    QVector<QPointF> chartPoints;
    if (auto* ls = qobject_cast<QLineSeries*>(series))
        chartPoints = ls->points();
    else if (auto* ss = qobject_cast<QScatterSeries*>(series))
        chartPoints = ss->points();

    if (chartPoints.isEmpty()) return {};

    // Original data points from PlotData (real x values even in axis-break mode)
    QVector<QPointF> originalPoints;
    auto pd = m_seriesToPlotData.value(series);
    if (pd) originalPoints = pd->points;

    double minDist = std::numeric_limits<double>::max();
    int nearestIdx = 0;
    for (int i = 0; i < chartPoints.size(); ++i) {
        QPointF mapped = m_chart->mapToPosition(chartPoints[i], series);
        double dist = QLineF(chartPos, mapped).length();
        if (dist < minDist) { minDist = dist; nearestIdx = i; }
    }

    if (nearestIdx < originalPoints.size())
        return originalPoints[nearestIdx];
    return chartPoints.value(nearestIdx);
}

void PlotWidget::showHoverTooltip(QAbstractSeries* series, const QPointF& dataPoint, const QPoint& viewPos)
{
    auto pd = m_seriesToPlotData.value(series);
    if (!pd) return;

    // Variable display name and unit
    QPair<QString, QString> varInfo = DataProcessor::getVariableInfo(pd->variable);
    QString varName = varInfo.first.isEmpty() ? pd->variable : varInfo.first;
    QString unit    = varInfo.second;

    // X value
    QString xStr;
    if (m_currentXVar == "DATE") {
        xStr = QDateTime::fromMSecsSinceEpoch((qint64)dataPoint.x()).toString("MMM dd, yyyy");
    } else {
        xStr = QString::number(dataPoint.x(), 'f', 1);
    }

    // Y value — adaptive precision
    double y = dataPoint.y();
    QString yStr;
    if (qAbs(y) >= 0.01 && qAbs(y) < 1e6)
        yStr = QString::number(y, 'f', 2);
    else
        yStr = QString::number(y, 'g', 4);
    if (!unit.isEmpty()) yStr += " " + unit;

    QString typeStr = pd->isObserved ? "Obs" : "Sim";
    QString html = QString(
        "<div style='font-family:sans-serif;font-size:11px;'>"
        "<b>%1</b> <span style='color:#aad4ff;'>(%2)</span><br>"
        "<span style='color:#cccccc;'>%3</span><br>"
        "<span style='color:#88aacc;'>%4:</span> %5<br>"
        "<b style='font-size:12px;'>%6</b>"
        "</div>")
        .arg(varName.toHtmlEscaped())
        .arg(typeStr)
        .arg(pd->treatmentName.toHtmlEscaped())
        .arg(m_currentXVar.toHtmlEscaped())
        .arg(xStr.toHtmlEscaped())
        .arg(yStr.toHtmlEscaped());

    if (!m_hoverTooltip) {
        m_hoverTooltip = new QLabel(m_chartView);
        m_hoverTooltip->setStyleSheet(
            "background-color: rgba(255,255,255,242);"
            "color: #1a1a2e;"
            "border: 1px solid #4a6fa5;"
            "border-radius: 4px;"
            "padding: 5px 7px;"
        );
        m_hoverTooltip->setTextFormat(Qt::RichText);
        m_hoverTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    m_hoverTooltip->setText(html);
    m_hoverTooltip->adjustSize();

    // Position near cursor, keep within chartView bounds
    const int offsetX = 14, offsetY = -m_hoverTooltip->height() - 4;
    QPoint pos = viewPos + QPoint(offsetX, offsetY);
    QRect avail = m_chartView->rect();
    pos.setX(qBound(2, pos.x(), avail.right()  - m_hoverTooltip->width()  - 2));
    pos.setY(qBound(2, pos.y(), avail.bottom() - m_hoverTooltip->height() - 2));
    m_hoverTooltip->move(pos);
    m_hoverTooltip->show();
    m_hoverTooltip->raise();
}

void PlotWidget::hideHoverTooltip()
{
    if (m_hoverTooltip)
        m_hoverTooltip->hide();
}

void PlotWidget::resetAllHighlightedItems()
{
    // Reset all legend rows
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

    // Restore original pen/brush from PlotData — works for both single and multi-panel.
    // pd->pen/brush are stored at series creation time and never mutated, so they are
    // always the correct "unhighlighted" baseline regardless of m_tsPanelViews state.
    for (const auto &pd : m_plotDataList) {
        if (!pd || !pd->series) continue;
        QAbstractSeries* series = pd->series.data();
        if (QLineSeries* ls = qobject_cast<QLineSeries*>(series)) {
            ls->setPen(pd->pen);
        } else if (QScatterSeries* ss = qobject_cast<QScatterSeries*>(series)) {
            if (series->property("custom_shape").isValid()) {
                ss->setPen(Qt::NoPen);
                ss->setBrush(Qt::NoBrush);
            } else {
                ss->setPen(pd->pen);
                ss->setBrush(pd->brush);
            }
        }
    }
}

void PlotWidget::highlightPlotItems(const QVector<QAbstractSeries*>& seriesToHighlight)
{
    // Build a fast lookup set
    QSet<QAbstractSeries*> highlightSet(seriesToHighlight.begin(), seriesToHighlight.end());

    // Use m_plotDataList (covers both single and multi-panel) and baseline pens/brushes
    // stored at series creation time — no m_tsPanelViews branch needed.
    for (const auto &pd : m_plotDataList) {
        if (!pd || !pd->series) continue;
        QAbstractSeries* series = pd->series.data();
        bool isHighlighted = highlightSet.contains(series);
        bool isCustomShape = series->property("custom_shape").isValid();

        if (QLineSeries* ls = qobject_cast<QLineSeries*>(series)) {
            QPen pen = pd->pen;
            QColor c = pen.color();
            c.setAlphaF(isHighlighted ? 1.0 : 0.2);
            pen.setColor(c);
            pen.setWidth(isHighlighted ? 3 : 2);
            ls->setPen(pen);
        } else if (QScatterSeries* ss = qobject_cast<QScatterSeries*>(series)) {
            if (!isCustomShape) {
                QPen pen = pd->pen;
                QColor c = pen.color();
                c.setAlphaF(isHighlighted ? 1.0 : 0.2);
                pen.setColor(c);
                ss->setPen(pen);
                QBrush brush = pd->brush;
                QColor bc = brush.color();
                bc.setAlphaF(isHighlighted ? 1.0 : 0.2);
                brush.setColor(bc);
                ss->setBrush(brush);
            }
        }
    }
}

// ============================================================================
// OBS / SIM GLOBAL TOGGLE
// ============================================================================

void PlotWidget::updateObsSimHeaders()
{
    if (m_obsHeaderLabel) {
        if (m_obsVisible) {
            m_obsHeaderLabel->setText("<b>Obs.</b>");
            m_obsHeaderLabel->setStyleSheet("");
            m_obsHeaderLabel->setToolTip("Click to hide observed data");
        } else {
            m_obsHeaderLabel->setText("<s>Obs.</s>");
            m_obsHeaderLabel->setStyleSheet("color: #aaaaaa;");
            m_obsHeaderLabel->setToolTip("Click to show observed data");
        }
    }
    if (m_simHeaderLabel) {
        if (m_simVisible) {
            m_simHeaderLabel->setText("<b>Sim.</b>");
            m_simHeaderLabel->setStyleSheet("");
            m_simHeaderLabel->setToolTip("Click to hide simulated data");
        } else {
            m_simHeaderLabel->setText("<s>Sim.</s>");
            m_simHeaderLabel->setStyleSheet("color: #aaaaaa;");
            m_simHeaderLabel->setToolTip("Click to show simulated data");
        }
    }
}

void PlotWidget::setObsSeriesVisible(bool visible)
{
    m_obsVisible = visible;
    for (const auto& pd : m_plotDataList) {
        if (pd && pd->isObserved && pd->series)
            pd->series->setVisible(visible);
    }
}

void PlotWidget::setSimSeriesVisible(bool visible)
{
    m_simVisible = visible;
    for (const auto& pd : m_plotDataList) {
        if (pd && !pd->isObserved && pd->series)
            pd->series->setVisible(visible);
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

        if (m_symbol == "o") {
            painter.drawEllipse(center, symbolSize/2, symbolSize/2);
        } else if (m_symbol == "s") {
            QRectF rect(center.x() - symbolSize/2, center.y() - symbolSize/2, symbolSize, symbolSize);
            painter.drawRect(rect);
        } else if (m_symbol == "t") {
            QPolygonF triangle;
            triangle << QPointF(center.x(), center.y() - symbolSize/2)
                     << QPointF(center.x() + symbolSize/2, center.y() + symbolSize/2)
                     << QPointF(center.x() - symbolSize/2, center.y() + symbolSize/2);
            painter.drawPolygon(triangle);
        } else if (m_symbol == "diamond" || m_symbol == "d") {
            QPolygonF diamond;
            diamond << QPointF(center.x(), center.y() - symbolSize/2)
                    << QPointF(center.x() + symbolSize/2, center.y())
                    << QPointF(center.x(), center.y() + symbolSize/2)
                    << QPointF(center.x() - symbolSize/2, center.y());
            painter.drawPolygon(diamond);
        } else if (m_symbol == "v") {
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
