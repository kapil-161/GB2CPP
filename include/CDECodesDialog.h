#ifndef CDECODESDIALOG_H
#define CDECODESDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QStringList>

class CDECodesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CDECodesDialog(QWidget *parent = nullptr);

private slots:
    void onSearchTextChanged(const QString &text);

private:
    void setupUI();
    bool loadFromPath(const QString &cdeFilePath);
    void applyFilter(const QString &searchText);

    QLineEdit *m_searchEdit;
    QTableWidget *m_table;
    QStringList m_allSection;
    QStringList m_allCde;
    QStringList m_allLabel;
    QStringList m_allDescription;
};

#endif // CDECODESDIALOG_H
