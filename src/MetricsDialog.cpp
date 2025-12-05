#include "MetricsDialog.h"

MetricsDialog::MetricsDialog(const QVariantList& metricsData, bool isScatterPlot, QWidget* parent)
    : QDialog(parent)
    , m_layout(nullptr)
    , m_metricsWidget(nullptr)
    , m_buttonBox(nullptr)
{
    setWindowTitle("Model Performance Metrics");
    resize(800, 500);
    
    setupUI();
    
    if (!metricsData.isEmpty()) {
        m_metricsWidget->setMetrics(metricsData, isScatterPlot);
    }
}

void MetricsDialog::setupUI()
{
    m_layout = new QVBoxLayout(this);
    
    // Create metrics widget
    m_metricsWidget = new MetricsTableWidget();
    m_layout->addWidget(m_metricsWidget);
    
    // Create button box with Close button
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_layout->addWidget(m_buttonBox);
}

void MetricsDialog::setMetrics(const QVariantList& metricsData, bool isScatterPlot)
{
    if (m_metricsWidget) {
        m_metricsWidget->setMetrics(metricsData, isScatterPlot);
    }
}