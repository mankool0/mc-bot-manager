#ifndef LOGFILESINK_H
#define LOGFILESINK_H

#include <QFile>
#include <QTextStream>
#include <QDate>
#include <QMutex>
#include <QString>

class LogFileSink
{
public:
    LogFileSink() = default;
    ~LogFileSink();

    void open(const QString &baseDir, const QString &prefix);
    void close();
    void write(const QString &plainText);
    void setMaxSizeBytes(qint64 bytes);
    void setMaxFiles(int maxFiles);
    bool isOpen() const;

private:
    void openFile();
    void rolloverSize();
    void pruneOldFiles();
    QString buildPath() const;

    QFile m_file;
    QTextStream m_stream;
    qint64 m_maxSizeBytes = 10LL * 1024 * 1024;
    int m_maxFiles = 0;
    int m_rolloverIndex = 0;
    QString m_baseDir;
    QString m_prefix;
    QDate m_currentDate;
    mutable QMutex m_mutex;
};

#endif // LOGFILESINK_H
