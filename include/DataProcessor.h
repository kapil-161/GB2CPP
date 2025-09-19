#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QDateTime>
#include <QMap>
#include <QVector>
#include <QObject>
#include <memory>

struct DataColumn {
    QString name;
    QVector<QVariant> data;
    QString dataType; // "numeric", "categorical", "datetime", "string"
    
    DataColumn() = default;
    DataColumn(const QString &columnName) : name(columnName) {}
};

struct DataTable {
    QString tableName;
    QVector<DataColumn> columns;
    QStringList columnNames;
    int rowCount = 0;
    
    void addColumn(const DataColumn &column);
    DataColumn* getColumn(const QString &name);
    const DataColumn* getColumn(const QString &name) const;
    QVariant getValue(int row, const QString &columnName) const;
    void setValue(int row, const QString &columnName, const QVariant &value);
    void addRow(const QVector<QVariant> &rowData);
    void clear();
    int getColumnIndex(const QString &name) const;
    void merge(const DataTable &other);
};

struct CropDetails {
    QString cropCode;
    QString cropName;
    QString directory;
};



class DataProcessor : public QObject
{
    Q_OBJECT

public:
    explicit DataProcessor(QObject *parent = nullptr);

    bool readFile(const QString &filePath, DataTable &data);
    bool readObservedData(const QString &simFilePath, const QString &expCode, const QString &cropCode, DataTable &obsData);
    bool readOutFile(const QString &filePath, DataTable &table);
    bool readOsuFile(const QString &filePath, DataTable &table);
    bool readTFile(const QString &filePath, DataTable &table);

    QStringList prepareFolders(bool includeExtraFolders);
    QStringList prepareOutFiles(const QString &folderName);
    QString getActualFolderPath(const QString &folderName);
    bool isFilePlottable(const QString &filePath);
    
    // SensWork specific methods
    QPair<QString, QString> extractSensWorkCodes(const QString &filePath);
    bool readSensWorkObservedData(const QString &sensWorkFilePath, DataTable &observedData);

    void standardizeDataTypes(DataTable &table);
    void addDateColumns(DataTable &table);
    void addDasDapColumns(DataTable &observedData, const DataTable &simulatedData);
    bool handleMissingValues(DataTable &table, const QString &xVariable);
    void convertDates(DataTable &table);
    void setDSSATBasePath(const QString &path);
    static DataTable filterData(const DataTable &data, const QString &columnName, const QString &filterValue);

signals:
    void dataProcessed(const QString &message);
    void errorOccurred(const QString &error);
    void progressUpdate(int percentage);

public: // Static utility functions
    static QMap<QString, QPair<QString, QString>> m_variableInfoCache;
    static bool m_variableInfoLoaded;
    static QString m_dssatBasePath;

    static void parseDataCDE();
    static QPair<QString, QString> getVariableInfo(const QString &variableName);
    static bool isMissingValue(const QVariant &value);
    static double toDouble(const QVariant &value, bool *ok = nullptr);
    static QDateTime parseDate(const QString &dateStr);
    static QString detectDataType(const QVector<QVariant> &data);
    static QString parseColonSeparatedLine(const QString &line, int index = 1);
    static QDateTime unifiedDateConvert(int year, int doy, const QString &dateStr = QString());
    static QDateTime convertYearDOYToDate(int year, int doy);
    static int calculateDaysAfterSowing(const QDateTime &date, const QDateTime &sowingDate);
    static int calculateDaysAfterPlanting(const QDateTime &date, const QDateTime &plantingDate);
    static QString getDSSATBase();
    static QVector<CropDetails> getCropDetails();
    static QString findDetailCde();
    static QString findDssatProFile();
    static bool verifyDssatInstallation(const QString &basePath);
    static QMap<QString, QString> getOutfileDescriptions();
    static QString findOutfileCde();

private: // Private helper functions (non-static)
    bool parseFileHeader(const QString &filePath, QStringList &headers);
    QStringList parseDataLine(const QString &line, const QStringList &headers);
    void detectColumnTypes(DataTable &table);
    void processNumericColumn(DataColumn &column);
    void processCategoricalColumn(DataColumn &column);
    void processDateColumn(DataColumn &column);
    

private:
};

#endif // DATAPROCESSOR_H