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
#include <QDateTimeEdit>
#include <QComboBox>
#include <QColorDialog>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QSet>
#include <QDateTime>

// Forward declaration
class PlotWidget;

struct PlotSettings {
    // Grid settings
    bool showGrid = true;
    bool showMinorGrid = true;
    int minorTickCount = 4;
    
    // Legend settings
    bool showLegend = true;
    
    // Error bar settings
    bool showErrorBars = false;
    QString errorBarType = "SD";  // "SD" or "SE"
    
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

    // Axis range overrides (NaN = auto)
    bool useCustomXMin = false;
    double xAxisMin = 0.0;
    bool useCustomXMax = false;
    double xAxisMax = 100.0;
    bool useCustomYMin = false;
    double yAxisMin = 0.0;
    bool useCustomYMax = false;
    double yAxisMax = 100.0;
    
    // Plot appearance
    QString plotTitle = "";
    QColor backgroundColor = Qt::white;
    QColor plotAreaColor = Qt::white;
    
    // Export settings
    int exportWidth = 800;
    int exportHeight = 600;
    int exportDpi = 300;

    // Font settings
    QString fontFamily = "Arial";
    int titleFontSize = 14;
    int axisLabelFontSize = 10;
    int axisTickFontSize = 9;
    int legendFontSize = 9;
    bool boldTitle = true;
    bool boldAxisLabels = false;

    // Scatter panel overlay metrics (subset of available stats to display)
    QSet<QString> scatterMetrics = {"RMSE", "R²"};

    // Treatment filter (empty excludedSeriesKeys = show all)
    QSet<QString> excludedSeriesKeys;  // format: "varName::expId::trtId"
    QStringList availableExperiments;
    QMap<QString, QStringList> experimentTreatments;  // expId -> [trtIds]
    QMap<QString, QString> treatmentDisplayNames;  // "expId::trtId" -> display name
    QStringList lastYVars;  // Track Y vars to reset filter on variable change
    QStringList availableYVars;  // Y variables for tree hierarchy
    QMap<QString, QString> yVarDisplayNames;  // varCode -> display name
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
    
    // Error bar controls
    QCheckBox *m_showErrorBarsCheckBox;
    QComboBox *m_errorBarTypeComboBox;
    
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
    QCheckBox *m_useCustomXMinCheckBox;
    QDoubleSpinBox *m_xAxisMinSpinBox;      // numeric X axis
    QDateTimeEdit *m_xAxisMinDateEdit;      // date X axis
    QCheckBox *m_useCustomXMaxCheckBox;
    QDoubleSpinBox *m_xAxisMaxSpinBox;      // numeric X axis
    QDateTimeEdit *m_xAxisMaxDateEdit;      // date X axis
    QCheckBox *m_useCustomYMinCheckBox;
    QDoubleSpinBox *m_yAxisMinSpinBox;
    QCheckBox *m_useCustomYMaxCheckBox;
    QDoubleSpinBox *m_yAxisMaxSpinBox;
    bool m_xAxisIsDate = false;
    
    // Plot appearance controls
    QLineEdit *m_plotTitleEdit;
    QPushButton *m_backgroundColorButton;
    QPushButton *m_plotAreaColorButton;
    
    // Export controls
    QSpinBox *m_exportWidthSpinBox;
    QSpinBox *m_exportHeightSpinBox;
    QSpinBox *m_exportDpiSpinBox;

    // Font controls
    QComboBox *m_fontFamilyComboBox;
    QSpinBox *m_titleFontSizeSpinBox;
    QSpinBox *m_axisLabelFontSizeSpinBox;
    QSpinBox *m_axisTickFontSizeSpinBox;
    QSpinBox *m_legendFontSizeSpinBox;
    QCheckBox *m_boldTitleCheckBox;
    QCheckBox *m_boldAxisLabelsCheckBox;

    // Scatter metrics checkboxes
    QMap<QString, QCheckBox*> m_scatterMetricCheckBoxes;

    // Buttons
    QPushButton *m_resetButton;
    QPushButton *m_previewButton;
    QPushButton *m_exportButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif // PLOTSETTINGSDIALOG_H