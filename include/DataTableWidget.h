#ifndef DATATABLEWIDGET_H
#define DATATABLEWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableView>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTextStream>
#include "PandasTableModel.h"
#include "DataProcessor.h"

class DataTableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DataTableWidget(QWidget* parent = nullptr);

    void setData(const DataTable& simData = DataTable(), const DataTable& obsData = DataTable());
    void clear();

private slots:
    void exportData();
    void applyFilter();
    void clearFilter();
    void updateFilterValues();
    void onTabChanged(int index);

private:
    void setupUI();
    DataTable removeEmptyColumns(const DataTable& data);
    void updateFilterColumns();
    
    // UI Components
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_controlsLayout;
    
    QPushButton* m_exportButton;
    
    // Filter controls
    QGroupBox* m_filterGroup;
    QComboBox* m_filterColumn;
    QComboBox* m_filterValue;
    QPushButton* m_applyFilterButton;
    QPushButton* m_clearFilterButton;
    
    // Tab widget and table views
    QTabWidget* m_tabWidget;
    QTableView* m_simTableView;
    QTableView* m_obsTableView;
    
    // Data storage
    DataTable m_simData;
    DataTable m_filteredSimData;
    DataTable m_obsData;
    DataTable m_filteredObsData;
    
    // Models
    PandasTableModel* m_simModel;
    PandasTableModel* m_obsModel;
};

#endif // DATATABLEWIDGET_H