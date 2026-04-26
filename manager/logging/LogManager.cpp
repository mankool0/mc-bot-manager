#include "LogManager.h"
#include "ui/AppColors.h"

LogManager::LogManager(QObject *parent)
    : QObject(parent)
{
    // Connect signals to slots for thread-safe GUI updates
    connect(this, &LogManager::logRequested, this, &LogManager::logImpl, Qt::QueuedConnection);
    connect(this, &LogManager::logPrismRequested, this, &LogManager::logPrismImpl, Qt::QueuedConnection);
    connect(this, &LogManager::clearManagerLogRequested, this, &LogManager::clearManagerLogImpl, Qt::QueuedConnection);
    connect(this, &LogManager::clearPrismLogRequested, this, &LogManager::clearPrismLogImpl, Qt::QueuedConnection);
}

void LogManager::logImpl(const QString &message, LogLevel level)
{
    if (managerLogWidget) {
        QString formattedMessage = formatMessage(message, level);
        managerLogWidget->appendHtml(formattedMessage);

        if (autoScroll) {
            managerLogWidget->moveCursor(QTextCursor::End);
        }
    }

    if (m_fileSink)
        m_fileSink->write(formatPlainText(message, level));
}

void LogManager::logPrismImpl(const QString &message)
{
    if (!prismLogWidget) return;

    QString formattedMessage = formatPrismMessage(message);
    prismLogWidget->appendHtml(formattedMessage);

    if (autoScroll) {
        prismLogWidget->moveCursor(QTextCursor::End);
    }
}

void LogManager::clearManagerLogImpl()
{
    if (managerLogWidget) {
        managerLogWidget->clear();
        logImpl("Manager log cleared", Info);
    }
}

void LogManager::clearPrismLogImpl()
{
    if (prismLogWidget) {
        prismLogWidget->clear();
        logPrismImpl("Prism log cleared");
    }
}

QString LogManager::formatMessage(const QString &message, LogLevel level)
{
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss]");

    QString color;
    QString prefix;
    switch (level) {
    case Debug:
        color = AppColors::logDebug().name();
        prefix = "DEBUG";
        break;
    case Info:
        color = AppColors::logInfo().name();
        prefix = "INFO";
        break;
    case Warning:
        color = AppColors::logWarning().name();
        prefix = "WARN";
        break;
    case Error:
        color = AppColors::logError().name();
        prefix = "ERROR";
        break;
    case Success:
        color = AppColors::logSuccess().name();
        prefix = "OK";
        break;
    }

    return QString("<span style='color: %1'>%2</span> "
                   "<span style='color: %3; font-weight: bold'>[%4]</span> %5")
        .arg(AppColors::logTimestamp().name(), timestamp, color, prefix, message);
}

QString LogManager::formatPlainText(const QString &message, LogLevel level)
{
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss]");

    QString prefix;
    switch (level) {
    case Debug:   prefix = "DEBUG"; break;
    case Info:    prefix = "INFO";  break;
    case Warning: prefix = "WARN";  break;
    case Error:   prefix = "ERROR"; break;
    case Success: prefix = "OK";    break;
    }

    return QString("%1 [%2] %3").arg(timestamp, prefix, message);
}

QString LogManager::formatPrismMessage(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss]");
    return QString("<span style='color: %1'>%2</span> %3").arg(AppColors::logTimestamp().name(), timestamp, message);
}
