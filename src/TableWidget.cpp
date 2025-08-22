#include "TableWidget.h"
#include <QHeaderView>
#include <QStandardItem>
#include <QTextStream>
#include <QFile>
#include <QApplication>

TableWidget::TableWidget(QWidget *parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_filterLineEdit(nullptr)
    , m_infoLabel(nullptr)
    , m_tableView(nullptr)
    , m_model(nullptr)
    , m_proxyModel(nullptr)
{
    setupUI();
    setupModel();
}

TableWidget::~TableWidget()
{
}

void TableWidget::setupUI()
{
    m_layout = new QVBoxLayout(this);
    
    // Filter input
    m_filterLineEdit = new QLineEdit();
    m_filterLineEdit->setPlaceholderText("Filter data...");
    m_layout->addWidget(m_filterLineEdit);
    
    // Info label
    m_infoLabel = new QLabel("No data");
    m_infoLabel->setStyleSheet("font-style: italic; color: gray;");
    m_layout->addWidget(m_infoLabel);
    
    // Table view
    m_tableView = new QTableView();
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_layout->addWidget(m_tableView);
    
    // Connect filter
    connect(m_filterLineEdit, &QLineEdit::textChanged, this, &TableWidget::onFilterTextChanged);
}

void TableWidget::setupModel()
{
    m_model = new QStandardItemModel(this);
    
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterKeyColumn(-1); // Filter all columns
    
    m_tableView->setModel(m_proxyModel);
}

void TableWidget::setData(const DataTable &data)
{
    m_currentData = data;
    updateModel(data);
}

void TableWidget::updateModel(const DataTable &data)
{
    m_model->clear();
    
    if (data.columns.isEmpty()) {
        m_infoLabel->setText("No data");
        return;
    }
    
    // Set headers
    QStringList headers;
    for (const auto &column : data.columns) {
        headers << column.name;
    }
    m_model->setHorizontalHeaderLabels(headers);
    
    // Add data rows
    m_model->setRowCount(data.rowCount);
    m_model->setColumnCount(data.columns.size());
    
    for (int row = 0; row < data.rowCount; ++row) {
        for (int col = 0; col < data.columns.size(); ++col) {
            const DataColumn &column = data.columns[col];
            QVariant value;
            
            if (row < column.data.size()) {
                value = column.data[row];
            }
            
            QStandardItem *item = new QStandardItem();
            
            // Format the value based on data type
            if (DataProcessor::isMissingValue(value)) {
                item->setText("");
                item->setForeground(QColor(Qt::gray));
                item->setData("Missing", Qt::ToolTipRole);
            } else {
                QString displayText;
                
                if (column.dataType == "numeric") {
                    bool ok;
                    double numValue = DataProcessor::toDouble(value, &ok);
                    if (ok) {
                        // Format numeric values with appropriate precision
                        if (numValue == static_cast<int>(numValue)) {
                            displayText = QString::number(static_cast<int>(numValue));
                        } else {
                            displayText = QString::number(numValue, 'f', 3);
                        }
                    } else {
                        displayText = value.toString();
                    }
                } else if (column.dataType == "datetime") {
                    QDateTime dateTime = value.toDateTime();
                    if (dateTime.isValid()) {
                        displayText = dateTime.toString("yyyy-MM-dd");
                    } else {
                        displayText = value.toString();
                    }
                } else {
                    displayText = value.toString();
                }
                
                item->setText(displayText);
                item->setData(value, Qt::UserRole); // Store original value
            }
            
            // Make item read-only
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            
            m_model->setItem(row, col, item);
        }
        
        // Update progress for large datasets
        if (row % 1000 == 0) {
            QApplication::processEvents();
        }
    }
    
    // Update info label
    m_infoLabel->setText(QString("Showing %1 rows, %2 columns")
                        .arg(data.rowCount)
                        .arg(data.columns.size()));
    
    // Resize columns to content
    m_tableView->resizeColumnsToContents();
    
    // Limit column width to reasonable maximum
    QHeaderView *header = m_tableView->horizontalHeader();
    for (int i = 0; i < header->count(); ++i) {
        int currentWidth = header->sectionSize(i);
        if (currentWidth > 200) {
            header->resizeSection(i, 200);
        }
    }
}

void TableWidget::clear()
{
    m_model->clear();
    m_currentData.clear();
    m_filterLineEdit->clear();
    m_infoLabel->setText("No data");
}

void TableWidget::onFilterTextChanged(const QString &text)
{
    m_proxyModel->setFilterFixedString(text);
    
    // Update info label to show filtered count
    if (!text.isEmpty()) {
        int filteredCount = m_proxyModel->rowCount();
        m_infoLabel->setText(QString("Showing %1 of %2 rows, %3 columns (filtered)")
                            .arg(filteredCount)
                            .arg(m_currentData.rowCount)
                            .arg(m_currentData.columns.size()));
    } else {
        m_infoLabel->setText(QString("Showing %1 rows, %2 columns")
                            .arg(m_currentData.rowCount)
                            .arg(m_currentData.columns.size()));
    }
}

void TableWidget::exportToCsv(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    
    QTextStream out(&file);
    
    // Write headers
    QStringList headers;
    for (int col = 0; col < m_model->columnCount(); ++col) {
        headers << m_model->headerData(col, Qt::Horizontal).toString();
    }
    out << headers.join(",") << "\n";
    
    // Write data rows
    for (int row = 0; row < m_proxyModel->rowCount(); ++row) {
        QStringList rowData;
        for (int col = 0; col < m_proxyModel->columnCount(); ++col) {
            QModelIndex index = m_proxyModel->index(row, col);
            QString value = m_proxyModel->data(index).toString();
            
            // Escape commas and quotes in CSV
            if (value.contains(",") || value.contains("\"")) {
                value = "\"" + value.replace("\"", "\"\"") + "\"";
            }
            
            rowData << value;
        }
        out << rowData.join(",") << "\n";
    }
    
    file.close();
}