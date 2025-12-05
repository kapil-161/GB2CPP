#ifndef METRICSTABLEWIDGET_H
#define METRICSTABLEWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTableView>
#include <QPushButton>
#include <QAbstractTableModel>
#include <QHeaderView>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QMap>
#include <QFont>
#include <QColor>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTextStream>

class MetricsTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MetricsTableModel(const QVariantList& data, bool isScatterPlot = false, QObject* parent = nullptr);

    // QAbstractTableModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    QVariantList m_data;
    QStringList m_headers;
    QMap<QString, QStringList> m_keyMap;
    
    QVariant getValueForColumn(const QVariantMap& rowData, const QString& columnName) const;
};

class MetricsTableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MetricsTableWidget(QWidget* parent = nullptr);

    void setMetrics(const QVariantList& metricsData, bool isScatterPlot = false);
    void clear();

private slots:
    void exportMetrics();

private:
    void setupUI();

    QVBoxLayout* m_layout;
    QLabel* m_titleLabel;
    QLabel* m_descriptionLabel;
    QTableView* m_tableView;
    QPushButton* m_exportButton;
    
    QVariantList m_metricsData;
};

#endif // METRICSTABLEWIDGET_H