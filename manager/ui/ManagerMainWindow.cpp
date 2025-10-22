#include "ManagerMainWindow.h"
#include "BotConsoleWidget.h"
#include "MeteorModulesWidget.h"
#include "BaritoneWidget.h"
#include "PrismSettingsDialog.h"
#include "NetworkStatsWidget.h"
#include "logging/LogManager.h"
#include "prism/PrismLauncherManager.h"
#include "network/PipeServer.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QProcess>
#include <QDateTime>
#include <QTextCursor>
#include <QRegularExpression>
#include <QActionGroup>

ManagerMainWindow::ManagerMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ManagerMainWindow)
    , loadingConfiguration(false)
    , detailsPinned(false)
{
    ui->setupUi(this);

    LogManager::setManagerLogWidget(ui->managerLogTextEdit);
    LogManager::setPrismLogWidget(ui->prismLogTextEdit);

    PrismLauncherManager::setPrismConfig(&prismConfig);

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::minecraftLaunching,
            this,
            [this](const QString &botName) {
                QVector<BotInstance> &bots = BotManager::getBots();
                for (BotInstance &bot : bots) {
                    if (bot.status == BotStatus::Starting) {
                        LogManager::log(QString("Minecraft launching for bot '%1'").arg(bot.name), LogManager::Success);
                        break;
                    }
                }
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::minecraftStarting,
            this,
            [this](const QString &botName) {
                // Minecraft process started - bot remains in Starting status
                // until client connection is established
                LogManager::log("Minecraft process detected starting", LogManager::Info);
            });

    setupUI();
    loadSettings();
}

ManagerMainWindow::~ManagerMainWindow()
{
    PipeServer::stop();
    delete ui;
}

void ManagerMainWindow::closeEvent(QCloseEvent *event)
{
    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (bot.status == BotStatus::Online) {
            bot.manualStop = true;
            BotManager::sendShutdownCommand(bot.name, "Manager closing");
        }
    }

    QMainWindow::closeEvent(event);
}

void ManagerMainWindow::setupUI()
{
    connect(ui->instancesTableWidget, &QWidget::customContextMenuRequested,
            this, &ManagerMainWindow::showInstancesContextMenu);
    connect(ui->instancesTableWidget, &QTableWidget::itemSelectionChanged,
            this, &ManagerMainWindow::onInstanceSelectionChanged);

    connect(ui->launchBotButton, &QPushButton::clicked, this, &ManagerMainWindow::launchBot);
    connect(ui->stopBotButton, &QPushButton::clicked, this, &ManagerMainWindow::stopBot);
    connect(ui->restartBotButton, &QPushButton::clicked, this, &ManagerMainWindow::restartBot);

    connect(ui->actionPrismSettings, &QAction::triggered, this, &ManagerMainWindow::configurePrismLauncher);
    connect(ui->actionNetworkStats, &QAction::toggled, this, &ManagerMainWindow::showNetworkStats);
    connect(ui->actionSave, &QAction::triggered, this, &ManagerMainWindow::saveSettings);
    connect(ui->actionOpen, &QAction::triggered, this, &ManagerMainWindow::loadSettingsFromFile);
    connect(ui->actionLaunchAll, &QAction::triggered, this, &ManagerMainWindow::launchAllBots);
    connect(ui->actionStopAll, &QAction::triggered, this, &ManagerMainWindow::stopAllBots);
    connect(ui->actionAbout, &QAction::triggered, this, &ManagerMainWindow::showAboutDialog);

    connect(ui->instanceComboBox, &QComboBox::currentTextChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->accountComboBox, &QComboBox::currentTextChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->serverLineEdit, &QLineEdit::textChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->memorySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->restartThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->autoRestartCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->tokenRefreshCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->debugModeCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);

    ui->detailsStackedWidget->setCurrentIndex(0);
    ui->detailsStackedWidget->hide();

    QPushButton *pinButton = new QPushButton(this);
    pinButton->setCheckable(true);
    pinButton->setIcon(
        QIcon::fromTheme("window-pin", style()->standardIcon(QStyle::SP_TitleBarShadeButton)));
    pinButton->setToolTip("Pin details panel");
    pinButton->setMaximumWidth(30);
    ui->mainTabWidget->setCornerWidget(pinButton, Qt::TopRightCorner);
    connect(pinButton, &QPushButton::toggled, this, &ManagerMainWindow::onPinDetailsToggled);

    connect(ui->clearLogButton, &QPushButton::clicked, this, &ManagerMainWindow::onClearLog);
    connect(ui->autoScrollCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onAutoScrollToggled);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    NetworkStatsWidget *statsWidget = new NetworkStatsWidget(this);
    networkStatsDock = new QDockWidget("Network Statistics", this);
    networkStatsDock->setWidget(statsWidget);
    networkStatsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    networkStatsDock->setObjectName("networkStatsDock");
    addDockWidget(Qt::RightDockWidgetArea, networkStatsDock);
    networkStatsDock->hide();

    loadPrismLauncherConfig();

    // Connect BotManager signals for reactive UI updates
    connect(&BotManager::instance(), &BotManager::botAdded,
            this, [this](const QString &) { updateInstancesTable(); });
    connect(&BotManager::instance(), &BotManager::botRemoved,
            this, [this](const QString &) { updateInstancesTable(); });
    connect(&BotManager::instance(), &BotManager::botUpdated,
            this, [this](const QString &) { updateInstancesTable(); });
    connect(&BotManager::instance(), &BotManager::meteorModulesReceived,
            this, &ManagerMainWindow::onMeteorModulesReceived);
    connect(&BotManager::instance(), &BotManager::meteorSingleModuleUpdated,
            this, &ManagerMainWindow::onMeteorSingleModuleUpdated);
    connect(&BotManager::instance(), &BotManager::baritoneSettingsReceived,
            this, &ManagerMainWindow::onBaritoneSettingsReceived);
    connect(&BotManager::instance(), &BotManager::baritoneCommandsReceived,
            this, &ManagerMainWindow::onBaritoneCommandsReceived);
    connect(&BotManager::instance(), &BotManager::baritoneSingleSettingUpdated,
            this, &ManagerMainWindow::onBaritoneSingleSettingUpdated);

    launchSchedulerTimer = new QTimer(this);
    connect(launchSchedulerTimer, &QTimer::timeout, this, &ManagerMainWindow::checkScheduledLaunches);

    uptimeCheckTimer = new QTimer(this);
    connect(uptimeCheckTimer, &QTimer::timeout, this, &ManagerMainWindow::checkBotUptimes);
    uptimeCheckTimer->start(60000);

    setupPipeServer();

    LogManager::log("MC Bot Manager started", LogManager::Info);
}

void ManagerMainWindow::showInstancesContextMenu(const QPoint &pos)
{
    QMenu contextMenu(this);

    bool botAtPos = false;
    QTableWidgetItem *item = ui->instancesTableWidget->itemAt(pos);
    if (item) {
        int row = item->row();
        QVector<BotInstance> &bots = BotManager::getBots();
        if (row >= 0 && row < bots.size()) {
            botAtPos = true;
            const BotInstance &bot = bots[row];
            bool isOnline = (bot.status == BotStatus::Online);
            bool isOffline = (bot.status == BotStatus::Offline);
            bool inLaunchQueue = std::any_of(scheduledLaunches.begin(), scheduledLaunches.end(),
                                              [&bot](const ScheduledLaunch &s) { return s.botName == bot.name; });

            contextMenu.addSeparator();

            if (isOffline && !inLaunchQueue) {
                QAction *launchAction = contextMenu.addAction("Launch Bot");
                connect(launchAction, &QAction::triggered, this, &ManagerMainWindow::launchBot);
            } else if (isOnline) {
                QAction *stopAction = contextMenu.addAction("Stop Bot");
                connect(stopAction, &QAction::triggered, this, &ManagerMainWindow::stopBot);

                QAction *restartAction = contextMenu.addAction("Restart Bot");
                connect(restartAction, &QAction::triggered, this, &ManagerMainWindow::restartBot);
            }
        }
    }

    QAction *addBotAction = contextMenu.addAction("Add New Bot");
    connect(addBotAction, &QAction::triggered, this, &ManagerMainWindow::addNewBot);
    if (botAtPos) {
        contextMenu.addSeparator();
        QAction *removeBotAction = contextMenu.addAction("Remove Bot");
        connect(removeBotAction, &QAction::triggered, this, &ManagerMainWindow::removeBot);
    }

    contextMenu.exec(ui->instancesTableWidget->mapToGlobal(pos));
}

void ManagerMainWindow::addNewBot()
{
    bool ok;
    QString botName = QInputDialog::getText(this, "Add New Bot",
                                           "Bot name:", QLineEdit::Normal,
                                           QString("NewBot_%1").arg(BotManager::getBots().size() + 1), &ok);

    if (ok && !botName.isEmpty()) {
        BotInstance newBot;
        newBot.name = botName;
        newBot.status = BotStatus::Offline;

        newBot.instance = "";
        newBot.account = "";
        newBot.server = "";
        newBot.connectionId = -1;
        newBot.maxMemory = 4096;
        newBot.currentMemory = 0;
        newBot.restartThreshold = 48;
        newBot.autoRestart = true;
        newBot.tokenRefresh = true;
        newBot.debugLogging = false;
        newBot.position = QVector3D(0, 0, 0);
        newBot.dimension = "";

        BotManager::addBot(newBot);

        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot) {
            if (!bot->consoleWidget) {
                bot->consoleWidget = new BotConsoleWidget(this);
                connect(bot->consoleWidget, &BotConsoleWidget::commandEntered,
                        this, &ManagerMainWindow::onConsoleCommandEntered);
                bot->consoleWidget->hide();

                QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->consoleTab->layout());
                if (layout) {
                    layout->addWidget(bot->consoleWidget);
                }
            }

            if (!bot->meteorWidget) {
                bot->meteorWidget = new MeteorModulesWidget(this);
                connect(bot->meteorWidget, &MeteorModulesWidget::moduleToggled,
                        this, &ManagerMainWindow::onMeteorModuleToggled);
                connect(bot->meteorWidget, &MeteorModulesWidget::settingChanged,
                        this, &ManagerMainWindow::onMeteorSettingChanged);
                bot->meteorWidget->hide();

                QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->meteorTab->layout());
                if (layout) {
                    layout->addWidget(bot->meteorWidget);
                }
            }

            if (!bot->baritoneWidget) {
                bot->baritoneWidget = new BaritoneWidget(this);
                connect(bot->baritoneWidget, &BaritoneWidget::settingChanged,
                        this, &ManagerMainWindow::onBaritoneSettingChanged);
                bot->baritoneWidget->hide();

                QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->baritoneTab->layout());
                if (layout) {
                    layout->addWidget(bot->baritoneWidget);
                }
            }
        }

        updateInstancesTable();

        LogManager::log(QString("Added new bot '%1'").arg(botName), LogManager::Success);
    }
}

void ManagerMainWindow::removeBot()
{
    QList<QTableWidgetItem*> selectedItems = ui->instancesTableWidget->selectedItems();
    if (selectedItems.isEmpty()) return;

    int row = selectedItems[0]->row();
    QVector<BotInstance> &bots = BotManager::getBots();
    if (row >= 0 && row < bots.size()) {
        QString botName = bots[row].name;

        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "Remove Bot",
            QString("Are you sure you want to remove bot '%1'?").arg(botName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            BotManager::removeBot(botName);
            updateInstancesTable();

            if (selectedBotName == botName) {
                selectedBotName.clear();
                ui->detailsStackedWidget->setCurrentIndex(0);
                ui->detailsStackedWidget->hide();
            }

            LogManager::log(QString("Removed bot '%1'").arg(botName), LogManager::Success);
        }
    }
}

void ManagerMainWindow::updateInstancesTable()
{
    QVector<BotInstance> &bots = BotManager::getBots();
    ui->instancesTableWidget->setRowCount(bots.size());

    for (int i = 0; i < bots.size(); ++i) {
        const BotInstance &bot = bots[i];

        // Name column (0)
        QTableWidgetItem *nameItem = ui->instancesTableWidget->item(i, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 0, nameItem);
        }
        nameItem->setText(bot.name);

        // Status column (1)
        QTableWidgetItem *statusItem = ui->instancesTableWidget->item(i, 1);
        if (!statusItem) {
            statusItem = new QTableWidgetItem();
            statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 1, statusItem);
        }
        statusItem->setText(QString("● %1").arg(statusToString(bot.status)));

        QColor statusColor;
        if (bot.status == BotStatus::Online) {
            statusColor = QColor(0x4CAF50);
        } else if (bot.status == BotStatus::Offline) {
            statusColor = QColor(0x9E9E9E);
        } else {
            statusColor = QColor(0xFF9800);
        }
        if (statusItem->foreground().color() != statusColor) {
            statusItem->setForeground(statusColor);
        }

        // Instance column (2)
        QTableWidgetItem *instanceItem = ui->instancesTableWidget->item(i, 2);
        if (!instanceItem) {
            instanceItem = new QTableWidgetItem();
            instanceItem->setFlags(instanceItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 2, instanceItem);
        }
        instanceItem->setText(bot.instance);

        // Server column (3)
        QTableWidgetItem *serverItem = ui->instancesTableWidget->item(i, 3);
        if (!serverItem) {
            serverItem = new QTableWidgetItem();
            serverItem->setFlags(serverItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 3, serverItem);
        }
        serverItem->setText(bot.server);

        // Memory column (4)
        QTableWidgetItem *memoryItem = ui->instancesTableWidget->item(i, 4);
        if (!memoryItem) {
            memoryItem = new QTableWidgetItem();
            memoryItem->setFlags(memoryItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 4, memoryItem);
        }
        QString memoryText = bot.status == BotStatus::Online
            ? QString("%1/%2 MB").arg(bot.currentMemory).arg(bot.maxMemory)
            : QString("-/%1 MB").arg(bot.maxMemory);
        memoryItem->setText(memoryText);

        // Position column (5)
        QTableWidgetItem *positionItem = ui->instancesTableWidget->item(i, 5);
        if (!positionItem) {
            positionItem = new QTableWidgetItem();
            positionItem->setFlags(positionItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 5, positionItem);
        }
        QString posText = bot.status == BotStatus::Online
            ? QString("%1, %2, %3")
                .arg(bot.position.x(), 0, 'f', 1)
                .arg(bot.position.y(), 0, 'f', 1)
                .arg(bot.position.z(), 0, 'f', 1)
            : "-";
        positionItem->setText(posText);

        // Dimension column (6)
        QTableWidgetItem *dimensionItem = ui->instancesTableWidget->item(i, 6);
        if (!dimensionItem) {
            dimensionItem = new QTableWidgetItem();
            dimensionItem->setFlags(dimensionItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 6, dimensionItem);
        }
        dimensionItem->setText(bot.dimension.isEmpty() ? "-" : bot.dimension);

        // PID column (7)
        QTableWidgetItem *pidItem = ui->instancesTableWidget->item(i, 7);
        if (!pidItem) {
            pidItem = new QTableWidgetItem();
            pidItem->setFlags(pidItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 7, pidItem);
        }
        QString pidText = bot.minecraftPid > 0 ? QString::number(bot.minecraftPid) : "-";
        pidItem->setText(pidText);
    }

    ui->instancesTableWidget->resizeColumnsToContents();

    // Update status display if the selected bot's state changed
    if (!selectedBotName.isEmpty()) {
        updateStatusDisplay();
    }
}

void ManagerMainWindow::onInstanceSelectionChanged()
{
    QList<QTableWidgetItem*> selectedItems = ui->instancesTableWidget->selectedItems();

    if (selectedItems.isEmpty() && detailsPinned && !selectedBotName.isEmpty()) {
        BotInstance *pinnedBot = BotManager::getBotByName(selectedBotName);
        if (pinnedBot) {
            QVector<BotInstance> &bots = BotManager::getBots();
            for (int i = 0; i < bots.size(); ++i) {
                if (bots[i].name == selectedBotName) {
                    ui->instancesTableWidget->selectRow(i);
                    return;
                }
            }
        }
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (bot.consoleWidget) {
            bot.consoleWidget->hide();
        }
        if (bot.meteorWidget) {
            bot.meteorWidget->hide();
        }
        if (bot.baritoneWidget) {
            bot.baritoneWidget->hide();
        }
    }

    if (selectedItems.isEmpty()) {
        if (!detailsPinned) {
            selectedBotName.clear();
            ui->detailsStackedWidget->setCurrentIndex(0);
            ui->detailsStackedWidget->hide();
        }
        return;
    }

    int row = selectedItems[0]->row();
    if (row >= 0 && row < bots.size()) {
        BotInstance &bot = bots[row];
        selectedBotName = bot.name;
        loadBotConfiguration(bot);
        ui->detailsStackedWidget->setCurrentIndex(1);
        ui->detailsStackedWidget->show();
        updateStatusDisplay();

        if (bot.consoleWidget) {
            bot.consoleWidget->show();
        }
        if (bot.meteorWidget) {
            bot.meteorWidget->show();
        }
        if (bot.baritoneWidget) {
            bot.baritoneWidget->show();
        }
    }
}

void ManagerMainWindow::onConfigurationChanged()
{
    if (loadingConfiguration) return;

    if (!selectedBotName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(selectedBotName);
        if (bot) {
            bot->instance = ui->instanceComboBox->currentText() == "(None)" ? "" : ui->instanceComboBox->currentText();
            bot->account = ui->accountComboBox->currentText() == "(None)" ? "" : ui->accountComboBox->currentText();
            bot->server = ui->serverLineEdit->text();
            bot->maxMemory = ui->memorySpinBox->value();
            bot->restartThreshold = ui->restartThresholdSpinBox->value();
            bot->autoRestart = ui->autoRestartCheckBox->isChecked();
            bot->tokenRefresh = ui->tokenRefreshCheckBox->isChecked();
            bot->debugLogging = ui->debugModeCheckBox->isChecked();
            updateInstancesTable();
        }
    }
}


void ManagerMainWindow::loadBotConfiguration(const BotInstance &bot)
{
    loadingConfiguration = true;

    QString currentInstance = bot.instance;
    QString currentAccount = bot.account;

    updateInstanceComboBox();
    updateAccountComboBox();

    if (currentInstance.isEmpty()) {
        ui->instanceComboBox->setCurrentText("(None)");
    } else {
        int index = ui->instanceComboBox->findText(currentInstance);
        if (index != -1) {
            ui->instanceComboBox->setCurrentIndex(index);
        } else {
            ui->instanceComboBox->setCurrentText("(None)");
        }
    }

    if (currentAccount.isEmpty()) {
        ui->accountComboBox->setCurrentText("(None)");
    } else {
        int index = ui->accountComboBox->findText(currentAccount);
        if (index != -1) {
            ui->accountComboBox->setCurrentIndex(index);
        } else {
            ui->accountComboBox->setCurrentText("(None)");
        }
    }
    ui->serverLineEdit->setText(bot.server);
    ui->pipeStatusLabel->setText(QString("Connection %1 [%2]").arg(bot.connectionId).arg(bot.status == BotStatus::Online ? "Connected" : "Not Connected"));
    ui->memorySpinBox->setValue(bot.maxMemory);
    ui->restartThresholdSpinBox->setValue(bot.restartThreshold);
    ui->autoRestartCheckBox->setChecked(bot.autoRestart);
    ui->tokenRefreshCheckBox->setChecked(bot.tokenRefresh);
    ui->debugModeCheckBox->setChecked(bot.debugLogging);

    // Clear flag to allow user changes to sync to memory
    loadingConfiguration = false;
}

void ManagerMainWindow::updateStatusDisplay()
{
    if (selectedBotName.isEmpty()) {
        ui->detailsStackedWidget->setCurrentIndex(0);
        ui->detailsStackedWidget->hide();
        return;
    }

    BotInstance *selectedBot = BotManager::getBotByName(selectedBotName);
    if (selectedBot) {
        ui->pipeStatusLabel->setText(QString("Connection %1 [%2]")
            .arg(selectedBot->connectionId)
            .arg(selectedBot->status == BotStatus::Online ? "Connected" : "Not Connected"));

        bool isOnline = (selectedBot->status == BotStatus::Online);
        bool isStarting = (selectedBot->status == BotStatus::Starting);
        bool isOffline = (selectedBot->status == BotStatus::Offline);
        bool inLaunchQueue = std::any_of(scheduledLaunches.begin(), scheduledLaunches.end(),
                                          [this](const ScheduledLaunch &s) { return s.botName == selectedBotName; });

        ui->launchBotButton->setEnabled(isOffline && !inLaunchQueue);
        ui->stopBotButton->setEnabled(isOnline);
        ui->restartBotButton->setEnabled(isOnline);
        ui->instanceComboBox->setEnabled(!isOnline);
        ui->accountComboBox->setEnabled(!isOnline);
        ui->serverLineEdit->setEnabled(!isOnline);
        ui->memorySpinBox->setEnabled(!isOnline);
    }
}

void ManagerMainWindow::launchBot()
{
    if (selectedBotName.isEmpty()) return;
    launchBotByName(selectedBotName);
}

bool ManagerMainWindow::launchBotByName(const QString &botName)
{
    BotInstance *botToLaunch = BotManager::getBotByName(botName);

    if (!botToLaunch) {
        LogManager::log(QString("Bot '%1' not found").arg(botName), LogManager::Error);
        return false;
    }

    if (botToLaunch->status == BotStatus::Online || botToLaunch->status == BotStatus::Starting) {
        return true;  // Already launched or starting
    }

    if (botToLaunch->instance.isEmpty()) {
        LogManager::log(QString("Bot '%1' has no instance configured").arg(botName), LogManager::Error);
        return false;
    }

    if (botToLaunch->account.isEmpty()) {
        LogManager::log(QString("Bot '%1' has no account configured").arg(botName), LogManager::Error);
        return false;
    }

    QString prismCommand = prismConfig.prismExecutable;
    if (prismCommand.isEmpty()) {
        LogManager::log("PrismLauncher not configured. Go to Tools -> PrismLauncher Settings", LogManager::Error);
        return false;
    }

    botToLaunch->status = BotStatus::Starting;
    updateInstancesTable();
    updateStatusDisplay();

    PrismLauncherManager::launchBot(botToLaunch);

    return true;
}


void ManagerMainWindow::stopBot()
{
    if (selectedBotName.isEmpty()) return;

    BotInstance *bot = BotManager::getBotByName(selectedBotName);
    if (bot) {
        if (bot->minecraftPid > 0 || bot->status == BotStatus::Online) {
            LogManager::log(QString("Stopping bot '%1'...").arg(bot->name), LogManager::Info);
            bot->status = BotStatus::Stopping;
            bot->manualStop = true;
            updateStatusDisplay();

            // Try graceful shutdown first
            BotManager::sendShutdownCommand(bot->name, "Stopped by manager");

            // Force kill after 5 seconds if not shut down
            QTimer::singleShot(5000, this, [this, botName = bot->name, pid = bot->minecraftPid]() {
                BotInstance *b = BotManager::getBotByName(botName);
                // Only kill if bot is still offline AND the PID hasn't changed (no restart)
                if (b && b->status != BotStatus::Offline && b->minecraftPid == pid && pid > 0) {
                    LogManager::log(QString("Bot '%1' didn't shut down gracefully, force killing...").arg(botName), LogManager::Warning);
                    PrismLauncherManager::stopBot(pid);
                }
            });

            bot->minecraftPid = 0;
        } else if (bot->status == BotStatus::Starting) {
            LogManager::log(QString("Bot '%1' is still starting, cannot stop yet").arg(bot->name), LogManager::Warning);
        } else {
            LogManager::log(QString("Bot '%1' is not running").arg(bot->name), LogManager::Warning);
        }
    }
}

void ManagerMainWindow::restartBot()
{
    if (!selectedBotName.isEmpty()) {
        restartBotByName(selectedBotName, "Manual restart");
    }
}

void ManagerMainWindow::restartBotByName(const QString &botName, const QString &reason)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (!bot) return;

    if (bot->status == BotStatus::Online) {
        LogManager::log(QString("Restarting bot '%1': %2").arg(botName).arg(reason), LogManager::Info);
        bot->manualStop = true;
        bot->status = BotStatus::Stopping;
        BotManager::sendShutdownCommand(botName, reason);

        QTimer::singleShot(3000, this, [this, botName]() {
            launchBotByName(botName);
        });
    }
}

void ManagerMainWindow::loadSettingsFromFile()
{
    // Save current changes to memory before loading
    if (!selectedBotName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(selectedBotName);
        if (bot) {
            bot->instance = ui->instanceComboBox->currentText() == "(None)" ? "" : ui->instanceComboBox->currentText();
            bot->account = ui->accountComboBox->currentText() == "(None)" ? "" : ui->accountComboBox->currentText();
            bot->server = ui->serverLineEdit->text();
            bot->maxMemory = ui->memorySpinBox->value();
            bot->restartThreshold = ui->restartThresholdSpinBox->value();
            bot->autoRestart = ui->autoRestartCheckBox->isChecked();
            bot->tokenRefresh = ui->tokenRefreshCheckBox->isChecked();
            bot->debugLogging = ui->debugModeCheckBox->isChecked();
        }
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Load Configuration",
        "This will discard any unsaved changes. Continue?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        loadSettings();
        updateInstancesTable();

        selectedBotName.clear();
        ui->detailsStackedWidget->setCurrentIndex(0);
        ui->detailsStackedWidget->hide();

        LogManager::log("Configuration loaded from file successfully", LogManager::Success);
    }
}

void ManagerMainWindow::loadPrismLauncherConfig()
{
    updateInstanceComboBox();
    updateAccountComboBox();
}


void ManagerMainWindow::updateInstanceComboBox()
{
    ui->instanceComboBox->clear();

    // Add empty option to allow deselection
    ui->instanceComboBox->addItem("(None)");

    if (!prismConfig.instances.isEmpty()) {
        QStringList usedInstances = getUsedInstances();
        QString currentBotInstance;

        // Get the current bot's instance if we're editing
        if (!selectedBotName.isEmpty()) {
            BotInstance *bot = BotManager::getBotByName(selectedBotName);
            if (bot) {
                currentBotInstance = bot->instance;
            }
        }

        // Add available instances (not used by other bots)
        for (const QString &instance : std::as_const(prismConfig.instances)) {
            if (!usedInstances.contains(instance) || instance == currentBotInstance) {
                ui->instanceComboBox->addItem(instance);
            }
        }
    }
}

void ManagerMainWindow::updateAccountComboBox()
{
    ui->accountComboBox->clear();

    // Add empty option to allow deselection
    ui->accountComboBox->addItem("(None)");

    if (!prismConfig.accounts.isEmpty()) {
        ui->accountComboBox->addItems(prismConfig.accounts);
    }
}

void ManagerMainWindow::configurePrismLauncher()
{
    PrismSettingsDialog dialog(this);

    // Auto-detect PrismLauncher path if not configured
    QString pathToSet = prismConfig.prismPath;
    bool pathWasDetected = false;
    if (pathToSet.isEmpty()) {
        pathToSet = PrismSettingsDialog::detectPrismLauncherPath();
        pathWasDetected = !pathToSet.isEmpty();
    }

    // Set path (this will parse and auto-fill for new paths)
    dialog.setCurrentPath(pathToSet);

    // Only override with saved config if we're using an existing path
    if (!pathWasDetected && !prismConfig.prismPath.isEmpty()) {
        dialog.setExecutable(prismConfig.prismExecutable);
        dialog.setInstances(prismConfig.instances);
        dialog.setAccounts(prismConfig.accounts);
    }

    if (dialog.exec() == QDialog::Accepted) {
        QString newPath = dialog.getCurrentPath();
        QString newExecutable = dialog.getExecutable();

        if (!newPath.isEmpty() && newPath != prismConfig.prismPath) {
            prismConfig.prismPath = newPath;
            prismConfig.instances = dialog.getInstances();
            prismConfig.accounts = dialog.getAccounts();

            updateInstanceComboBox();
            updateAccountComboBox();
        }

        // Update executable even if path didn't change
        prismConfig.prismExecutable = newExecutable;
    }
}

QStringList ManagerMainWindow::getUsedInstances() const
{
    QStringList used;
    for (const BotInstance &bot : BotManager::getBots()) {
        if (!bot.instance.isEmpty()) {
            used.append(bot.instance);
        }
    }
    return used;
}

void ManagerMainWindow::saveSettings()
{
    LogManager::log("Saving configuration to file...", LogManager::Info);
    QSettings settings("MCBotManager", "MCBotManager");

    // Save PrismLauncher configuration
    settings.beginGroup("PrismLauncher");
    settings.setValue("path", prismConfig.prismPath);
    settings.setValue("executable", prismConfig.prismExecutable);
    settings.setValue("instances", prismConfig.instances);
    settings.setValue("accounts", prismConfig.accounts);
    settings.endGroup();

    // Save bot instances
    settings.beginGroup("Bots");
    QVector<BotInstance> &bots = BotManager::getBots();
    settings.setValue("count", bots.size());

    for (int i = 0; i < bots.size(); ++i) {
        saveBotInstance(settings, bots[i], i);
    }
    settings.endGroup();

    // Save window state
    settings.beginGroup("Window");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("detailsPinned", detailsPinned);
    settings.setValue("networkStatsVisible", ui->actionNetworkStats->isChecked());
    settings.endGroup();

    LogManager::log(QString("Configuration saved successfully (%1 bots)").arg(bots.size()), LogManager::Success);
}

void ManagerMainWindow::loadSettings()
{
    QSettings settings("MCBotManager", "MCBotManager");

    // Load PrismLauncher configuration
    settings.beginGroup("PrismLauncher");
    prismConfig.prismPath = settings.value("path", "").toString();
    prismConfig.prismExecutable = settings.value("executable", "").toString();
    prismConfig.instances = settings.value("instances", QStringList()).toStringList();
    prismConfig.accounts = settings.value("accounts", QStringList()).toStringList();
    settings.endGroup();

    // Re-parse PrismLauncher directory if path exists to get latest instances/accounts
    if (!prismConfig.prismPath.isEmpty()) {
        prismConfig.instances = PrismSettingsDialog::parsePrismInstances(prismConfig.prismPath);
        prismConfig.accounts = PrismSettingsDialog::parsePrismAccounts(prismConfig.prismPath);
    }

    // Load bot instances
    settings.beginGroup("Bots");
    int botCount = settings.value("count", 0).toInt();

    BotManager::getBots().clear();
    for (int i = 0; i < botCount; ++i) {
        BotInstance bot = loadBotInstance(settings, i);
        if (!bot.name.isEmpty()) {
            BotManager::addBot(bot);
        }
    }
    settings.endGroup();

    setupConsoleTab();
    setupMeteorTab();
    setupBaritoneTab();

    settings.beginGroup("Window");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    detailsPinned = settings.value("detailsPinned", false).toBool();
    bool networkStatsVisible = settings.value("networkStatsVisible", false).toBool();
    settings.endGroup();

    QPushButton *pinButton = qobject_cast<QPushButton*>(ui->mainTabWidget->cornerWidget(Qt::TopRightCorner));
    if (pinButton) {
        pinButton->setChecked(detailsPinned);
    }

    ui->actionNetworkStats->setChecked(networkStatsVisible);
}

void ManagerMainWindow::saveBotInstance(QSettings &settings, const BotInstance &bot, int index)
{
    settings.beginGroup(QString("Bot_%1").arg(index));

    settings.setValue("name", bot.name);
    settings.setValue("instance", bot.instance);
    settings.setValue("account", bot.account);
    settings.setValue("server", bot.server);
    settings.setValue("connectionId", bot.connectionId);
    settings.setValue("maxMemory", bot.maxMemory);
    settings.setValue("restartThreshold", bot.restartThreshold);
    settings.setValue("autoRestart", bot.autoRestart);
    settings.setValue("tokenRefresh", bot.tokenRefresh);
    settings.setValue("debugLogging", bot.debugLogging);

    settings.endGroup();
}

BotInstance ManagerMainWindow::loadBotInstance(QSettings &settings, int index)
{
    settings.beginGroup(QString("Bot_%1").arg(index));

    BotInstance bot;
    bot.name = settings.value("name", "").toString();
    bot.status = BotStatus::Offline;  // Always start as offline
    bot.instance = settings.value("instance", "").toString();
    bot.account = settings.value("account", "").toString();
    bot.server = settings.value("server", "").toString();
    bot.connectionId = settings.value("connectionId", -1).toInt();
    bot.maxMemory = settings.value("maxMemory", 4096).toInt();
    bot.currentMemory = 0;
    bot.restartThreshold = settings.value("restartThreshold", 48.0).toDouble();
    bot.autoRestart = settings.value("autoRestart", true).toBool();
    bot.tokenRefresh = settings.value("tokenRefresh", true).toBool();
    bot.debugLogging = settings.value("debugLogging", false).toBool();

    bot.position = QVector3D(0, 0, 0);
    bot.dimension = "";

    settings.endGroup();
    return bot;
}

void ManagerMainWindow::launchAllBots()
{
    if (!scheduledLaunches.isEmpty()) {
        LogManager::log("Sequential launch already in progress", LogManager::Warning);
        return;
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    QDateTime now = QDateTime::currentDateTime();
    int delaySeconds = 0;

    for (const BotInstance &bot : std::as_const(bots)) {
        if (bot.status != BotStatus::Online && bot.status != BotStatus::Starting) {
            ScheduledLaunch launch;
            launch.botName = bot.name;
            launch.launchTime = now.addSecs(delaySeconds);
            scheduledLaunches.append(launch);
            delaySeconds += 30;
        }
    }

    if (scheduledLaunches.isEmpty()) {
        LogManager::log("All bots are already online", LogManager::Info);
        return;
    }

    LogManager::log(QString("Scheduled %1 bots for sequential launch (30s intervals)").arg(scheduledLaunches.size()), LogManager::Info);
    launchSchedulerTimer->start(1000);
    updateInstancesTable();
    updateStatusDisplay();
}

void ManagerMainWindow::stopAllBots()
{
    int stoppedCount = 0;

    LogManager::log("Stopping all bots...", LogManager::Info);

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (bot.status == BotStatus::Online || bot.status == BotStatus::Starting) {
            LogManager::log(QString("Stopping bot '%1'").arg(bot.name), LogManager::Info);

            bot.manualStop = true;

            // Send graceful shutdown first
            if (bot.minecraftPid > 0) {
                BotManager::sendShutdownCommand(bot.name, "Stopped by manager");
                bot.status = BotStatus::Stopping;

                QString botName = bot.name;
                qint64 pid = bot.minecraftPid;
                QTimer::singleShot(5000, this, [this, botName, pid]() {
                    BotInstance *b = BotManager::getBotByName(botName);
                    if (b && b->status != BotStatus::Offline) {
                        LogManager::log(QString("Force killing bot '%1' (PID: %2)").arg(botName).arg(pid), LogManager::Warning);
                        PrismLauncherManager::stopBot(pid);
                        b->status = BotStatus::Offline;
                        b->connectionId = -1;
                        b->minecraftPid = 0;
                        emit BotManager::instance().botUpdated(botName);
                    }
                });
            } else {
                // If no PID, just update status
                bot.status = BotStatus::Offline;
                bot.currentMemory = 0;
                bot.position = QVector3D(0, 0, 0);
                bot.dimension = "";
            }

            stoppedCount++;
        }
    }

    if (stoppedCount > 0) {
        LogManager::log(QString("Sent shutdown command to %1 bot(s)").arg(stoppedCount), LogManager::Success);
    } else {
        LogManager::log("No bots were online to stop", LogManager::Info);
    }
}


void ManagerMainWindow::onClearLog()
{
    // Clear the currently visible tab
    if (ui->logTabWidget->currentIndex() == 0) {
        LogManager::clearManagerLog();
    } else {
        LogManager::clearPrismLog();
    }
}

void ManagerMainWindow::onAutoScrollToggled(bool checked)
{
    LogManager::setAutoScroll(checked);
}

void ManagerMainWindow::checkBotUptimes()
{
    QVector<BotInstance> &bots = BotManager::getBots();
    QDateTime now = QDateTime::currentDateTime();

    for (BotInstance &bot : bots) {
        if (bot.status == BotStatus::Online && bot.restartThreshold > 0 && bot.startTime.isValid()) {
            qint64 uptimeSeconds = bot.startTime.secsTo(now);
            double uptimeHours = uptimeSeconds / 3600.0;

            if (uptimeHours >= bot.restartThreshold) {
                restartBotByName(bot.name, QString("Uptime threshold reached (%1 hours)").arg(uptimeHours, 0, 'f', 2));
            }
        }
    }
}

void ManagerMainWindow::checkScheduledLaunches()
{
    if (scheduledLaunches.isEmpty()) {
        return;
    }

    QDateTime now = QDateTime::currentDateTime();
    QList<ScheduledLaunch> toRemove;

    for (const ScheduledLaunch &launch : std::as_const(scheduledLaunches)) {
        if (launch.launchTime <= now) {
            LogManager::log(QString("Launching scheduled bot '%1'").arg(launch.botName), LogManager::Info);
            launchBotByName(launch.botName);
            toRemove.append(launch);
        }
    }

    for (const ScheduledLaunch &launch : toRemove) {
        scheduledLaunches.removeAll(launch);
    }

    if (toRemove.size() > 0) {
        updateInstancesTable();
        updateStatusDisplay();
    }

    if (!toRemove.isEmpty() && scheduledLaunches.isEmpty()) {
        LogManager::log("Sequential launch complete: All scheduled bots launched", LogManager::Success);
        launchSchedulerTimer->stop();
    }
}

QString ManagerMainWindow::statusToString(BotStatus status)
{
    switch (status) {
    case BotStatus::Offline:
        return "Offline";
    case BotStatus::Starting:
        return "Starting";
    case BotStatus::Online:
        return "Online";
    case BotStatus::Stopping:
        return "Stopping";
    case BotStatus::Error:
        return "Error";
    }
    return "Unknown";
}

void ManagerMainWindow::setupPipeServer()
{
    connect(&PipeServer::instance(), &PipeServer::clientConnected,
            this, &ManagerMainWindow::onClientConnected);

    connect(&PipeServer::instance(), &PipeServer::clientDisconnected,
            this, &ManagerMainWindow::onClientDisconnected);

    // Get socket path (prefer XDG_RUNTIME_DIR, fallback to /tmp)
    QString socketPath;
    QByteArray xdgRuntime = qgetenv("XDG_RUNTIME_DIR");
    if (!xdgRuntime.isEmpty()) {
        socketPath = QString::fromUtf8(xdgRuntime) + "/minecraft_manager";
    } else {
        socketPath = "/tmp/minecraft_manager";
    }

    if (!PipeServer::start(socketPath)) {
        LogManager::log("Failed to start pipe server", LogManager::Error);
    }
}

void ManagerMainWindow::onClientConnected(int connectionId, const QString &botName)
{
    if (!botName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot) {
            updateInstancesTable();
            updateStatusDisplay();

            // Request Meteor modules list
            BotManager::sendCommand(botName, "meteor list");

            BotManager::requestBaritoneSettings(botName);
            BotManager::requestBaritoneCommands(botName);
        }
    }
}

void ManagerMainWindow::onClientDisconnected(int connectionId)
{
    BotInstance *bot = BotManager::getBotByConnectionId(connectionId);
    if (bot) {
        QString botName = bot->name;
        bool shouldAutoRestart = bot->autoRestart && !bot->manualStop;

        bot->connectionId = -1;
        bot->status = BotStatus::Offline;
        bot->manualStop = false;
        updateInstancesTable();
        updateStatusDisplay();

        if (shouldAutoRestart) {
            LogManager::log(QString("Bot '%1' crashed, auto-restarting...").arg(botName), LogManager::Warning);
            QTimer::singleShot(2000, this, [this, botName]() {
                launchBotByName(botName);
            });
        }
    }
}

void ManagerMainWindow::setupConsoleTab()
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->consoleTab->layout());
    if (!layout) {
        layout = new QVBoxLayout(ui->consoleTab);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (!bot.consoleWidget) {
            bot.consoleWidget = new BotConsoleWidget(this);
            connect(bot.consoleWidget, &BotConsoleWidget::commandEntered,
                    this, &ManagerMainWindow::onConsoleCommandEntered);
            bot.consoleWidget->hide();
            layout->addWidget(bot.consoleWidget);
        }
    }
}

void ManagerMainWindow::onConsoleCommandEntered(const QString &command)
{
    if (selectedBotName.isEmpty()) {
        return;
    }

    BotInstance *bot = BotManager::getBotByName(selectedBotName);
    if (!bot || bot->connectionId < 0) {
        if (bot && bot->consoleWidget) {
            bot->consoleWidget->appendResponse(false, "Bot not connected");
        }
        return;
    }

    QString commandToSend = command;
    QString cmdName = command.split(' ').first().toLower();
    bool isBaritoneCommand = false;

    if (cmdName == "baritone" && command.split(' ').size() > 1) {
        commandToSend = command.mid(cmdName.length()).trimmed();
        cmdName = commandToSend.split(' ').first().toLower();
    }

    // Check if it's a known Baritone command
    for (const auto &baritoneCmd : bot->baritoneCommands) {
        if (baritoneCmd.name.toLower() == cmdName) {
            isBaritoneCommand = true;
            break;
        }
        for (const auto &alias : baritoneCmd.aliases) {
            if (alias.toLower() == cmdName) {
                isBaritoneCommand = true;
                break;
            }
        }
        if (isBaritoneCommand) break;
    }

    if (isBaritoneCommand) {
        BotManager::sendBaritoneCommand(selectedBotName, commandToSend);
    } else {
        BotManager::sendCommand(selectedBotName, command);
    }
}

void ManagerMainWindow::handleCommandResponse(const QString &botName, bool success, const QString &message)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->consoleWidget) {
        bot->consoleWidget->appendResponse(success, message);
    }
}

void ManagerMainWindow::setupMeteorTab()
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->meteorTab->layout());
    if (!layout) {
        layout = new QVBoxLayout(ui->meteorTab);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (!bot.meteorWidget) {
            bot.meteorWidget = new MeteorModulesWidget(this);
            connect(bot.meteorWidget, &MeteorModulesWidget::moduleToggled,
                    this, &ManagerMainWindow::onMeteorModuleToggled);
            connect(bot.meteorWidget, &MeteorModulesWidget::settingChanged,
                    this, &ManagerMainWindow::onMeteorSettingChanged);
            bot.meteorWidget->hide();
            layout->addWidget(bot.meteorWidget);
        }
    }
}

void ManagerMainWindow::onMeteorModulesReceived(const QString &botName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->meteorWidget) {
        bot->meteorWidget->updateModules(bot->meteorModules);
    }
}

void ManagerMainWindow::onMeteorSingleModuleUpdated(const QString &botName, const QString &moduleName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->meteorWidget) {
        if (bot->meteorModules.contains(moduleName)) {
            bot->meteorWidget->updateSingleModule(bot->meteorModules[moduleName]);
        }
    }
}

void ManagerMainWindow::onMeteorModuleToggled(const QString &moduleName, bool enabled)
{
    if (selectedBotName.isEmpty()) {
        return;
    }

    BotInstance *bot = BotManager::getBotByName(selectedBotName);
    if (!bot || bot->status != BotStatus::Online || bot->connectionId < 0) {
        LogManager::log(QString("Cannot toggle module: bot '%1' is not connected").arg(selectedBotName),
                       LogManager::Warning);
        return;
    }

    QString command = QString("meteor set %1 enabled %2").arg(moduleName, enabled ? "true" : "false");
    BotManager::sendCommand(selectedBotName, command);
}

void ManagerMainWindow::onMeteorSettingChanged(const QString &moduleName, const QString &settingPath, const QVariant &value)
{
    if (selectedBotName.isEmpty()) {
        return;
    }

    BotManager::sendMeteorSettingChange(selectedBotName, moduleName, settingPath, value);
}

void ManagerMainWindow::setupBaritoneTab()
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->baritoneTab->layout());
    if (!layout) {
        layout = new QVBoxLayout(ui->baritoneTab);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (!bot.baritoneWidget) {
            bot.baritoneWidget = new BaritoneWidget(this);
            connect(bot.baritoneWidget, &BaritoneWidget::settingChanged,
                    this, &ManagerMainWindow::onBaritoneSettingChanged);
            bot.baritoneWidget->hide();
            layout->addWidget(bot.baritoneWidget);
        }
    }
}

void ManagerMainWindow::onBaritoneSettingsReceived(const QString &botName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->baritoneWidget) {
        bot->baritoneWidget->updateSettings(bot->baritoneSettings);
    }
}

void ManagerMainWindow::onBaritoneCommandsReceived(const QString &botName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->consoleWidget) {
        QVector<QPair<QString, QString>> commands;
        for (const auto &cmd : bot->baritoneCommands) {
            QString description = cmd.shortDesc;
            if (!cmd.longDesc.isEmpty()) {
                description += QString("\n") + cmd.longDesc.join("\n");
            }
            commands.append(qMakePair(cmd.name, description));
        }
        bot->consoleWidget->addBaritoneCommands(commands);
        LogManager::log(QString("[%1] Baritone: Added %2 commands to console").arg(botName).arg(commands.size()), LogManager::Info);
    }
}

void ManagerMainWindow::onBaritoneSingleSettingUpdated(const QString &botName, const QString &settingName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->baritoneWidget) {
        if (bot->baritoneSettings.contains(settingName)) {
            bot->baritoneWidget->updateSingleSetting(bot->baritoneSettings[settingName]);
        }
    }
}

void ManagerMainWindow::onBaritoneSettingChanged(const QString &settingName, const QVariant &value)
{
    if (selectedBotName.isEmpty()) {
        return;
    }

    BotManager::sendBaritoneSettingChange(selectedBotName, settingName, value);
}

void ManagerMainWindow::showNetworkStats(bool show)
{
    if (show) {
        networkStatsDock->show();
        networkStatsDock->raise();
    } else {
        networkStatsDock->hide();
    }
}

void ManagerMainWindow::onPinDetailsToggled(bool pinned)
{
    detailsPinned = pinned;
}

void ManagerMainWindow::showAboutDialog()
{
    QString aboutText = QString(
        "<h2>MC Bot Manager</h2>"
        "<p>Version " APP_VERSION "</p>"
        "<p>A Qt-based GUI application for managing multiple Minecraft bot instances.</p>"
        "<p>Copyright © 2025 mankool</p>"
        "<p>Licensed under the GNU General Public License v3.0</p>"
        "<p>This program comes with ABSOLUTELY NO WARRANTY. "
        "This is free software, and you are welcome to redistribute it "
        "under certain conditions.</p>"
        "<p>Built with Qt %1</p>"
    ).arg(QT_VERSION_STR);

    QMessageBox::about(this, "About MC Bot Manager", aboutText);
}
