#include "MetricsTableWidget.h"
#include <QDebug>

// MetricsTableModel Implementation
MetricsTableModel::MetricsTableModel(const QVariantList& data, QObject* parent)
    : QAbstractTableModel(parent)
    , m_data(data)
{
    m_headers = {"Treatment", "Treatment Name", "Experiment", "Crop", "Variable", "n", "R²", "RMSE", "d-stat"};
    
    // Set up key mapping for flexible data access
    m_keyMap["Treatment"] = {"Treatment", "treatment", "trt", "TRT"};
    m_keyMap["Treatment Name"] = {"TreatmentName", "Treatment Name", "treatment_name", "trt_name"};
    m_keyMap["Experiment"] = {"Experiment", "experiment", "exp", "EXP"};
    m_keyMap["Crop"] = {"Crop", "crop", "CROP"};
    m_keyMap["Variable"] = {"Variable", "variable", "var"};
    m_keyMap["n"] = {"n", "N", "samples", "count"};
    m_keyMap["R²"] = {"R²", "R2", "r_squared", "rsquared", "r-squared"};
    m_keyMap["RMSE"] = {"RMSE", "rmse", "root_mean_square_error"};
    m_keyMap["d-stat"] = {"d-stat", "Willmott's d-stat", "d_stat", "dstat", "willmott_d"};
}

int MetricsTableModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_data.size();
}

int MetricsTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_headers.size();
}

QVariant MetricsTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size()) {
        return QVariant();
    }
    
    const QString columnName = m_headers[index.column()];
    const QVariantMap rowData = m_data[index.row()].toMap();
    const QVariant value = getValueForColumn(rowData, columnName);
    
    switch (role) {
    case Qt::DisplayRole: {
        if (!value.isValid()) {
            return "NA";
        }
        
        if (value.canConvert<double>()) {
            return QString::number(value.toDouble(), 'f', 4);
        }
        
        return value.toString();
    }
    
    case Qt::TextAlignmentRole: {
        if (columnName == "Variable") {
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            return int(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    
    case Qt::FontRole: {
        if (columnName == "Variable") {
            QFont font;
            font.setBold(true);
            return font;
        }
        return QVariant();
    }
    
    case Qt::BackgroundRole: {
        return QVariant();
    }
    
    default:
        return QVariant();
    }
}

QVariant MetricsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < m_headers.size()) {
            return m_headers[section];
        }
    }
    return QVariant();
}

void MetricsTableModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= m_headers.size()) {
        return;
    }
    
    layoutAboutToBeChanged();
    
    const QString columnName = m_headers[column];
    const QStringList& possibleKeys = m_keyMap[columnName];
    
    // Find the key that exists in the data
    QString keyToSort;
    for (const auto& key : possibleKeys) {
        for (const auto& item : m_data) {
            const QVariantMap itemMap = item.toMap();
            if (itemMap.contains(key)) {
                keyToSort = key;
                break;
            }
        }
        if (!keyToSort.isEmpty()) {
            break;
        }
    }
    
    if (!keyToSort.isEmpty()) {
        std::sort(m_data.begin(), m_data.end(), [keyToSort, order](const QVariant& a, const QVariant& b) {
            const QVariantMap mapA = a.toMap();
            const QVariantMap mapB = b.toMap();
            
            const QVariant valueA = mapA.value(keyToSort, 0);
            const QVariant valueB = mapB.value(keyToSort, 0);
            
            if (order == Qt::AscendingOrder) {
                return valueA.toString() < valueB.toString();
            } else {
                return valueA.toString() > valueB.toString();
            }
        });
    }
    
    layoutChanged();
}

QVariant MetricsTableModel::getValueForColumn(const QVariantMap& rowData, const QString& columnName) const
{
    const QStringList& possibleKeys = m_keyMap[columnName];
    
    for (const QString& key : possibleKeys) {
        if (rowData.contains(key)) {
            return rowData[key];
        }
    }
    
    return QVariant();
}

// MetricsTableWidget Implementation
MetricsTableWidget::MetricsTableWidget(QWidget* parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_titleLabel(nullptr)
    , m_descriptionLabel(nullptr)
    , m_tableView(nullptr)
    , m_exportButton(nullptr)
{
    setupUI();
}

void MetricsTableWidget::setupUI()
{
    m_layout = new QVBoxLayout(this);
    
    // Title label
    m_titleLabel = new QLabel("Model Performance Metrics");
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; margin-bottom: 10px;");
    m_layout->addWidget(m_titleLabel);
    
    // Description label
    m_descriptionLabel = new QLabel("This table shows performance metrics for simulated versus measured data.");
    m_layout->addWidget(m_descriptionLabel);
    
    // Table view
    m_tableView = new QTableView();
    m_tableView->setSortingEnabled(true);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->verticalHeader()->setVisible(false);
    m_layout->addWidget(m_tableView);
    
    // Export button
    m_exportButton = new QPushButton("Export Metrics");
    connect(m_exportButton, &QPushButton::clicked, this, &MetricsTableWidget::exportMetrics);
    m_layout->addWidget(m_exportButton);
}

void MetricsTableWidget::setMetrics(const QVariantList& metricsData)
{
    if (metricsData.isEmpty()) {
        clear();
        return;
    }
    
    m_metricsData = metricsData;
    
    MetricsTableModel* model = new MetricsTableModel(m_metricsData, this);
    m_tableView->setModel(model);
    
    // Resize columns to content
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    // Get column widths and adjust the first column to be wider
    QList<int> widths;
    for (int i = 0; i < model->columnCount(); ++i) {
        widths.append(m_tableView->columnWidth(i));
    }
    
    if (!widths.isEmpty()) {
        widths[0] = qMax(widths[0], 250);  // Ensure Variable column is at least 250px wide
    }
    
    // Set fixed column widths with some padding
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    for (int i = 0; i < widths.size(); ++i) {
        m_tableView->setColumnWidth(i, widths[i] + 20);
    }
}

void MetricsTableWidget::clear()
{
    m_metricsData.clear();
    m_tableView->setModel(nullptr);
}

void MetricsTableWidget::exportMetrics()
{
    if (m_metricsData.isEmpty()) {
        qWarning() << "No metrics data to export";
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Metrics",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QDir::separator() + "metrics.csv",
        "CSV Files (*.csv);;All Files (*)"
    );
    
    if (fileName.isEmpty()) {
        return;
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for writing:" << fileName;
        return;
    }
    
    QTextStream out(&file);
    
    // Write header
    QStringList headers = {"Variable", "n", "R²", "RMSE", "d-stat"};
    out << headers.join(",") << "\n";
    
    // Write data rows
    for (const QVariant& item : m_metricsData) {
        const QVariantMap rowData = item.toMap();
        QStringList rowValues;
        
        // Extract values for each column
        rowValues << rowData.value("Variable", "").toString();
        rowValues << rowData.value("n", "").toString();
        rowValues << QString::number(rowData.value("R²", 0.0).toDouble(), 'f', 4);
        rowValues << QString::number(rowData.value("RMSE", 0.0).toDouble(), 'f', 4);
        rowValues << QString::number(rowData.value("d-stat", 0.0).toDouble(), 'f', 4);
        
        out << rowValues.join(",") << "\n";
    }
    
    file.close();
    qDebug() << "Metrics exported to:" << fileName;
}

