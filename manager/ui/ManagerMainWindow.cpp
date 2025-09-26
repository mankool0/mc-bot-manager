#include "ManagerMainWindow.h"
#include "PrismSettingsDialog.h"
#include "logging/LogManager.h"
#include "prism/PrismLauncherManager.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QProcess>
#include <QDateTime>
#include <QTextCursor>
#include <QRegularExpression>

ManagerMainWindow::ManagerMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ManagerMainWindow)
    , loadingConfiguration(false)
{
    ui->setupUi(this);

    logManager = new LogManager(this);
    logManager->setManagerLogWidget(ui->managerLogTextEdit);
    logManager->setPrismLogWidget(ui->prismLogTextEdit);

    prismLauncherManager = new PrismLauncherManager(logManager, this);
    prismLauncherManager->setPrismConfig(&prismConfig);

    connect(prismLauncherManager,
            &PrismLauncherManager::minecraftLaunching,
            this,
            [this](const QString &botName) {
                for (BotInstance &bot : botInstances) {
                    if (bot.status == BotStatus::Starting) {
                        logManager->log(QString("Minecraft launching for bot '%1'").arg(bot.name), LogManager::Success);
                        break;
                    }
                }
            });

    connect(prismLauncherManager,
            &PrismLauncherManager::minecraftStarting,
            this,
            [this](const QString &botName) {
                for (BotInstance &bot : botInstances) {
                    if (bot.status == BotStatus::Starting) {
                        //logManager->log(QString("Bot '%1' Minecraft process is - starting...").arg(bot.name), LogManager::Info);

                        // TODO: This is temp for testing until client side pipe connection is implemented
                        bot.status = BotStatus::Online;
                        logManager->log(QString("Bot '%1' is now online").arg(bot.name), LogManager::Success);
                        onBotStatusChanged();
                        break;
                    }
                }
            });

    setupUI();
    loadSettings();
}

ManagerMainWindow::~ManagerMainWindow()
{
    delete logManager;
    delete prismLauncherManager;
    delete ui;
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
    connect(ui->actionSave, &QAction::triggered, this, &ManagerMainWindow::saveSettings);
    connect(ui->actionOpen, &QAction::triggered, this, &ManagerMainWindow::loadSettingsFromFile);
    connect(ui->actionLaunchAll, &QAction::triggered, this, &ManagerMainWindow::launchAllBots);
    connect(ui->actionStopAll, &QAction::triggered, this, &ManagerMainWindow::stopAllBots);

    connect(ui->instanceComboBox, &QComboBox::currentTextChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->accountComboBox, &QComboBox::currentTextChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->serverLineEdit, &QLineEdit::textChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->memorySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->restartThresholdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->autoRestartCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->tokenRefreshCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->debugModeCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);

    ui->detailsStackedWidget->setCurrentIndex(0);
    ui->detailsStackedWidget->hide();

    connect(ui->clearLogButton, &QPushButton::clicked, this, &ManagerMainWindow::onClearLog);
    connect(ui->autoScrollCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onAutoScrollToggled);

    loadPrismLauncherConfig();

    tableUpdateTimer = new QTimer(this);
    connect(tableUpdateTimer, &QTimer::timeout, this, &ManagerMainWindow::updateInstancesTable);
    tableUpdateTimer->start(50);

    logManager->log("MC Bot Manager started", LogManager::Info);
}

void ManagerMainWindow::showInstancesContextMenu(const QPoint &pos)
{
    QMenu contextMenu(this);

    bool botAtPos = false;
    QTableWidgetItem *item = ui->instancesTableWidget->itemAt(pos);
    if (item) {
        int row = item->row();
        if (row >= 0 && row < botInstances.size()) {
            botAtPos = true;
            const BotInstance &bot = botInstances[row];
            bool isOnline = (bot.status == BotStatus::Online);

            contextMenu.addSeparator();

            if (!isOnline) {
                QAction *launchAction = contextMenu.addAction("Launch Bot");
                connect(launchAction, &QAction::triggered, this, &ManagerMainWindow::launchBot);
            } else {
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
                                           QString("NewBot_%1").arg(botInstances.size() + 1), &ok);

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

        botInstances.append(newBot);
        updateInstancesTable();

        logManager->log(QString("Added new bot '%1'").arg(botName), LogManager::Success);
    }
}

void ManagerMainWindow::removeBot()
{
    QList<QTableWidgetItem*> selectedItems = ui->instancesTableWidget->selectedItems();
    if (selectedItems.isEmpty()) return;

    int row = selectedItems[0]->row();
    if (row >= 0 && row < botInstances.size()) {
        QString botName = botInstances[row].name;

        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "Remove Bot",
            QString("Are you sure you want to remove bot '%1'?").arg(botName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            botInstances.removeAt(row);
            updateInstancesTable();

            if (selectedBotName == botName) {
                selectedBotName.clear();
                ui->detailsStackedWidget->setCurrentIndex(0);
                ui->detailsStackedWidget->hide();
            }

            logManager->log(QString("Removed bot '%1'").arg(botName), LogManager::Success);
        }
    }
}

void ManagerMainWindow::updateInstancesTable()
{
    ui->instancesTableWidget->setRowCount(botInstances.size());

    for (int i = 0; i < botInstances.size(); ++i) {
        const BotInstance &bot = botInstances[i];

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
}

void ManagerMainWindow::onInstanceSelectionChanged()
{
    QList<QTableWidgetItem*> selectedItems = ui->instancesTableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        selectedBotName.clear();
        ui->detailsStackedWidget->setCurrentIndex(0);
        ui->detailsStackedWidget->hide();
        return;
    }

    int row = selectedItems[0]->row();
    if (row >= 0 && row < botInstances.size()) {
        const BotInstance &bot = botInstances[row];
        selectedBotName = bot.name;
        loadBotConfiguration(bot);
        ui->detailsStackedWidget->setCurrentIndex(1);
        ui->detailsStackedWidget->show();
        updateStatusDisplay();
    }
}

void ManagerMainWindow::onConfigurationChanged()
{
    if (loadingConfiguration) return;

    if (!selectedBotName.isEmpty()) {
        for (BotInstance &bot : botInstances) {
            if (bot.name == selectedBotName) {
                bot.instance = ui->instanceComboBox->currentText() == "(None)" ? "" : ui->instanceComboBox->currentText();
                bot.account = ui->accountComboBox->currentText() == "(None)" ? "" : ui->accountComboBox->currentText();
                bot.server = ui->serverLineEdit->text();
                bot.maxMemory = ui->memorySpinBox->value();
                bot.restartThreshold = ui->restartThresholdSpinBox->value();
                bot.autoRestart = ui->autoRestartCheckBox->isChecked();
                bot.tokenRefresh = ui->tokenRefreshCheckBox->isChecked();
                bot.debugLogging = ui->debugModeCheckBox->isChecked();

                break;
            }
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

    // Update button states based on bot status
    BotInstance *selectedBot = nullptr;
    for (BotInstance &bot : botInstances) {
        if (bot.name == selectedBotName) {
            selectedBot = &bot;
            break;
        }
    }

    if (selectedBot) {
        ui->launchBotButton->setEnabled(selectedBot->status == BotStatus::Offline);
        ui->stopBotButton->setEnabled(selectedBot->status == BotStatus::Online);
        ui->restartBotButton->setEnabled(selectedBot->status == BotStatus::Online);
    }
}

void ManagerMainWindow::launchBot()
{
    if (selectedBotName.isEmpty()) return;
    launchBotByName(selectedBotName);
}

bool ManagerMainWindow::launchBotByName(const QString &botName)
{
    // Find the bot to launch
    BotInstance *botToLaunch = nullptr;
    for (BotInstance &bot : botInstances) {
        if (bot.name == botName) {
            botToLaunch = &bot;
            break;
        }
    }

    if (!botToLaunch) {
        logManager->log(QString("Bot '%1' not found").arg(botName), LogManager::Error);
        return false;
    }

    if (botToLaunch->status == BotStatus::Online || botToLaunch->status == BotStatus::Starting) {
        return true;  // Already launched or starting
    }

    if (botToLaunch->instance.isEmpty()) {
        logManager->log(QString("Bot '%1' has no instance configured").arg(botName), LogManager::Error);
        return false;
    }

    if (botToLaunch->account.isEmpty()) {
        logManager->log(QString("Bot '%1' has no account configured").arg(botName), LogManager::Error);
        return false;
    }

    QString prismCommand = prismConfig.prismExecutable;
    if (prismCommand.isEmpty()) {
        logManager->log("PrismLauncher not configured. Go to Tools -> PrismLauncher Settings", LogManager::Error);
        return false;
    }

    botToLaunch->status = BotStatus::Starting;
    updateStatusDisplay();

    prismLauncherManager->launchBot(botToLaunch);

    return true;
}


void ManagerMainWindow::stopBot()
{
    if (selectedBotName.isEmpty()) return;

    for (BotInstance &bot : botInstances) {
        if (bot.name == selectedBotName) {
            if (bot.process != nullptr) {
                logManager->log(QString("Stopping bot '%1'...").arg(bot.name), LogManager::Info);
                bot.status = BotStatus::Stopping;
                updateStatusDisplay();

                bot.process->terminate();

                // If it doesn't terminate gracefully in 5 seconds, kill it
                QTimer::singleShot(5000, this, [&bot, this]() {
                    if (bot.process != nullptr && bot.process->state() == QProcess::Running) {
                        logManager->log(QString("Force killing bot '%1' process").arg(bot.name), LogManager::Warning);
                        bot.process->kill();
                    }
                });
            } else {
                logManager->log(QString("Bot '%1' is not running").arg(bot.name), LogManager::Warning);
            }
            break;
        }
    }
}

void ManagerMainWindow::restartBot()
{
    stopBot();
    QTimer::singleShot(1500, this, &ManagerMainWindow::launchBot);
}

void ManagerMainWindow::loadSettingsFromFile()
{
    // Save current changes to memory before loading
    if (!selectedBotName.isEmpty()) {
        for (BotInstance &bot : botInstances) {
            if (bot.name == selectedBotName) {
                bot.instance = ui->instanceComboBox->currentText() == "(None)" ? "" : ui->instanceComboBox->currentText();
                bot.account = ui->accountComboBox->currentText() == "(None)" ? "" : ui->accountComboBox->currentText();
                bot.server = ui->serverLineEdit->text();
                bot.maxMemory = ui->memorySpinBox->value();
                bot.restartThreshold = ui->restartThresholdSpinBox->value();
                bot.autoRestart = ui->autoRestartCheckBox->isChecked();
                bot.tokenRefresh = ui->tokenRefreshCheckBox->isChecked();
                bot.debugLogging = ui->debugModeCheckBox->isChecked();
                break;
            }
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

        logManager->log("Configuration loaded from file successfully", LogManager::Success);
    }
}

void ManagerMainWindow::loadPrismLauncherConfig()
{
    updateInstanceComboBox();
    updateAccountComboBox();
}

void ManagerMainWindow::parsePrismInstances()
{
    prismConfig.instances.clear();

    if (prismConfig.prismPath.isEmpty()) return;

    QString instancesPath = prismConfig.prismPath + "/instances";
    QDir instancesDir(instancesPath);

    if (!instancesDir.exists()) return;

    QStringList instanceDirs = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &instanceName : std::as_const(instanceDirs)) {
        QString instanceCfgPath = instancesPath + "/" + instanceName + "/instance.cfg";
        if (QFile::exists(instanceCfgPath)) {
            prismConfig.instances.append(instanceName);
        }
    }

    prismConfig.instances.sort();
}

void ManagerMainWindow::parsePrismAccounts()
{
    prismConfig.accounts.clear();

    if (prismConfig.prismPath.isEmpty()) return;

    QString accountsPath = prismConfig.prismPath + "/accounts.json";
    QFile accountsFile(accountsPath);

    if (!accountsFile.open(QIODevice::ReadOnly)) return;

    QByteArray data = accountsFile.readAll();
    accountsFile.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) return;

    QJsonObject rootObj = doc.object();
    QJsonArray accountsArray = rootObj["accounts"].toArray();

    for (const QJsonValue &value : std::as_const(accountsArray)) {
        QJsonObject accountObj = value.toObject();
        QJsonObject profileObj = accountObj["profile"].toObject();
        QString accountName = profileObj["name"].toString();

        if (!accountName.isEmpty()) {
            prismConfig.accounts.append(accountName);
        }
    }

    prismConfig.accounts.sort();
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
            for (const BotInstance &bot : std::as_const(botInstances)) {
                if (bot.name == selectedBotName) {
                    currentBotInstance = bot.instance;
                    break;
                }
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

    // Set current configuration
    dialog.setCurrentPath(prismConfig.prismPath);
    dialog.setExecutable(prismConfig.prismExecutable);
    dialog.setInstances(prismConfig.instances);
    dialog.setAccounts(prismConfig.accounts);

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

QString ManagerMainWindow::detectPrismLauncherPath()
{
    // TODO: Not used for now, can add auto-detection later
    return QString();
}

QStringList ManagerMainWindow::getUsedInstances() const
{
    QStringList used;
    for (const BotInstance &bot : botInstances) {
        if (!bot.instance.isEmpty()) {
            used.append(bot.instance);
        }
    }
    return used;
}

void ManagerMainWindow::saveSettings()
{
    logManager->log("Saving configuration to file...", LogManager::Info);
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
    settings.setValue("count", botInstances.size());

    for (int i = 0; i < botInstances.size(); ++i) {
        saveBotInstance(settings, botInstances[i], i);
    }
    settings.endGroup();

    // Save window state
    settings.beginGroup("Window");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();

    logManager->log(QString("Configuration saved successfully (%1 bots)").arg(botInstances.size()), LogManager::Success);
}

void ManagerMainWindow::loadSettings()
{
    QSettings settings("MCBotManager", "MCBotManager");

    // Load PrismLauncher configuration
    settings.beginGroup("PrismLauncher");
    prismConfig.prismPath = settings.value("path", "").toString();
    prismConfig.prismExecutable = settings.value("executable", "prismlauncher").toString();
    prismConfig.instances = settings.value("instances", QStringList()).toStringList();
    prismConfig.accounts = settings.value("accounts", QStringList()).toStringList();
    settings.endGroup();

    // Re-parse PrismLauncher directory if path exists to get latest instances/accounts
    if (!prismConfig.prismPath.isEmpty()) {
        parsePrismInstances();
        parsePrismAccounts();
    }

    // Load bot instances
    settings.beginGroup("Bots");
    int botCount = settings.value("count", 0).toInt();

    botInstances.clear();
    for (int i = 0; i < botCount; ++i) {
        BotInstance bot = loadBotInstance(settings, i);
        if (!bot.name.isEmpty()) {
            botInstances.append(bot);
        }
    }
    settings.endGroup();

    // Restore window state
    settings.beginGroup("Window");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    settings.endGroup();
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
    bot.restartThreshold = settings.value("restartThreshold", 48).toInt();
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
    if (isSequentialLaunching) {
        logManager->log("Sequential launch already in progress", LogManager::Warning);
        return;
    }

    pendingLaunchQueue.clear();

    for (const BotInstance &bot : std::as_const(botInstances)) {
        if (bot.status != BotStatus::Online && bot.status != BotStatus::Starting) {
            pendingLaunchQueue.append(bot.name);
        }
    }

    if (pendingLaunchQueue.isEmpty()) {
        logManager->log("All bots are already online", LogManager::Info);
        return;
    }

    isSequentialLaunching = true;
    logManager->log(QString("Starting sequential launch of %1 bots...").arg(pendingLaunchQueue.size()), LogManager::Info);
    launchNextBotInQueue();
}

void ManagerMainWindow::stopAllBots()
{
    int stoppedCount = 0;

    logManager->log("Stopping all bots...", LogManager::Info);

    for (BotInstance &bot : botInstances) {
        if (bot.status == BotStatus::Online || bot.status == BotStatus::Starting) {
            logManager->log(QString("Stopping bot '%1'").arg(bot.name), LogManager::Info);

            // Terminate the process if it exists
            if (bot.process != nullptr) {
                bot.process->terminate();
                // Process cleanup will happen in the finished signal handler
            } else {
                // If no process, just update status
                bot.status = BotStatus::Offline;
                bot.currentMemory = 0;
                bot.position = QVector3D(0, 0, 0);
                bot.dimension = "";
            }

            stoppedCount++;
        }
    }

    // Log results
    if (stoppedCount > 0) {
        logManager->log(QString("Marked %1 bot(s) as offline").arg(stoppedCount), LogManager::Success);
        logManager->log("Note: Minecraft processes may still be running - close them manually if needed", LogManager::Warning);
    } else {
        logManager->log("No bots were online to stop", LogManager::Info);
    }
}


void ManagerMainWindow::onClearLog()
{
    // Clear the currently visible tab
    if (ui->logTabWidget->currentIndex() == 0) {
        logManager->clearManagerLog();
    } else {
        logManager->clearPrismLog();
    }
}

void ManagerMainWindow::onAutoScrollToggled(bool checked)
{
    logManager->setAutoScroll(checked);
}

void ManagerMainWindow::launchNextBotInQueue()
{
    if (pendingLaunchQueue.isEmpty()) {
        isSequentialLaunching = false;
        logManager->log("Sequential launch complete: All bots processed", LogManager::Success);
        return;
    }

    QString botName = pendingLaunchQueue.takeFirst();
    logManager->log(QString("Launching bot '%1'... (waiting for online status before next launch)").arg(botName), LogManager::Info);

    if (!launchBotByName(botName)) {
        logManager->log(QString("Failed to launch bot '%1' - check instance and account configuration").arg(botName), LogManager::Error);
        // Continue with next bot even if this one failed
        QTimer::singleShot(1000, this, &ManagerMainWindow::launchNextBotInQueue);
    }
    // If launch succeeds, onBotStatusChanged will be called when the bot comes online
}

void ManagerMainWindow::onBotStatusChanged()
{
    if (!isSequentialLaunching) {
        return;
    }

    // If we have more bots to launch, continue with the next one
    if (!pendingLaunchQueue.isEmpty()) {
        // Wait a bit before launching the next bot
        QTimer::singleShot(3000, this, &ManagerMainWindow::launchNextBotInQueue);
    } else {
        // No more bots to launch
        isSequentialLaunching = false;
        logManager->log("Sequential launch complete: All queued bots have been launched", LogManager::Success);
    }
}

QString ManagerMainWindow::statusToString(BotStatus status)
{
    switch (status) {
        case BotStatus::Offline: return "Offline";
        case BotStatus::Starting: return "Starting";  
        case BotStatus::Online: return "Online";
        case BotStatus::Stopping: return "Stopping";
    }
    return "Unknown";
}
