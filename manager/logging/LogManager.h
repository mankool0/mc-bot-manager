#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QPointer>

class LogManager : public QObject
{
    Q_OBJECT

public:
    enum LogLevel { Debug, Info, Warning, Error, Success };

    static LogManager& instance() {
        static LogManager instance;
        return instance;
    }

    // Delete copy constructor and assignment operator
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    static void setManagerLogWidget(QPlainTextEdit *widget) {
        instance().managerLogWidget = widget;
    }
    static void setPrismLogWidget(QPlainTextEdit *widget) {
        instance().prismLogWidget = widget;
    }
    static void setAutoScroll(bool enabled) {
        instance().autoScroll = enabled;
    }

    static void log(const QString &message, LogLevel level = Info) {
        emit instance().logRequested(message, level);
    }
    static void logPrism(const QString &message) {
        emit instance().logPrismRequested(message);
    }
    static void clearManagerLog() {
        emit instance().clearManagerLogRequested();
    }
    static void clearPrismLog() {
        emit instance().clearPrismLogRequested();
    }

signals:
    void logRequested(const QString &message, LogLevel level);
    void logPrismRequested(const QString &message);
    void clearManagerLogRequested();
    void clearPrismLogRequested();

private:
    explicit LogManager(QObject *parent = nullptr);

    QPointer<QPlainTextEdit> managerLogWidget;
    QPointer<QPlainTextEdit> prismLogWidget;
    bool autoScroll = true;

    void logImpl(const QString &message, LogLevel level);
    void logPrismImpl(const QString &message);
    void clearManagerLogImpl();
    void clearPrismLogImpl();

    QString formatMessage(const QString &message, LogLevel level);
    QString formatPrismMessage(const QString &message);
};

#endif // LOGMANAGER_H
