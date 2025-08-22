#include "DataTableWidget.h"
#include <QDebug>
#include <QMessageBox>

DataTableWidget::DataTableWidget(QWidget* parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_controlsLayout(nullptr)
    , m_exportButton(nullptr)
    , m_filterGroup(nullptr)
    , m_filterColumn(nullptr)
    , m_filterValue(nullptr)
    , m_applyFilterButton(nullptr)
    , m_clearFilterButton(nullptr)
    , m_tabWidget(nullptr)
    , m_simTableView(nullptr)
    , m_obsTableView(nullptr)
    , m_simModel(nullptr)
    , m_obsModel(nullptr)
{
    setupUI();
}

void DataTableWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    
    // Controls layout
    m_controlsLayout = new QHBoxLayout();
    
    // Export button
    m_exportButton = new QPushButton("Export Data");
    m_exportButton->setToolTip("Export table to CSV/Excel");
    m_controlsLayout->addWidget(m_exportButton);
    
    // Filter controls
    m_filterGroup = new QGroupBox("Filter");
    QHBoxLayout* filterLayout = new QHBoxLayout(m_filterGroup);
    
    filterLayout->addWidget(new QLabel("Column:"));
    m_filterColumn = new QComboBox();
    filterLayout->addWidget(m_filterColumn);
    
    filterLayout->addWidget(new QLabel("Value:"));
    m_filterValue = new QComboBox();
    m_filterValue->setEditable(true);
    filterLayout->addWidget(m_filterValue);
    
    m_applyFilterButton = new QPushButton("Apply");
    filterLayout->addWidget(m_applyFilterButton);
    
    m_clearFilterButton = new QPushButton("Clear");
    filterLayout->addWidget(m_clearFilterButton);
    
    m_controlsLayout->addWidget(m_filterGroup);
    
    // Add controls to main layout
    m_mainLayout->addLayout(m_controlsLayout);
    
    // Create tab widget
    m_tabWidget = new QTabWidget();
    
    // Create table views
    m_simTableView = new QTableView();
    m_obsTableView = new QTableView();
    
    // Configure table views
    for (QTableView* table : {m_simTableView, m_obsTableView}) {
        table->setSortingEnabled(true);
        table->setAlternatingRowColors(true);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(true);
    }
    
    // Add tables to tabs
    m_tabWidget->addTab(m_simTableView, "Simulated Data");
    m_tabWidget->addTab(m_obsTableView, "Observed Data");
    m_tabWidget->setCurrentIndex(0);  // Start with simulated data tab
    
    m_mainLayout->addWidget(m_tabWidget);
    
    // Connect signals
    connect(m_exportButton, &QPushButton::clicked, this, &DataTableWidget::exportData);
    connect(m_applyFilterButton, &QPushButton::clicked, this, &DataTableWidget::applyFilter);
    connect(m_clearFilterButton, &QPushButton::clicked, this, &DataTableWidget::clearFilter);
    connect(m_filterColumn, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &DataTableWidget::updateFilterValues);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &DataTableWidget::onTabChanged);
}

void DataTableWidget::setData(const DataTable& simData, const DataTable& obsData)
{
    qDebug() << "DataTableWidget: setData called. Sim data rows:" << simData.rowCount 
             << "Obs data rows:" << obsData.rowCount;
    
    // Clean data by removing empty columns
    if (simData.rowCount > 0) {
        m_simData = removeEmptyColumns(simData);
        m_filteredSimData = m_simData;
        
        if (m_simModel) {
            delete m_simModel;
        }
        m_simModel = new PandasTableModel(m_filteredSimData, this);
        m_simTableView->setModel(m_simModel);
        m_simTableView->resizeColumnsToContents();
        
        qDebug() << "DataTableWidget: Simulated data loaded successfully. Row count:" << m_simModel->rowCount();
    } else {
        m_simData.clear();
        m_filteredSimData.clear();
        if (m_simModel) {
            delete m_simModel;
            m_simModel = nullptr;
        }
        m_simTableView->setModel(nullptr);
        qDebug() << "DataTableWidget: No simulation data provided or data is empty";
    }
    
    // Handle observed data
    if (obsData.rowCount > 0) {
        qDebug() << "DataTableWidget: Observed data columns:" << obsData.columnNames;
        
        m_obsData = removeEmptyColumns(obsData);
        m_filteredObsData = m_obsData;
        
        if (m_obsModel) {
            delete m_obsModel;
        }
        m_obsModel = new PandasTableModel(m_filteredObsData, this);
        m_obsTableView->setModel(m_obsModel);
        m_obsTableView->resizeColumnsToContents();
        
        qDebug() << "DataTableWidget: Observed data loaded successfully. Row count:" << m_obsModel->rowCount();
    } else {
        qDebug() << "DataTableWidget: No observed data provided or data is empty";
        m_obsData.clear();
        m_filteredObsData.clear();
        if (m_obsModel) {
            delete m_obsModel;
            m_obsModel = nullptr;
        }
        m_obsTableView->setModel(nullptr);
    }
    
    // Update filter controls based on active tab
    updateFilterColumns();
    
    // Force refresh of the current tab's data
    onTabChanged(m_tabWidget->currentIndex());
}

void DataTableWidget::clear()
{
    m_simData.clear();
    m_filteredSimData.clear();
    m_obsData.clear();
    m_filteredObsData.clear();
    
    if (m_simModel) {
        delete m_simModel;
        m_simModel = nullptr;
    }
    if (m_obsModel) {
        delete m_obsModel;
        m_obsModel = nullptr;
    }
    
    m_simTableView->setModel(nullptr);
    m_obsTableView->setModel(nullptr);
    
    m_filterColumn->clear();
    m_filterValue->clear();
}

void DataTableWidget::exportData()
{
    int currentTab = m_tabWidget->currentIndex();
    const DataTable* dataToExport = nullptr;
    QString defaultFileName;
    
    if (currentTab == 0 && m_simModel) {
        dataToExport = &m_filteredSimData;
        defaultFileName = "simulated_data.csv";
    } else if (currentTab == 1 && m_obsModel) {
        dataToExport = &m_filteredObsData;
        defaultFileName = "observed_data.csv";
    }
    
    if (!dataToExport || dataToExport->rowCount == 0) {
        QMessageBox::warning(this, "Export Warning", "No data to export");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Data",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QDir::separator() + defaultFileName,
        "CSV Files (*.csv);;All Files (*)"
    );
    
    if (fileName.isEmpty()) {
        return;
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Failed to open file for writing");
        return;
    }
    
    QTextStream out(&file);
    
    // Write header
    out << dataToExport->columnNames.join(",") << "\n";
    
    // Write data rows
    for (int row = 0; row < dataToExport->rowCount; ++row) {
        QStringList rowValues;
        for (const QString& columnName : dataToExport->columnNames) {
            const DataColumn* column = dataToExport->getColumn(columnName);
            if (column && row < column->data.size()) {
                const QVariant& value = column->data[row];
                if (DataProcessor::isMissingValue(value)) {
                    rowValues << "";
                } else {
                    rowValues << value.toString();
                }
            } else {
                rowValues << "";
            }
        }
        out << rowValues.join(",") << "\n";
    }
    
    file.close();
    QMessageBox::information(this, "Export Complete", QString("Data exported to: %1").arg(fileName));
}

void DataTableWidget::applyFilter()
{
    QString filterColumn = m_filterColumn->currentData().toString();
    QString filterValue = m_filterValue->currentText();
    
    if (filterColumn.isEmpty() || filterValue.isEmpty()) {
        return;
    }
    
    int currentTab = m_tabWidget->currentIndex();
    
    if (currentTab == 0 && m_simData.rowCount > 0) {
        // Filter simulated data
        m_filteredSimData = DataProcessor::filterData(m_simData, filterColumn, filterValue);
        if (m_simModel) {
            m_simModel->setData(m_filteredSimData);
        }
    } else if (currentTab == 1 && m_obsData.rowCount > 0) {
        // Filter observed data
        m_filteredObsData = DataProcessor::filterData(m_obsData, filterColumn, filterValue);
        if (m_obsModel) {
            m_obsModel->setData(m_filteredObsData);
        }
    }
}

void DataTableWidget::clearFilter()
{
    int currentTab = m_tabWidget->currentIndex();
    
    if (currentTab == 0 && m_simData.rowCount > 0) {
        m_filteredSimData = m_simData;
        if (m_simModel) {
            m_simModel->setData(m_filteredSimData);
        }
    } else if (currentTab == 1 && m_obsData.rowCount > 0) {
        m_filteredObsData = m_obsData;
        if (m_obsModel) {
            m_obsModel->setData(m_filteredObsData);
        }
    }
    
    m_filterValue->clear();
}

void DataTableWidget::updateFilterValues()
{
    QString selectedColumn = m_filterColumn->currentData().toString();
    if (selectedColumn.isEmpty()) {
        return;
    }
    
    m_filterValue->clear();
    
    int currentTab = m_tabWidget->currentIndex();
    const DataTable* currentData = (currentTab == 0) ? &m_simData : &m_obsData;
    
    if (currentData->rowCount == 0) {
        return;
    }
    
    const DataColumn* column = currentData->getColumn(selectedColumn);
    if (column) {
        QSet<QString> uniqueValues;
        for (const QVariant& value : column->data) {
            if (!DataProcessor::isMissingValue(value)) {
                uniqueValues.insert(value.toString());
            }
        }
        
        QStringList sortedValues = uniqueValues.values();
        sortedValues.sort();
        m_filterValue->addItems(sortedValues);
    }
}

void DataTableWidget::onTabChanged(int index)
{
    updateFilterColumns();
}

DataTable DataTableWidget::removeEmptyColumns(const DataTable& data)
{
    if (data.rowCount == 0) {
        return data;
    }
    
    DataTable result;
    result.rowCount = data.rowCount;
    
    for (const QString& columnName : data.columnNames) {
        const DataColumn* column = data.getColumn(columnName);
        if (!column) {
            continue;
        }
        
        // Check if column has any non-missing values
        bool hasValidData = false;
        for (const QVariant& value : column->data) {
            if (!DataProcessor::isMissingValue(value)) {
                hasValidData = true;
                break;
            }
        }
        
        if (hasValidData) {
            result.addColumn(*column);
        } else {
            qDebug() << "DataTableWidget: Removing empty column:" << columnName;
        }
    }
    qDebug() << "DataTableWidget: removeEmptyColumns - Original columns:" << data.columnNames.size() << ", Remaining columns:" << result.columnNames.size();
    return result;
}

void DataTableWidget::updateFilterColumns()
{
    m_filterColumn->clear();
    
    int currentTab = m_tabWidget->currentIndex();
    const DataTable* currentData = (currentTab == 0) ? &m_simData : &m_obsData;
    
    if (currentData->rowCount == 0) {
        return;
    }
    
    for (const QString& columnName : currentData->columnNames) {
        m_filterColumn->addItem(columnName, columnName);
    }
}

