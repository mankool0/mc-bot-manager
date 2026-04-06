#include "LogFileSink.h"
#include <QDir>
#include <QDateTime>

LogFileSink::~LogFileSink()
{
    close();
}

void LogFileSink::open(const QString &baseDir, const QString &prefix)
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen())
        m_file.close();

    m_baseDir = baseDir;
    m_prefix = prefix;
    m_currentDate = QDate::currentDate();
    m_rolloverIndex = 0;

    QDir dir;
    dir.mkpath(baseDir);

    openFile();

    if (m_file.isOpen()) {
        QString marker = QString("=== Session started %1 ===")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        m_stream << marker << "\n";
        m_stream.flush();
    }
}

void LogFileSink::close()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }
}

void LogFileSink::write(const QString &plainText)
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen())
        return;

    QDate today = QDate::currentDate();
    if (today != m_currentDate) {
        m_stream.flush();
        m_file.close();
        m_currentDate = today;
        m_rolloverIndex = 0;
        openFile();
        if (!m_file.isOpen())
            return;
    }

    if (m_file.size() > m_maxSizeBytes)
        rolloverSize();

    if (!m_file.isOpen())
        return;

    m_stream << plainText << "\n";
    m_stream.flush();
}

void LogFileSink::setMaxSizeBytes(qint64 bytes)
{
    QMutexLocker locker(&m_mutex);
    m_maxSizeBytes = bytes;
}

void LogFileSink::setMaxFiles(int maxFiles)
{
    QMutexLocker locker(&m_mutex);
    m_maxFiles = maxFiles;
}

bool LogFileSink::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_file.isOpen();
}

void LogFileSink::openFile()
{
    // Called with mutex held
    QString path = buildPath();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::Append | QIODevice::Text))
        return;
    m_stream.setDevice(&m_file);
}

void LogFileSink::rolloverSize()
{
    // Called with mutex held
    m_stream.flush();
    m_file.close();

    QString dateStr = m_currentDate.toString("yyyy-MM-dd");
    QString basePath = m_baseDir + "/" + m_prefix + "-" + dateStr;

    // Find next available rollover index
    m_rolloverIndex++;
    while (QFile::exists(QString("%1-%2.log").arg(basePath).arg(m_rolloverIndex)))
        m_rolloverIndex++;

    QString renameTo = QString("%1-%2.log").arg(basePath).arg(m_rolloverIndex);
    QFile::rename(basePath + ".log", renameTo);

    // Reset index to 0 so the next file gets the base name again
    m_rolloverIndex = 0;
    openFile();
    pruneOldFiles();
}

void LogFileSink::pruneOldFiles()
{
    // Called with mutex held
    if (m_maxFiles <= 0)
        return;

    QDir dir(m_baseDir);
    QFileInfoList files = dir.entryInfoList({m_prefix + "-*.log"}, QDir::Files, QDir::Time | QDir::Reversed);

    // files is sorted oldest-first; delete from the front until we're within the limit
    while (files.size() > m_maxFiles) {
        QFile::remove(files.first().absoluteFilePath());
        files.removeFirst();
    }
}

QString LogFileSink::buildPath() const
{
    // Called with mutex held
    QString dateStr = m_currentDate.toString("yyyy-MM-dd");
    if (m_rolloverIndex == 0)
        return m_baseDir + "/" + m_prefix + "-" + dateStr + ".log";
    return m_baseDir + "/" + m_prefix + "-" + dateStr + "-" + QString::number(m_rolloverIndex) + ".log";
}
