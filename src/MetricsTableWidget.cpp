#include "MetricsTableWidget.h"
#include "DataProcessor.h"
#include <QDebug>
#include <QMessageBox>
#include <QDir>
#include <QStringConverter>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>

// MetricsTableModel Implementation
MetricsTableModel::MetricsTableModel(const QVariantList& data, bool isScatterPlot, QObject* parent)
    : QAbstractTableModel(parent)
    , m_data(data)
{
    // For scatter plots, exclude Treatment and Treatment Name columns
    if (isScatterPlot) {
        m_headers = {"Experiment", "Crop", "Variable", "n", "R²", "RMSE", "d-stat"};
    } else {
        m_headers = {"Treatment", "Treatment Name", "Experiment", "Crop", "Variable", "n", "R²", "RMSE", "d-stat"};
    }
    
    // Set up key mapping for flexible data access
    m_keyMap["Treatment"] = {"Treatment", "treatment", "trt", "TRT"};
    m_keyMap["Treatment Name"] = {"TreatmentName", "Treatment Name", "treatment_name", "trt_name"};
    m_keyMap["Experiment"] = {"Experiment", "experiment", "exp", "EXP"};
    // For Crop - prefer CropName (display name), fallback to Crop (code)
    m_keyMap["Crop"] = {"CropName", "Crop", "crop", "CROP"};
    // For Variable - prefer VariableName (display name), fallback to Variable (code)
    m_keyMap["Variable"] = {"VariableName", "Variable", "variable", "var"};
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
        
        // Check column type - text fields should always be displayed as strings
        if (columnName == "Treatment" || columnName == "Treatment Name" || 
            columnName == "Experiment" || columnName == "Crop" || columnName == "Variable") {
            // Always treat text fields as strings, regardless of stored type
            // Note: getValueForColumn already prefers CropName over Crop, and VariableName over Variable
            return value.toString();
        }
        
        // Numeric fields - format as numbers
        if (columnName == "R²") {
            if (value.canConvert<double>()) {
                return QString::number(value.toDouble(), 'f', 3);
            }
            // If R² not provided, show "-"
            return value.toString().isEmpty() ? "-" : value.toString();
        }
        
        if (columnName == "n" || columnName == "RMSE" || columnName == "d-stat") {
            if (value.canConvert<double>()) {
                if (columnName == "n") {
                    return QString::number((int)value.toDouble());
                } else if (columnName == "d-stat") {
                    return QString::number(value.toDouble(), 'f', 4);
                } else if (columnName == "RMSE") {
                    return QString::number(value.toDouble(), 'f', 3);
                } else {
                    return QString::number(value.toDouble(), 'f', 4);
                }
            }
        }
        
        // Default: convert to string if can't determine type
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
    m_tableView->horizontalHeader()->setDefaultSectionSize(100); // Default width before auto-fit
    m_tableView->horizontalHeader()->setMinimumSectionSize(50);  // Minimum column width
    m_tableView->verticalHeader()->setVisible(false);
    m_layout->addWidget(m_tableView);
    
    // Export button
    m_exportButton = new QPushButton("Export Metrics");
    connect(m_exportButton, &QPushButton::clicked, this, &MetricsTableWidget::exportMetrics);
    m_layout->addWidget(m_exportButton);
}

void MetricsTableWidget::setMetrics(const QVariantList& metricsData, bool isScatterPlot)
{
    if (metricsData.isEmpty()) {
        clear();
        return;
    }
    
    m_metricsData = metricsData;
    
    MetricsTableModel* model = new MetricsTableModel(m_metricsData, isScatterPlot, this);
    m_tableView->setModel(model);
    
    // Auto-fit columns to content initially
    // Set Interactive mode so users can still resize columns if needed
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    
    // Auto-fit all columns to their content
    m_tableView->resizeColumnsToContents();
    
    // Ensure minimum column widths for readability
    for (int i = 0; i < model->columnCount(); ++i) {
        int currentWidth = m_tableView->columnWidth(i);
        int minWidth = 80; // Minimum width for readability
        if (currentWidth < minWidth) {
            m_tableView->setColumnWidth(i, minWidth);
        }
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
        "Export Metrics to Excel",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QDir::separator() + "metrics.csv",
        "Excel/CSV Files (*.csv);;Excel Files (*.xlsx);;CSV Files (*.csv);;All Files (*)"
    );
    
    if (fileName.isEmpty()) {
        return;
    }
    
    // If user selected .xlsx but we can only export CSV, change extension
    if (fileName.endsWith(".xlsx", Qt::CaseInsensitive)) {
        fileName.chop(5);
        fileName += ".csv";
    } else if (!fileName.contains(".")) {
        fileName += ".csv";
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for writing:" << fileName;
        return;
    }
    
    QTextStream out(&file);
    // Set UTF-8 encoding with BOM for Excel compatibility
    out.setEncoding(QStringConverter::Utf8);
    out << "\xEF\xBB\xBF";  // UTF-8 BOM for Excel
    
    // Get all column headers from the model
    MetricsTableModel* model = qobject_cast<MetricsTableModel*>(m_tableView->model());
    QStringList headers;
    if (model) {
        for (int col = 0; col < model->columnCount(); ++col) {
            headers << model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
        }
    } else {
        // Fallback headers if model not available
        headers = {"Treatment", "Treatment Name", "Experiment", "Crop", "Variable", "n", "R²", "RMSE", "d-stat"};
    }
    
    // Helper function to escape CSV values
    auto escapeCsvValue = [](const QString& value) -> QString {
        if (value.contains(',') || value.contains('"') || value.contains('\n')) {
            // Escape quotes by doubling them, then wrap in quotes
            QString escaped = value;
            escaped.replace("\"", "\"\"");
            return "\"" + escaped + "\"";
        }
        return value;
    };
    
    // Write header row
    QStringList escapedHeaders;
    for (const QString& header : headers) {
        QString headerForExport = header;
        // Replace R² with R2 for CSV compatibility (Excel encoding issues)
        if (headerForExport == "R²") {
            headerForExport = "R2";
        }
        escapedHeaders << escapeCsvValue(headerForExport);
    }
    out << escapedHeaders.join(",") << "\n";
    
    // Write data rows
    for (const QVariant& item : m_metricsData) {
        const QVariantMap rowData = item.toMap();
        QStringList rowValues;
        
    // Use the model's key mapping if available, otherwise use fallback
    // Headers are already obtained from model above, so they respect scatter plot mode
    
    // Extract values for each column
    for (const QString& header : headers) {
            QVariant value;
            
            // Extract value using same key mapping logic as the model
            // (Model's keyMap is private, so we replicate the logic here)
            QStringList possibleKeys;
            if (header == "Treatment") {
                possibleKeys = {"Treatment", "treatment", "trt", "TRT"};
            } else if (header == "Treatment Name") {
                possibleKeys = {"TreatmentName", "Treatment Name", "treatment_name", "trt_name"};
            } else if (header == "Experiment") {
                possibleKeys = {"Experiment", "experiment", "exp", "EXP"};
            } else if (header == "Crop") {
                // Prefer CropName (display name), fallback to Crop (code)
                possibleKeys = {"CropName", "Crop", "crop", "CROP"};
            } else if (header == "Variable") {
                // Prefer VariableName (display name), fallback to Variable (code)
                possibleKeys = {"VariableName", "Variable", "variable", "var"};
            } else if (header == "n") {
                possibleKeys = {"n", "N", "samples", "count"};
            } else if (header == "R²") {
                possibleKeys = {"R²", "R2", "r_squared", "rsquared", "r-squared"};
            } else if (header == "RMSE") {
                possibleKeys = {"RMSE", "rmse", "root_mean_square_error", "NRMSE"};
            } else if (header == "d-stat") {
                possibleKeys = {"d-stat", "Willmott's d-stat", "d_stat", "dstat", "willmott_d"};
            }
            
            for (const QString& key : possibleKeys) {
                if (rowData.contains(key)) {
                    value = rowData[key];
                    break;
                }
            }
            
            // Format the value based on column type
            QString cellValue;
            if (!value.isValid() || value.isNull()) {
                cellValue = "";
            } else if (header == "Treatment" || header == "Treatment Name" || 
                       header == "Experiment" || header == "Crop" || header == "Variable") {
                // These are ALWAYS text fields - convert to string regardless of stored type
                cellValue = value.toString();
            } else if (header == "R²") {
                // R² is not calculated for time series data - always show "-"
                cellValue = "-";
            } else if (header == "n" || header == "RMSE" || header == "d-stat") {
                // These are numeric fields - check if we can convert to number
                if (value.canConvert<double>()) {
                    double numValue = value.toDouble();
                    // Format numbers with appropriate precision
                    if (header == "n") {
                        cellValue = QString::number((int)numValue);
                    } else if (header == "d-stat") {
                        cellValue = QString::number(numValue, 'f', 4);
                    } else if (header == "RMSE") {
                        cellValue = QString::number(numValue, 'f', 3);
                    } else {
                        cellValue = QString::number(numValue, 'f', 4);
                    }
                } else {
                    // Can't convert to number, treat as string
                    cellValue = value.toString();
                }
            } else {
                // Unknown column - convert to string
                cellValue = value.toString();
            }
            
            rowValues << escapeCsvValue(cellValue);
        }
        
        out << rowValues.join(",") << "\n";
    }
    
    file.close();
    
    // Show success message with View button
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Export Complete");
    msgBox.setText(QString("Metrics exported successfully to:\n%1\n\nNote: This file can be opened in Microsoft Excel.").arg(fileName));
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
    
    qDebug() << "Metrics exported to:" << fileName;
}

