#include "SingleInstanceApp.h"
#include <QMessageBox>
#include <QDebug>
#include <QStandardPaths>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/locking.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif

SingleInstanceApp::SingleInstanceApp(int &argc, char **argv, const QString &appId)
    : QApplication(argc, argv)
    , m_appId(appId)
    , m_lockFile(nullptr)
    , m_isFirstInstance(true)
#ifdef Q_OS_WIN
    , m_lockHandle(nullptr)
#endif
{
    // Check if we're the first instance
    m_isFirstInstance = !lockInstance();
    
    if (!m_isFirstInstance) {
    } else {
    }
}

SingleInstanceApp::~SingleInstanceApp()
{
    cleanupLock();
}

bool SingleInstanceApp::lockInstance()
{
    // Create lock file in current directory, similar to Python example
    QString lockFileName = ".lock.instance.GB2";
    m_lockFilePath = QDir::currentPath() + "/" + lockFileName;
    
    
    try {
        m_lockFile = new QFile(m_lockFilePath);
        
        // Open the file for writing
        if (!m_lockFile->open(QIODevice::WriteOnly)) {
            qWarning() << "SingleInstanceApp: Could not open lock file:" << m_lockFile->errorString();
            delete m_lockFile;
            m_lockFile = nullptr;
            return false; // Assume no other instance, allow app to run
        }
        
#ifdef Q_OS_WIN
        // Windows implementation using _locking
        int fileHandle = m_lockFile->handle();
        if (fileHandle == -1) {
            qWarning() << "SingleInstanceApp: Could not get file handle";
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return false;
        }
        
        // Try to lock the file (non-blocking)
        if (_locking(fileHandle, _LK_NBLCK, 1) == 0) {
            // Successfully locked the file
            
            // Write PID to lock file
            QString pidString = QString::number(QCoreApplication::applicationPid());
            m_lockFile->write(pidString.toUtf8());
            m_lockFile->flush();
            
            return false; // No other instance is running
        } else {
            // Could not lock the file, another instance is running
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return true; // Another instance is running
        }
        
#else
        // Unix/Linux/Mac implementation using flock
        int fileHandle = m_lockFile->handle();
        if (fileHandle == -1) {
            qWarning() << "SingleInstanceApp: Could not get file handle";
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return false;
        }
        
        // Try to acquire an exclusive lock (non-blocking)
        if (flock(fileHandle, LOCK_EX | LOCK_NB) == 0) {
            // Successfully locked the file
            
            // Write PID to lock file
            QString pidString = QString::number(QCoreApplication::applicationPid());
            m_lockFile->write(pidString.toUtf8());
            m_lockFile->flush();
            
            return false; // No other instance is running
        } else {
            // Could not lock the file, another instance is running
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return true; // Another instance is running
        }
#endif
        
    } catch (const std::exception &e) {
        qCritical() << "SingleInstanceApp: Error creating/locking file:" << m_lockFilePath << "Error:" << e.what();
        // In case of error, allow the application to run
        return false;
    }
}

void SingleInstanceApp::cleanupLock()
{
    if (m_lockFile) {
        
#ifdef Q_OS_WIN
        // On Windows, the lock is automatically released when the file is closed
        m_lockFile->close();
#else
        // On Unix systems, we should explicitly unlock
        int fileHandle = m_lockFile->handle();
        if (fileHandle != -1) {
            flock(fileHandle, LOCK_UN);
        }
        m_lockFile->close();
#endif
        
        delete m_lockFile;
        m_lockFile = nullptr;
        
        // Try to remove the lock file
        if (QFile::exists(m_lockFilePath)) {
            if (QFile::remove(m_lockFilePath)) {
            } else {
                qWarning() << "SingleInstanceApp: Could not remove lock file:" << m_lockFilePath;
            }
        }
    }
}

void SingleInstanceApp::showAlreadyRunningMessage()
{
    QMessageBox::critical(
        nullptr,
        "ERROR",
        "GB2 is already opened.",
        QMessageBox::Ok
    );
}