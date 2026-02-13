#include "CDECodesDialog.h"
#include "DataProcessor.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>

CDECodesDialog::CDECodesDialog(QWidget *parent)
    : QDialog(parent)
    , m_searchEdit(nullptr)
    , m_table(nullptr)
{
    setWindowTitle("CDE Codes Reference");
    setModal(false);
    resize(720, 520);

    setupUI();

    QString cdePath = DataProcessor::findDataCde();
    if (cdePath.isEmpty() || !loadFromPath(cdePath)) {
        QMessageBox::warning(this, "CDE Codes Reference",
            "DATA.CDE not found. Install DSSAT or set DSSAT_PATH to the DSSAT folder (e.g. C:\\DSSAT48).");
    }
}

void CDECodesDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        "DSSAT variable codes (CDE) from all sections of DATA.CDE. Search by section, code, label, or description.");
    intro->setWordWrap(true);
    mainLayout->addWidget(intro);

    QHBoxLayout *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Filter by section, CDE, label, or description...");
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CDECodesDialog::onSearchTextChanged);
    searchLayout->addWidget(m_searchEdit, 1);
    mainLayout->addLayout(searchLayout);

    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({"Section", "CDE", "Label", "Description"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QTableWidget::SelectRows);
    m_table->setEditTriggers(QTableWidget::NoEditTriggers);
    mainLayout->addWidget(m_table);
}

bool CDECodesDialog::loadFromPath(const QString &cdeFilePath)
{
    if (cdeFilePath.isEmpty() || !QFile::exists(cdeFilePath)) {
        return false;
    }

    QFile file(cdeFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    m_allSection.clear();
    m_allCde.clear();
    m_allLabel.clear();
    m_allDescription.clear();
    m_table->setRowCount(0);

    QTextStream in(&file);
    QString currentSection;
    bool inDataBlock = false;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("*")) {
            currentSection = line.mid(1).trimmed();
            inDataBlock = false;
            continue;
        }
        if (line.startsWith("@")) {
            inDataBlock = true;
            continue;
        }
        if (!inDataBlock || line.startsWith("!") || line.trimmed().isEmpty()) {
            continue;
        }
        if (line.length() >= 23) {
            QString cde = line.left(6).trimmed();
            QString label = line.mid(7, 16).trimmed();
            QString description = line.length() >= 78 ? line.mid(23, 55).trimmed() : line.mid(23).trimmed();

            if (!cde.isEmpty()) {
                m_allSection.append(currentSection);
                m_allCde.append(cde);
                m_allLabel.append(label);
                m_allDescription.append(description);

                int row = m_table->rowCount();
                m_table->insertRow(row);
                m_table->setItem(row, 0, new QTableWidgetItem(currentSection));
                m_table->setItem(row, 1, new QTableWidgetItem(cde));
                m_table->setItem(row, 2, new QTableWidgetItem(label));
                m_table->setItem(row, 3, new QTableWidgetItem(description));
            }
        }
    }
    file.close();

    if (m_table->rowCount() == 0) {
        QMessageBox::warning(this, "CDE Codes Reference",
            "DATA.CDE was found but no codes were parsed. Check the file format.");
        return true;
    }

    applyFilter(m_searchEdit->text());
    return true;
}

void CDECodesDialog::onSearchTextChanged(const QString &text)
{
    applyFilter(text);
}

void CDECodesDialog::applyFilter(const QString &searchText)
{
    QString key = searchText.trimmed().toLower();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool match = key.isEmpty()
            || m_allSection[row].toLower().contains(key)
            || m_allCde[row].toLower().contains(key)
            || m_allLabel[row].toLower().contains(key)
            || m_allDescription[row].toLower().contains(key);
        m_table->setRowHidden(row, !match);
    }
}
