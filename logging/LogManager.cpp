#include "LogManager.h"

LogManager::LogManager(QObject *parent)
    : QObject(parent)
{
}

void LogManager::logImpl(const QString &message, LogLevel level)
{
    if (!managerLogWidget) return;

    QString formattedMessage = formatMessage(message, level);
    managerLogWidget->appendHtml(formattedMessage);

    if (autoScroll) {
        managerLogWidget->moveCursor(QTextCursor::End);
    }
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
        color = "gray";
        prefix = "DEBUG";
        break;
    case Info:
        color = "black";
        prefix = "INFO";
        break;
    case Warning:
        color = "#FF8C00"; // Dark orange
        prefix = "WARN";
        break;
    case Error:
        color = "#DC143C"; // Crimson
        prefix = "ERROR";
        break;
    case Success:
        color = "#228B22"; // Forest green
        prefix = "OK";
        break;
    }

    return QString("<span style='color: gray'>%1</span> "
                   "<span style='color: %2; font-weight: bold'>[%3]</span> %4")
        .arg(timestamp, color, prefix, message);
}

QString LogManager::formatPrismMessage(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss]");
    return QString("<span style='color: gray'>%1</span> %2").arg(timestamp, message);
}
