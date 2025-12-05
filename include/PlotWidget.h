#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QWidget>
#include <QResizeEvent>
#include <QtCharts/QChart>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLegend>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>
#include <QSpinBox>
#include "PlotSettingsDialog.h"
#include <QColorDialog>
#include <QLabel>
#include <QScrollArea>
#include <QListWidget>
#include <QSplitter>
#include <QMap>
#include <QVector>
#include <QSharedPointer>
#include <QPointer>
#include <QtCharts/QAbstractSeries>
#include "DataProcessor.h"

struct ErrorBarData {
    double meanX;
    double meanY;
    double errorValue;  // SD or SE
    int n;              // Number of replicates
    QPointF meanPoint() const { return QPointF(meanX, meanY); }
};

struct PlotData {
    QString crop;
    QString experiment;
    QString treatment;
    QString treatmentName;
    QVector<QPointF> points;
    QColor color;
    QString variable;
    bool isObserved;
    int lineStyleIndex; // New member for line style
    int symbolIndex;    // New member for symbol index
    QPointer<QAbstractSeries> series;  // Reference to the chart series
    QPen pen;
    QBrush brush;
    QString symbol;
    // Error bar data (for aggregated replicates)
    QVector<ErrorBarData> errorBars;  // Empty if not aggregated or no replicates
};

class ErrorBarChartView : public QChartView
{
    Q_OBJECT
public:
    explicit ErrorBarChartView(QChart *chart, QWidget *parent = nullptr);
    void setErrorBarData(const QMap<QAbstractSeries*, QVector<ErrorBarData>> &errorBars);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    QMap<QAbstractSeries*, QVector<ErrorBarData>> m_errorBars;
};

struct LegendTreatmentData {
    QString name;
    QString trtId;
    QString experimentId;
    QSharedPointer<PlotData> simData; // Pointer to PlotData
    QSharedPointer<PlotData> obsData; // Pointer to PlotData
};

struct LegendEntry {
    QString variable;
    QString displayName;
    QMap<QString, LegendTreatmentData> treatments;
};

class LegendSampleWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LegendSampleWidget(bool hasSymbol = false, const QPen& pen = QPen(), 
                               const QString& symbol = QString(), const QBrush& brush = QBrush(),
                               const QString& tooltip = QString(), QWidget* parent = nullptr);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    bool m_hasSymbol;
    QPen m_pen;
    QString m_symbol;
    QBrush m_brush;
};

class PlotWidget;

// Simple legend - no complex row widgets needed

struct ScalingInfo {
    double scaleFactor;
    double offset;
    QString originalUnit;
};

// Simplified legend - no complex sections needed

class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlotWidget(QWidget *parent = nullptr);
    ~PlotWidget();
    
    // Main plotting function (matching Python)
    void plotTimeSeries(
        const DataTable &simData,
        const QString &selectedFolder,
        const QStringList &selectedOutFiles,
        const QString &selectedExperiment,
        const QStringList &selectedTreatments,
        const QString &xVar,
        const QStringList &yVars,
        const DataTable &obsData,
        const QMap<QString, QMap<QString, QString>> &treatmentNames = QMap<QString, QMap<QString, QString>>()
    );
    
    // Scatter plot function for model evaluation (simulated vs measured)
    void plotScatter(
        const DataTable &evaluateData,
        const QString &xVar,
        const QString &yVar,
        const QStringList &selectedTreatments = QStringList(),
        const QMap<QString, QMap<QString, QString>> &treatmentNames = QMap<QString, QMap<QString, QString>>()
    );
    
    void setData(const DataTable &data);
    void setupAxes(const QString &xVar);
    void autoFitAxes();
    double calculateNiceMax(double rawMax);
    double calculateNiceInterval(double max);
    double calculateNiceXInterval(double range);
    double calculateNiceYInterval(double max);
    int calculateOptimalDateTickCount() const;
    void updatePlot(const QString &xVariable, const QString &yVariable, 
                   const QString &treatment = QString(), const QString &plotType = "Line");
    void clear();
    void clearChart();  // Clear chart without clearing data
    void exportPlot(const QString &filePath, const QString &format = "PNG");
    void exportPlot(const QString &filePath, const QString &format, int width, int height, int dpi);
    void exportPlotComposite(const QString &filePath, const QString &format, int width, int height, int dpi);
    void copyPlotToClipboard();  // Copy plot to clipboard
    QPixmap cropToContent(const QPixmap &source);
    void renderLegendContent(QPainter *painter, const QRect &rect);
    
    // Plot customization
    void setShowLegend(bool show);
    void setShowGrid(bool show);
    void setPlotTitle(const QString &title);
    void setAxisTitles(const QString &xTitle, const QString &yTitle);
    void setXAxisButtonsVisible(bool visible);  // Show/hide DAS, DAP, DATE buttons
    
    // Simple legend functionality (matching Python)
    
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    void plotUpdated();
    void errorOccurred(const QString &error);
    void metricsCalculated(const QVector<QMap<QString, QVariant>> &metrics);
    void xVariableChanged(const QString &xVariable);

private slots:
    void onPlotSettingsChanged();
    void onXAxisButtonClicked();
    void onSettingsButtonClicked();
    void applyPlotSettings(const PlotSettings &settings);

private:
    // Core plotting functions (matching Python structure)
    // DataTable loadSimulationData(const QString &selectedFolder, const QStringList &selectedOutFiles); // Removed
    
    
    // Data processing functions
    QMap<QString, QMap<QString, ScalingInfo>> calculateScalingFactors(const DataTable &simData, const DataTable &obsData, 
                                                               const QStringList &yVars);
    DataTable applyScaling(const DataTable &data, const QStringList &yVars);
    QMap<QString, QString> extractTreatmentNames(const DataTable &data);
    
    // Plotting functions
    void plotDatasets(const DataTable &simData, const DataTable &obsData,
                     const QString &xVar, const QStringList &yVars, 
                     const QStringList &treatments, const QString &selectedExperiment);
    QVector<ErrorBarData> aggregateReplicates(const QVector<QPointF> &points, const QString &xVar, double xTolerance = 0.01);
    void setupUI();
    void setupChart();
    void addSeriesToPlot(const QVector<PlotData> &plotDataList);
    void updateScalingLabel(const QStringList &yVars);
    void updatePlotWithScaling();
    
    void updateLegend(const QVector<PlotData> &plotDataList);
    void updateLegendAdvanced(const QMap<QString, QMap<QString, QVector<QSharedPointer<PlotData>>>>& legendEntries);
    void clearLegend();
    void setAxisLabels(const QString &xVar, const QStringList &yVars, const DataTable &data);
    QScatterSeries::MarkerShape getMarkerShape(const QString &symbol) const;
    QString getActualRenderedSymbol(const QString &originalSymbol) const;
    
    // Simple legend functions (matching Python)
    void createLegendRowFromData(const QMap<QString, QVariant>& treatmentData, const QString& varName, const QString& displayName);
    void createSimpleLegendRow(const LegendTreatmentData& treatmentData, const QString& varName, const QString& displayName);
    void createToggleHandler(QWidget* rowWidget);
    bool shouldCropLegend(int totalTreatments) const;
    QStringList cropTreatmentList(const QStringList& treatments, int maxEntries) const;
    QString getCropNameFromCode(const QString& cropCode) const;
    void highlightSeries(QAbstractSeries* series, bool highlight);
    
    // Legacy functions (kept for compatibility)
    void createLegendHeader();
    void createVariableSection(const QString& varName, const QString& displayName);
    void createLegendRow(const LegendTreatmentData& treatmentData, const QString& varName, const QString& displayName);
    
public:
    void resetAllHighlightedItems();
    void highlightPlotItems(const QVector<QAbstractSeries*>& seriesToHighlight);
    void resetPlotItemHighlights();

private slots:
    void onDasButtonClicked();
    void onDapButtonClicked();
    void onDateButtonClicked();

private:
    
    // X-axis management
    void setXAxisVariable(const QString &xVar);
    
    // Utility functions
    void styleChart();
    QColor getColorForTreatment(const QString &treatment, int index);
    int getMarkerIndexForVariable(const QString &variable);
    int getMarkerIndexForTreatment(const QString &treatment);
    QString getVariableGroup(const QString &variable);
    bool hasVariable(const QString &varName, const DataTable &data);
    void changeXAxis(const QString &newXVar);
    void calculateMetrics();
    void addTestData();
    QString getTreatmentDisplayName(const QString &trtId, const QString &experimentId, const QString &cropId = QString());
    QString getTreatmentNameFromData(const QString &treatment, const QString &experiment, const QString &crop);
    void testScalingFunctionality(); // TEMPORARY: Test scaling logic
    
    // UI Components
    QHBoxLayout *m_mainLayout;
    QWidget *m_leftContainer;
    QVBoxLayout *m_leftLayout;
    QChart *m_chart;
    ErrorBarChartView *m_chartView;
    
    // X-axis control buttons
    QWidget *m_bottomContainer;
    QHBoxLayout *m_bottomLayout;
    QPushButton *m_dateButton;
    QPushButton *m_dasButton;
    QPushButton *m_dapButton;
    QPushButton *m_settingsButton;
    QLabel *m_scalingLabel;
    
    // Legend area
    QScrollArea *m_legendScrollArea;
    QWidget *m_legendWidget;
    QVBoxLayout *m_legendLayout;
    
    // Simple legend (matching Python)
    
    // Data storage (matching Python structure)
    DataTable m_simData;
    DataTable m_obsData;
    QMap<QString, QMap<QString, ScalingInfo>> m_scaleFactors;
    QMap<QString, double> m_appliedScalingFactors;  // Simpler storage for label
    QMap<QString, QMap<QString, QString>> m_treatmentNames;
    QString m_currentXVar;
    QStringList m_currentYVars;
    QStringList m_currentTreatments;
    QString m_selectedExperiment;
    QString m_selectedFolder;
    
    // Legend management (simplified)
    QVector<QSharedPointer<PlotData>> m_plotDataList;
    QMap<QAbstractSeries*, QSharedPointer<PlotData>> m_seriesToPlotData;
    
    // Plot settings
    bool m_showLegend;
    bool m_showGrid;
    QString m_currentPlotType;
    QVector<QColor> m_plotColors;
    QVector<QString> m_markerSymbols;
    QMap<QString, QColor> m_treatmentColorMap;
    PlotSettings m_plotSettings;
    int m_maxLegendEntries;
    bool m_isScatterMode;
    
    // Data processor reference
    DataProcessor *m_dataProcessor;
};

#endif // PLOTWIDGET_H