#ifndef PANDASTABLEMODEL_H
#define PANDASTABLEMODEL_H

#include <QAbstractTableModel>
#include <QVariant>
#include <QStringList>
#include <QVector>
#include <QMap>
#include "DataProcessor.h"

class PandasTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PandasTableModel(const DataTable& data, QObject* parent = nullptr);

    // QAbstractTableModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    
    // Data management
    void setData(const DataTable& data);
    const DataTable& getData() const { return m_data; }
    void clear();

private:
    DataTable m_data;
    QString formatValue(const QVariant& value) const;
    bool isNumericValue(const QVariant& value) const;
};

#endif // PANDASTABLEMODEL_H