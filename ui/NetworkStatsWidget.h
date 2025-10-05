#ifndef NETWORKSTATSWIDGET_H
#define NETWORKSTATSWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QDateTime>
#include <QMap>

class NetworkStatsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkStatsWidget(QWidget *parent = nullptr);
    ~NetworkStatsWidget();

    void updateStats();

private:
    QTableWidget *statsTable;
    QLabel *totalStatsLabel;
    QTimer *updateTimer;
    QDateTime startTime;

    qint64 totalBytesReceived = 0;
    qint64 totalBytesSent = 0;

    struct BotRateData {
        qint64 lastBytesReceived = 0;
        qint64 lastBytesSent = 0;
    };
    QMap<QString, BotRateData> botRateData;
    QDateTime lastUpdateTime;

    QString formatBytes(qint64 bytes);
    QString formatRate(double bytesPerSec);
};

#endif // NETWORKSTATSWIDGET_H
