#include "PandasTableModel.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

PandasTableModel::PandasTableModel(const DataTable& data, QObject* parent)
    : QAbstractTableModel(parent)
    , m_data(data)
{
}

int PandasTableModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_data.rowCount;
}

int PandasTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_data.columnNames.size();
}

QVariant PandasTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.rowCount || 
        index.column() >= m_data.columnNames.size()) {
        return QVariant();
    }
    
    if (role == Qt::DisplayRole) {
        const QString columnName = m_data.columnNames[index.column()];
        const DataColumn* column = m_data.getColumn(columnName);
        
        if (!column || index.row() >= column->data.size()) {
            return "NA";
        }
        
        const QVariant& value = column->data[index.row()];
        QString formattedValue = formatValue(value);
        return formattedValue;
    }
    
    return QVariant();
}

QVariant PandasTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            if (section >= 0 && section < m_data.columnNames.size()) {
                return m_data.columnNames[section];
            }
        } else {
            return QString::number(section);
        }
    }
    return QVariant();
}

void PandasTableModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= m_data.columnNames.size()) {
        return;
    }
    
    layoutAboutToBeChanged();
    
    const QString columnName = m_data.columnNames[column];
    const DataColumn* sortColumn = m_data.getColumn(columnName);
    
    if (sortColumn) {
        // Create indices for sorting
        QVector<int> indices(m_data.rowCount);
        std::iota(indices.begin(), indices.end(), 0);
        
        // Sort indices based on column values
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            const QVariant& valueA = sortColumn->data[a];
            const QVariant& valueB = sortColumn->data[b];
            
            // Handle missing values
            if (DataProcessor::isMissingValue(valueA) && DataProcessor::isMissingValue(valueB)) {
                return false;
            }
            if (DataProcessor::isMissingValue(valueA)) {
                return order == Qt::DescendingOrder;
            }
            if (DataProcessor::isMissingValue(valueB)) {
                return order == Qt::AscendingOrder;
            }
            
            // Compare values
            if (isNumericValue(valueA) && isNumericValue(valueB)) {
                double numA = valueA.toDouble();
                double numB = valueB.toDouble();
                return (order == Qt::AscendingOrder) ? (numA < numB) : (numA > numB);
            } else {
                QString strA = valueA.toString();
                QString strB = valueB.toString();
                return (order == Qt::AscendingOrder) ? (strA < strB) : (strA > strB);
            }
        });
        
        // Reorder all columns based on sorted indices
        for (const QString& colName : m_data.columnNames) {
            DataColumn* col = const_cast<DataColumn*>(m_data.getColumn(colName));
            if (col) {
                QVector<QVariant> newData(col->data.size());
                for (int i = 0; i < indices.size(); ++i) {
                    newData[i] = col->data[indices[i]];
                }
                col->data = newData;
            }
        }
    }
    
    layoutChanged();
}

void PandasTableModel::setData(const DataTable& data)
{
    beginResetModel();
    m_data = data;
    endResetModel();
}

void PandasTableModel::clear()
{
    beginResetModel();
    m_data.clear();
    endResetModel();
}

QString PandasTableModel::formatValue(const QVariant& value) const
{
    if (DataProcessor::isMissingValue(value)) {
        return "NA";
    }
    
    if (value.metaType() == QMetaType::fromType<double>()) {
        double doubleVal = value.toDouble();
        if (std::isnan(doubleVal) || std::isinf(doubleVal)) {
            return "NA";
        }
        return QString::number(doubleVal, 'f', 4);
    }
    
    if (value.metaType() == QMetaType::fromType<int>()) {
        return QString::number(value.toInt());
    }
    
    return value.toString();
}

bool PandasTableModel::isNumericValue(const QVariant& value) const
{
    return value.metaType() == QMetaType::fromType<double>() || 
           value.metaType() == QMetaType::fromType<int>() ||
           value.metaType() == QMetaType::fromType<float>();
}

