#include "DataProcessor.h"
#include "Config.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <QTime>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// DataTable implementation
void DataTable::addColumn(const DataColumn &column)
{
    columns.append(column);
    columnNames.append(column.name);
    if (column.data.size() > rowCount) {
        rowCount = column.data.size();
    }
}

DataColumn* DataTable::getColumn(const QString &name)
{
    for (auto &column : columns) {
        if (column.name == name) {
            return &column;
        }
    }
    return nullptr;
}

const DataColumn* DataTable::getColumn(const QString &name) const
{
    for (const auto &column : columns) {
        if (column.name == name) {
            return &column;
        }
    }
    return nullptr;
}

QVariant DataTable::getValue(int row, const QString &columnName) const
{
    const DataColumn* column = getColumn(columnName);
    if (column && row >= 0 && row < column->data.size()) {
        return column->data[row];
    }
    return QVariant();
}

void DataTable::setValue(int row, const QString &columnName, const QVariant &value)
{
    DataColumn* column = getColumn(columnName);
    if (column && row >= 0 && row < column->data.size()) {
        column->data[row] = value;
    }
}

void DataTable::addRow(const QVector<QVariant> &rowData)
{
    int colIndex = 0;
    for (auto &column : columns) {
        if (colIndex < rowData.size()) {
            column.data.append(rowData[colIndex]);
        } else {
            column.data.append(QVariant());
        }
        colIndex++;
    }
    rowCount++;
}

void DataTable::clear()
{
    columns.clear();
    columnNames.clear();
    rowCount = 0;
}

int DataTable::getColumnIndex(const QString &name) const
{
    return columnNames.indexOf(name);
}

void DataTable::merge(const DataTable &other)
{
    if (other.rowCount == 0) {
        return;
    }

    // Identify all unique column names from both tables
    QStringList allColumnNames = this->columnNames;
    for (const QString &colName : other.columnNames) {
        if (!allColumnNames.contains(colName)) {
            allColumnNames.append(colName);
        }
    }

    // Create new columns in 'this' if they exist in 'other' but not in 'this'
    for (const QString &colName : allColumnNames) {
        if (!this->columnNames.contains(colName)) {
            DataColumn newCol(colName);
            // Fill with empty variants for existing rows in 'this'
            for (int i = 0; i < this->rowCount; ++i) {
                newCol.data.append(QVariant());
            }
            this->addColumn(newCol);
        }
    }

    // Append rows from 'other' to 'this'
    for (int i = 0; i < other.rowCount; ++i) {
        QVector<QVariant> newRowData;
        for (const QString &colName : allColumnNames) {
            const DataColumn* otherCol = other.getColumn(colName);
            if (otherCol && i < otherCol->data.size()) {
                newRowData.append(otherCol->data[i]);
            } else {
                newRowData.append(QVariant()); // Fill with empty variant if column not in 'other'
            }
        }
        this->addRow(newRowData);
    }
}

QMap<QString, QPair<QString, QString>> DataProcessor::m_variableInfoCache;
bool DataProcessor::m_variableInfoLoaded = false;
QString DataProcessor::m_dssatBasePath = "";

// DataProcessor implementation
DataProcessor::DataProcessor(QObject *parent)
    : QObject(parent)
{
}

bool DataProcessor::readFile(const QString &filePath, DataTable &table)
{
    if (!QFile::exists(filePath)) {
        emit errorOccurred(QString("File does not exist: %1").arg(filePath));
        return false;
    }
    
    QString extension = QFileInfo(filePath).suffix().toUpper();
    
    // Route to appropriate reader based on extension (matching Python logic)
    if (extension == "OSU") {
        return readOsuFile(filePath, table);
    } else if (extension.startsWith("O")) {  // .OUT, .OPT, .OVT, etc.
        return readOutFile(filePath, table);
    } else {
        // Try OUT first, then OSU as fallback
        if (readOutFile(filePath, table)) {
            return true;
        }
        return readOsuFile(filePath, table);
    }
}

bool DataProcessor::readOutFile(const QString &filePath, DataTable &table)
{
    qDebug() << "DataProcessor::readOutFile() called with:" << filePath;
    
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "DataProcessor: Cannot open file:" << filePath;
            emit errorOccurred(QString("Cannot open file: %1").arg(filePath));
            return false;
        }
        
        qDebug() << "DataProcessor: File opened successfully, size:" << file.size();
        
        QTextStream in(&file);
        // Try UTF-8 first, fallback to Latin-1 if needed
        in.setEncoding(QStringConverter::Utf8);
        
        QStringList lines;
        int lineCount = 0;
        while (!in.atEnd()) {
            QString line = in.readLine();
            lines.append(line);
            lineCount++;
            if (lineCount % 1000 == 0) {
                qDebug() << "DataProcessor: Read" << lineCount << "lines so far...";
            }
        }
        file.close();
        
        qDebug() << "DataProcessor: Read" << lineCount << "total lines";
    
    // If UTF-8 failed, try Latin-1
    if (lines.isEmpty()) {
        file.open(QIODevice::ReadOnly | QIODevice::Text);
        QTextStream in2(&file);
        in2.setEncoding(QStringConverter::Latin1);
        while (!in2.atEnd()) {
            lines.append(in2.readLine());
        }
        file.close();
    }
    
        if (lines.isEmpty()) {
            qDebug() << "DataProcessor: No lines read from file";
            emit errorOccurred(QString("Cannot read file or file is empty: %1").arg(filePath));
            return false;
        }
        
        qDebug() << "DataProcessor: Starting file parsing...";
        table.clear();
        table.tableName = QFileInfo(filePath).baseName();
        
        // Track current context (matching Python logic)
        QString currentExp = "DEFAULT";
        QString currentTrt = "1";
        QString currentRun = "1";
        QMap<QString, QString> trtToTname;
        
        QVector<DataTable> allTables;
    
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        
        // Track experiment
        if (line.contains("EXPERIMENT") && line.contains(":")) {
            QString afterColon = parseColonSeparatedLine(line);
            QStringList parts = afterColon.simplified().split(' ', Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                currentExp = parts[0];
            }
        }
        // Track RUN
        else if (line.startsWith("*RUN")) {
            if (line.contains(":")) {
                QString runPart = line.split(":")[0].replace("*RUN", "").trimmed();
                if (runPart.toInt() > 0) {
                    currentRun = runPart;
                }
            }
        }
        // Track treatment
        else if (line.toUpper().startsWith("TREATMENT")) {
            QStringList words = line.simplified().split(' ', Qt::SkipEmptyParts);
            if (words.size() >= 2) {
                QString trtStr = words[1].replace(":", "");
                bool ok;
                int trtNum = trtStr.toInt(&ok);
                if (ok) {
                    currentTrt = trtStr;
                    if (line.contains(":")) {
                        QString afterColon = parseColonSeparatedLine(line);
                        QStringList tnameWords = afterColon.simplified().split(' ', Qt::SkipEmptyParts);
                        QString tname = tnameWords.size() > 1 ? 
                            tnameWords.mid(0, tnameWords.size() - 1).join(" ") : afterColon;
                        trtToTname[currentTrt] = tname;
                    } else {
                        trtToTname[currentTrt] = QString("Treatment %1").arg(currentTrt);
                    }
                }
            }
        }
        // Process data tables starting with @
        else if (line.startsWith("@")) {
            QStringList headers = line.mid(1).simplified().split(' ', Qt::SkipEmptyParts);
            QVector<QStringList> dataRows;
            
            int j = i + 1;
            while (j < lines.size()) {
                QString dataLine = lines[j].trimmed();
                
                if (dataLine.isEmpty() || 
                    dataLine.startsWith("@") || 
                    dataLine.startsWith("EXPERIMENT") || 
                    dataLine.startsWith("TREATMENT") ||
                    dataLine.toUpper().contains("MODEL") ||
                    dataLine.toUpper().contains("SUMMARY") ||
                    dataLine.toUpper().contains("SEASONAL")) {
                    if (dataLine.startsWith("@") || 
                        dataLine.startsWith("EXPERIMENT") || 
                        dataLine.startsWith("TREATMENT")) {
                        break;
                    }
                    j++;
                    continue;
                }
                
                if (dataLine.startsWith("*RUN")) {
                    break;
                } else if (dataLine.startsWith("*") || dataLine.startsWith("!") || dataLine.startsWith("#")) {
                    j++;
                    continue;
                }
                
                QStringList fields = dataLine.simplified().split(' ', Qt::SkipEmptyParts);
                if (!fields.isEmpty()) {
                    dataRows.append(fields);
                }
                j++;
            }
            
            if (!dataRows.isEmpty() && !headers.isEmpty()) {
                // Create a temporary table for this data section
                DataTable sectionTable;
                sectionTable.tableName = table.tableName;
                
                // Normalize data rows to match header count
                for (auto &row : dataRows) {
                    while (row.size() < headers.size()) {
                        row.append("");
                    }
                    if (row.size() > headers.size()) {
                        row = row.mid(0, headers.size());
                    }
                }
                
                // Add columns
                for (const QString &header : headers) {
                    DataColumn column(header);
                    sectionTable.addColumn(column);
                }
                
                // Add data rows
                for (const QStringList &row : dataRows) {
                    QVector<QVariant> rowData;
                    for (const QString &value : row) {
                        rowData.append(QVariant(value));
                    }
                    sectionTable.addRow(rowData);
                }
                
                // Add metadata columns
                DataColumn expCol("EXPERIMENT");
                DataColumn trtCol("TRT");
                DataColumn runCol("RUN");
                DataColumn tnameCol("TNAME");
                
                for (int r = 0; r < sectionTable.rowCount; ++r) {
                    expCol.data.append(currentExp);
                    trtCol.data.append(currentTrt);
                    runCol.data.append(currentRun);
                    tnameCol.data.append(trtToTname.value(currentTrt, QString("Treatment %1").arg(currentTrt)));
                }
                
                sectionTable.addColumn(expCol);
                sectionTable.addColumn(trtCol);
                sectionTable.addColumn(runCol);
                sectionTable.addColumn(tnameCol);
                
                allTables.append(sectionTable);
            }
            
            i = j - 1;
        }
    }
    
    if (allTables.isEmpty()) {
        emit errorOccurred("No valid data tables found in file");
        return false;
    }
    
    // Combine all tables (simplified version of Python's concat)
    table = allTables[0]; // Start with first table
    
    for (int t = 1; t < allTables.size(); ++t) {
        const DataTable &otherTable = allTables[t];
        
        // Add rows from other tables (basic concatenation)
        for (int r = 0; r < otherTable.rowCount; ++r) {
            QVector<QVariant> rowData;
            for (const QString &colName : table.columnNames) {
                const DataColumn* col = otherTable.getColumn(colName);
                if (col && r < col->data.size()) {
                    rowData.append(col->data[r]);
                } else {
                    rowData.append(QVariant());
                }
            }
            table.addRow(rowData);
        }
    }
    
    // Handle treatment columns (find the best TRT column)
    QStringList trtCols = {"TRNO", "TR", "TN"};
    for (const QString &col : trtCols) {
        DataColumn* trtColumn = table.getColumn(col);
        if (trtColumn) {
            bool hasValidData = false;
            for (const QVariant &val : trtColumn->data) {
                if (!val.toString().isEmpty() && val.toString() != "0") {
                    hasValidData = true;
                    break;
                }
            }
            if (hasValidData) {
                // Rename this column to TRT
                int colIndex = table.getColumnIndex(col);
                if (colIndex >= 0) {
                    table.columnNames[colIndex] = "TRT";
                    table.columns[colIndex].name = "TRT";
                }
                break;
            }
        }
    }
    
    // Create DATE column from YEAR and DOY if available
    DataColumn* yearCol = table.getColumn("YEAR");
    DataColumn* doyCol = table.getColumn("DOY");
    if (yearCol && doyCol) {
        DataColumn dateCol("DATE");
        for (int r = 0; r < table.rowCount; ++r) {
            int year = yearCol->data[r].toInt();
            int doy = doyCol->data[r].toInt();
            
            if (year > 0 && doy > 0 && doy <= 366) {
                QDateTime dateTime = unifiedDateConvert(year, doy);
                if (dateTime.isValid()) {
                    dateCol.data.append(dateTime.toString("yyyy-MM-dd"));
                } else {
                    dateCol.data.append(QVariant());
                }
            } else {
                dateCol.data.append(QVariant());
            }
        }
        table.addColumn(dateCol);
    }
    
    // Process and standardize data types
    standardizeDataTypes(table);
    
        emit dataProcessed(QString("Successfully loaded %1 rows from %2").arg(table.rowCount).arg(filePath));
        qDebug() << "DataProcessor: readOutFile completed successfully";
        return true;
        
    } catch (const std::exception& e) {
        qDebug() << "DataProcessor: Exception in readOutFile:" << e.what();
        emit errorOccurred(QString("Error parsing file: %1").arg(e.what()));
        return false;
    } catch (...) {
        qDebug() << "DataProcessor: Unknown exception in readOutFile";
        emit errorOccurred("Unknown error parsing file");
        return false;
    }
}

bool DataProcessor::readOsuFile(const QString &filePath, DataTable &table)
{
    // OSU file reader with fixed-width column parsing
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QString("Cannot open OSU file: %1").arg(filePath));
        return false;
    }
    
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());  // Don't trim - we need exact positions
    }
    file.close();
    
    if (lines.isEmpty()) {
        emit errorOccurred("OSU file is empty");
        return false;
    }
    
    // Track treatment names from header comments
    QMap<QString, QString> trtToTname;
    QString currentExp = "DEFAULT";
    
    // Parse header section for treatment information
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        
        // Track experiment from SUMMARY line
        if (line.toUpper().contains("SUMMARY") && line.contains(":")) {
            QStringList parts = line.split(":");
            if (parts.size() > 1) {
                QString afterColon = parts[1].trimmed();
                QStringList expParts = afterColon.simplified().split(' ', Qt::SkipEmptyParts);
                if (!expParts.isEmpty()) {
                    currentExp = expParts[0];
                }
            }
        }
        // Stop at data header
        else if (line.startsWith("@")) {
            break;
        }
    }
    
    // Find header line (starts with @)
    int headerIdx = -1;
    QString headerLine;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith("@")) {
            headerIdx = i;
            headerLine = lines[i];
            break;
        }
    }
    
    if (headerIdx == -1) {
        emit errorOccurred("No header found in OSU file");
        return false;
    }
    
    // Parse OSU files following the Python approach - handle TNAM specially
    QString headerString = headerLine.mid(1); // Remove @
    QStringList headers = headerString.simplified().split(' ', Qt::SkipEmptyParts);
    
    // Find TNAM position like in Python
    int tnamStart = -1;
    int tnamEnd = -1;
    int tnamIdx = -1;
    int fnamIdx = -1;
    
    for (int i = 0; i < headers.size(); ++i) {
        if (headers[i].contains("TNAM")) {
            tnamIdx = i;
        }
        if (headers[i].contains("FNAM")) {
            fnamIdx = i;
        }
    }
    
    if (tnamIdx >= 0) {
        QString tnamPattern = headers[tnamIdx];
        tnamStart = headerLine.indexOf(tnamPattern);
        
        if (fnamIdx >= 0 && fnamIdx == tnamIdx + 1) {
            // FNAM is right after TNAM - use FNAM position as end boundary
            QString fnamPattern = headers[fnamIdx];
            int fnamPos = headerLine.indexOf(fnamPattern, tnamStart + tnamPattern.length());
            tnamEnd = (fnamPos != -1) ? fnamPos - 1 : tnamStart + 25;
        } else {
            // Default TNAM width
            tnamEnd = tnamStart + 25;
        }
    }
    
    table.clear();
    table.tableName = QFileInfo(filePath).baseName();
    
    // Initialize columns
    for (const QString &header : headers) {
        DataColumn column(header);
        table.addColumn(column);
    }
    
    // Parse data rows using Python approach - handle TNAM specially
    for (int i = headerIdx + 1; i < lines.size(); ++i) {
        QString line = lines[i];
        if (line.trimmed().isEmpty() || line.startsWith("!") || line.startsWith("#") || 
            line.startsWith("*") || line.startsWith("@")) {
            continue;
        }
        
        QStringList rowData;
        
        // MAIN FIX: Use position-based extraction for TNAM FIRST, then split the rest
        if (tnamStart >= 0 && tnamEnd >= 0 && line.length() > tnamStart && tnamIdx >= 0) {
            // Extract TNAM using position
            QString tnamValue = line.mid(tnamStart, tnamEnd - tnamStart).trimmed();
            
            // Split line but replace TNAM section with placeholder
            QString lineForSplit = line.left(tnamStart) + "TNAM_PLACEHOLDER" + line.mid(tnamEnd);
            rowData = lineForSplit.simplified().split(' ', Qt::SkipEmptyParts);
            
            // Replace placeholder with actual TNAM value
            for (int j = 0; j < rowData.size(); ++j) {
                if (rowData[j].contains("TNAM_PLACEHOLDER")) {
                    rowData[j] = tnamValue;
                    break;
                }
            }
        } else {
            // Fallback: normal space-splitting
            rowData = line.simplified().split(' ', Qt::SkipEmptyParts);
        }
        
        // Adjust row length to match headers
        while (rowData.size() < headers.size()) {
            rowData.append("");
        }
        while (rowData.size() > headers.size()) {
            rowData.removeLast();
        }
        
        // Convert to QVariant vector
        QVector<QVariant> rowVariants;
        for (const QString &field : rowData) {
            rowVariants.append(QVariant(field));
        }
        
        table.addRow(rowVariants);
    }
    
    // Optimize column name standardization with single loop
    QString tnamColumnName, exnameColumnName;
    int crIndex = -1, trtIndex = -1;
    
    // Single pass to find all columns that need renaming
    for (int i = 0; i < table.columnNames.size(); ++i) {
        const QString &colName = table.columnNames[i];
        
        if (colName == "CR") {
            crIndex = i;
        }
        else if (colName == "TRNO" && trtIndex == -1) {
            trtIndex = i; // Use first TRNO found
        }
        else if (colName.startsWith("TNAM") && tnamColumnName.isEmpty()) {
            tnamColumnName = colName;
        }
        else if (colName.startsWith("EXNAME") && exnameColumnName.isEmpty()) {
            exnameColumnName = colName;
        }
    }
    
    // Apply renamings
    if (crIndex >= 0) {
        table.columnNames[crIndex] = "CROP";
        table.columns[crIndex].name = "CROP";
    }
    
    if (trtIndex >= 0) {
        table.columnNames[trtIndex] = "TRT";
        table.columns[trtIndex].name = "TRT";
    }
    
    if (!tnamColumnName.isEmpty()) {
        int tnamIndex = table.getColumnIndex(tnamColumnName);
        if (tnamIndex >= 0) {
            table.columnNames[tnamIndex] = "TNAME";
            table.columns[tnamIndex].name = "TNAME";
        }
    }
    
    if (!exnameColumnName.isEmpty()) {
        int exnameIndex = table.getColumnIndex(exnameColumnName);
        if (exnameIndex >= 0) {
            table.columnNames[exnameIndex] = "EXPERIMENT";
            table.columns[exnameIndex].name = "EXPERIMENT";
        }
    }
    
    // Add EXPERIMENT column if we found experiment info and don't already have one
    if (!currentExp.isEmpty() && currentExp != "DEFAULT" && !table.columnNames.contains("EXPERIMENT")) {
        DataColumn expCol("EXPERIMENT");
        for (int r = 0; r < table.rowCount; ++r) {
            expCol.data.append(currentExp);
        }
        table.addColumn(expCol);
    }
    
    // Create DATE column from YEAR and DOY if available (similar to OUT file logic)
    DataColumn* yearCol = table.getColumn("WYEAR");
    if (!yearCol) yearCol = table.getColumn("YEAR");
    
    // Look for day of year columns
    DataColumn* doyCol = nullptr;
    QStringList doyColumns = {"PDAT", "HDAT", "ADAT", "MDAT"};
    for (const QString &doyColName : doyColumns) {
        doyCol = table.getColumn(doyColName);
        if (doyCol) break;
    }
    
    if (yearCol && doyCol) {
        DataColumn dateCol("DATE");
        for (int r = 0; r < table.rowCount; ++r) {
            int year = yearCol->data[r].toInt();
            QString doyStr = doyCol->data[r].toString();
            
            // Handle DSSAT date format (YYYYDDD)
            if (doyStr.length() == 7) {
                int fileYear = doyStr.left(4).toInt();
                int doy = doyStr.mid(4).toInt();
                
                if (fileYear > 0 && doy > 0 && doy <= 366) {
                    QDateTime dateTime = unifiedDateConvert(fileYear, doy);
                    if (dateTime.isValid()) {
                        dateCol.data.append(dateTime.toString("yyyy-MM-dd"));
                    } else {
                        dateCol.data.append(QVariant());
                    }
                } else {
                    dateCol.data.append(QVariant());
                }
            } else {
                dateCol.data.append(QVariant());
            }
        }
        table.addColumn(dateCol);
    }
    
    // Clean up all dotted column names for better display
    for (int i = 0; i < table.columnNames.size(); ++i) {
        QString cleanName = table.columnNames[i];
        
        // Remove trailing dots from column names
        while (cleanName.endsWith('.')) {
            cleanName.chop(1);
        }
        
        // Update both the column names list and the column object
        if (cleanName != table.columnNames[i]) {
            table.columnNames[i] = cleanName;
            table.columns[i].name = cleanName;
        }
    }
    
    
    // Debug: Print detailed parsing information
    emit errorOccurred("=== OSU PARSING DEBUG (Python approach) ===");
    emit errorOccurred(QString("Header line: %1").arg(headerLine.left(100)));
    emit errorOccurred(QString("Headers found: %1").arg(headers.join(", ")));
    emit errorOccurred(QString("TNAM index: %1, start: %2, end: %3").arg(tnamIdx).arg(tnamStart).arg(tnamEnd));
    
    // Debug first few data rows
    if (lines.size() > headerIdx + 1) {
        QString firstDataLine = lines[headerIdx + 1];
        emit errorOccurred(QString("First data: %1").arg(firstDataLine.left(100)));
        
        if (tnamStart >= 0 && tnamEnd >= 0) {
            QString tnamValue = firstDataLine.mid(tnamStart, tnamEnd - tnamStart).trimmed();
            emit errorOccurred(QString("TNAM extracted: [%1]").arg(tnamValue));
        }
    }
    
    emit errorOccurred(QString("Final columns: %1").arg(table.columnNames.size()));
    emit errorOccurred(QString("Rows: %1").arg(table.rowCount));
    emit errorOccurred("=== END OSU DEBUG ===");
    
    emit dataProcessed(QString("Successfully loaded %1 rows from OSU file %2").arg(table.rowCount).arg(filePath));
    return true;
}

bool DataProcessor::readObservedData(const QString &simulatedFilePath, const QString &experimentCode, const QString &cropCode, DataTable &table)
{
    qDebug() << "DataProcessor: Attempting to find and read observed data for:" << simulatedFilePath << ", Exp Code:" << experimentCode << ", Crop Code:" << cropCode;
    table.clear(); // Clear the table before populating

    QFileInfo simFileInfo(simulatedFilePath);
    QString folderPath = simFileInfo.absolutePath(); // e.g., /Applications/DSSAT48/Alfalfa

    QString baseName = experimentCode; // Use the passed experimentCode as the base name for the T file

    QStringList tFilePatterns;
    if (!cropCode.isEmpty() && cropCode != "XX") {
        // Primary crop-specific pattern: {experiment}.{cropCode}T
        tFilePatterns.append(QString("%1.%2T").arg(baseName).arg(cropCode));
        
        // Also check for DSSAT version-specific extensions
#ifdef Q_OS_WIN
        tFilePatterns.append(QString("%1.V48").arg(baseName));
#else
        tFilePatterns.append(QString("%1.L48").arg(baseName));
#endif
    }

    QString foundTFile;
    for (const QString& pattern : tFilePatterns) {
        QString fullPath = QDir(folderPath).absoluteFilePath(pattern);
        qDebug() << "DataProcessor: Checking for observed file:" << fullPath;
        if (QFile::exists(fullPath)) {
            foundTFile = fullPath;
            break;
        }
    }

    if (!foundTFile.isEmpty()) {
        qDebug() << "DataProcessor: Found observed data file:" << foundTFile;
        bool success = readTFile(foundTFile, table);
        if (success) {
            // Add EXPERIMENT column to tag data with experiment code
            if (!table.columnNames.contains("EXPERIMENT")) {
                DataColumn expCol("EXPERIMENT");
                for (int r = 0; r < table.rowCount; ++r) {
                    expCol.data.append(experimentCode);
                }
                table.addColumn(expCol);
                qDebug() << "DataProcessor: Added EXPERIMENT column with code:" << experimentCode;
            }
            
            // Add CROP column to tag data with crop code
            if (!table.columnNames.contains("CROP")) {
                DataColumn cropCol("CROP");
                for (int r = 0; r < table.rowCount; ++r) {
                    cropCol.data.append(cropCode);
                }
                table.addColumn(cropCol);
                qDebug() << "DataProcessor: Added CROP column with code:" << cropCode;
            }
            qDebug() << "DataProcessor: Successfully read observed data. Rows:" << table.rowCount << ", Columns:" << table.columnNames.size();
        } else {
            qDebug() << "DataProcessor: Failed to parse observed data from:" << foundTFile;
        }
        return success;
    } else {
        qDebug() << "DataProcessor: No observed data file found for:" << simulatedFilePath;
        return false;
    }
}

void DataProcessor::standardizeDataTypes(DataTable &table)
{
    for (auto &column : table.columns) {
        if (column.dataType == "numeric") {
            processNumericColumn(column);
        } else if (column.dataType == "categorical") {
            processCategoricalColumn(column);
        } else if (column.dataType == "datetime") {
            processDateColumn(column);
        }
    }
}

void DataProcessor::addDateColumns(DataTable &table)
{
    // Look for YEAR and DOY columns to create DATE column
    const DataColumn* yearCol = table.getColumn("YEAR");
    const DataColumn* doyCol = table.getColumn("DOY");
    
    if (yearCol && doyCol) {
        DataColumn dateColumn("DATE");
        dateColumn.dataType = "datetime";
        
        for (int i = 0; i < table.rowCount; ++i) {
            if (i < yearCol->data.size() && i < doyCol->data.size()) {
                bool yearOk, doyOk;
                int year = yearCol->data[i].toInt(&yearOk);
                int doy = doyCol->data[i].toInt(&doyOk);
                
                if (yearOk && doyOk) {
                    QDateTime date = convertYearDOYToDate(year, doy);
                    dateColumn.data.append(date);
                } else {
                    dateColumn.data.append(QVariant());
                }
            } else {
                dateColumn.data.append(QVariant());
            }
        }
        
        table.addColumn(dateColumn);
    }
}

bool DataProcessor::handleMissingValues(DataTable &table, const QString &xVariable)
{
    DataColumn* xCol = table.getColumn(xVariable);
    if (!xCol) {
        return false;
    }
    
    // Remove rows where X variable has missing values
    QVector<int> validRows;
    for (int i = 0; i < xCol->data.size(); ++i) {
        if (!isMissingValue(xCol->data[i])) {
            validRows.append(i);
        }
    }
    
    // Create new columns with only valid rows
    for (auto &column : table.columns) {
        QVector<QVariant> newData;
        for (int row : validRows) {
            if (row < column.data.size()) {
                newData.append(column.data[row]);
            }
        }
        column.data = newData;
    }
    
    table.rowCount = validRows.size();
    return true;
}

void DataProcessor::convertDates(DataTable &table)
{
    for (auto &column : table.columns) {
        if (column.dataType == "datetime") {
            processDateColumn(column);
        }
    }
}

QString DataProcessor::getDSSATBase()
{
    // Check environment variable override first
    QString envPath = qgetenv("DSSAT_PATH");
    if (!envPath.isEmpty() && QDir(envPath).exists()) {
        DataProcessor::m_dssatBasePath = envPath;
        return envPath;
    }
    
    // Use Config.h base path
    QString basePath = Config::DSSAT_BASE;
    if (QDir(basePath).exists()) {
        DataProcessor::m_dssatBasePath = basePath;
        return basePath;
    }
    
    // Try other search paths from Config.h
    for (const QString& searchPath : Config::DSSAT_SEARCH_PATHS) {
        if (QDir(searchPath).exists()) {
            DataProcessor::m_dssatBasePath = searchPath;
            return searchPath;
        }
    }
    
    return QString();
}

QStringList DataProcessor::prepareFolders(bool includeExtraFolders)
{
    QStringList folders;
    
    // Get crop details from DETAIL.CDE and DSSATPRO
    QVector<CropDetails> cropDetails = getCropDetails();
    
    // Add crop folder names - all folders now follow DETAIL.CDE mapping
    for (const CropDetails &crop : cropDetails) {
        if (!crop.cropName.isEmpty()) {
            folders.append(crop.cropName);
        }
    }
    
    // Add SensWork as special sensitivity analysis folder
    if (includeExtraFolders) {
        folders.append("SensWork");
    }
    
    // Remove duplicates and sort
    folders.removeDuplicates();
    folders.sort();
    
    return folders;
}

QStringList DataProcessor::prepareOutFiles(const QString &folderName)
{
    QStringList outFiles;
    
    
    QString actualPath;
    
    // Check if it's already an absolute path
    if (QDir::isAbsolutePath(folderName)) {
        actualPath = folderName;
    } else {
        // Try to find the crop directory by name
        QVector<CropDetails> cropDetails = getCropDetails();
        
        // Special handling for SensWork - sensitivity analysis folder
        if (folderName.compare("SensWork", Qt::CaseInsensitive) == 0) {
            actualPath = QDir(getDSSATBase()).absoluteFilePath("SensWork");
            qDebug() << "prepareOutFiles: Using special SensWork path:" << actualPath;
        } else {
            // Regular crop folders - lookup in DETAIL.CDE mapping
            for (const CropDetails &crop : cropDetails) {
                if (crop.cropName == folderName && !crop.directory.isEmpty()) {
                    actualPath = crop.directory;
                    break;
                }
            }
        }
    }
    
    if (actualPath.isEmpty()) {
        qDebug() << "prepareOutFiles: Could not resolve folder path for:" << folderName;
        return outFiles;
    }
    
    QDir dir(actualPath);
    qDebug() << "prepareOutFiles: Checking if directory exists:" << actualPath;
    if (!dir.exists()) {
        qDebug() << "prepareOutFiles: Directory does not exist:" << actualPath;
        return outFiles;
    }
    
    qDebug() << "prepareOutFiles: Directory exists, looking for files...";
    
    // Get all .OUT files (and other O-files like .OSU, .OVT, .OPT, .OPG, etc.)
    QStringList filters;
    filters << "*.OUT" << "*.out"
            << "*.OSU" << "*.osu"
            << "*.OVT" << "*.ovt"
            << "*.OPT" << "*.opt"
            << "*.OPG" << "*.opg"
            << "*.OEB" << "*.oeb"
            << "*.OEV" << "*.oev"
            << "*.OG2" << "*.og2"
            << "*.OGF" << "*.ogf"
            << "*.OLN" << "*.oln"
            << "*.OME" << "*.ome"
            << "*.OMO" << "*.omo"
            << "*.ONO" << "*.ono"
            << "*.OOV" << "*.oov"
            << "*.OPC" << "*.opc"
            << "*.OPN" << "*.opn"
            << "*.OSN" << "*.osn"
            << "*.OSW" << "*.osw"
            << "*.OTS" << "*.ots"
            << "*.OWE" << "*.owe";
    QStringList allFiles = dir.entryList(filters, QDir::Files, QDir::Name);
    
    qDebug() << "prepareOutFiles: Found" << allFiles.size() << "total files:" << allFiles;
    
    // Filter out non-plottable files with more selective approach
    // Note: Removed "evaluate" as Evaluate.OUT files contain important simulated vs observed comparisons
    QStringList definitelyNonPlottablePatterns = {
        "summary", "overview", "mgmtevent", "mgmtops", "measured"
    };
    
    for (const QString &file : allFiles) {
        QString baseName = QFileInfo(file).baseName().toLower();
        QString extension = QFileInfo(file).suffix().toUpper();
        bool isPlottable = true;
        QString filterReason = "";
        
        // Known plottable file types - always allow these through
        QStringList knownPlottableExtensions = {"OSU", "OPG", "OVT", "OPT"};
        if (knownPlottableExtensions.contains(extension)) {
            outFiles.append(file);
            qDebug() << "prepareOutFiles: Allowing known plottable file:" << file << "(extension:" << extension << ")";
            continue;
        }
        
        // Only apply strict filename filtering to .OUT files
        if (extension == "OUT") {
            // Always allow EVALUATE.OUT files (they're for scatter plots, not time series)
            if (baseName.contains("evaluate")) {
                outFiles.append(file);
                qDebug() << "prepareOutFiles: Allowing EVALUATE.OUT file:" << file;
                continue;
            }
            
            // Check for definitely non-plottable filename patterns
            for (const QString &pattern : definitelyNonPlottablePatterns) {
                if (baseName.contains(pattern)) {
                    isPlottable = false;
                    filterReason = QString("matches non-plottable pattern: %1").arg(pattern);
                    break;
                }
            }
            
            // If passed filename check, analyze file structure (only for .OUT files)
            if (isPlottable) {
                QString fullPath = QDir(actualPath).absoluteFilePath(file);
                if (!isFilePlottable(fullPath)) {
                    isPlottable = false;
                    filterReason = "lacks time-series structure";
                }
            }
        }
        // For other file types, allow them through (they'll be tested when user tries to plot)
        
        if (isPlottable) {
            outFiles.append(file);
        } else {
            qDebug() << "prepareOutFiles: Filtering out non-plottable file:" << file << "(" << filterReason << ")";
        }
    }
    
    qDebug() << "prepareOutFiles: After filtering, found" << outFiles.size() << "plottable files:" << outFiles;
    
    return outFiles;
}

QString DataProcessor::getActualFolderPath(const QString &folderName)
{
    QString actualPath;
    
    // Check if it's already an absolute path
    if (QDir::isAbsolutePath(folderName)) {
        return folderName;
    }
    
    // Special handling for SensWork - sensitivity analysis folder
    if (folderName.compare("SensWork", Qt::CaseInsensitive) == 0) {
        return QDir(getDSSATBase()).absoluteFilePath("SensWork");
    }
    
    // Try to find the crop directory by name
    QVector<CropDetails> cropDetails = getCropDetails();
    
    for (const CropDetails &crop : cropDetails) {
        if (crop.cropName == folderName && !crop.directory.isEmpty()) {
            actualPath = crop.directory;
            break;
        }
    }
    
    // No fallback - directory must come from profile files only
    
    return actualPath;
}

bool DataProcessor::isFilePlottable(const QString &filePath)
{
    // Quick check: try to read a sample of the file to determine if it has time-series structure
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    
    bool hasDataTable = false;
    bool hasTimeColumns = false;
    int dataRowCount = 0;
    QStringList headers;
    
    // Read first 100 lines to analyze structure
    int lineCount = 0;
    while (!in.atEnd() && lineCount < 100) {
        QString line = in.readLine().trimmed();
        lineCount++;
        
        // Found data table header
        if (line.startsWith("@")) {
            hasDataTable = true;
            headers = line.mid(1).simplified().split(' ', Qt::SkipEmptyParts);
            
            // Check for time-series columns
            for (const QString &header : headers) {
                QString upperHeader = header.toUpper();
                if (upperHeader == "YEAR" || upperHeader == "DOY" || 
                    upperHeader == "DAP" || upperHeader == "DAS" || 
                    upperHeader == "DATE") {
                    hasTimeColumns = true;
                    break;
                }
            }
            continue;
        }
        
        // Count data rows after finding headers
        if (hasDataTable && !line.isEmpty() && 
            !line.startsWith("*") && !line.startsWith("!") && !line.startsWith("#") &&
            !line.startsWith("@") && !line.startsWith("EXPERIMENT") && 
            !line.startsWith("TREATMENT") && !line.toUpper().contains("SUMMARY") &&
            !line.toUpper().contains("MODEL")) {
            
            QStringList fields = line.simplified().split(' ', Qt::SkipEmptyParts);
            if (fields.size() >= headers.size() / 2) { // At least half the expected columns
                dataRowCount++;
            }
        }
        
        // Early exit if we have enough info
        if (hasDataTable && hasTimeColumns && dataRowCount >= 5) {
            break;
        }
    }
    
    file.close();
    
    // File is plottable if it has:
    // 1. Data table with @ header
    // 2. Time-series columns (YEAR/DOY/DAP/DAS)
    // 3. Multiple data rows (>= 3 for time series)
    bool isPlottable = hasDataTable && hasTimeColumns && dataRowCount >= 3;
    
    qDebug() << "isFilePlottable:" << filePath << "-> hasDataTable:" << hasDataTable 
             << "hasTimeColumns:" << hasTimeColumns << "dataRowCount:" << dataRowCount 
             << "isPlottable:" << isPlottable;
    
    return isPlottable;
}

QString DataProcessor::findDetailCde()
{
    QStringList searchPaths;
    
    // Check environment variable override first
    QString envPath = qgetenv("DSSAT_PATH");
    if (!envPath.isEmpty()) {
        searchPaths << envPath + QDir::separator() + "DETAIL.CDE";
    }
    
    // Add Config.h-defined search paths
    for (const QString& basePath : Config::DSSAT_SEARCH_PATHS) {
        searchPaths << basePath + QDir::separator() + "DETAIL.CDE";
    }
    
    // Add user home directory paths
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    searchPaths << homeDir + QDir::separator() + "DSSAT48" + QDir::separator() + "DETAIL.CDE";
    
    // Check each path
    for (const QString &path : searchPaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    
    // Try using current DSSAT base
    QString dssatBase = getDSSATBase();
    if (!dssatBase.isEmpty()) {
        QString detailPath = dssatBase + QDir::separator() + "DETAIL.CDE";
        if (QFile::exists(detailPath)) {
            return detailPath;
        }
    }
    
    return QString(); // Not found
}

QString DataProcessor::findDssatProFile()
{
    QString dssatBase = getDSSATBase();
    if (dssatBase.isEmpty()) {
        return QString();
    }
    
#ifdef Q_OS_WIN
    QString proFile = dssatBase + QDir::separator() + "DSSATPRO.V48";
#else
    QString proFile = dssatBase + QDir::separator() + "DSSATPRO.L48";
#endif
    
    if (QFile::exists(proFile)) {
        return proFile;
    }
    return QString();
}

bool DataProcessor::verifyDssatInstallation(const QString &basePath)
{
    if (basePath.isEmpty()) {
        return false;
    }
    
    QStringList requiredFiles;
#ifdef Q_OS_WIN
    requiredFiles << "DSSATPRO.V48" << "DETAIL.CDE" << "DSCSM048.EXE";
#else
    requiredFiles << "DSSATPRO.L48" << "DETAIL.CDE" << "DSCSM048";
#endif
    
    for (const QString &file : requiredFiles) {
        if (!QFile::exists(basePath + QDir::separator() + file)) {
            return false;
        }
    }
    return true;
}

void DataProcessor::parseDataCDE()
{
    if (m_variableInfoLoaded) {
        return; // Already loaded
    }

    QString cdeFile = Config::DSSAT_BASE + QDir::separator() + "DATA.CDE";

    if (!QFile::exists(cdeFile)) {
        qDebug() << "DATA.CDE file not found:" << cdeFile;
        return;
    }

    QFile file(cdeFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Cannot open DATA.CDE file:" << cdeFile;
        return;
    }

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    // Find header line and process data
    int headerIdx = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith("@") && !lines[i].startsWith("!") && !lines[i].startsWith("*")) {
            headerIdx = i;
            break;
        }
    }

    if (headerIdx == -1) {
        qDebug() << "No header found in DATA.CDE file";
        return;
    }

    // Process data lines after header
    for (int i = headerIdx + 1; i < lines.size(); ++i) {
        QString line = lines[i];
        if (line.trimmed().isEmpty() || line.startsWith("!") || line.startsWith("*")) {
            continue;
        }

        if (line.length() >= 23) {
            QString cde = line.left(6).trimmed();
            QString label = line.mid(7, 16).trimmed();
            QString description = line.mid(23).trimmed();

            if (!cde.isEmpty()) {
                m_variableInfoCache[cde] = qMakePair(label, description);
            }
        }
    }
    m_variableInfoLoaded = true;
}

QVector<CropDetails> DataProcessor::getCropDetails()
{
    QVector<CropDetails> cropDetails;
    
    
    // Find DETAIL.CDE and DSSATPRO files
    QString detailPath = findDetailCde();
    QString dssatProPath = findDssatProFile();
    
    
    if (detailPath.isEmpty() || dssatProPath.isEmpty()) {
        return cropDetails;
    }
    
    QMap<QString, CropDetails> cropMap;
    
    // Step 1: Parse DETAIL.CDE for crop codes and names, and applications
    QFile detailFile(detailPath);
    if (detailFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&detailFile);
        bool inCropSection = false;
        bool inApplicationsSection = false;
        
        while (!in.atEnd()) {
            QString line = in.readLine();
            
            if (line.contains("*Crop and Weed Species")) {
                inCropSection = true;
                inApplicationsSection = false;
                continue;
            }
            
            if (line.contains("*Applications")) {
                inApplicationsSection = true;
                inCropSection = false;
                continue;
            }
            
            if (line.startsWith("@CDE")) {
                continue;
            }
            
            if (line.startsWith("*") && (inCropSection || inApplicationsSection)) {
                // Check if this is another section start
                if (!line.contains("*Crop and Weed Species") && !line.contains("*Applications")) {
                    inCropSection = false;
                    inApplicationsSection = false;
                }
                continue;
            }
            
            if ((inCropSection || inApplicationsSection) && !line.trimmed().isEmpty()) {
                if (line.length() >= 8) {
                    QString cropCode = line.left(8).trimmed();
                    QString cropName = line.mid(8, 64).trimmed();
                    
                    if (!cropCode.isEmpty() && !cropName.isEmpty()) {
                        CropDetails crop;
                        crop.cropCode = cropCode.left(2); // First 2 characters
                        crop.cropName = cropName;
                        crop.directory = ""; // Will be filled from DSSATPRO
                        cropMap[crop.cropCode] = crop;
                    }
                }
            }
        }
        detailFile.close();
    }
    
    // Step 2: Parse DSSATPRO file for directories
    QFile proFile(dssatProPath);
    if (proFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&proFile);
        QString dssatBase = QFileInfo(dssatProPath).absolutePath();
        
        
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("*")) {
                continue;
            }
            
            
            // Handle the DSSATPRO format: "ALD C: \DSSAT48\ALFALFA" or "ALD // /Applications/DSSAT48/ALFALFA"
            QStringList parts;
            if (line.contains(" C: ")) {
                parts = line.split(" C: ", Qt::SkipEmptyParts);
            } else if (line.contains(" // ")) {
                parts = line.split(" // ", Qt::SkipEmptyParts);
            }
            
            if (parts.size() >= 2) {
                QString folderCode = parts[0].trimmed();
                QString directory = parts[1].trimmed();
                
                if (folderCode.endsWith("D") && folderCode.length() >= 3) {
                    QString code = folderCode.left(folderCode.length() - 1); // Remove 'D'
                    qDebug() << "DSSATPRO: Found directory mapping:" << folderCode << "(" << code << ") ->" << directory;
                    
                    // Update matching crop
                    if (cropMap.contains(code)) {
                        cropMap[code].directory = directory;
                        qDebug() << "DSSATPRO: Updated directory for code" << code;
                    } else {
                        qDebug() << "DSSATPRO: No matching crop found for code" << code;
                    }
                }
            }
        }
        proFile.close();
    }
    
    // Convert to vector
    for (auto it = cropMap.begin(); it != cropMap.end(); ++it) {
        cropDetails.append(it.value());
    }
    
    return cropDetails;
}

QPair<QString, QString> DataProcessor::extractSensWorkCodes(const QString &filePath)
{
    QString experimentCode;
    QString cropCode;
    
    qDebug() << "DataProcessor::extractSensWorkCodes - Reading from:" << filePath;
    
    // Read the file to extract EXPERIMENT and CROP codes
    DataTable tempData;
    if (!readFile(filePath, tempData)) {
        qWarning() << "DataProcessor::extractSensWorkCodes - Failed to read file:" << filePath;
        return qMakePair(QString(), QString());
    }
    
    // Extract EXPERIMENT code
    if (tempData.columnNames.contains("EXPERIMENT")) {
        const DataColumn* expCol = tempData.getColumn("EXPERIMENT");
        if (expCol && !expCol->data.isEmpty()) {
            for (const QVariant &value : expCol->data) {
                QString exp = value.toString().trimmed();
                if (!exp.isEmpty() && exp != "DEFAULT") {
                    experimentCode = exp;
                    break; // Use first valid experiment code
                }
            }
        }
    }
    
    // Extract CROP code - try multiple sources
    // 1. Try CROP column
    if (tempData.columnNames.contains("CROP")) {
        const DataColumn* cropCol = tempData.getColumn("CROP");
        if (cropCol && !cropCol->data.isEmpty()) {
            for (const QVariant &value : cropCol->data) {
                QString crop = value.toString().trimmed();
                if (!crop.isEmpty()) {
                    cropCode = crop;
                    qDebug() << "DataProcessor::extractSensWorkCodes - Found crop code in CROP column:" << cropCode;
                    break; // Use first valid crop code
                }
            }
        }
    }
    
    // 2. Try CR column if CROP not found
    if (cropCode.isEmpty() && tempData.columnNames.contains("CR")) {
        const DataColumn* crCol = tempData.getColumn("CR");
        if (crCol && !crCol->data.isEmpty()) {
            for (const QVariant &value : crCol->data) {
                QString crop = value.toString().trimmed();
                if (!crop.isEmpty()) {
                    cropCode = crop;
                    qDebug() << "DataProcessor::extractSensWorkCodes - Found crop code in CR column:" << cropCode;
                    break; // Use first valid crop code
                }
            }
        }
    }
    
    // 3. Try to parse MODEL line from file header if no column found
    if (cropCode.isEmpty()) {
        qDebug() << "DataProcessor::extractSensWorkCodes - No CROP/CR column found, parsing MODEL line";
        
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString line;
            int lineCount = 0;
            
            // Read first 50 lines to find MODEL line
            while (!in.atEnd() && lineCount < 50) {
                line = in.readLine().trimmed();
                lineCount++;
                
                // Look for MODEL line: "MODEL          : CSCER048 - Wheat"
                if (line.startsWith("MODEL") && line.contains(":")) {
                    QString modelInfo = line.split(":").last().trimmed();
                    qDebug() << "DataProcessor::extractSensWorkCodes - Found MODEL line:" << modelInfo;
                    
                    // Extract crop name from model info (e.g., "CSCER048 - Wheat")
                    if (modelInfo.contains(" - ")) {
                        QString cropName = modelInfo.split(" - ").last().trimmed();
                        qDebug() << "DataProcessor::extractSensWorkCodes - Extracted crop name:" << cropName;
                        
                        // Map crop name to crop code using DETAIL.CDE
                        QVector<CropDetails> cropDetails = getCropDetails();
                        for (const CropDetails &crop : cropDetails) {
                            if (crop.cropName.compare(cropName, Qt::CaseInsensitive) == 0) {
                                cropCode = crop.cropCode.toUpper();
                                qDebug() << "DataProcessor::extractSensWorkCodes - Mapped crop name to code:" << cropName << "->" << cropCode;
                                break;
                            }
                        }
                        
                        // If no exact match, try partial matching
                        if (cropCode.isEmpty()) {
                            for (const CropDetails &crop : cropDetails) {
                                if (crop.cropName.contains(cropName, Qt::CaseInsensitive) || 
                                    cropName.contains(crop.cropName, Qt::CaseInsensitive)) {
                                    cropCode = crop.cropCode.toUpper();
                                    qDebug() << "DataProcessor::extractSensWorkCodes - Partial match crop name to code:" << cropName << "->" << cropCode;
                                    break;
                                }
                            }
                        }
                    }
                    break; // Found MODEL line, stop searching
                }
            }
            file.close();
        }
        
        // Fallback if still no crop code found
        if (cropCode.isEmpty()) {
            cropCode = "XX"; // Default unknown crop
            qDebug() << "DataProcessor::extractSensWorkCodes - Could not determine crop code, using default: XX";
        }
    }
    
    qDebug() << "DataProcessor::extractSensWorkCodes - Extracted experiment:" << experimentCode << "crop:" << cropCode;
    
    return qMakePair(experimentCode, cropCode);
}

bool DataProcessor::readSensWorkObservedData(const QString &sensWorkFilePath, DataTable &observedData)
{
    qDebug() << "DataProcessor::readSensWorkObservedData - Processing SensWork file:" << sensWorkFilePath;
    
    // Extract experiment and crop codes from the SensWork file
    QPair<QString, QString> codes = extractSensWorkCodes(sensWorkFilePath);
    QString experimentCode = codes.first;
    QString cropCode = codes.second;
    
    if (experimentCode.isEmpty() || cropCode.isEmpty()) {
        qWarning() << "DataProcessor::readSensWorkObservedData - Could not extract experiment/crop codes";
        return false;
    }
    
    // Construct observed data file pattern: experiment_code.crop_code+T
    QString observedFileName = QString("%1.%2T").arg(experimentCode).arg(cropCode);
    qDebug() << "DataProcessor::readSensWorkObservedData - Looking for observed data file:" << observedFileName;
    
    // Search for observed data file in SensWork directory and crop directories
    QStringList searchPaths;
    
    // 1. First check SensWork directory itself
    QString sensWorkDir = QFileInfo(sensWorkFilePath).absolutePath();
    searchPaths << sensWorkDir;
    
    // 2. Then check the original crop directory where the experiment likely came from
    QVector<CropDetails> cropDetails = getCropDetails();
    for (const CropDetails &crop : cropDetails) {
        if (crop.cropCode.compare(cropCode, Qt::CaseInsensitive) == 0 && !crop.directory.isEmpty()) {
            searchPaths << crop.directory;
            qDebug() << "DataProcessor::readSensWorkObservedData - Added crop directory to search:" << crop.directory;
        }
    }
    
    // 3. As fallback, check standard DSSAT directories
    QString dssatBase = getDSSATBase();
    QStringList possibleCropDirs = {
        QDir(dssatBase).absoluteFilePath("Maize"),
        QDir(dssatBase).absoluteFilePath("MAIZE"), 
        QDir(dssatBase).absoluteFilePath("Wheat"),
        QDir(dssatBase).absoluteFilePath("WHEAT"),
        QDir(dssatBase).absoluteFilePath("Soybean"),
        QDir(dssatBase).absoluteFilePath("SOYBEAN")
    };
    searchPaths << possibleCropDirs;
    
    // Search for the observed data file
    QString observedFilePath;
    for (const QString &searchPath : searchPaths) {
        QDir dir(searchPath);
        if (!dir.exists()) continue;
        
        // Try exact filename
        QString candidatePath = dir.absoluteFilePath(observedFileName);
        if (QFile::exists(candidatePath)) {
            observedFilePath = candidatePath;
            qDebug() << "DataProcessor::readSensWorkObservedData - Found observed data at:" << observedFilePath;
            break;
        }
        
        // Try case-insensitive search
        QStringList filters;
        filters << QString("*%1*").arg(observedFileName.toUpper())
                << QString("*%1*").arg(observedFileName.toLower());
        
        QStringList matches = dir.entryList(filters, QDir::Files);
        if (!matches.isEmpty()) {
            observedFilePath = dir.absoluteFilePath(matches.first());
            qDebug() << "DataProcessor::readSensWorkObservedData - Found observed data (case-insensitive):" << observedFilePath;
            break;
        }
    }
    
    if (observedFilePath.isEmpty()) {
        qWarning() << "DataProcessor::readSensWorkObservedData - Could not find observed data file:" << observedFileName;
        return false;
    }
    
    // Read the observed data file
    bool success = readTFile(observedFilePath, observedData);
    if (success) {
        qDebug() << "DataProcessor::readSensWorkObservedData - Successfully loaded observed data:" << observedData.rowCount << "rows";
        
        // Add CROP column to observed data if it doesn't exist
        if (!observedData.columnNames.contains("CROP")) {
            DataColumn cropCol("CROP");
            for (int r = 0; r < observedData.rowCount; ++r) {
                cropCol.data.append(cropCode);
            }
            observedData.addColumn(cropCol);
            qDebug() << "DataProcessor::readSensWorkObservedData - Added CROP column to observed data";
        }
        
        // Add EXPERIMENT column to observed data if it doesn't exist
        if (!observedData.columnNames.contains("EXPERIMENT")) {
            DataColumn expCol("EXPERIMENT");
            for (int r = 0; r < observedData.rowCount; ++r) {
                expCol.data.append(experimentCode);
            }
            observedData.addColumn(expCol);
            qDebug() << "DataProcessor::readSensWorkObservedData - Added EXPERIMENT column to observed data";
        }
    } else {
        qWarning() << "DataProcessor::readSensWorkObservedData - Failed to read observed data file:" << observedFilePath;
    }
    
    return success;
}

QPair<QString, QString> DataProcessor::getVariableInfo(const QString &variableName)
{
    if (!m_variableInfoLoaded) {
        parseDataCDE();
    }

    if (m_variableInfoCache.contains(variableName)) {
        return qMakePair(m_variableInfoCache[variableName].first, m_variableInfoCache[variableName].second);
    }
    return qMakePair(QString(), QString());
}

bool DataProcessor::isMissingValue(const QVariant &value)
{
    if (value.isNull() || !value.isValid()) {
        return true;
    }
    
    bool ok;
    double numValue = value.toDouble(&ok);
    if (ok && Config::MISSING_VALUES.count(numValue) > 0) {
        return true;
    }
    
    QString strValue = value.toString().trimmed();
    return Config::MISSING_VALUE_STRINGS.contains(strValue);
}

double DataProcessor::toDouble(const QVariant &value, bool *ok)
{
    if (isMissingValue(value)) {
        if (ok) *ok = false;
        return std::numeric_limits<double>::quiet_NaN();
    }
    
    return value.toDouble(ok);
}

QDateTime DataProcessor::parseDate(const QString &dateStr)
{
    // Try different date formats
    QStringList formats = {
        "yyyy-MM-dd",
        "MM/dd/yyyy",
        "dd/MM/yyyy",
        "yyyy/MM/dd",
        "yyyyMMdd"
    };
    
    for (const QString &format : formats) {
        QDateTime date = QDateTime::fromString(dateStr, format);
        if (date.isValid()) {
            return date;
        }
    }
    
    return QDateTime();
}

QString DataProcessor::detectDataType(const QVector<QVariant> &data)
{
    if (data.isEmpty()) {
        return "string";
    }
    
    int numericCount = 0;
    int dateCount = 0;
    int validCount = 0;
    
    for (const QVariant &value : data) {
        if (isMissingValue(value)) {
            continue;
        }
        
        validCount++;
        
        // Check if numeric
        bool ok;
        value.toDouble(&ok);
        if (ok) {
            numericCount++;
        }
        
        // Check if date
        QString strValue = value.toString();
        if (!parseDate(strValue).isNull()) {
            dateCount++;
        }
    }
    
    if (validCount == 0) {
        return "string";
    }
    
    double numericRatio = static_cast<double>(numericCount) / validCount;
    double dateRatio = static_cast<double>(dateCount) / validCount;
    
    if (dateRatio > 0.8) {
        return "datetime";
    } else if (numericRatio > 0.8) {
        return "numeric";
    } else if (numericRatio > 0.3) {
        return "categorical";
    } else {
        return "string";
    }
}

void DataProcessor::setDSSATBasePath(const QString &path)
{
    m_dssatBasePath = path;
}

QString DataProcessor::parseColonSeparatedLine(const QString &line, int index)
{
    QStringList parts = line.split(":");
    if (parts.size() > index) {
        return parts[index].trimmed();
    }
    return QString();
}

QDateTime DataProcessor::unifiedDateConvert(int year, int doy, const QString &dateStr)
{
    // Handle year/doy format (most common in DSSAT)
    if (year > 0 && doy > 0) {
        if (year >= 1900 && year <= 2100 && doy >= 1 && doy <= 366) {
            QDate baseDate(year, 1, 1);
            QDate targetDate = baseDate.addDays(doy - 1);
            return QDateTime(targetDate, QTime(0, 0));
        }
        return QDateTime();
    }
    
    // Handle date string format
    if (!dateStr.isEmpty()) {
        QString cleanStr = dateStr.trimmed();
        
        // Check for invalid/missing values
        if (cleanStr.isEmpty() || cleanStr == "-99" || cleanStr == "-99.0" || 
            cleanStr == "NA" || cleanStr == "NaN") {
            return QDateTime();
        }
        
        // Handle 7-digit format (YYYYDDD)
        if (cleanStr.length() == 7 && cleanStr.toInt() > 0) {
            int yearPart = cleanStr.left(4).toInt();
            int doyPart = cleanStr.mid(4).toInt();
            if (yearPart >= 1900 && yearPart <= 2100 && doyPart >= 1 && doyPart <= 366) {
                QDate baseDate(yearPart, 1, 1);
                QDate targetDate = baseDate.addDays(doyPart - 1);
                return QDateTime(targetDate, QTime(0, 0));
            }
        }
        
        // Handle 5-digit format (YYDDD)
        if (cleanStr.length() == 5 && cleanStr.toInt() > 0) {
            int yearPart = cleanStr.left(2).toInt();
            int doyPart = cleanStr.mid(2).toInt();
            int fullYear = (yearPart <= 30) ? 2000 + yearPart : 1900 + yearPart;
            if (doyPart >= 1 && doyPart <= 366) {
                QDate baseDate(fullYear, 1, 1);
                QDate targetDate = baseDate.addDays(doyPart - 1);
                return QDateTime(targetDate, QTime(0, 0));
            }
        }
        
        // Try standard Qt date parsing
        QDateTime result = QDateTime::fromString(cleanStr, Qt::ISODate);
        if (result.isValid()) {
            return result;
        }
        
        // Try other common formats
        QStringList formats = {
            "yyyy-MM-dd", "MM/dd/yyyy", "dd/MM/yyyy", "yyyy/MM/dd",
            "dd-MM-yyyy", "MM-dd-yyyy", "yyyyMMdd"
        };
        
        for (const QString &format : formats) {
            result = QDateTime::fromString(cleanStr, format);
            if (result.isValid()) {
                return result;
            }
        }
    }
    
    return QDateTime();
}

bool DataProcessor::parseFileHeader(const QString &filePath, QStringList &headers)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream in(&file);
    QString line;
    
    // Skip comment lines and find header
    while (!in.atEnd()) {
        line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("*") || line.startsWith("!")) {
            continue;
        }
        break;
    }
    
    // Parse header line
    headers = line.simplified().split(' ', Qt::SkipEmptyParts);
    return !headers.isEmpty();
}

QStringList DataProcessor::parseDataLine(const QString &line, const QStringList &headers)
{
    QStringList values = line.simplified().split(' ', Qt::SkipEmptyParts);
    
    // Ensure we have the right number of columns
    while (values.size() < headers.size()) {
        values.append("");
    }
    
    return values;
}

void DataProcessor::detectColumnTypes(DataTable &table)
{
    for (auto &column : table.columns) {
        column.dataType = detectDataType(column.data);
    }
}


void DataProcessor::processNumericColumn(DataColumn &column)
{
    for (auto &value : column.data) {
        if (!isMissingValue(value)) {
            bool ok;
            double numValue = value.toDouble(&ok);
            if (ok) {
                value = numValue;
            }
        } else {
            value = QVariant();
        }
    }
}

void DataProcessor::processCategoricalColumn(DataColumn &column)
{
    // Convert to strings and handle missing values
    for (auto &value : column.data) {
        if (isMissingValue(value)) {
            value = QVariant();
        } else {
            value = value.toString();
        }
    }
}

void DataProcessor::processDateColumn(DataColumn &column)
{
    for (auto &value : column.data) {
        if (!isMissingValue(value)) {
            QString dateStr = value.toString();
            QDateTime date = parseDate(dateStr);
            if (date.isValid()) {
                value = date;
            } else {
                value = QVariant();
            }
        } else {
            value = QVariant();
        }
    }
}



bool DataProcessor::readTFile(const QString &filePath, DataTable &table)
{
    qDebug() << "DataProcessor: Reading T file:" << filePath;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QString("Cannot open T file: %1").arg(filePath));
        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    if (lines.isEmpty()) {
        emit errorOccurred("T file is empty");
        return false;
    }

    table.clear();
    table.tableName = QFileInfo(filePath).baseName();

    QVector<int> headerIndices;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].trimmed().startsWith("@")) {
            headerIndices.append(i);
        }
    }

    if (headerIndices.isEmpty()) {
        emit errorOccurred("No header found in T file");
        return false;
    }

    QVector<DataTable> allDataTables;

    for (int hIdx = 0; hIdx < headerIndices.size(); ++hIdx) {
        int currentHeaderLine = headerIndices[hIdx];
        QString headerLineContent = lines[currentHeaderLine].trimmed();
        QStringList headers = headerLineContent.mid(1).simplified().split(' ', Qt::SkipEmptyParts);

        DataTable currentSectionTable;
        for (const QString &header : headers) {
            currentSectionTable.addColumn(DataColumn(header));
        }

        int dataStartLine = currentHeaderLine + 1;
        int dataEndLine = (hIdx + 1 < headerIndices.size()) ? headerIndices[hIdx + 1] : lines.size();

        for (int i = dataStartLine; i < dataEndLine; ++i) {
            QString line = lines[i].trimmed();
            if (line.isEmpty() || line.startsWith("!") || line.startsWith("*") || line.startsWith("#")) {
                continue;
            }

            QStringList fields = line.simplified().split(' ', Qt::SkipEmptyParts);

            // Normalize field count
            while (fields.size() < headers.size()) {
                fields.append("");
            }
            if (fields.size() > headers.size()) {
                fields = fields.mid(0, headers.size());
            }

            QVector<QVariant> rowData;
            for (const QString &field : fields) {
                rowData.append(QVariant(field));
            }
            currentSectionTable.addRow(rowData);
        }
        allDataTables.append(currentSectionTable);
    }

    if (allDataTables.isEmpty()) {
        emit errorOccurred("No valid data sections found in T file");
        return false;
    }

    // Combine all sections. For simplicity, we'll just take the first non-empty one for now.
    // A more robust solution would merge based on common columns like TRNO and DATE.
    for (const DataTable& sectionTable : allDataTables) {
        if (sectionTable.rowCount > 0) {
            table.merge(sectionTable);
        }
    }

    if (table.rowCount == 0) {
        emit errorOccurred("No data found in any section of T file");
        return false;
    }

    // Rename common columns (TRNO to TRT)
    if (table.columnNames.contains("TRNO")) {
        int idx = table.getColumnIndex("TRNO");
        table.columnNames[idx] = "TRT";
        table.columns[idx].name = "TRT";
    }

    // Convert dates (PDAT or DATE) to unified format
    if (table.columnNames.contains("PDAT")) {
        DataColumn* pdatCol = table.getColumn("PDAT");
        if (pdatCol) {
            DataColumn dateCol("DATE");
            for (const QVariant& val : pdatCol->data) {
                QDateTime date = unifiedDateConvert(-1, -1, val.toString());
                if (date.isValid()) {
                    dateCol.data.append(date.toString("yyyy-MM-dd"));
                } else {
                    dateCol.data.append(QVariant());
                }
            }
            table.addColumn(dateCol);
        }
    } else if (table.columnNames.contains("DATE")) {
        DataColumn* dateCol = table.getColumn("DATE");
        if (dateCol) {
            for (int r = 0; r < dateCol->data.size(); ++r) {
                QDateTime date = unifiedDateConvert(-1, -1, dateCol->data[r].toString());
                if (date.isValid()) {
                    dateCol->data[r] = date.toString("yyyy-MM-dd");
                } else {
                    dateCol->data[r] = QVariant();
                }
            }
        }
    }

    // Process and standardize data types
    standardizeDataTypes(table);

    qDebug() << "DataProcessor: readTFile - Final table columns:" << table.columnNames << ", rows:" << table.rowCount;
    return true;
}

QDateTime DataProcessor::convertYearDOYToDate(int year, int doy)
{
    QDate date(year, 1, 1);
    QDate targetDate = date.addDays(doy - 1);
    return QDateTime(targetDate, QTime(0, 0));
}

int DataProcessor::calculateDaysAfterSowing(const QDateTime &date, const QDateTime &sowingDate)
{
    return sowingDate.daysTo(date);
}

int DataProcessor::calculateDaysAfterPlanting(const QDateTime &date, const QDateTime &plantingDate)
{
    return plantingDate.daysTo(date);
}

DataTable DataProcessor::filterData(const DataTable &data, const QString &columnName, const QString &filterValue)
{
    DataTable result;
    
    if (data.rowCount == 0 || columnName.isEmpty() || filterValue.isEmpty()) {
        return result;
    }
    
    const DataColumn* filterColumn = data.getColumn(columnName);
    if (!filterColumn) {
        return result;
    }
    
    // Find matching rows
    QVector<int> matchingRows;
    for (int i = 0; i < filterColumn->data.size(); ++i) {
        const QVariant& value = filterColumn->data[i];
        if (!isMissingValue(value) && value.toString() == filterValue) {
            matchingRows.append(i);
        }
    }
    
    if (matchingRows.isEmpty()) {
        return result;
    }
    
    // Create filtered data table
    result.rowCount = matchingRows.size();
    result.columnNames = data.columnNames;
    
    for (const QString& colName : data.columnNames) {
        const DataColumn* sourceColumn = data.getColumn(colName);
        if (sourceColumn) {
            DataColumn filteredColumn;
            filteredColumn.name = sourceColumn->name;
            filteredColumn.dataType = sourceColumn->dataType;
            
            for (int rowIndex : matchingRows) {
                if (rowIndex < sourceColumn->data.size()) {
                    filteredColumn.data.append(sourceColumn->data[rowIndex]);
                }
            }
            
            result.addColumn(filteredColumn);
        }
    }
    
    return result;
}

void DataProcessor::addDasDapColumns(DataTable &observedData, const DataTable &simulatedData)
{
    qDebug() << "DataProcessor: Adding DAS/DAP columns to observed data";
    
    // Check if required columns exist
    if (!observedData.columnNames.contains("DATE") || !simulatedData.columnNames.contains("DATE")) {
        qDebug() << "DataProcessor: Missing DATE column in observed or simulated data";
        return;
    }
    
    if (!observedData.columnNames.contains("TRT") || !simulatedData.columnNames.contains("TRT")) {
        qDebug() << "DataProcessor: Missing TRT column in observed or simulated data";
        return;
    }
    
    if (!simulatedData.columnNames.contains("DAS") || !simulatedData.columnNames.contains("DAP")) {
        qDebug() << "DataProcessor: Missing DAS or DAP column in simulated data";
        return;
    }
    
    // Add DAS and DAP columns to observed data
    DataColumn dasColumn("DAS");
    DataColumn dapColumn("DAP");
    dasColumn.dataType = "numeric";
    dapColumn.dataType = "numeric";
    
    const DataColumn* obsDateCol = observedData.getColumn("DATE");
    const DataColumn* obsTrtCol = observedData.getColumn("TRT");
    const DataColumn* simDateCol = simulatedData.getColumn("DATE");
    const DataColumn* simTrtCol = simulatedData.getColumn("TRT");
    const DataColumn* simDasCol = simulatedData.getColumn("DAS");
    const DataColumn* simDapCol = simulatedData.getColumn("DAP");
    
    if (!obsDateCol || !obsTrtCol || !simDateCol || !simTrtCol || !simDasCol || !simDapCol) {
        qDebug() << "DataProcessor: Failed to get required columns";
        return;
    }
    
    // Process each observed data row
    for (int obsRow = 0; obsRow < observedData.rowCount; ++obsRow) {
        QString obsDateStr = obsDateCol->data[obsRow].toString();
        QString obsTrt = obsTrtCol->data[obsRow].toString();
        
        QDate obsDate = QDate::fromString(obsDateStr, "yyyy-MM-dd");
        if (!obsDate.isValid()) {
            // Try alternative date format
            obsDate = QDate::fromString(obsDateStr, "yyyyDDD");
        }
        
        if (!obsDate.isValid()) {
            qDebug() << "DataProcessor: Invalid date format in observed data:" << obsDateStr;
            dasColumn.data.append(QVariant());
            dapColumn.data.append(QVariant());
            continue;
        }
        
        // Find matching treatment in simulated data
        QVariant foundDas, foundDap;
        bool exactMatch = false;
        
        // First try exact date match
        for (int simRow = 0; simRow < simulatedData.rowCount; ++simRow) {
            QString simTrt = simTrtCol->data[simRow].toString();
            if (simTrt != obsTrt) continue;
            
            QString simDateStr = simDateCol->data[simRow].toString();
            QDate simDate = QDate::fromString(simDateStr, "yyyy-MM-dd");
            if (!simDate.isValid()) {
                simDate = QDate::fromString(simDateStr, "yyyyDDD");
            }
            
            if (simDate.isValid() && simDate == obsDate) {
                foundDas = simDasCol->data[simRow];
                foundDap = simDapCol->data[simRow];
                exactMatch = true;
                break;
            }
        }
        
        // If no exact match, interpolate between nearest dates
        if (!exactMatch) {
            QDate closestBefore;
            QDate closestAfter;
            QVariant dasBefore, dapBefore, dasAfter, dapAfter;
            
            for (int simRow = 0; simRow < simulatedData.rowCount; ++simRow) {
                QString simTrt = simTrtCol->data[simRow].toString();
                if (simTrt != obsTrt) continue;
                
                QString simDateStr = simDateCol->data[simRow].toString();
                QDate simDate = QDate::fromString(simDateStr, "yyyy-MM-dd");
                if (!simDate.isValid()) {
                    simDate = QDate::fromString(simDateStr, "yyyyDDD");
                }
                
                if (!simDate.isValid()) continue;
                
                if (simDate < obsDate && (!closestBefore.isValid() || simDate > closestBefore)) {
                    closestBefore = simDate;
                    dasBefore = simDasCol->data[simRow];
                    dapBefore = simDapCol->data[simRow];
                }
                
                if (simDate > obsDate && (!closestAfter.isValid() || simDate < closestAfter)) {
                    closestAfter = simDate;
                    dasAfter = simDasCol->data[simRow];
                    dapAfter = simDapCol->data[simRow];
                }
            }
            
            // Interpolate or extrapolate
            if (closestBefore.isValid() && closestAfter.isValid()) {
                // Linear interpolation
                int totalDays = closestBefore.daysTo(closestAfter);
                int daysFromBefore = closestBefore.daysTo(obsDate);
                
                if (totalDays > 0) {
                    double dasStart = dasBefore.toDouble();
                    double dapStart = dapBefore.toDouble();
                    double dasEnd = dasAfter.toDouble();
                    double dapEnd = dapAfter.toDouble();
                    
                    double dasDiff = dasEnd - dasStart;
                    double dapDiff = dapEnd - dapStart;
                    
                    foundDas = QVariant(qRound(dasStart + (dasDiff * daysFromBefore / totalDays)));
                    foundDap = QVariant(qRound(dapStart + (dapDiff * daysFromBefore / totalDays)));
                }
            } else if (closestBefore.isValid()) {
                // Extrapolate from before
                int daysDiff = closestBefore.daysTo(obsDate);
                foundDas = QVariant(dasBefore.toInt() + daysDiff);
                foundDap = QVariant(dapBefore.toInt() + daysDiff);
            } else if (closestAfter.isValid()) {
                // Extrapolate from after
                int daysDiff = obsDate.daysTo(closestAfter);
                foundDas = QVariant(dasAfter.toInt() - daysDiff);
                foundDap = QVariant(dapAfter.toInt() - daysDiff);
            }
        }
        
        dasColumn.data.append(foundDas);
        dapColumn.data.append(foundDap);
    }
    
    // Add the columns to observed data
    observedData.addColumn(dasColumn);
    observedData.addColumn(dapColumn);
    
    qDebug() << "DataProcessor: Successfully added DAS/DAP columns to observed data";
}

QString DataProcessor::findOutfileCde()
{
    QString dssatBase = getDSSATBase();
    qDebug() << "DataProcessor::findOutfileCde() - DSSAT Base:" << dssatBase;
    
    if (!dssatBase.isEmpty()) {
        QString outfilePath = dssatBase + QDir::separator() + "OUTPUT.CDE";
        qDebug() << "DataProcessor::findOutfileCde() - Looking for:" << outfilePath;
        
        if (QFile::exists(outfilePath)) {
            qDebug() << "DataProcessor::findOutfileCde() - Found OUTPUT.CDE at:" << outfilePath;
            return outfilePath;
        } else {
            qDebug() << "DataProcessor::findOutfileCde() - OUTPUT.CDE not found at:" << outfilePath;
        }
    }
    
    return QString();
}

QMap<QString, QString> DataProcessor::getOutfileDescriptions()
{
    static QMap<QString, QString> outfileDescriptions;
    static bool loaded = false;
    
    if (loaded) {
        return outfileDescriptions;
    }
    
    QString outfilePath = findOutfileCde();
    if (outfilePath.isEmpty()) {
        qDebug() << "DataProcessor::getOutfileDescriptions() - OUTPUT.CDE not found";
        loaded = true;
        return outfileDescriptions;
    }
    
    QFile file(outfilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "DataProcessor::getOutfileDescriptions() - Cannot open OUTPUT.CDE:" << outfilePath;
        loaded = true;
        return outfileDescriptions;
    }
    
    QTextStream stream(&file);
    QString line;
    
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        
        // Skip empty lines, comments, and header lines
        if (line.isEmpty() || line.startsWith("*") || line.startsWith("!") || line.startsWith("@")) {
            continue;
        }
        
        // Parse lines in OUTPUT.CDE format
        // Example formats:
        // Chemical.OUT    CH Daily chemical applications output file            OUTCH
        // PlantP.OUT      PP Daily plant phosphorus output
        // Weather.OUT     WE Daily weather output file                          OUTWTH
        if (line.contains(".OUT") || line.contains(".csv")) {
            QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                QString filename = parts[0].trimmed();
                QString baseFilename = QFileInfo(filename).baseName(); // Get filename without extension
                
                QString description;
                
                // Determine description start index based on whether there's a short CDE code
                int descStartIndex = 1;
                if (parts.size() >= 3 && parts[1].length() <= 3 && parts[1] == parts[1].toUpper()) {
                    // Second part looks like a CDE code (2-3 uppercase chars), skip it
                    descStartIndex = 2;
                }
                
                if (parts.size() > descStartIndex) {
                    QStringList descParts;
                    for (int i = descStartIndex; i < parts.size(); ++i) {
                        // Stop if we hit what looks like an alias at the end (short uppercase word)
                        if (i > descStartIndex + 2 && parts[i].length() <= 8 && parts[i] == parts[i].toUpper() && 
                            (parts[i].startsWith("OUT") || parts[i].startsWith("CSP_") || i == parts.size() - 1)) {
                            break;
                        }
                        descParts.append(parts[i]);
                    }
                    description = descParts.join(" ").trimmed();
                }
                
                if (!baseFilename.isEmpty() && !description.isEmpty()) {
                    outfileDescriptions[baseFilename] = description;
                    qDebug() << "DataProcessor::getOutfileDescriptions() - Loaded:" << baseFilename << "=" << description;
                }
            }
        }
    }
    
    file.close();
    loaded = true;
    
    qDebug() << "DataProcessor::getOutfileDescriptions() - Loaded" << outfileDescriptions.size() << "descriptions from" << outfilePath;
    return outfileDescriptions;
}

bool DataProcessor::readEvaluateFile(const QString &filePath, DataTable &table)
{
    // EVALUATE.OUT files are standard .OUT files, so use readOutFile
    return readOutFile(filePath, table);
}

QVector<QMap<QString, QString>> DataProcessor::getEvaluateVariablePairs(const DataTable &evaluateData)
{
    QVector<QMap<QString, QString>> pairs;
    
    // Metadata columns to exclude (EXCODE, TRT, CR, RN are metadata in EVALUATE.OUT)
    QStringList metadataColumns = {"RUN", "TRNO", "EXPNO", "EXPERIMENT", "TREATMENT", "TRTNO", "TRT", "EXP", 
                                   "EXCODE", "CR", "RN"};
    
    // Find all simulated variables (ending with 'S')
    QStringList simVariables;
    QStringList measVariables;
    
    for (const QString &colName : evaluateData.columnNames) {
        QString upperCol = colName.toUpper();
        
        // Skip metadata columns
        if (metadataColumns.contains(upperCol)) {
            continue;
        }
        
        // Check if it's a simulated variable (ends with 'S')
        if (upperCol.endsWith("S") && upperCol.length() > 1) {
            simVariables.append(colName);
        }
        // Check if it's a measured variable (ends with 'M')
        else if (upperCol.endsWith("M") && upperCol.length() > 1) {
            measVariables.append(colName);
        }
    }
    
    // Match simulated and measured variables
    for (const QString &simVar : simVariables) {
        QString baseName = simVar.left(simVar.length() - 1);  // Remove 'S' or 's'
        QString measVar = baseName + "M";  // Add 'M'
        
        // Check if corresponding measured variable exists
        bool found = false;
        for (const QString &mVar : measVariables) {
            if (mVar.compare(measVar, Qt::CaseInsensitive) == 0) {
                found = true;
                measVar = mVar;  // Use actual case from data
                break;
            }
        }
        
        if (!found) {
            continue;  // Skip if no matching measured variable
        }
        
        // Validate the pair: both must have data
        const DataColumn *simCol = evaluateData.getColumn(simVar);
        const DataColumn *measCol = evaluateData.getColumn(measVar);
        
        if (!simCol || !measCol) {
            continue;
        }
        
        // Check if both columns have valid data (not all missing)
        bool simHasData = false;
        bool measHasData = false;
        int validPairs = 0;
        bool allIdentical = true;
        double firstValue = 0.0;
        bool firstValueSet = false;
        
        for (int i = 0; i < evaluateData.rowCount; ++i) {
            QVariant simVal = simCol->data.value(i);
            QVariant measVal = measCol->data.value(i);
            
            if (!isMissingValue(simVal)) {
                simHasData = true;
            }
            if (!isMissingValue(measVal)) {
                measHasData = true;
            }
            
            // Check for valid overlapping pairs
            if (!isMissingValue(simVal) && !isMissingValue(measVal)) {
                validPairs++;
                double simDbl = toDouble(simVal);
                double measDbl = toDouble(measVal);
                
                if (!firstValueSet) {
                    firstValue = simDbl;
                    firstValueSet = true;
                } else if (qAbs(simDbl - firstValue) > 1e-6) {
                    allIdentical = false;
                }
            }
        }
        
        // Only add pair if both have data, have overlapping points, and aren't all identical
        if (simHasData && measHasData && validPairs > 0 && !allIdentical) {
            // Get display name from variable info
            QPair<QString, QString> varInfo = getVariableInfo(baseName);
            QString displayName = varInfo.first.isEmpty() ? baseName : varInfo.first;
            
            QMap<QString, QString> pair;
            pair["display_name"] = displayName;
            pair["sim_variable"] = simVar;
            pair["meas_variable"] = measVar;
            pairs.append(pair);
        }
    }
    
    return pairs;
}

QVector<QPair<QString, QString>> DataProcessor::getAllEvaluateVariables(const DataTable &evaluateData)
{
    QVector<QPair<QString, QString>> variables;
    
    // Metadata columns to exclude (EXCODE, TRT, CR, RN are metadata in EVALUATE.OUT)
    QStringList metadataColumns = {"RUN", "TRNO", "EXPNO", "EXPERIMENT", "TREATMENT", "TRTNO", "TRT", "EXP", 
                                   "EXCODE", "CR", "RN"};
    
    for (const QString &colName : evaluateData.columnNames) {
        QString upperCol = colName.toUpper();
        
        // Skip metadata columns
        if (metadataColumns.contains(upperCol)) {
            continue;
        }
        
        // Check if column has at least some data
        const DataColumn *col = evaluateData.getColumn(colName);
        if (!col) {
            continue;
        }
        
        bool hasData = false;
        for (int i = 0; i < evaluateData.rowCount; ++i) {
            QVariant val = col->data.value(i);
            if (!isMissingValue(val)) {
                hasData = true;
                break;
            }
        }
        
        if (hasData) {
            // Get human-readable label
            QPair<QString, QString> varInfo = getVariableInfo(colName);
            QString displayName = varInfo.first.isEmpty() ? colName : varInfo.first;
            
            variables.append(qMakePair(displayName, colName));
        }
    }
    
    return variables;
}