#include "NetworkStatsWidget.h"
#include "bot/BotManager.h"
#include <QHeaderView>
#include <QTime>

NetworkStatsWidget::NetworkStatsWidget(QWidget *parent)
    : QWidget(parent)
    , startTime(QDateTime::currentDateTime())
    , lastUpdateTime(QDateTime::currentDateTime())
{
    setWindowTitle("Network Statistics");
    resize(460, 240);

    QVBoxLayout *layout = new QVBoxLayout(this);

    totalStatsLabel = new QLabel(this);
    layout->addWidget(totalStatsLabel);

    // Stats table
    statsTable = new QTableWidget(this);
    statsTable->setColumnCount(6);
    statsTable->setHorizontalHeaderLabels({
        "Bot Name",
        "Status",
        "Received",
        "Sent",
        "Rate In",
        "Rate Out"
    });
    statsTable->horizontalHeader()->setStretchLastSection(true);
    statsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    statsTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    statsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    statsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(statsTable);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &NetworkStatsWidget::updateStats);
    updateTimer->start(1000);

    updateStats();
}

NetworkStatsWidget::~NetworkStatsWidget()
{
}

void NetworkStatsWidget::updateStats()
{
    QVector<BotInstance> &bots = BotManager::getBots();
    statsTable->setRowCount(bots.size());

    QDateTime now = QDateTime::currentDateTime();
    double elapsed = lastUpdateTime.msecsTo(now) / 1000.0; // seconds
    lastUpdateTime = now;

    totalBytesReceived = 0;
    totalBytesSent = 0;

    for (int i = 0; i < bots.size(); ++i) {
        const BotInstance &bot = bots[i];

        BotRateData &rateData = botRateData[bot.name];

        double rateIn = 0.0;
        double rateOut = 0.0;
        if (elapsed > 0) {
            rateIn = (bot.bytesReceived - rateData.lastBytesReceived) / elapsed;
            rateOut = (bot.bytesSent - rateData.lastBytesSent) / elapsed;
        }

        rateData.lastBytesReceived = bot.bytesReceived;
        rateData.lastBytesSent = bot.bytesSent;

        QTableWidgetItem *nameItem = statsTable->item(i, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            statsTable->setItem(i, 0, nameItem);
        }
        nameItem->setText(bot.name);

        QTableWidgetItem *statusItem = statsTable->item(i, 1);
        if (!statusItem) {
            statusItem = new QTableWidgetItem();
            statsTable->setItem(i, 1, statusItem);
        }
        QString statusText = bot.status == BotStatus::Online ? "Online" : "Offline";
        statusItem->setText(statusText);
        statusItem->setForeground(bot.status == BotStatus::Online ? QColor(0x4CAF50) : QColor(0x9E9E9E));

        QTableWidgetItem *receivedItem = statsTable->item(i, 2);
        if (!receivedItem) {
            receivedItem = new QTableWidgetItem();
            statsTable->setItem(i, 2, receivedItem);
        }
        receivedItem->setText(formatBytes(bot.bytesReceived));

        QTableWidgetItem *sentItem = statsTable->item(i, 3);
        if (!sentItem) {
            sentItem = new QTableWidgetItem();
            statsTable->setItem(i, 3, sentItem);
        }
        sentItem->setText(formatBytes(bot.bytesSent));

        QTableWidgetItem *rateInItem = statsTable->item(i, 4);
        if (!rateInItem) {
            rateInItem = new QTableWidgetItem();
            statsTable->setItem(i, 4, rateInItem);
        }
        rateInItem->setText(formatRate(rateIn));

        QTableWidgetItem *rateOutItem = statsTable->item(i, 5);
        if (!rateOutItem) {
            rateOutItem = new QTableWidgetItem();
            statsTable->setItem(i, 5, rateOutItem);
        }
        rateOutItem->setText(formatRate(rateOut));

        totalBytesReceived += bot.bytesReceived;
        totalBytesSent += bot.bytesSent;
    }

    qint64 uptime = startTime.secsTo(QDateTime::currentDateTime());
    QString uptimeStr = QTime(0, 0).addSecs(uptime).toString("hh:mm:ss");

    totalStatsLabel->setText(QString(
        "Uptime: %1 | Total Received: %2 | Total Sent: %3 | Combined: %4"
    ).arg(uptimeStr)
     .arg(formatBytes(totalBytesReceived))
     .arg(formatBytes(totalBytesSent))
     .arg(formatBytes(totalBytesReceived + totalBytesSent)));
}

QString NetworkStatsWidget::formatBytes(qint64 bytes)
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

QString NetworkStatsWidget::formatRate(double bytesPerSec)
{
    if (bytesPerSec < 1024) {
        return QString("%1 B/s").arg(bytesPerSec, 0, 'f', 0);
    } else if (bytesPerSec < 1024 * 1024) {
        return QString("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 2);
    } else {
        return QString("%1 MB/s").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 2);
    }
}
