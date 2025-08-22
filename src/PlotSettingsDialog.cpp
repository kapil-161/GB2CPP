#include "PlotSettingsDialog.h"
#include "PlotWidget.h"
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QColorDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>

PlotSettingsDialog::PlotSettingsDialog(const PlotSettings &currentSettings, PlotWidget *plotWidget, QWidget *parent)
    : QDialog(parent), m_settings(currentSettings), m_plotWidget(plotWidget)
{
    setWindowTitle("Plot Settings");
    setModal(true);
    resize(450, 600);
    
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
    settings.lineWidth = m_lineWidthSpinBox->value();
    settings.markerSize = m_markerSizeSpinBox->value();
    settings.showAxisLabels = m_showAxisLabelsCheckBox->isChecked();
    settings.showAxisTitles = m_showAxisTitlesCheckBox->isChecked();
    settings.xAxisTitle = m_xAxisTitleEdit->text();
    settings.yAxisTitle = m_yAxisTitleEdit->text();
    settings.xAxisTickCount = m_xAxisTickCountSpinBox->value();
    settings.xAxisTickSpacing = m_xAxisTickSpacingSpinBox->value();
    settings.plotTitle = m_plotTitleEdit->text();
    settings.exportWidth = m_exportWidthSpinBox->value();
    settings.exportHeight = m_exportHeightSpinBox->value();
    settings.exportDpi = m_exportDpiSpinBox->value();
    
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
    
    gridLayout->addWidget(new QLabel("Minor Ticks per Major Tick:"), 2, 0);
    m_minorTickCountSpinBox = new QSpinBox();
    m_minorTickCountSpinBox->setRange(1, 10);
    m_minorTickCountSpinBox->setValue(m_settings.minorTickCount);
    gridLayout->addWidget(m_minorTickCountSpinBox, 2, 1);
    
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
    m_xAxisTickSpacingSpinBox->setRange(0.0, 1000.0);
    m_xAxisTickSpacingSpinBox->setDecimals(1);
    m_xAxisTickSpacingSpinBox->setValue(m_settings.xAxisTickSpacing);
    m_xAxisTickSpacingSpinBox->setSpecialValueText("Auto");
    m_xAxisTickSpacingSpinBox->setToolTip("Custom spacing between tick labels (0 = automatic)\nOnly works for numeric axes (DAS, DAP, etc.)\nDate axes use tick count only");
    axisLayout->addWidget(m_xAxisTickSpacingSpinBox, 5, 1);
    
    gridAxesLayout->addWidget(axisGroup);
    gridAxesLayout->addStretch();
    
    tabWidget->addTab(gridAxesTab, "Grid & Axes");
    
    // Appearance Tab
    QWidget *appearanceTab = new QWidget();
    QVBoxLayout *appearanceLayout = new QVBoxLayout(appearanceTab);
    
    // Legend settings group
    QGroupBox *legendGroup = new QGroupBox("Legend Settings");
    QVBoxLayout *legendLayout = new QVBoxLayout(legendGroup);
    
    m_showLegendCheckBox = new QCheckBox("Show Legend");
    m_showLegendCheckBox->setChecked(m_settings.showLegend);
    legendLayout->addWidget(m_showLegendCheckBox);
    
    appearanceLayout->addWidget(legendGroup);
    
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
    
    tabWidget->addTab(appearanceTab, "Appearance");
    
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
    m_lineWidthSpinBox->setValue(defaults.lineWidth);
    m_markerSizeSpinBox->setValue(defaults.markerSize);
    m_showAxisLabelsCheckBox->setChecked(defaults.showAxisLabels);
    m_showAxisTitlesCheckBox->setChecked(defaults.showAxisTitles);
    m_xAxisTitleEdit->setText(defaults.xAxisTitle);
    m_yAxisTitleEdit->setText(defaults.yAxisTitle);
    m_xAxisTickCountSpinBox->setValue(defaults.xAxisTickCount);
    m_xAxisTickSpacingSpinBox->setValue(defaults.xAxisTickSpacing);
    m_plotTitleEdit->setText(defaults.plotTitle);
    updateColorButton(m_backgroundColorButton, defaults.backgroundColor);
    updateColorButton(m_plotAreaColorButton, defaults.plotAreaColor);
    m_exportWidthSpinBox->setValue(defaults.exportWidth);
    m_exportHeightSpinBox->setValue(defaults.exportHeight);
    m_exportDpiSpinBox->setValue(defaults.exportDpi);
}

void PlotSettingsDialog::onPreviewSettings()
{
    // Update settings from controls
    m_settings.showGrid = m_showGridCheckBox->isChecked();
    m_settings.showMinorGrid = m_showMinorGridCheckBox->isChecked();
    m_settings.minorTickCount = m_minorTickCountSpinBox->value();
    m_settings.showLegend = m_showLegendCheckBox->isChecked();
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
    
    QMessageBox::information(this, "Export Complete", 
        QString("Plot exported successfully to:\n%1\n\nDimensions: %2 x %3 pixels\nDPI: %4")
        .arg(fileName)
        .arg(width)
        .arg(height)
        .arg(dpi));
}