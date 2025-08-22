#ifndef METRICSDIALOG_H
#define METRICSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QVariantList>
#include "MetricsTableWidget.h"

class MetricsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MetricsDialog(const QVariantList& metricsData = QVariantList(), QWidget* parent = nullptr);
    
    void setMetrics(const QVariantList& metricsData);

private:
    void setupUI();
    
    QVBoxLayout* m_layout;
    MetricsTableWidget* m_metricsWidget;
    QDialogButtonBox* m_buttonBox;
};

#endif // METRICSDIALOG_H