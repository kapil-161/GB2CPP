#ifndef TABLEWIDGET_H
#define TABLEWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QLineEdit>
#include <QLabel>
#include "DataProcessor.h"

class TableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TableWidget(QWidget *parent = nullptr);
    ~TableWidget();
    
    void setData(const DataTable &data);
    void clear();
    void exportToCsv(const QString &filePath);
    
    QTableView* getTableView() const { return m_tableView; }

private slots:
    void onFilterTextChanged(const QString &text);

private:
    void setupUI();
    void setupModel();
    void updateModel(const DataTable &data);
    
    QVBoxLayout *m_layout;
    QLineEdit *m_filterLineEdit;
    QLabel *m_infoLabel;
    QTableView *m_tableView;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxyModel;
    
    DataTable m_currentData;
};

#endif // TABLEWIDGET_H