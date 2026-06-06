#include "MetricsTableWidget.h"
#include <cmath>
#include "DataProcessor.h"
#include <QDebug>
#include <QMessageBox>
#include <QDir>
#include <QStringConverter>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QApplication>
#include <QHBoxLayout>

// MetricsTableModel Implementation
MetricsTableModel::MetricsTableModel(const QVariantList& data, bool isScatterPlot, QObject* parent)
    : QAbstractTableModel(parent)
    , m_data(data)
{
    // For scatter plots, exclude Treatment and Treatment Name columns
    if (isScatterPlot) {
        m_headers = {"Experiment", "Crop", "Variable", "n", "R²", "RMSE", "d-stat",
                     "BIAS", "MSEs/MSE", "MSEu/MSE"};
    } else {
        m_headers = {"Treatment", "Treatment Name", "Experiment", "Crop", "Variable", "n", "Obs. Mean", "Sim. Mean", "R²", "RMSE", "NRMSE", "d-stat"};
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
    m_keyMap["Obs. Mean"] = {"ObsMean", "obs_mean", "mean_obs"};
    m_keyMap["Sim. Mean"] = {"SimMean", "sim_mean", "mean_sim"};
    m_keyMap["RMSE"] = {"RMSE", "rmse", "root_mean_square_error"};
    m_keyMap["NRMSE"] = {"NRMSE", "nrmse", "normalized_rmse"};
    m_keyMap["d-stat"] = {"d-stat", "Willmott's d-stat", "d_stat", "dstat", "willmott_d"};
    m_keyMap["BIAS"] = {"BIAS", "BiasIndex", "Bias Index", "Bias index", "bias_index"};
    m_keyMap["MSEs"]     = {"MSEs", "MSE systematic", "MSE_systematic", "MSE_s"};
    m_keyMap["MSEu"]     = {"MSEu", "MSE unsystematic", "MSE_unsystematic", "MSE_u"};
    m_keyMap["MSEs/MSE"] = {"MSEs", "MSE systematic", "MSE_systematic", "MSE_s"};
    m_keyMap["MSEu/MSE"] = {"MSEu", "MSE unsystematic", "MSE_unsystematic", "MSE_u"};
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
        // Overall row: show blank for all text columns except Variable (which shows the var name)
        if (rowData.value("isOverall").toBool()) {
            if (columnName == "Variable")       return value.toString();
            if (columnName == "Treatment")      return QString("Overall");
            if (columnName == "Treatment Name") return QString("Overall");
            if (columnName == "Experiment")     return QString("Overall");
            if (columnName == "Crop")           return QString("Overall");
            if (!value.isValid()) return QVariant();
            // fall through to numeric formatting below
        }

        if (!value.isValid()) {
            return "NA";
        }

        // Check column type - text fields should always be displayed as strings
        if (columnName == "Treatment" || columnName == "Treatment Name" ||
            columnName == "Experiment" || columnName == "Crop" || columnName == "Variable") {
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
        
        if (columnName == "n" || columnName == "Obs. Mean" || columnName == "Sim. Mean" ||
            columnName == "RMSE" || columnName == "NRMSE" || columnName == "d-stat" ||
            columnName == "BIAS" || columnName == "MSEs" || columnName == "MSEu" ||
            columnName == "MSEs/MSE" || columnName == "MSEu/MSE") {
            if (value.canConvert<double>()) {
                if (columnName == "n") {
                    return QString::number((int)value.toDouble());
                } else if (columnName == "d-stat") {
                    return QString::number(value.toDouble(), 'f', 4);
                } else if (columnName == "Obs. Mean" || columnName == "Sim. Mean") {
                    return QString::number(value.toDouble(), 'f', 3);
                } else if (columnName == "RMSE") {
                    return QString::number(value.toDouble(), 'f', 3);
                } else if (columnName == "NRMSE") {
                    return QString::number(value.toDouble(), 'f', 2) + "%";
                } else if (columnName == "BIAS") {
                    // Dimensionless bias index (Eq. 7) – show 4 decimals
                    return QString::number(value.toDouble(), 'f', 4);
                } else if (columnName == "MSEs" || columnName == "MSEu" ||
                           columnName == "MSEs/MSE" || columnName == "MSEu/MSE") {
                    return QString::number(value.toDouble(), 'f', 4);
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
        QFont font;
        font.setBold(rowData.value("isOverall").toBool() || columnName == "Variable");
        return font;
    }

    case Qt::BackgroundRole: {
        if (rowData.value("isOverall").toBool())
            return QColor("#e0e0e0");
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
    if (column < 0 || column >= m_headers.size()) return;

    layoutAboutToBeChanged();

    // Separate Overall rows (keyed by variable name) from regular rows.
    // After sorting, Overall rows are re-inserted at the end of their variable group.
    QMap<QString, QVariant> overallByVar;  // varName → overall row
    QVariantList regular;
    for (const QVariant& item : m_data) {
        const QVariantMap row = item.toMap();
        if (row.value("isOverall").toBool()) {
            QString varName = row.value("VariableName").toString();
            overallByVar[varName] = item;
        } else {
            regular.append(item);
        }
    }

    // Find sort key
    const QString columnName = m_headers[column];
    const QStringList& possibleKeys = m_keyMap[columnName];
    QString keyToSort;
    for (const QString& key : possibleKeys) {
        for (const QVariant& item : regular) {
            if (item.toMap().contains(key)) { keyToSort = key; break; }
        }
        if (!keyToSort.isEmpty()) break;
    }

    if (!keyToSort.isEmpty()) {
        std::sort(regular.begin(), regular.end(), [keyToSort, order](const QVariant& a, const QVariant& b) {
            const QVariant va = a.toMap().value(keyToSort, 0);
            const QVariant vb = b.toMap().value(keyToSort, 0);
            return order == Qt::AscendingOrder ? va.toString() < vb.toString()
                                               : va.toString() > vb.toString();
        });
    }

    // Rebuild: group regular rows by variable in sorted order, append Overall after each group
    QStringList varOrder;
    QMap<QString, QVariantList> byVar;
    for (const QVariant& item : regular) {
        QString varName = item.toMap().value("VariableName").toString();
        if (varName.isEmpty()) varName = item.toMap().value("Variable").toString();
        if (!byVar.contains(varName)) varOrder.append(varName);
        byVar[varName].append(item);
    }

    m_data.clear();
    for (const QString& varName : varOrder) {
        m_data.append(byVar[varName]);
        if (overallByVar.contains(varName))
            m_data.append(overallByVar[varName]);
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
    , m_copyButton(nullptr)
    , m_exportButton(nullptr)
{
    setupUI();
}

void MetricsTableWidget::setupUI()
{
    m_layout = new QVBoxLayout(this);
    
    // Table view
    m_tableView = new QTableView();
    m_tableView->setSortingEnabled(true);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setDefaultSectionSize(100); // Default width before auto-fit
    m_tableView->horizontalHeader()->setMinimumSectionSize(50);  // Minimum column width
    m_tableView->verticalHeader()->setVisible(false);
    m_layout->addWidget(m_tableView);
    
    // Button row
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_copyButton = new QPushButton("Copy Metrics");
    m_copyButton->setToolTip("Copy metrics table to clipboard (tab-separated)");
    connect(m_copyButton, &QPushButton::clicked, this, &MetricsTableWidget::copyMetrics);
    buttonLayout->addWidget(m_copyButton);

    m_exportButton = new QPushButton("Export Metrics");
    connect(m_exportButton, &QPushButton::clicked, this, &MetricsTableWidget::exportMetrics);
    buttonLayout->addWidget(m_exportButton);

    buttonLayout->addStretch();
    m_layout->addLayout(buttonLayout);
}

static QVariant rowGet(const QVariantMap& row, const QStringList& keys)
{
    for (const QString& k : keys) {
        if (row.contains(k)) return row[k];
    }
    return QVariant();
}

// Compute one Overall row from a list of same-variable rows.
// varDisplayName is stored so the Variable column shows the variable name.
static QVariantMap computeOverallRow(const QVariantList& varRows, bool isScatterPlot,
                                     const QString& varDisplayName)
{
    double totalN = 0, sumObsMean = 0, sumSimMean = 0;
    double sumSS = 0, sumDStat = 0, sumBias = 0;

    for (const QVariant& item : varRows) {
        const QVariantMap row = item.toMap();
        double n = rowGet(row, {"n","N","samples","count"}).toDouble();
        if (n <= 0) continue;
        totalN     += n;
        sumObsMean += n * rowGet(row, {"ObsMean","obs_mean","mean_obs"}).toDouble();
        sumSimMean += n * rowGet(row, {"SimMean","sim_mean","mean_sim"}).toDouble();
        double rmse = rowGet(row, {"RMSE","rmse"}).toDouble();
        sumSS      += n * rmse * rmse;
        sumDStat   += n * rowGet(row, {"Willmott's d-stat","d-stat","dstat","d_stat","willmott_d"}).toDouble();
        sumBias    += n * rowGet(row, {"BIAS","BiasIndex","Bias Index","bias_index"}).toDouble();
    }

    QVariantMap overall;
    overall["isOverall"]     = true;
    overall["VariableName"]  = varDisplayName;
    overall["Treatment"]     = isScatterPlot ? QVariant() : QVariant(QString());
    overall["TreatmentName"] = QString();
    overall["Experiment"]    = QString();
    overall["CropName"]      = QString();

    if (totalN > 0) {
        double obsMean  = sumObsMean / totalN;
        double rmse     = std::sqrt(sumSS / totalN);
        overall["n"]       = totalN;
        overall["ObsMean"] = obsMean;
        overall["SimMean"] = sumSimMean / totalN;
        overall["RMSE"]    = rmse;
        overall["NRMSE"]   = (obsMean > 0) ? (rmse / obsMean) * 100.0 : 0.0;
        overall["d-stat"]  = sumDStat / totalN;
        if (isScatterPlot) overall["BIAS"] = sumBias / totalN;
    }
    return overall;
}

void MetricsTableWidget::setMetrics(const QVariantList& metricsData, bool isScatterPlot)
{
    if (metricsData.isEmpty()) {
        clear();
        return;
    }

    // Group rows by variable (preserving first-appearance order), then interleave
    // each group with its per-variable Overall row at the end.
    QStringList varOrder;
    QMap<QString, QVariantList> byVar;
    for (const QVariant& item : metricsData) {
        const QVariantMap row = item.toMap();
        QString varName = rowGet(row, {"VariableName","Variable","variable","var"}).toString();
        if (!byVar.contains(varName)) varOrder.append(varName);
        byVar[varName].append(item);
    }

    m_metricsData.clear();
    for (const QString& varName : varOrder) {
        const QVariantList& varRows = byVar[varName];
        m_metricsData.append(varRows);
        if (!isScatterPlot)
            m_metricsData.append(computeOverallRow(varRows, isScatterPlot, varName));
    }

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

void MetricsTableWidget::copyMetrics()
{
    MetricsTableModel* model = qobject_cast<MetricsTableModel*>(m_tableView->model());
    if (!model || model->rowCount() == 0) {
        qWarning() << "No metrics data to copy";
        return;
    }

    QStringList lines;

    // Header row
    QStringList headers;
    for (int col = 0; col < model->columnCount(); ++col) {
        headers << model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
    }
    lines << headers.join("\t");

    // Data rows
    for (int row = 0; row < model->rowCount(); ++row) {
        QStringList rowValues;
        for (int col = 0; col < model->columnCount(); ++col) {
            rowValues << model->data(model->index(row, col), Qt::DisplayRole).toString();
        }
        lines << rowValues.join("\t");
    }

    QApplication::clipboard()->setText(lines.join("\n"));
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
            } else if (header == "Obs. Mean") {
                possibleKeys = {"ObsMean", "obs_mean", "mean_obs"};
            } else if (header == "Sim. Mean") {
                possibleKeys = {"SimMean", "sim_mean", "mean_sim"};
            } else if (header == "RMSE") {
                possibleKeys = {"RMSE", "rmse", "root_mean_square_error"};
            } else if (header == "NRMSE") {
                possibleKeys = {"NRMSE", "nrmse", "normalized_rmse"};
            } else if (header == "d-stat") {
                possibleKeys = {"d-stat", "Willmott's d-stat", "d_stat", "dstat", "willmott_d"};
            } else if (header == "BIAS") {
                possibleKeys = {"BIAS", "BiasIndex", "Bias Index", "Bias index", "bias_index"};
            } else if (header == "MSEs") {
                possibleKeys = {"MSEs", "MSE systematic", "MSE_systematic", "MSE_s"};
            } else if (header == "MSEu") {
                possibleKeys = {"MSEu", "MSE unsystematic", "MSE_unsystematic", "MSE_u"};
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
            } else if (header == "n" || header == "Obs. Mean" || header == "Sim. Mean" ||
                       header == "RMSE" || header == "NRMSE" || header == "d-stat" ||
                       header == "BIAS" || header == "MSEs" || header == "MSEu" ||
                       header == "MSEs/MSE" || header == "MSEu/MSE") {
                // These are numeric fields - check if we can convert to number
                if (value.canConvert<double>()) {
                    double numValue = value.toDouble();
                    // Format numbers with appropriate precision
                    if (header == "n") {
                        cellValue = QString::number((int)numValue);
                    } else if (header == "d-stat") {
                        cellValue = QString::number(numValue, 'f', 4);
                    } else if (header == "Obs. Mean" || header == "Sim. Mean") {
                        cellValue = QString::number(numValue, 'f', 3);
                    } else if (header == "RMSE") {
                        cellValue = QString::number(numValue, 'f', 3);
                    } else if (header == "NRMSE") {
                        cellValue = QString::number(numValue, 'f', 2);
                    } else if (header == "BIAS") {
                        cellValue = QString::number(numValue, 'f', 4);
                    } else if (header == "MSEs" || header == "MSEu" ||
                               header == "MSEs/MSE" || header == "MSEu/MSE") {
                        cellValue = QString::number(numValue, 'f', 4);
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
    
}

