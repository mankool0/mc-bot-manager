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
        instance().logImpl(message, level);
    }
    static void logPrism(const QString &message) {
        instance().logPrismImpl(message);
    }
    static void clearManagerLog() {
        instance().clearManagerLogImpl();
    }
    static void clearPrismLog() {
        instance().clearPrismLogImpl();
    }

private:
    explicit LogManager(QObject *parent = nullptr);

    QPlainTextEdit *managerLogWidget = nullptr;
    QPlainTextEdit *prismLogWidget = nullptr;
    bool autoScroll = true;

    void logImpl(const QString &message, LogLevel level);
    void logPrismImpl(const QString &message);
    void clearManagerLogImpl();
    void clearPrismLogImpl();

    QString formatMessage(const QString &message, LogLevel level);
    QString formatPrismMessage(const QString &message);
};

#endif // LOGMANAGER_H
