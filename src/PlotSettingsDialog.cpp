#include "PlotSettingsDialog.h"
#include "PlotWidget.h"
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QFrame>
#include <QTabWidget>
#include <QColorDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChart>

PlotSettingsDialog::PlotSettingsDialog(const PlotSettings &currentSettings, PlotWidget *plotWidget, QWidget *parent)
    : QDialog(parent), m_settings(currentSettings), m_plotWidget(plotWidget)
{
    setWindowTitle("Plot Settings");
    setModal(true);
    resize(450, 600);

    // Detect whether the X axis is a date axis so setupUI can show the right widget
    if (m_plotWidget) {
        auto hAxes = m_plotWidget->chart()->axes(Qt::Horizontal);
        if (!hAxes.isEmpty())
            m_xAxisIsDate = qobject_cast<QDateTimeAxis*>(hAxes.first()) != nullptr;
    }

    setupUI();
}

PlotSettings PlotSettingsDialog::getSettings() const
{
    // Create a copy and update it with current control values
    PlotSettings settings = m_settings;
    
    // Update settings from controls
    settings.showGrid = m_showGridCheckBox->isChecked();
    settings.showMinorGrid = m_showMinorGridCheckBox->isChecked();
    settings.minorTickCount = m_minorTickCountSpinBox->value();
    settings.showLegend = m_showLegendCheckBox->isChecked();
    settings.legendPosition = m_legendPositionComboBox->currentData().toString();
    settings.legendX = m_legendXSpinBox->value();
    settings.legendY = m_legendYSpinBox->value();
    settings.showErrorBars = m_showErrorBarsCheckBox->isChecked();
    settings.errorBarType = m_errorBarTypeComboBox->currentData().toString();
    settings.lineWidth = m_lineWidthSpinBox->value();
    settings.markerSize = m_markerSizeSpinBox->value();
    settings.showAxisLabels = m_showAxisLabelsCheckBox->isChecked();
    settings.showAxisTitles = m_showAxisTitlesCheckBox->isChecked();
    settings.xAxisTitle = m_xAxisTitleEdit->text();
    settings.yAxisTitle = m_yAxisTitleEdit->text();
    settings.xAxisTickCount = m_xAxisTickCountSpinBox->value();
    settings.xAxisTickSpacing = m_xAxisTickSpacingSpinBox->value();
    settings.xAxisMinorTickCount = m_xAxisMinorTickCountSpinBox->value();
    settings.xAxisDecimals = m_xAxisDecimalsSpinBox->value();
    settings.yAxisTickCount = m_yAxisTickCountSpinBox->value();
    settings.yAxisTickSpacing = m_yAxisTickSpacingSpinBox->value();
    settings.yAxisMinorTickCount = m_yAxisMinorTickCountSpinBox->value();
    settings.yAxisDecimals = m_yAxisDecimalsSpinBox->value();
    settings.useCustomXMin = m_useCustomXMinCheckBox->isChecked();
    settings.xAxisMin = m_xAxisIsDate
        ? static_cast<double>(m_xAxisMinDateEdit->dateTime().toMSecsSinceEpoch())
        : m_xAxisMinSpinBox->value();
    settings.useCustomXMax = m_useCustomXMaxCheckBox->isChecked();
    settings.xAxisMax = m_xAxisIsDate
        ? static_cast<double>(m_xAxisMaxDateEdit->dateTime().toMSecsSinceEpoch())
        : m_xAxisMaxSpinBox->value();
    settings.useCustomYMin = m_useCustomYMinCheckBox->isChecked();
    settings.yAxisMin = m_yAxisMinSpinBox->value();
    settings.useCustomYMax = m_useCustomYMaxCheckBox->isChecked();
    settings.yAxisMax = m_yAxisMaxSpinBox->value();
    settings.plotTitle = m_plotTitleEdit->text();
    settings.exportWidth = m_exportWidthSpinBox->value();
    settings.exportHeight = m_exportHeightSpinBox->value();
    settings.exportDpi = m_exportDpiSpinBox->value();
    settings.fontFamily = m_fontFamilyComboBox->currentText();
    settings.titleFontSize = m_titleFontSizeSpinBox->value();
    settings.axisLabelFontSize = m_axisLabelFontSizeSpinBox->value();
    settings.axisTickFontSize = m_axisTickFontSizeSpinBox->value();
    settings.legendFontSize = m_legendFontSizeSpinBox->value();
    settings.boldTitle = m_boldTitleCheckBox->isChecked();
    settings.boldAxisLabels = m_boldAxisLabelsCheckBox->isChecked();

    // Scatter metrics — collect checked items
    settings.scatterMetrics.clear();
    for (auto it = m_scatterMetricCheckBoxes.begin(); it != m_scatterMetricCheckBoxes.end(); ++it) {
        if (it.value()->isChecked())
            settings.scatterMetrics.insert(it.key());
    }

    // Preserve treatment filter and available data unchanged
    settings.excludedSeriesKeys = m_settings.excludedSeriesKeys;
    settings.availableExperiments = m_settings.availableExperiments;
    settings.experimentTreatments = m_settings.experimentTreatments;
    settings.treatmentDisplayNames = m_settings.treatmentDisplayNames;
    settings.availableYVars = m_settings.availableYVars;
    settings.yVarDisplayNames = m_settings.yVarDisplayNames;

    return settings;
}

void PlotSettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Create tab widget for organized settings
    QTabWidget *tabWidget = new QTabWidget();
    
    // Grid & Axes Tab
    QWidget *gridAxesTab = new QWidget();
    QVBoxLayout *gridAxesLayout = new QVBoxLayout(gridAxesTab);
    
    // Grid settings group
    QGroupBox *gridGroup = new QGroupBox("Grid Settings");
    QGridLayout *gridLayout = new QGridLayout(gridGroup);
    
    m_showGridCheckBox = new QCheckBox("Show Grid Lines");
    m_showGridCheckBox->setChecked(m_settings.showGrid);
    gridLayout->addWidget(m_showGridCheckBox, 0, 0, 1, 2);
    
    m_showMinorGridCheckBox = new QCheckBox("Show Minor Grid Lines");
    m_showMinorGridCheckBox->setChecked(m_settings.showMinorGrid);
    gridLayout->addWidget(m_showMinorGridCheckBox, 1, 0, 1, 2);
    
    // Minor tick count is now per-axis (see Axis Settings group below)
    m_minorTickCountSpinBox = new QSpinBox(); // kept to avoid null pointer, hidden
    m_minorTickCountSpinBox->hide();
    
    gridAxesLayout->addWidget(gridGroup);
    
    // Axis settings group
    QGroupBox *axisGroup = new QGroupBox("Axis Settings");
    QGridLayout *axisLayout = new QGridLayout(axisGroup);
    
    m_showAxisLabelsCheckBox = new QCheckBox("Show Axis Labels");
    m_showAxisLabelsCheckBox->setChecked(m_settings.showAxisLabels);
    axisLayout->addWidget(m_showAxisLabelsCheckBox, 0, 0, 1, 2);
    
    m_showAxisTitlesCheckBox = new QCheckBox("Show Axis Titles");
    m_showAxisTitlesCheckBox->setChecked(m_settings.showAxisTitles);
    axisLayout->addWidget(m_showAxisTitlesCheckBox, 1, 0, 1, 2);
    
    axisLayout->addWidget(new QLabel("X-Axis Title:"), 2, 0);
    m_xAxisTitleEdit = new QLineEdit(m_settings.xAxisTitle);
    axisLayout->addWidget(m_xAxisTitleEdit, 2, 1);
    
    axisLayout->addWidget(new QLabel("Y-Axis Title:"), 3, 0);
    m_yAxisTitleEdit = new QLineEdit(m_settings.yAxisTitle);
    axisLayout->addWidget(m_yAxisTitleEdit, 3, 1);
    
    axisLayout->addWidget(new QLabel("X-Axis Tick Count:"), 4, 0);
    m_xAxisTickCountSpinBox = new QSpinBox();
    m_xAxisTickCountSpinBox->setRange(2, 20);
    m_xAxisTickCountSpinBox->setValue(m_settings.xAxisTickCount);
    m_xAxisTickCountSpinBox->setToolTip("Number of tick labels to show on X-axis");
    axisLayout->addWidget(m_xAxisTickCountSpinBox, 4, 1);
    
    axisLayout->addWidget(new QLabel("X-Axis Tick Spacing:"), 5, 0);
    m_xAxisTickSpacingSpinBox = new QDoubleSpinBox();
    m_xAxisTickSpacingSpinBox->setRange(0.0, 1000000.0);
    m_xAxisTickSpacingSpinBox->setDecimals(1);
    m_xAxisTickSpacingSpinBox->setValue(m_settings.xAxisTickSpacing);
    m_xAxisTickSpacingSpinBox->setSpecialValueText("Auto");
    m_xAxisTickSpacingSpinBox->setToolTip("Spacing between major tick labels (0 = automatic)\nOnly works for numeric axes (DAS, DAP, etc.)");
    axisLayout->addWidget(m_xAxisTickSpacingSpinBox, 5, 1);

    axisLayout->addWidget(new QLabel("X Minor Ticks per Major:"), 6, 0);
    m_xAxisMinorTickCountSpinBox = new QSpinBox();
    m_xAxisMinorTickCountSpinBox->setRange(0, 20);
    m_xAxisMinorTickCountSpinBox->setValue(m_settings.xAxisMinorTickCount);
    m_xAxisMinorTickCountSpinBox->setToolTip("Number of minor tick marks between each major tick on X-axis (0 = none)");
    axisLayout->addWidget(m_xAxisMinorTickCountSpinBox, 6, 1);

    axisLayout->addWidget(new QLabel("X Label Decimals:"), 7, 0);
    m_xAxisDecimalsSpinBox = new QSpinBox();
    m_xAxisDecimalsSpinBox->setRange(-1, 6);
    m_xAxisDecimalsSpinBox->setValue(m_settings.xAxisDecimals);
    m_xAxisDecimalsSpinBox->setSpecialValueText("Auto");
    m_xAxisDecimalsSpinBox->setToolTip("Decimal places for X-axis tick labels (-1 = auto)");
    axisLayout->addWidget(m_xAxisDecimalsSpinBox, 7, 1);

    axisLayout->addWidget(new QLabel("Y-Axis Tick Count:"), 8, 0);
    m_yAxisTickCountSpinBox = new QSpinBox();
    m_yAxisTickCountSpinBox->setRange(2, 30);
    m_yAxisTickCountSpinBox->setValue(m_settings.yAxisTickCount);
    m_yAxisTickCountSpinBox->setToolTip("Number of major tick labels on Y-axis");
    axisLayout->addWidget(m_yAxisTickCountSpinBox, 8, 1);

    axisLayout->addWidget(new QLabel("Y-Axis Tick Spacing:"), 9, 0);
    m_yAxisTickSpacingSpinBox = new QDoubleSpinBox();
    m_yAxisTickSpacingSpinBox->setRange(0.0, 1000000.0);
    m_yAxisTickSpacingSpinBox->setDecimals(1);
    m_yAxisTickSpacingSpinBox->setValue(m_settings.yAxisTickSpacing);
    m_yAxisTickSpacingSpinBox->setSpecialValueText("Auto");
    m_yAxisTickSpacingSpinBox->setToolTip("Spacing between major tick labels on Y-axis (0 = automatic)");
    axisLayout->addWidget(m_yAxisTickSpacingSpinBox, 9, 1);

    axisLayout->addWidget(new QLabel("Y Minor Ticks per Major:"), 10, 0);
    m_yAxisMinorTickCountSpinBox = new QSpinBox();
    m_yAxisMinorTickCountSpinBox->setRange(0, 20);
    m_yAxisMinorTickCountSpinBox->setValue(m_settings.yAxisMinorTickCount);
    m_yAxisMinorTickCountSpinBox->setToolTip("Number of minor tick marks between each major tick on Y-axis (0 = none)");
    axisLayout->addWidget(m_yAxisMinorTickCountSpinBox, 10, 1);

    axisLayout->addWidget(new QLabel("Y Label Decimals:"), 11, 0);
    m_yAxisDecimalsSpinBox = new QSpinBox();
    m_yAxisDecimalsSpinBox->setRange(-1, 6);
    m_yAxisDecimalsSpinBox->setValue(m_settings.yAxisDecimals);
    m_yAxisDecimalsSpinBox->setSpecialValueText("Auto");
    m_yAxisDecimalsSpinBox->setToolTip("Decimal places for Y-axis tick labels (-1 = auto)");
    axisLayout->addWidget(m_yAxisDecimalsSpinBox, 11, 1);

    // Axis range overrides
    QGroupBox *rangeGroup = new QGroupBox("Axis Range Overrides");
    rangeGroup->setToolTip("Override automatic axis limits. Leave unchecked for automatic scaling.");
    QGridLayout *rangeLayout = new QGridLayout(rangeGroup);

    // X Min
    m_useCustomXMinCheckBox = new QCheckBox("X Min:");
    m_useCustomXMinCheckBox->setChecked(m_settings.useCustomXMin);
    rangeLayout->addWidget(m_useCustomXMinCheckBox, 0, 0);

    m_xAxisMinSpinBox = new QDoubleSpinBox();
    m_xAxisMinSpinBox->setRange(-1e9, 1e9);
    m_xAxisMinSpinBox->setDecimals(4);
    m_xAxisMinSpinBox->setValue(m_settings.xAxisMin);
    m_xAxisMinSpinBox->setEnabled(m_settings.useCustomXMin);

    m_xAxisMinDateEdit = new QDateTimeEdit();
    m_xAxisMinDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_xAxisMinDateEdit->setCalendarPopup(true);
    m_xAxisMinDateEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m_settings.xAxisMin)));
    m_xAxisMinDateEdit->setEnabled(m_settings.useCustomXMin);

    if (m_xAxisIsDate) {
        m_xAxisMinSpinBox->hide();
        rangeLayout->addWidget(m_xAxisMinDateEdit, 0, 1);
        connect(m_useCustomXMinCheckBox, &QCheckBox::toggled, m_xAxisMinDateEdit, &QDateTimeEdit::setEnabled);
    } else {
        m_xAxisMinDateEdit->hide();
        rangeLayout->addWidget(m_xAxisMinSpinBox, 0, 1);
        connect(m_useCustomXMinCheckBox, &QCheckBox::toggled, m_xAxisMinSpinBox, &QDoubleSpinBox::setEnabled);
    }

    // X Max
    m_useCustomXMaxCheckBox = new QCheckBox("X Max:");
    m_useCustomXMaxCheckBox->setChecked(m_settings.useCustomXMax);
    rangeLayout->addWidget(m_useCustomXMaxCheckBox, 1, 0);

    m_xAxisMaxSpinBox = new QDoubleSpinBox();
    m_xAxisMaxSpinBox->setRange(-1e9, 1e9);
    m_xAxisMaxSpinBox->setDecimals(4);
    m_xAxisMaxSpinBox->setValue(m_settings.xAxisMax);
    m_xAxisMaxSpinBox->setEnabled(m_settings.useCustomXMax);

    m_xAxisMaxDateEdit = new QDateTimeEdit();
    m_xAxisMaxDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_xAxisMaxDateEdit->setCalendarPopup(true);
    m_xAxisMaxDateEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m_settings.xAxisMax)));
    m_xAxisMaxDateEdit->setEnabled(m_settings.useCustomXMax);

    if (m_xAxisIsDate) {
        m_xAxisMaxSpinBox->hide();
        rangeLayout->addWidget(m_xAxisMaxDateEdit, 1, 1);
        connect(m_useCustomXMaxCheckBox, &QCheckBox::toggled, m_xAxisMaxDateEdit, &QDateTimeEdit::setEnabled);
    } else {
        m_xAxisMaxDateEdit->hide();
        rangeLayout->addWidget(m_xAxisMaxSpinBox, 1, 1);
        connect(m_useCustomXMaxCheckBox, &QCheckBox::toggled, m_xAxisMaxSpinBox, &QDoubleSpinBox::setEnabled);
    }

    // Y Min
    m_useCustomYMinCheckBox = new QCheckBox("Y Min:");
    m_useCustomYMinCheckBox->setChecked(m_settings.useCustomYMin);
    m_yAxisMinSpinBox = new QDoubleSpinBox();
    m_yAxisMinSpinBox->setRange(-1e9, 1e9);
    m_yAxisMinSpinBox->setDecimals(4);
    m_yAxisMinSpinBox->setValue(m_settings.yAxisMin);
    m_yAxisMinSpinBox->setEnabled(m_settings.useCustomYMin);
    connect(m_useCustomYMinCheckBox, &QCheckBox::toggled, m_yAxisMinSpinBox, &QDoubleSpinBox::setEnabled);
    rangeLayout->addWidget(m_useCustomYMinCheckBox, 2, 0);
    rangeLayout->addWidget(m_yAxisMinSpinBox, 2, 1);

    // Y Max
    m_useCustomYMaxCheckBox = new QCheckBox("Y Max:");
    m_useCustomYMaxCheckBox->setChecked(m_settings.useCustomYMax);
    m_yAxisMaxSpinBox = new QDoubleSpinBox();
    m_yAxisMaxSpinBox->setRange(-1e9, 1e9);
    m_yAxisMaxSpinBox->setDecimals(4);
    m_yAxisMaxSpinBox->setValue(m_settings.yAxisMax);
    m_yAxisMaxSpinBox->setEnabled(m_settings.useCustomYMax);
    connect(m_useCustomYMaxCheckBox, &QCheckBox::toggled, m_yAxisMaxSpinBox, &QDoubleSpinBox::setEnabled);
    rangeLayout->addWidget(m_useCustomYMaxCheckBox, 3, 0);
    rangeLayout->addWidget(m_yAxisMaxSpinBox, 3, 1);

    gridAxesLayout->addWidget(axisGroup);
    gridAxesLayout->addWidget(rangeGroup);
    gridAxesLayout->addStretch();
    
    tabWidget->addTab(gridAxesTab, "Grid & Axes");
    
    // Appearance Tab — wrapped in a scroll area so all controls are reachable
    QWidget *appearanceTabScroll = new QWidget();
    QScrollArea *appearanceScrollArea = new QScrollArea();
    appearanceScrollArea->setWidgetResizable(true);
    appearanceScrollArea->setFrameShape(QFrame::NoFrame);
    QWidget *appearanceTab = new QWidget();
    QVBoxLayout *appearanceLayout = new QVBoxLayout(appearanceTab);
    
    // Legend settings group
    QGroupBox *legendGroup = new QGroupBox("Legend Settings");
    QVBoxLayout *legendLayout = new QVBoxLayout(legendGroup);
    
    m_showLegendCheckBox = new QCheckBox("Show Legend");
    m_showLegendCheckBox->setChecked(m_settings.showLegend);
    legendLayout->addWidget(m_showLegendCheckBox);

    QHBoxLayout *legendPosLayout = new QHBoxLayout();
    legendPosLayout->addWidget(new QLabel("Legend Position (export):"));
    m_legendPositionComboBox = new QComboBox();
    m_legendPositionComboBox->addItem("Outside Right", "outside-right");
    m_legendPositionComboBox->addItem("Inside (custom X/Y)", "inside-custom");
    int posIdx = m_legendPositionComboBox->findData(m_settings.legendPosition);
    if (posIdx >= 0) m_legendPositionComboBox->setCurrentIndex(posIdx);
    legendPosLayout->addWidget(m_legendPositionComboBox);
    legendPosLayout->addStretch();
    legendLayout->addLayout(legendPosLayout);

    QHBoxLayout *legendXYLayout = new QHBoxLayout();
    legendXYLayout->addWidget(new QLabel("X (%):"));
    m_legendXSpinBox = new QDoubleSpinBox();
    m_legendXSpinBox->setRange(0.0, 100.0);
    m_legendXSpinBox->setDecimals(1);
    m_legendXSpinBox->setSingleStep(1.0);
    m_legendXSpinBox->setValue(m_settings.legendX);
    m_legendXSpinBox->setToolTip("Horizontal position of legend as % of plot area width (0=left, 100=right)");
    m_legendXSpinBox->setFixedWidth(70);
    legendXYLayout->addWidget(m_legendXSpinBox);
    legendXYLayout->addSpacing(12);
    legendXYLayout->addWidget(new QLabel("Y (%):"));
    m_legendYSpinBox = new QDoubleSpinBox();
    m_legendYSpinBox->setRange(0.0, 100.0);
    m_legendYSpinBox->setDecimals(1);
    m_legendYSpinBox->setSingleStep(1.0);
    m_legendYSpinBox->setValue(m_settings.legendY);
    m_legendYSpinBox->setToolTip("Vertical position of legend as % of plot area height (0=top, 100=bottom)");
    m_legendYSpinBox->setFixedWidth(70);
    legendXYLayout->addWidget(m_legendYSpinBox);
    legendXYLayout->addStretch();
    legendLayout->addLayout(legendXYLayout);
    
    // Error bar settings
    m_showErrorBarsCheckBox = new QCheckBox("Show Error Bars (Mean ± SD/SE)");
    m_showErrorBarsCheckBox->setChecked(m_settings.showErrorBars);
    m_showErrorBarsCheckBox->setToolTip("Aggregate replicates and show mean with error bars");
    legendLayout->addWidget(m_showErrorBarsCheckBox);
    
    QHBoxLayout *errorBarTypeLayout = new QHBoxLayout();
    errorBarTypeLayout->addWidget(new QLabel("Error Bar Type:"));
    m_errorBarTypeComboBox = new QComboBox();
    m_errorBarTypeComboBox->addItem("Standard Deviation (SD)", "SD");
    m_errorBarTypeComboBox->addItem("Standard Error (SE)", "SE");
    int errorBarIndex = m_errorBarTypeComboBox->findData(m_settings.errorBarType);
    if (errorBarIndex >= 0) {
        m_errorBarTypeComboBox->setCurrentIndex(errorBarIndex);
    }
    errorBarTypeLayout->addWidget(m_errorBarTypeComboBox);
    errorBarTypeLayout->addStretch();
    legendLayout->addLayout(errorBarTypeLayout);
    
    appearanceLayout->addWidget(legendGroup);

    // Scatter panel metrics group
    QGroupBox *scatterMetricsGroup = new QGroupBox("Scatter Panel Metrics");
    QGridLayout *scatterMetricsLayout = new QGridLayout(scatterMetricsGroup);
    scatterMetricsGroup->setToolTip("Choose which statistics to display inside each scatter panel");

    QStringList metricOptions = {"RMSE", "R²", "d-stat", "BIAS", "MSEs/MSE", "MSEu/MSE", "N"};
    int col = 0, row = 0;
    for (const QString &metric : metricOptions) {
        QCheckBox *cb = new QCheckBox(metric);
        cb->setChecked(m_settings.scatterMetrics.contains(metric));
        m_scatterMetricCheckBoxes[metric] = cb;
        scatterMetricsLayout->addWidget(cb, row, col);
        col++;
        if (col >= 3) { col = 0; row++; }
    }
    appearanceLayout->addWidget(scatterMetricsGroup);

    // Plot appearance group
    QGroupBox *plotGroup = new QGroupBox("Plot Appearance");
    QGridLayout *plotLayout = new QGridLayout(plotGroup);
    
    plotLayout->addWidget(new QLabel("Plot Title:"), 0, 0);
    m_plotTitleEdit = new QLineEdit(m_settings.plotTitle);
    plotLayout->addWidget(m_plotTitleEdit, 0, 1);
    
    plotLayout->addWidget(new QLabel("Background Color:"), 1, 0);
    m_backgroundColorButton = new QPushButton();
    m_backgroundColorButton->setFixedSize(50, 30);
    updateColorButton(m_backgroundColorButton, m_settings.backgroundColor);
    connect(m_backgroundColorButton, &QPushButton::clicked, this, &PlotSettingsDialog::onBackgroundColorClicked);
    plotLayout->addWidget(m_backgroundColorButton, 1, 1);
    
    plotLayout->addWidget(new QLabel("Plot Area Color:"), 2, 0);
    m_plotAreaColorButton = new QPushButton();
    m_plotAreaColorButton->setFixedSize(50, 30);
    updateColorButton(m_plotAreaColorButton, m_settings.plotAreaColor);
    connect(m_plotAreaColorButton, &QPushButton::clicked, this, &PlotSettingsDialog::onPlotAreaColorClicked);
    plotLayout->addWidget(m_plotAreaColorButton, 2, 1);
    
    appearanceLayout->addWidget(plotGroup);
    appearanceLayout->addStretch();

    appearanceScrollArea->setWidget(appearanceTab);
    tabWidget->addTab(appearanceScrollArea, "Appearance");
    
    // Lines & Markers Tab
    QWidget *linesMarkersTab = new QWidget();
    QVBoxLayout *linesMarkersLayout = new QVBoxLayout(linesMarkersTab);
    
    // Line settings group
    QGroupBox *lineGroup = new QGroupBox("Line Settings");
    QGridLayout *lineLayout = new QGridLayout(lineGroup);
    
    lineLayout->addWidget(new QLabel("Line Width:"), 0, 0);
    m_lineWidthSpinBox = new QSpinBox();
    m_lineWidthSpinBox->setRange(1, 10);
    m_lineWidthSpinBox->setValue(m_settings.lineWidth);
    lineLayout->addWidget(m_lineWidthSpinBox, 0, 1);
    
    linesMarkersLayout->addWidget(lineGroup);
    
    // Marker settings group
    QGroupBox *markerGroup = new QGroupBox("Marker Settings");
    QGridLayout *markerLayout = new QGridLayout(markerGroup);
    
    markerLayout->addWidget(new QLabel("Marker Size:"), 0, 0);
    m_markerSizeSpinBox = new QSpinBox();
    m_markerSizeSpinBox->setRange(4, 20);
    m_markerSizeSpinBox->setValue(m_settings.markerSize);
    markerLayout->addWidget(m_markerSizeSpinBox, 0, 1);
    
    linesMarkersLayout->addWidget(markerGroup);
    linesMarkersLayout->addStretch();
    
    tabWidget->addTab(linesMarkersTab, "Lines & Markers");
    
    // Export Tab
    QWidget *exportTab = new QWidget();
    QVBoxLayout *exportLayout = new QVBoxLayout(exportTab);
    
    QGroupBox *exportGroup = new QGroupBox("Export Settings");
    QGridLayout *exportGridLayout = new QGridLayout(exportGroup);
    
    exportGridLayout->addWidget(new QLabel("Width (pixels):"), 0, 0);
    m_exportWidthSpinBox = new QSpinBox();
    m_exportWidthSpinBox->setRange(100, 5000);
    m_exportWidthSpinBox->setValue(m_settings.exportWidth);
    exportGridLayout->addWidget(m_exportWidthSpinBox, 0, 1);
    
    exportGridLayout->addWidget(new QLabel("Height (pixels):"), 1, 0);
    m_exportHeightSpinBox = new QSpinBox();
    m_exportHeightSpinBox->setRange(100, 5000);
    m_exportHeightSpinBox->setValue(m_settings.exportHeight);
    exportGridLayout->addWidget(m_exportHeightSpinBox, 1, 1);
    
    exportGridLayout->addWidget(new QLabel("DPI:"), 2, 0);
    m_exportDpiSpinBox = new QSpinBox();
    m_exportDpiSpinBox->setRange(72, 600);
    m_exportDpiSpinBox->setValue(m_settings.exportDpi);
    exportGridLayout->addWidget(m_exportDpiSpinBox, 2, 1);
    
    exportLayout->addWidget(exportGroup);
    exportLayout->addStretch();
    
    tabWidget->addTab(exportTab, "Export");

    // Font Tab
    QWidget *fontTab = new QWidget();
    QVBoxLayout *fontLayout = new QVBoxLayout(fontTab);

    QGroupBox *fontGroup = new QGroupBox("Font Settings");
    QGridLayout *fontGridLayout = new QGridLayout(fontGroup);

    // Font family
    fontGridLayout->addWidget(new QLabel("Font Family:"), 0, 0);
    m_fontFamilyComboBox = new QComboBox();
    m_fontFamilyComboBox->addItems({"Arial", "Times New Roman", "Courier New", "Helvetica", "Verdana", "Georgia", "Calibri", "Tahoma"});
    int fontFamilyIndex = m_fontFamilyComboBox->findText(m_settings.fontFamily);
    if (fontFamilyIndex >= 0) {
        m_fontFamilyComboBox->setCurrentIndex(fontFamilyIndex);
    }
    fontGridLayout->addWidget(m_fontFamilyComboBox, 0, 1);

    // Title font size
    fontGridLayout->addWidget(new QLabel("Title Font Size:"), 1, 0);
    m_titleFontSizeSpinBox = new QSpinBox();
    m_titleFontSizeSpinBox->setRange(8, 36);
    m_titleFontSizeSpinBox->setValue(m_settings.titleFontSize);
    fontGridLayout->addWidget(m_titleFontSizeSpinBox, 1, 1);

    // Bold title checkbox
    m_boldTitleCheckBox = new QCheckBox("Bold Title");
    m_boldTitleCheckBox->setChecked(m_settings.boldTitle);
    fontGridLayout->addWidget(m_boldTitleCheckBox, 1, 2);

    // Axis label font size
    fontGridLayout->addWidget(new QLabel("Axis Label Font Size:"), 2, 0);
    m_axisLabelFontSizeSpinBox = new QSpinBox();
    m_axisLabelFontSizeSpinBox->setRange(6, 24);
    m_axisLabelFontSizeSpinBox->setValue(m_settings.axisLabelFontSize);
    fontGridLayout->addWidget(m_axisLabelFontSizeSpinBox, 2, 1);

    // Bold axis labels checkbox
    m_boldAxisLabelsCheckBox = new QCheckBox("Bold Axis Labels");
    m_boldAxisLabelsCheckBox->setChecked(m_settings.boldAxisLabels);
    fontGridLayout->addWidget(m_boldAxisLabelsCheckBox, 2, 2);

    // Axis tick font size
    fontGridLayout->addWidget(new QLabel("Axis Tick Font Size:"), 3, 0);
    m_axisTickFontSizeSpinBox = new QSpinBox();
    m_axisTickFontSizeSpinBox->setRange(6, 20);
    m_axisTickFontSizeSpinBox->setValue(m_settings.axisTickFontSize);
    fontGridLayout->addWidget(m_axisTickFontSizeSpinBox, 3, 1);

    // Legend font size
    fontGridLayout->addWidget(new QLabel("Legend Font Size:"), 4, 0);
    m_legendFontSizeSpinBox = new QSpinBox();
    m_legendFontSizeSpinBox->setRange(6, 20);
    m_legendFontSizeSpinBox->setValue(m_settings.legendFontSize);
    fontGridLayout->addWidget(m_legendFontSizeSpinBox, 4, 1);

    fontLayout->addWidget(fontGroup);
    fontLayout->addStretch();

    tabWidget->addTab(fontTab, "Fonts");

    mainLayout->addWidget(tabWidget);
    
    // Control buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    m_resetButton = new QPushButton("Reset to Defaults");
    connect(m_resetButton, &QPushButton::clicked, this, &PlotSettingsDialog::onResetDefaults);
    buttonLayout->addWidget(m_resetButton);
    
    m_previewButton = new QPushButton("Preview");
    connect(m_previewButton, &QPushButton::clicked, this, &PlotSettingsDialog::onPreviewSettings);
    buttonLayout->addWidget(m_previewButton);
    
    m_exportButton = new QPushButton("Export Plot");
    connect(m_exportButton, &QPushButton::clicked, this, &PlotSettingsDialog::onExportPlot);
    buttonLayout->addWidget(m_exportButton);
    
    buttonLayout->addStretch();
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    
    mainLayout->addLayout(buttonLayout);
}

void PlotSettingsDialog::updateColorButton(QPushButton *button, const QColor &color)
{
    QString colorStyle = QString("background-color: rgb(%1, %2, %3); border: 1px solid black;")
                        .arg(color.red()).arg(color.green()).arg(color.blue());
    button->setStyleSheet(colorStyle);
}

void PlotSettingsDialog::onBackgroundColorClicked()
{
    QColor color = QColorDialog::getColor(m_settings.backgroundColor, this, "Select Background Color");
    if (color.isValid()) {
        m_settings.backgroundColor = color;
        updateColorButton(m_backgroundColorButton, color);
    }
}

void PlotSettingsDialog::onPlotAreaColorClicked()
{
    QColor color = QColorDialog::getColor(m_settings.plotAreaColor, this, "Select Plot Area Color");
    if (color.isValid()) {
        m_settings.plotAreaColor = color;
        updateColorButton(m_plotAreaColorButton, color);
    }
}

void PlotSettingsDialog::onResetDefaults()
{
    PlotSettings defaults;
    m_settings = defaults;
    
    // Update all controls with default values
    m_showGridCheckBox->setChecked(defaults.showGrid);
    m_showMinorGridCheckBox->setChecked(defaults.showMinorGrid);
    m_minorTickCountSpinBox->setValue(defaults.minorTickCount);
    m_showLegendCheckBox->setChecked(defaults.showLegend);
    { int i = m_legendPositionComboBox->findData(defaults.legendPosition); if (i >= 0) m_legendPositionComboBox->setCurrentIndex(i); }
    m_legendXSpinBox->setValue(defaults.legendX);
    m_legendYSpinBox->setValue(defaults.legendY);
    m_showErrorBarsCheckBox->setChecked(defaults.showErrorBars);
    int defaultErrorBarIndex = m_errorBarTypeComboBox->findData(defaults.errorBarType);
    if (defaultErrorBarIndex >= 0) {
        m_errorBarTypeComboBox->setCurrentIndex(defaultErrorBarIndex);
    }
    m_lineWidthSpinBox->setValue(defaults.lineWidth);
    m_markerSizeSpinBox->setValue(defaults.markerSize);
    m_showAxisLabelsCheckBox->setChecked(defaults.showAxisLabels);
    m_showAxisTitlesCheckBox->setChecked(defaults.showAxisTitles);
    m_xAxisTitleEdit->setText(defaults.xAxisTitle);
    m_yAxisTitleEdit->setText(defaults.yAxisTitle);
    m_xAxisTickCountSpinBox->setValue(defaults.xAxisTickCount);
    m_xAxisTickSpacingSpinBox->setValue(defaults.xAxisTickSpacing);
    m_xAxisMinorTickCountSpinBox->setValue(defaults.xAxisMinorTickCount);
    m_xAxisDecimalsSpinBox->setValue(defaults.xAxisDecimals);
    m_yAxisTickCountSpinBox->setValue(defaults.yAxisTickCount);
    m_yAxisTickSpacingSpinBox->setValue(defaults.yAxisTickSpacing);
    m_yAxisMinorTickCountSpinBox->setValue(defaults.yAxisMinorTickCount);
    m_yAxisDecimalsSpinBox->setValue(defaults.yAxisDecimals);
    m_useCustomXMinCheckBox->setChecked(defaults.useCustomXMin);
    m_xAxisMinSpinBox->setValue(defaults.xAxisMin);
    m_xAxisMinSpinBox->setEnabled(defaults.useCustomXMin);
    m_xAxisMinDateEdit->setEnabled(defaults.useCustomXMin);
    m_useCustomXMaxCheckBox->setChecked(defaults.useCustomXMax);
    m_xAxisMaxSpinBox->setValue(defaults.xAxisMax);
    m_xAxisMaxSpinBox->setEnabled(defaults.useCustomXMax);
    m_xAxisMaxDateEdit->setEnabled(defaults.useCustomXMax);
    m_useCustomYMinCheckBox->setChecked(defaults.useCustomYMin);
    m_yAxisMinSpinBox->setValue(defaults.yAxisMin);
    m_yAxisMinSpinBox->setEnabled(defaults.useCustomYMin);
    m_useCustomYMaxCheckBox->setChecked(defaults.useCustomYMax);
    m_yAxisMaxSpinBox->setValue(defaults.yAxisMax);
    m_yAxisMaxSpinBox->setEnabled(defaults.useCustomYMax);
    m_plotTitleEdit->setText(defaults.plotTitle);
    updateColorButton(m_backgroundColorButton, defaults.backgroundColor);
    updateColorButton(m_plotAreaColorButton, defaults.plotAreaColor);
    m_exportWidthSpinBox->setValue(defaults.exportWidth);
    m_exportHeightSpinBox->setValue(defaults.exportHeight);
    m_exportDpiSpinBox->setValue(defaults.exportDpi);
    int defaultFontIndex = m_fontFamilyComboBox->findText(defaults.fontFamily);
    if (defaultFontIndex >= 0) {
        m_fontFamilyComboBox->setCurrentIndex(defaultFontIndex);
    }
    m_titleFontSizeSpinBox->setValue(defaults.titleFontSize);
    m_axisLabelFontSizeSpinBox->setValue(defaults.axisLabelFontSize);
    m_axisTickFontSizeSpinBox->setValue(defaults.axisTickFontSize);
    m_legendFontSizeSpinBox->setValue(defaults.legendFontSize);
    m_boldTitleCheckBox->setChecked(defaults.boldTitle);
    m_boldAxisLabelsCheckBox->setChecked(defaults.boldAxisLabels);

}

void PlotSettingsDialog::onPreviewSettings()
{
    // Update settings from controls
    m_settings.showGrid = m_showGridCheckBox->isChecked();
    m_settings.showMinorGrid = m_showMinorGridCheckBox->isChecked();
    m_settings.minorTickCount = m_minorTickCountSpinBox->value();
    m_settings.showLegend = m_showLegendCheckBox->isChecked();
    m_settings.legendPosition = m_legendPositionComboBox->currentData().toString();
    m_settings.legendX = m_legendXSpinBox->value();
    m_settings.legendY = m_legendYSpinBox->value();
    m_settings.showErrorBars = m_showErrorBarsCheckBox->isChecked();
    m_settings.errorBarType = m_errorBarTypeComboBox->currentData().toString();
    m_settings.lineWidth = m_lineWidthSpinBox->value();
    m_settings.markerSize = m_markerSizeSpinBox->value();
    m_settings.showAxisLabels = m_showAxisLabelsCheckBox->isChecked();
    m_settings.showAxisTitles = m_showAxisTitlesCheckBox->isChecked();
    m_settings.xAxisTitle = m_xAxisTitleEdit->text();
    m_settings.yAxisTitle = m_yAxisTitleEdit->text();
    m_settings.xAxisTickCount = m_xAxisTickCountSpinBox->value();
    m_settings.xAxisTickSpacing = m_xAxisTickSpacingSpinBox->value();
    m_settings.plotTitle = m_plotTitleEdit->text();
    m_settings.exportWidth = m_exportWidthSpinBox->value();
    m_settings.exportHeight = m_exportHeightSpinBox->value();
    m_settings.exportDpi = m_exportDpiSpinBox->value();
    
    // Show a preview message for now
    QMessageBox::information(this, "Preview", 
                            QString("Preview would apply these settings:\n"
                                   "Grid: %1\n"
                                   "Minor Grid: %2\n"
                                   "Minor Ticks: %3\n"
                                   "Legend: %4\n"
                                   "Line Width: %5\n"
                                   "Marker Size: %6")
                            .arg(m_settings.showGrid ? "On" : "Off")
                            .arg(m_settings.showMinorGrid ? "On" : "Off")
                            .arg(m_settings.minorTickCount)
                            .arg(m_settings.showLegend ? "On" : "Off")
                            .arg(m_settings.lineWidth)
                            .arg(m_settings.markerSize));
}

void PlotSettingsDialog::onExportPlot()
{
    if (!m_plotWidget) {
        QMessageBox::warning(this, "Export Error", "No plot widget available for export.");
        return;
    }
    
    // Get export settings from the dialog
    int width = m_exportWidthSpinBox->value();
    int height = m_exportHeightSpinBox->value();
    int dpi = m_exportDpiSpinBox->value();
    
    // Open file dialog to choose export location and format
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString fileName = QFileDialog::getSaveFileName(this, 
        "Export Plot", 
        defaultPath + "/plot.png",
        "PNG Image (*.png);;JPEG Image (*.jpg);;BMP Image (*.bmp);;PDF Document (*.pdf)");
    
    if (fileName.isEmpty()) {
        return; // User cancelled
    }
    
    // Determine format from file extension
    QString format = "PNG";
    if (fileName.toLower().endsWith(".jpg") || fileName.toLower().endsWith(".jpeg")) {
        format = "JPG";
    } else if (fileName.toLower().endsWith(".bmp")) {
        format = "BMP";
    } else if (fileName.toLower().endsWith(".pdf")) {
        format = "PDF";
    }
    
    // Try the composite export method first
    m_plotWidget->exportPlotComposite(fileName, format, width, height, dpi);
    
    // Create message box with OK and View buttons
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Export Complete");
    msgBox.setText(QString("Plot exported successfully to:\n%1\n\nDimensions: %2 x %3 pixels\nDPI: %4")
                   .arg(fileName)
                   .arg(width)
                   .arg(height)
                   .arg(dpi));
    msgBox.setIcon(QMessageBox::Information);
    
    // Add View button
    QPushButton *viewButton = msgBox.addButton("View", QMessageBox::ActionRole);
    QPushButton *okButton = msgBox.addButton(QMessageBox::Ok);
    
    // Execute the message box
    msgBox.exec();
    
    // If View button was clicked, open the file
    if (msgBox.clickedButton() == viewButton) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
    }
}

