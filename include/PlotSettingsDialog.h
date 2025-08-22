#ifndef PLOTSETTINGSDIALOG_H
#define PLOTSETTINGSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QColorDialog>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>

// Forward declaration
class PlotWidget;

struct PlotSettings {
    // Grid settings
    bool showGrid = true;
    bool showMinorGrid = true;
    int minorTickCount = 4;
    
    // Legend settings
    bool showLegend = true;
    
    // Line settings
    int lineWidth = 2;
    
    // Marker settings
    int markerSize = 8;
    
    // Axis settings
    bool showAxisLabels = true;
    bool showAxisTitles = true;
    QString xAxisTitle = "";
    QString yAxisTitle = "";
    int xAxisTickCount = 10;
    double xAxisTickSpacing = 0.0;
    
    // Plot appearance
    QString plotTitle = "";
    QColor backgroundColor = Qt::white;
    QColor plotAreaColor = Qt::white;
    
    // Export settings
    int exportWidth = 800;
    int exportHeight = 600;
    int exportDpi = 300;
};

class PlotSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PlotSettingsDialog(const PlotSettings &currentSettings, PlotWidget *plotWidget = nullptr, QWidget *parent = nullptr);
    virtual ~PlotSettingsDialog() = default;
    
    PlotSettings getSettings() const;

private slots:
    void onBackgroundColorClicked();
    void onPlotAreaColorClicked();
    void onResetDefaults();
    void onPreviewSettings();
    void onExportPlot();

private:
    void setupUI();
    void updateColorButton(QPushButton *button, const QColor &color);
    
    PlotSettings m_settings;
    PlotWidget *m_plotWidget;
    
    // Grid controls
    QCheckBox *m_showGridCheckBox;
    QCheckBox *m_showMinorGridCheckBox;
    QSpinBox *m_minorTickCountSpinBox;
    
    // Legend controls
    QCheckBox *m_showLegendCheckBox;
    
    // Line controls
    QSpinBox *m_lineWidthSpinBox;
    
    // Marker controls
    QSpinBox *m_markerSizeSpinBox;
    
    // Axis controls
    QCheckBox *m_showAxisLabelsCheckBox;
    QCheckBox *m_showAxisTitlesCheckBox;
    QLineEdit *m_xAxisTitleEdit;
    QLineEdit *m_yAxisTitleEdit;
    QSpinBox *m_xAxisTickCountSpinBox;
    QDoubleSpinBox *m_xAxisTickSpacingSpinBox;
    
    // Plot appearance controls
    QLineEdit *m_plotTitleEdit;
    QPushButton *m_backgroundColorButton;
    QPushButton *m_plotAreaColorButton;
    
    // Export controls
    QSpinBox *m_exportWidthSpinBox;
    QSpinBox *m_exportHeightSpinBox;
    QSpinBox *m_exportDpiSpinBox;
    
    // Buttons
    QPushButton *m_resetButton;
    QPushButton *m_previewButton;
    QPushButton *m_exportButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif // PLOTSETTINGSDIALOG_H