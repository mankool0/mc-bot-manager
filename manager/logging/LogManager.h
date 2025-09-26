#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QTextCursor>

class LogManager : public QObject
{
    Q_OBJECT

public:
    enum LogLevel {
        Info,
        Warning,
        Error,
        Success
    };

    explicit LogManager(QObject *parent = nullptr);

    void setManagerLogWidget(QPlainTextEdit *widget);
    void setPrismLogWidget(QPlainTextEdit *widget);
    void setAutoScroll(bool enabled);

    void log(const QString &message, LogLevel level = Info);
    void logPrism(const QString &message);
    void clearManagerLog();
    void clearPrismLog();

private:
    QPlainTextEdit *managerLogWidget = nullptr;
    QPlainTextEdit *prismLogWidget = nullptr;
    bool autoScroll = true;

    QString formatMessage(const QString &message, LogLevel level);
    QString formatPrismMessage(const QString &message);
};

#endif // LOGMANAGER_H