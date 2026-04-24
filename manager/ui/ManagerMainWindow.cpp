#include "ManagerMainWindow.h"
#include "BotConsoleWidget.h"
#include "MeteorModulesWidget.h"
#include "BaritoneWidget.h"
#include "ScriptsWidget.h"
#include "PrismSettingsDialog.h"
#include "GlobalSettingsDialog.h"
#include "NetworkStatsWidget.h"
#include "logging/LogManager.h"
#include "prism/PrismLauncherManager.h"
#include "network/PipeServer.h"
#include "scripting/ScriptEngine.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QSettings>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QTextCursor>
#include <QRegularExpression>
#include <QActionGroup>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QLocalSocket>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <QGuiApplication>
#include <QClipboard>

// Initialize static member
QString ManagerMainWindow::worldSaveBasePath = "worldSaves";

ManagerMainWindow::ManagerMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ManagerMainWindow)
    , loadingConfiguration(false)
    , detailsPinned(false)
{
    ui->setupUi(this);

    LogManager::setManagerLogWidget(ui->managerLogTextEdit);
    LogManager::setPrismLogWidget(ui->prismLogTextEdit);

    {
        QSettings settings("MCBotManager", "MCBotManager");
        if (settings.value("Logging/enabled", true).toBool()) {
            QString logDir = settings.value("Logging/logDir",
                QCoreApplication::applicationDirPath() + "/logs").toString();
            int maxSizeMiB = settings.value("Logging/maxSizeMiB", 10).toInt();
            int maxFiles = settings.value("Logging/maxFiles", 0).toInt();
            LogManager::initFileSink(logDir, (qint64)maxSizeMiB * 1024 * 1024, maxFiles);
        }
    }

    PrismLauncherManager::setPrismConfig(&prismConfig);

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::accountRefreshStarted,
            this,
            [this](const QString &accountName) {
                m_refreshingAccounts.insert(accountName);
                updateStatusDisplay();
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::accountRefreshSucceeded,
            this,
            &ManagerMainWindow::onAccountRefreshSucceeded);

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::accountRefreshFailed,
            this,
            [this](const QString &accountName) {
                m_refreshingAccounts.remove(accountName);
                updateStatusDisplay();
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::prismGUIStarted,
            this,
            [this]() { updateStatusDisplay(); });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::prismGUIStopped,
            this,
            [this]() { updateStatusDisplay(); });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::hookAvailabilityChanged,
            this,
            [this](bool) { updateStatusDisplay(); });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::instancesUpdated,
            this,
            [this](const QVector<PrismInstanceInfo> &instances) {
                prismConfig.instances.clear();
                for (const PrismInstanceInfo &info : instances)
                    prismConfig.instances.append(info.id);
                prismConfig.instances.sort();
                updateInstanceComboBox();
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::accountsUpdated,
            this,
            [this](const QVector<PrismAccountInfo> &accounts) {
                prismConfig.accountIdToNameMap.clear();
                for (const PrismAccountInfo &info : accounts)
                    prismConfig.accountIdToNameMap.insert(info.uuid, info.name);
                prismConfig.accounts = prismConfig.accountIdToNameMap.values();
                prismConfig.accounts.sort();
                updateAccountComboBox();
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::minecraftLaunching,
            this,
            [](const QString &botName) {
                if (!botName.isEmpty()) {
                    LogManager::log(QString("Minecraft process started for bot '%1'").arg(botName), LogManager::Success);
                }
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::minecraftStarting,
            this,
            [](const QString &botName) {
                if (!botName.isEmpty()) {
                    LogManager::log(QString("Profile locked for bot '%1'").arg(botName), LogManager::Info);
                }
            });

    connect(&PrismLauncherManager::instance(),
            &PrismLauncherManager::minecraftStopped,
            this,
            [this](const QString &botName) {
                if (!botName.isEmpty()) {
                    BotInstance *bot = BotManager::getBotByName(botName);
                    if (bot) {
                        if (bot->status == BotStatus::Starting) {
                            LogManager::log(QString("[%1] Crashed during startup").arg(botName), LogManager::Error);
                            bot->status = BotStatus::Error;
                            updateInstancesTable();
                        } else if (bot->status == BotStatus::Online) {
                            LogManager::log(QString("[%1] Stopped unexpectedly").arg(botName), LogManager::Warning);
                            bot->status = BotStatus::Offline;
                            bot->connectionId = -1;
                            bot->minecraftPid = 0;
                            updateInstancesTable();
                        }
                    }
                }
            });

    setupUI();
    loadSettings();
}

ManagerMainWindow::~ManagerMainWindow()
{
    PipeServer::stop();
    delete ui;
}

QString ManagerMainWindow::getWorldSaveBasePath()
{
    return worldSaveBasePath;
}

void ManagerMainWindow::setWorldSaveBasePath(const QString &path)
{
    worldSaveBasePath = QDir(path.isEmpty() ? "worldSaves" : path).absolutePath();
}

void ManagerMainWindow::closeEvent(QCloseEvent *event)
{
    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (bot.scriptEngine)
            bot.scriptEngine->stopAllScripts();
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

    ui->instancesTableWidget->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->instancesTableWidget->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &ManagerMainWindow::onHeaderContextMenu);

    connect(ui->launchBotButton, &QPushButton::clicked, this, &ManagerMainWindow::launchBot);
    connect(ui->stopBotButton, &QPushButton::clicked, this, &ManagerMainWindow::stopBot);
    connect(ui->restartBotButton, &QPushButton::clicked, this, &ManagerMainWindow::restartBot);
    connect(ui->refreshTokenButton, &QPushButton::clicked, this, &ManagerMainWindow::refreshToken);

    connect(ui->actionLaunchPrism, &QAction::triggered, this, &ManagerMainWindow::launchPrismLauncher);
    connect(ui->actionPrismSettings, &QAction::triggered, this, &ManagerMainWindow::configurePrismLauncher);
    connect(ui->actionWorldSavePath, &QAction::triggered, this, &ManagerMainWindow::configureWorldSavePath);
    connect(ui->actionGlobalSettings, &QAction::triggered, this, &ManagerMainWindow::openGlobalSettings);
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
    connect(ui->autoConnectCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->autoRestartCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->tokenRefreshCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->debugModeCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->saveWorldToDiskCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->saveBlockEntitiesCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->saveEntitiesCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->saveItemEntitiesCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->savePlayerDataCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->proxyEnabledCheckBox, &QCheckBox::toggled, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->proxyEnabledCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked || selectedBotName.isEmpty()) return;
        BotInstance *bot = BotManager::getBotByName(selectedBotName);
        if (bot && bot->connectionId > 0)
            BotManager::sendProxyConfig(bot->name);
    });
    connect(ui->proxyTypeComboBox, &QComboBox::currentTextChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->proxyTestButton, &QPushButton::clicked, this, &ManagerMainWindow::onTestProxyClicked);

    auto resetProxyTest = [this]() {
        m_proxyTestPassed = false;
        ui->proxyEnabledCheckBox->setChecked(false);
        ui->proxyEnabledCheckBox->setEnabled(false);
        ui->proxyStatusLabel->clear();
    };
    connect(ui->proxyTypeComboBox, &QComboBox::currentTextChanged, this, resetProxyTest);
    connect(ui->proxyPortSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, resetProxyTest);
    connect(ui->proxyUsernameLineEdit, &QLineEdit::textChanged, this, resetProxyTest);
    connect(ui->proxyPasswordLineEdit, &QLineEdit::textChanged, this, resetProxyTest);

    connect(ui->proxyHostLineEdit, &QLineEdit::textChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->proxyPortSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->proxyUsernameLineEdit, &QLineEdit::textChanged, this, &ManagerMainWindow::onConfigurationChanged);
    connect(ui->proxyPasswordLineEdit, &QLineEdit::textChanged, this, &ManagerMainWindow::onConfigurationChanged);

    auto prevLen = std::make_shared<int>(0);
    auto tryApplyProxy = [this, prevLen](const QString &candidate) -> bool {
        if (!candidate.contains(':')) return false;
        QStringList parts = candidate.trimmed().split(':');
        if (parts.size() < 2 || parts[0].isEmpty()) return false;
        bool portOk = false;
        int port = parts[1].toInt(&portOk);
        if (!portOk || port <= 0 || port > 65535) return false;

        ui->proxyHostLineEdit->blockSignals(true);
        ui->proxyPortSpinBox->blockSignals(true);
        ui->proxyUsernameLineEdit->blockSignals(true);
        ui->proxyPasswordLineEdit->blockSignals(true);

        ui->proxyHostLineEdit->setText(parts[0]);
        ui->proxyPortSpinBox->setValue(port);
        ui->proxyUsernameLineEdit->setText(parts.size() >= 3 ? parts[2] : QString());
        ui->proxyPasswordLineEdit->setText(parts.size() >= 4 ? parts[3] : QString());

        ui->proxyHostLineEdit->blockSignals(false);
        ui->proxyPortSpinBox->blockSignals(false);
        ui->proxyUsernameLineEdit->blockSignals(false);
        ui->proxyPasswordLineEdit->blockSignals(false);

        *prevLen = parts[0].length();
        onConfigurationChanged();
        return true;
    };

    connect(ui->proxyHostLineEdit, &QLineEdit::textChanged, this, [this, prevLen, resetProxyTest, tryApplyProxy](const QString &text) {
        int delta = text.length() - *prevLen;
        *prevLen = text.length();

        resetProxyTest();

        if (qAbs(delta) <= 1) return;

        if (tryApplyProxy(text)) return;

        QString clip = QGuiApplication::clipboard()->text().trimmed();
        if (!clip.isEmpty() && text.contains(clip)) tryApplyProxy(clip);
    });

    auto updateSaveSubSettings = [this](bool enabled) {
        ui->saveBlockEntitiesCheckBox->setVisible(enabled);
        ui->saveEntitiesCheckBox->setVisible(enabled);
        ui->saveItemEntitiesCheckBox->setVisible(enabled);
        ui->savePlayerDataCheckBox->setVisible(enabled);
    };
    connect(ui->saveWorldToDiskCheckBox, &QCheckBox::toggled, this, updateSaveSubSettings);
    updateSaveSubSettings(ui->saveWorldToDiskCheckBox->isChecked());

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
    connect(&BotManager::instance(), &BotManager::proxyDisconnectDetected,
            this, &ManagerMainWindow::onProxyDisconnectDetected);

    launchSchedulerTimer = new QTimer(this);
    connect(launchSchedulerTimer, &QTimer::timeout, this, &ManagerMainWindow::checkScheduledLaunches);

    uptimeCheckTimer = new QTimer(this);
    connect(uptimeCheckTimer, &QTimer::timeout, this, &ManagerMainWindow::checkBotUptimes);
    uptimeCheckTimer->start(60000);

    proxyHealthTimer = new QTimer(this);
    connect(proxyHealthTimer, &QTimer::timeout, this, &ManagerMainWindow::checkProxyHealth);
    proxyHealthTimer->start(60000);

    setupPipeServer();
    setupCodeEditorThemeMenu();

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
            bool canLaunch = (bot.status == BotStatus::Offline || bot.status == BotStatus::Error);
            bool inLaunchQueue = std::any_of(scheduledLaunches.begin(), scheduledLaunches.end(),
                                              [&bot](const ScheduledLaunch &s) { return s.botName == bot.name; });

            contextMenu.addSeparator();

            if (canLaunch && !inLaunchQueue) {
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
        newBot.accountId = "";
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

                {
                    QSettings settings("MCBotManager", "MCBotManager");
                    if (settings.value("Logging/enabled", true).toBool()) {
                        QString logDir = settings.value("Logging/logDir",
                            QCoreApplication::applicationDirPath() + "/logs").toString();
                        int maxSizeMiB = settings.value("Logging/maxSizeMiB", 10).toInt();
                        int maxFiles = settings.value("Logging/maxFiles", 0).toInt();
                        bot->consoleWidget->attachLogFile(logDir, bot->name, (qint64)maxSizeMiB * 1024 * 1024, maxFiles);
                    }
                }

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

            if (!bot->scriptEngine) {
                bot->scriptEngine = new ScriptEngine(bot, this);
                bot->scriptEngine->loadScriptsFromDisk();
            }

            if (!bot->scriptsWidget) {
                bot->scriptsWidget = new ScriptsWidget(bot->scriptEngine, this);
                bot->scriptsWidget->hide();

                QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->scriptsTab->layout());
                if (layout) {
                    layout->addWidget(bot->scriptsWidget);
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
            statusColor = QColor(76, 175, 80); // green
        } else if (bot.status == BotStatus::Offline) {
            statusColor = QColor(158, 158, 158); // gray
        } else if (bot.status == BotStatus::Error) {
            statusColor = QColor(244, 67, 54); // red
        } else {
            statusColor = QColor(255, 152, 0); // orange
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

        // Screen column (7)
        QTableWidgetItem *screenItem = ui->instancesTableWidget->item(i, 7);
        if (!screenItem) {
            screenItem = new QTableWidgetItem();
            screenItem->setFlags(screenItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 7, screenItem);
        }
        QString screenText;
        if (bot.status != BotStatus::Online) {
            screenText = "-";
        } else if (bot.screenState.screenClass.isEmpty()) {
            screenText = "Game";
        } else {
            screenText = bot.screenState.screenClass;
        }
        screenItem->setText(screenText);

        // PID column (8)
        QTableWidgetItem *pidItem = ui->instancesTableWidget->item(i, 8);
        if (!pidItem) {
            pidItem = new QTableWidgetItem();
            pidItem->setFlags(pidItem->flags() & ~Qt::ItemIsEditable);
            ui->instancesTableWidget->setItem(i, 8, pidItem);
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

void ManagerMainWindow::onHeaderContextMenu(const QPoint &pos)
{
    // Columns 0 (Name) and 1 (Status) are always visible
    static const QList<QPair<int, QString>> toggleableColumns = {
        {2, "Instance"},
        {3, "Server"},
        {4, "Memory"},
        {5, "Position"},
        {6, "Dimension"},
        {7, "Screen"},
        {8, "PID"},
    };

    QMenu menu(this);
    menu.setTitle("Columns");

    QHeaderView *header = ui->instancesTableWidget->horizontalHeader();
    for (const auto &col : toggleableColumns) {
        QAction *action = menu.addAction(col.second);
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(col.first));
        connect(action, &QAction::toggled, this, [this, col](bool checked) {
            ui->instancesTableWidget->setColumnHidden(col.first, !checked);
            saveColumnVisibility();
        });
    }

    menu.exec(header->mapToGlobal(pos));
}

void ManagerMainWindow::saveColumnVisibility()
{
    QSettings settings("MCBotManager", "MCBotManager");
    settings.beginGroup("Window/ColumnVisibility");
    QHeaderView *header = ui->instancesTableWidget->horizontalHeader();
    settings.setValue("Instance", !header->isSectionHidden(2));
    settings.setValue("Server", !header->isSectionHidden(3));
    settings.setValue("Memory", !header->isSectionHidden(4));
    settings.setValue("Position", !header->isSectionHidden(5));
    settings.setValue("Dimension", !header->isSectionHidden(6));
    settings.setValue("Screen", !header->isSectionHidden(7));
    settings.setValue("PID", !header->isSectionHidden(8));
    settings.endGroup();
}

void ManagerMainWindow::loadColumnVisibility()
{
    QSettings settings("MCBotManager", "MCBotManager");
    settings.beginGroup("Window/ColumnVisibility");
    ui->instancesTableWidget->setColumnHidden(2, !settings.value("Instance", true).toBool());
    ui->instancesTableWidget->setColumnHidden(3, !settings.value("Server", true).toBool());
    ui->instancesTableWidget->setColumnHidden(4, !settings.value("Memory", true).toBool());
    ui->instancesTableWidget->setColumnHidden(5, !settings.value("Position", true).toBool());
    ui->instancesTableWidget->setColumnHidden(6, !settings.value("Dimension", true).toBool());
    ui->instancesTableWidget->setColumnHidden(7, !settings.value("Screen", true).toBool());
    ui->instancesTableWidget->setColumnHidden(8, !settings.value("PID", true).toBool());
    settings.endGroup();
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
        if (bot.scriptsWidget) {
            bot.scriptsWidget->hide();
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
        if (bot.scriptsWidget) {
            bot.scriptsWidget->show();
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
            QString selectedAccount = ui->accountComboBox->currentText() == "(None)" ? "" : ui->accountComboBox->currentText();
            bot->account = selectedAccount;
            bot->accountId = selectedAccount.isEmpty() ? "" : prismConfig.accountIdToNameMap.key(selectedAccount, "");
            bot->server = ui->serverLineEdit->text();
            bot->maxMemory = ui->memorySpinBox->value();
            bot->restartThreshold = ui->restartThresholdSpinBox->value();
            bot->autoConnect = ui->autoConnectCheckBox->isChecked();
            bot->autoRestart = ui->autoRestartCheckBox->isChecked();
            bot->tokenRefresh = ui->tokenRefreshCheckBox->isChecked();
            bot->debugLogging = ui->debugModeCheckBox->isChecked();
            bot->saveWorldToDisk = ui->saveWorldToDiskCheckBox->isChecked();
            bot->worldSaveSettings.saveBlockEntities = ui->saveBlockEntitiesCheckBox->isChecked();
            bot->worldSaveSettings.saveEntities = ui->saveEntitiesCheckBox->isChecked();
            bot->worldSaveSettings.saveItemEntities = ui->saveItemEntitiesCheckBox->isChecked();
            bot->worldSaveSettings.savePlayerData = ui->savePlayerDataCheckBox->isChecked();
            bot->proxySettings.enabled = ui->proxyEnabledCheckBox->isChecked();
            bot->proxySettings.type = ui->proxyTypeComboBox->currentText();
            bot->proxySettings.host = ui->proxyHostLineEdit->text().trimmed();
            bot->proxySettings.port = ui->proxyPortSpinBox->value();
            bot->proxySettings.username = ui->proxyUsernameLineEdit->text();
            bot->proxySettings.password = ui->proxyPasswordLineEdit->text();
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
    ui->autoConnectCheckBox->setChecked(bot.autoConnect);
    ui->autoRestartCheckBox->setChecked(bot.autoRestart);
    ui->tokenRefreshCheckBox->setChecked(bot.tokenRefresh);
    ui->debugModeCheckBox->setChecked(bot.debugLogging);
    ui->saveWorldToDiskCheckBox->setChecked(bot.saveWorldToDisk);
    ui->saveBlockEntitiesCheckBox->setChecked(bot.worldSaveSettings.saveBlockEntities);
    ui->saveEntitiesCheckBox->setChecked(bot.worldSaveSettings.saveEntities);
    ui->saveItemEntitiesCheckBox->setChecked(bot.worldSaveSettings.saveItemEntities);
    ui->savePlayerDataCheckBox->setChecked(bot.worldSaveSettings.savePlayerData);

    ui->proxyTypeComboBox->setCurrentText(bot.proxySettings.type.isEmpty() ? "SOCKS5" : bot.proxySettings.type);
    ui->proxyHostLineEdit->setText(bot.proxySettings.host);
    ui->proxyPortSpinBox->setValue(bot.proxySettings.port);
    ui->proxyUsernameLineEdit->setText(bot.proxySettings.username);
    ui->proxyPasswordLineEdit->setText(bot.proxySettings.password);
    if (bot.proxySettings.enabled && !bot.proxySettings.host.isEmpty()) {
        m_proxyTestPassed = true;
        m_proxyTestedHost = bot.proxySettings.host;
        m_proxyTestedPort = bot.proxySettings.port;
        ui->proxyEnabledCheckBox->setEnabled(true);
        ui->proxyEnabledCheckBox->setChecked(true);
        ui->proxyStatusLabel->setText("Previously enabled - click Test to re-verify");
        ui->proxyStatusLabel->setStyleSheet("color: gray;");
    } else {
        m_proxyTestPassed = false;
        ui->proxyEnabledCheckBox->setChecked(false);
        ui->proxyEnabledCheckBox->setEnabled(!bot.proxySettings.host.isEmpty() && m_proxyTestPassed);
        ui->proxyStatusLabel->clear();
        ui->proxyStatusLabel->setStyleSheet("");
    }

    // Clear flag to allow user changes to sync to memory
    loadingConfiguration = false;
}

static QPair<bool, qint64> testSocksHandshake(const QString &host, int port, bool isSocks4)
{
    QTcpSocket socket;
    QElapsedTimer timer;
    timer.start();

    socket.connectToHost(host, port);
    if (!socket.waitForConnected(5000)) return {false, -1};

    if (isSocks4) {
        // SOCKS4 CONNECT request to 0.0.0.1:80 (expect reject or grant, both confirm SOCKS4)
        QByteArray req("\x04\x01\x00\x50\x00\x00\x00\x01\x00", 9);
        socket.write(req);
        if (!socket.waitForReadyRead(5000)) return {false, -1};
        QByteArray resp = socket.read(8);
        socket.disconnectFromHost();
        qint64 ms = timer.elapsed();
        if (resp.size() < 2 || static_cast<quint8>(resp[0]) != 0x00) return {false, ms};
        quint8 status = static_cast<quint8>(resp[1]);
        // 0x5A = granted, 0x5B = rejected - both are valid SOCKS4 responses
        if (status != 0x5A && status != 0x5B) return {false, ms};
        return {true, ms};
    } else {
        // SOCKS5 greeting: version=5, 2 methods (no-auth=0x00, user/pass=0x02)
        QByteArray greeting("\x05\x02\x00\x02", 4);
        socket.write(greeting);
        if (!socket.waitForReadyRead(5000)) return {false, -1};
        QByteArray resp = socket.read(2);
        socket.disconnectFromHost();
        qint64 ms = timer.elapsed();
        if (resp.size() < 2 || static_cast<quint8>(resp[0]) != 0x05) return {false, ms};
        if (static_cast<quint8>(resp[1]) != 0x00 && static_cast<quint8>(resp[1]) != 0x02) return {false, ms};
        return {true, ms};
    }
}

void ManagerMainWindow::onTestProxyClicked()
{
    QString host = ui->proxyHostLineEdit->text().trimmed();
    int port = ui->proxyPortSpinBox->value();
    if (host.isEmpty() || port == 0) return;

    bool isSocks4 = (ui->proxyTypeComboBox->currentText() == "SOCKS4");

    ui->proxyTestButton->setEnabled(false);
    ui->proxyStatusLabel->setText("Testing...");
    ui->proxyStatusLabel->setStyleSheet("");

    QFuture<QPair<bool, qint64>> future = QtConcurrent::run([host, port, isSocks4]() -> QPair<bool, qint64> {
        return testSocksHandshake(host, port, isSocks4);
    });

    auto* watcher = new QFutureWatcher<QPair<bool, qint64>>(this);
    connect(watcher, &QFutureWatcher<QPair<bool, qint64>>::finished, this, [this, watcher, host, port]() {
        auto result = watcher->result();
        bool ok = result.first;
        qint64 ms = result.second;

        ui->proxyTestButton->setEnabled(true);
        if (ok) {
            ui->proxyStatusLabel->setText(QString("OK (%1ms)").arg(ms));
            ui->proxyStatusLabel->setStyleSheet("color: green;");
            m_proxyTestPassed = true;
            m_proxyTestedHost = host;
            m_proxyTestedPort = port;
            ui->proxyEnabledCheckBox->setEnabled(true);
        } else {
            ui->proxyStatusLabel->setText(ms < 0 ? "Failed: no connection" : "Failed: not SOCKS5");
            ui->proxyStatusLabel->setStyleSheet("color: red;");
            m_proxyTestPassed = false;
            ui->proxyEnabledCheckBox->setChecked(false);
            ui->proxyEnabledCheckBox->setEnabled(false);
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
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
        bool isActive = (selectedBot->status == BotStatus::Online || selectedBot->status == BotStatus::Starting);
        bool canLaunch = (selectedBot->status == BotStatus::Offline || selectedBot->status == BotStatus::Error);
        bool inLaunchQueue = std::any_of(scheduledLaunches.begin(), scheduledLaunches.end(),
                                          [this](const ScheduledLaunch &s) { return s.botName == selectedBotName; });

        ui->launchBotButton->setEnabled(canLaunch && !inLaunchQueue);
        ui->stopBotButton->setEnabled(isOnline);
        ui->restartBotButton->setEnabled(isOnline);
        ui->refreshTokenButton->setEnabled(canLaunch && !selectedBot->account.isEmpty()
                                           && PrismLauncherManager::isHookAvailable()
                                           && !m_refreshingAccounts.contains(selectedBot->account));
        ui->instanceComboBox->setEnabled(!isActive);
        ui->accountComboBox->setEnabled(!isActive);
        ui->serverLineEdit->setEnabled(!isActive);
        ui->memorySpinBox->setEnabled(!isActive && !PrismLauncherManager::isPrismGUIRunning());
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
        LogManager::log(QString("[%1] Has no instance configured").arg(botName), LogManager::Error);
        return false;
    }

    if (botToLaunch->account.isEmpty()) {
        LogManager::log(QString("[%1] Has no account configured").arg(botName), LogManager::Error);
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

    m_lastBotLaunchTime[botName] = QDateTime::currentDateTime();
    PrismLauncherManager::launchBot(botToLaunch);

    // Startup timeout: if the bot hasn't connected within 2 minutes, mark as Error
    QTimer::singleShot(120000, this, [this, botName]() {
        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot && bot->status == BotStatus::Starting) {
            LogManager::log(QString("[%1] Startup timed out (no connection after 2 minutes)").arg(botName), LogManager::Error);
            bot->status = BotStatus::Error;
            updateInstancesTable();
            updateStatusDisplay();
        }
    });

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
            QTimer::singleShot(5000, this, [botName = bot->name, pid = bot->minecraftPid]() {
                BotInstance *b = BotManager::getBotByName(botName);
                // Only kill if bot is still offline AND the PID hasn't changed (no restart)
                if (b && b->status != BotStatus::Offline && b->minecraftPid == pid && pid > 0) {
                    LogManager::log(QString("[%1] Didn't shut down gracefully, force killing...").arg(botName), LogManager::Warning);
                    PrismLauncherManager::stopBot(pid);
                }
            });

            bot->minecraftPid = 0;
        } else if (bot->status == BotStatus::Starting) {
            LogManager::log(QString("[%1] Is still starting, cannot stop yet").arg(bot->name), LogManager::Warning);
        } else {
            LogManager::log(QString("[%1] Is not running").arg(bot->name), LogManager::Warning);
        }
    }
}

void ManagerMainWindow::restartBot()
{
    if (!selectedBotName.isEmpty()) {
        restartBotByName(selectedBotName, "Manual restart");
    }
}

void ManagerMainWindow::refreshToken()
{
    if (selectedBotName.isEmpty()) return;
    BotInstance *bot = BotManager::getBotByName(selectedBotName);
    if (!bot || bot->account.isEmpty()) return;

    const QString account = bot->account;
    const QString botName = selectedBotName;

    sendHookRefresh(account, botName, [this, account, botName](bool success) {
        if (success)
            m_lastAccountRefreshTime[account] = QDateTime::currentDateTime();
    });
}

void ManagerMainWindow::sendHookRefresh(const QString &account, const QString &botName,
                                        std::function<void(bool)> onDone)
{
    m_refreshingAccounts.insert(account);
    updateStatusDisplay();

    auto* socket = new QLocalSocket(this);
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    auto finish = [this, socket, timer, account, botName, onDone = std::move(onDone)](bool success) mutable {
        timer->stop();
        socket->disconnect();
        socket->deleteLater();
        timer->deleteLater();
        m_refreshingAccounts.remove(account);
        updateStatusDisplay();
        onDone(success);
    };

    connect(timer, &QTimer::timeout, this, [finish, botName]() mutable {
        LogManager::log(QString("[%1] Hook refresh timed out").arg(botName), LogManager::Warning);
        finish(false);
    });

    connect(socket, &QLocalSocket::connected, socket, [socket, account]() {
        socket->write(("refresh:" + account + "\n").toUtf8());
    });

    connect(socket, &QLocalSocket::readyRead, socket, [socket, botName, finish]() mutable {
        while (socket->canReadLine()) {
            QString line = QString::fromUtf8(socket->readLine()).trimmed();
            if (line == "ok") {
                LogManager::log(QString("[%1] Token refreshed").arg(botName), LogManager::Info);
                finish(true);
                return;
            } else if (line.startsWith("error:")) {
                LogManager::log(QString("[%1] Hook refresh error: %2").arg(botName, line.mid(6)),
                                LogManager::Warning);
                finish(false);
                return;
            }
            // "refreshing" is just ack - keep waiting
        }
    });

    connect(socket, &QLocalSocket::errorOccurred, socket,
            [botName, finish](QLocalSocket::LocalSocketError) mutable {
        LogManager::log(QString("[%1] Hook connection lost during refresh").arg(botName),
                        LogManager::Warning);
        finish(false);
    });

    socket->connectToServer(PrismLauncherManager::hookSocketPath());
    timer->start(120000);
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
            QString selectedAccount = ui->accountComboBox->currentText() == "(None)" ? "" : ui->accountComboBox->currentText();
            bot->account = selectedAccount;
            bot->accountId = selectedAccount.isEmpty() ? "" : prismConfig.accountIdToNameMap.key(selectedAccount, "");
            bot->server = ui->serverLineEdit->text();
            bot->maxMemory = ui->memorySpinBox->value();
            bot->restartThreshold = ui->restartThresholdSpinBox->value();
            bot->autoConnect = ui->autoConnectCheckBox->isChecked();
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
    QString current = ui->instanceComboBox->currentText();
    ui->instanceComboBox->blockSignals(true);

    ui->instanceComboBox->clear();
    ui->instanceComboBox->addItem("(None)");

    if (!prismConfig.instances.isEmpty()) {
        QStringList usedInstances = getUsedInstances();
        QString currentBotInstance;

        if (!selectedBotName.isEmpty()) {
            BotInstance *bot = BotManager::getBotByName(selectedBotName);
            if (bot)
                currentBotInstance = bot->instance;
        }

        for (const QString &instance : std::as_const(prismConfig.instances)) {
            if (!usedInstances.contains(instance) || instance == currentBotInstance)
                ui->instanceComboBox->addItem(instance);
        }
    }

    int idx = ui->instanceComboBox->findText(current);
    ui->instanceComboBox->setCurrentIndex(idx != -1 ? idx : 0);
    ui->instanceComboBox->blockSignals(false);
}

void ManagerMainWindow::updateAccountComboBox()
{
    QString current = ui->accountComboBox->currentText();
    ui->accountComboBox->blockSignals(true);

    ui->accountComboBox->clear();
    ui->accountComboBox->addItem("(None)");

    if (!prismConfig.accounts.isEmpty())
        ui->accountComboBox->addItems(prismConfig.accounts);

    int idx = ui->accountComboBox->findText(current);
    ui->accountComboBox->setCurrentIndex(idx != -1 ? idx : 0);
    ui->accountComboBox->blockSignals(false);
}

void ManagerMainWindow::openGlobalSettings()
{
    GlobalSettingsDialog dialog(this);
    dialog.exec();
}

void ManagerMainWindow::launchPrismLauncher()
{
    PrismLauncherManager::openPrismGUI();
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
        dialog.setAccountIdToNameMap(prismConfig.accountIdToNameMap);
    }

    dialog.setUseHook(prismConfig.useHook);

    if (dialog.exec() == QDialog::Accepted) {
        QString newPath = dialog.getCurrentPath();
        QString newExecutable = dialog.getExecutable();

        if (!newPath.isEmpty() && newPath != prismConfig.prismPath) {
            prismConfig.prismPath = newPath;
            prismConfig.instances = dialog.getInstances();
            prismConfig.accounts = dialog.getAccounts();
            prismConfig.accountIdToNameMap = dialog.getAccountIdToNameMap();

            updateInstanceComboBox();
            updateAccountComboBox();
        }

        // Update executable even if path didn't change
        prismConfig.prismExecutable = newExecutable;
        prismConfig.useHook = dialog.getUseHook();
    }
}

void ManagerMainWindow::configureWorldSavePath()
{
    QString currentPath = worldSaveBasePath;
    if (currentPath == "worldSaves") {
        currentPath = QDir::currentPath() + "/worldSaves";
    }

    QString newPath = QFileDialog::getExistingDirectory(
        this,
        "Select World Save Directory",
        currentPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!newPath.isEmpty() && newPath != currentPath) {
        setWorldSaveBasePath(newPath);
        LogManager::log(QString("World save path changed to: %1").arg(worldSaveBasePath), LogManager::Success);
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
    settings.setValue("useHook", prismConfig.useHook);
    settings.endGroup();

    // Save world save path
    settings.beginGroup("World");
    settings.setValue("savePath", worldSaveBasePath);
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

    saveColumnVisibility();

    LogManager::log(QString("Configuration saved successfully (%1 bots)").arg(bots.size()), LogManager::Success);
}

void ManagerMainWindow::loadSettings()
{
    QSettings settings("MCBotManager", "MCBotManager");

    // Load PrismLauncher configuration
    settings.beginGroup("PrismLauncher");
    prismConfig.prismPath = settings.value("path", "").toString();
    prismConfig.prismExecutable = settings.value("executable", "").toString();
    prismConfig.useHook = settings.value("useHook", true).toBool();
    settings.endGroup();

    // Load world save path
    settings.beginGroup("World");
    worldSaveBasePath = QDir(settings.value("savePath", "worldSaves").toString()).absolutePath();
    settings.endGroup();

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

    // Parse fresh Prism data and update loaded bots
    if (!prismConfig.prismPath.isEmpty()) {
        prismConfig.instances = PrismSettingsDialog::parsePrismInstances(prismConfig.prismPath);
        prismConfig.accountIdToNameMap = PrismSettingsDialog::parsePrismAccounts(prismConfig.prismPath);
        prismConfig.accounts = prismConfig.accountIdToNameMap.values();
        prismConfig.accounts.sort();

        QVector<BotInstance> &bots = BotManager::getBots();
        for (BotInstance &bot : bots) {
            if (!bot.account.isEmpty()) {
                QString foundId = prismConfig.accountIdToNameMap.key(bot.account, "");
                if (!foundId.isEmpty()) {
                    bot.accountId = foundId;
                } else {
                    LogManager::log(QString("Account '%1' no longer exists for bot '%2', clearing")
                                        .arg(bot.account, bot.name), LogManager::Warning);
                    bot.account = "";
                    bot.accountId = "";
                }
            }

            if (!bot.instance.isEmpty() && !prismConfig.instances.contains(bot.instance)) {
                LogManager::log(QString("Instance '%1' no longer exists for bot '%2', clearing")
                                    .arg(bot.instance, bot.name), LogManager::Warning);
                bot.instance = "";
            }
        }
    }

    setupConsoleTab();
    setupMeteorTab();
    setupBaritoneTab();
    setupScriptsTab();

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

    loadColumnVisibility();
}

void ManagerMainWindow::saveBotInstance(QSettings &settings, const BotInstance &bot, int index)
{
    settings.beginGroup(QString("Bot_%1").arg(index));

    settings.setValue("name", bot.name);
    settings.setValue("instance", bot.instance);
    settings.setValue("account", bot.account);
    settings.setValue("accountId", bot.accountId);
    settings.setValue("server", bot.server);
    settings.setValue("maxMemory", bot.maxMemory);
    settings.setValue("restartThreshold", bot.restartThreshold);
    settings.setValue("autoConnect", bot.autoConnect);
    settings.setValue("autoRestart", bot.autoRestart);
    settings.setValue("tokenRefresh", bot.tokenRefresh);
    settings.setValue("debugLogging", bot.debugLogging);
    settings.setValue("saveWorldToDisk", bot.saveWorldToDisk);
    settings.setValue("saveBlockEntities", bot.worldSaveSettings.saveBlockEntities);
    settings.setValue("saveEntities", bot.worldSaveSettings.saveEntities);
    settings.setValue("saveItemEntities", bot.worldSaveSettings.saveItemEntities);
    settings.setValue("savePlayerData", bot.worldSaveSettings.savePlayerData);

    settings.setValue("proxyEnabled", bot.proxySettings.enabled);
    settings.setValue("proxyType", bot.proxySettings.type);
    settings.setValue("proxyHost", bot.proxySettings.host);
    settings.setValue("proxyPort", bot.proxySettings.port);
    settings.setValue("proxyUsername", bot.proxySettings.username);
    settings.setValue("proxyPassword", bot.proxySettings.password);

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
    bot.accountId = settings.value("accountId", "").toString();
    bot.server = settings.value("server", "").toString();
    bot.connectionId = -1;
    bot.maxMemory = settings.value("maxMemory", 4096).toInt();
    bot.currentMemory = 0;
    bot.restartThreshold = settings.value("restartThreshold", 48.0).toDouble();
    bot.autoConnect = settings.value("autoConnect", true).toBool();
    bot.autoRestart = settings.value("autoRestart", true).toBool();
    bot.tokenRefresh = settings.value("tokenRefresh", true).toBool();
    bot.debugLogging = settings.value("debugLogging", false).toBool();
    bot.saveWorldToDisk = settings.value("saveWorldToDisk", true).toBool();
    bot.worldSaveSettings.saveBlockEntities = settings.value("saveBlockEntities", true).toBool();
    bot.worldSaveSettings.saveEntities = settings.value("saveEntities", true).toBool();
    bot.worldSaveSettings.saveItemEntities = settings.value("saveItemEntities", true).toBool();
    bot.worldSaveSettings.savePlayerData = settings.value("savePlayerData", true).toBool();

    bot.proxySettings.enabled = settings.value("proxyEnabled", false).toBool();
    bot.proxySettings.type = settings.value("proxyType", "SOCKS5").toString();
    bot.proxySettings.host = settings.value("proxyHost", "").toString();
    bot.proxySettings.port = settings.value("proxyPort", 1080).toInt();
    bot.proxySettings.username = settings.value("proxyUsername", "").toString();
    bot.proxySettings.password = settings.value("proxyPassword", "").toString();

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
                QTimer::singleShot(5000, this, [botName, pid]() {
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

void ManagerMainWindow::checkProxyHealth()
{
    using Status = mankool::mcbot::protocol::ServerConnectionStatus_QtProtobufNested::Status;
    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (!bot.proxySettings.enabled || bot.proxySettings.host.isEmpty()) continue;

        // If actively connected to MC server, the working connection proves proxy is alive
        // skip the check to avoid interfering with 1-connection proxies
        if (bot.status == BotStatus::Online && bot.serverConnectionStatus == Status::SUCCESSFUL) {
            if (bot.proxyHealth != BotInstance::ProxyHealth::Alive) {
                BotInstance *b = BotManager::getBotByName(bot.name);
                if (b) b->proxyHealth = BotInstance::ProxyHealth::Alive;
            }
            continue;
        }

        checkBotProxyHealth(bot.name);
    }
}

void ManagerMainWindow::checkBotProxyHealth(const QString &botName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (!bot || !bot->proxySettings.enabled || bot->proxySettings.host.isEmpty()) return;

    QString host = bot->proxySettings.host;
    int port = bot->proxySettings.port;
    bool isSocks4 = (bot->proxySettings.type == "SOCKS4");

    QFuture<bool> future = QtConcurrent::run([host, port, isSocks4]() -> bool {
        return testSocksHandshake(host, port, isSocks4).first;
    });

    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, botName]() {
        bool alive = watcher->result();
        watcher->deleteLater();

        BotInstance *b = BotManager::getBotByName(botName);
        if (!b || !b->proxySettings.enabled) return;

        BotInstance::ProxyHealth prev = b->proxyHealth;

        if (alive) {
            b->proxyHealth = BotInstance::ProxyHealth::Alive;
            if (prev == BotInstance::ProxyHealth::Dead) {
                LogManager::log(QString("[%1] Proxy recovered").arg(botName), LogManager::Info);
                if (b->proxyDisabledAutoReconnect) {
                    BotManager::setMeteorModuleEnabled(botName, "auto-reconnect", true);
                    b->proxyDisabledAutoReconnect = false;
                    LogManager::log(QString("[%1] Re-enabled auto-reconnect module").arg(botName), LogManager::Info);
                }
            }
        } else {
            b->proxyHealth = BotInstance::ProxyHealth::Dead;
            if (prev != BotInstance::ProxyHealth::Dead) {
                LogManager::log(QString("[%1] Proxy is unreachable - blocking connections").arg(botName), LogManager::Error);

                // Check if auto-reconnect is currently enabled for this bot
                bool autoReconnectEnabled = false;
                if (b->meteorModules.contains("auto-reconnect")) {
                    autoReconnectEnabled = b->meteorModules["auto-reconnect"].enabled;
                }
                if (autoReconnectEnabled) {
                    BotManager::setMeteorModuleEnabled(botName, "auto-reconnect", false);
                    b->proxyDisabledAutoReconnect = true;
                    LogManager::log(QString("[%1] Disabled auto-reconnect module due to dead proxy").arg(botName), LogManager::Warning);
                }
            }
        }
    });
    watcher->setFuture(future);
}

void ManagerMainWindow::onProxyDisconnectDetected(const QString &botName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (!bot || !bot->proxySettings.enabled || bot->proxySettings.host.isEmpty()) return;
    LogManager::log(QString("[%1] Abrupt MC connection drop detected - checking proxy health").arg(botName), LogManager::Info);
    checkBotProxyHealth(botName);
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
        BotInstance *bot = BotManager::getBotByConnectionId(connectionId);
        if (bot) {
            updateInstancesTable();
            updateStatusDisplay();

            // Request Meteor modules list
            BotManager::sendCommand(botName, "meteor list", true);

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

        bool tokenRefreshPending = bot->tokenRefreshPending;
        bot->tokenRefreshPending = false;

        if (tokenRefreshPending) {
            BotInstance *refreshBot = BotManager::getBotByName(botName);
            QString accountProfile = refreshBot ? refreshBot->account : QString();
            int attempts = ++m_tokenRefreshAttempts[botName];

            if (attempts == 1) {
                // First attempt: Prism's background scheduler may have already refreshed.
                // Just relaunch - if it connects we're done.
                LogManager::log(
                    QString("[%1] Invalid session - relaunching (Prism may have auto-refreshed)").arg(botName),
                    LogManager::Warning);
                QTimer::singleShot(2000, this, [this, botName]() {
                    m_lastBotLaunchTime[botName] = QDateTime::currentDateTime();
                    launchBotByName(botName);
                });
            } else if (attempts == 2) {
                // Still invalid after relaunch. Force refresh via hook or wait for Prism.
                LogManager::log(
                    QString("[%1] Token still invalid after relaunch - requesting refresh").arg(botName),
                    LogManager::Warning);
                QTimer::singleShot(1000, this, [this, botName, accountProfile]() {
                    refreshAccountThenLaunch(accountProfile, botName);
                });
            } else {
                // Repeated hook-refresh failures - something is fundamentally broken.
                m_tokenRefreshAttempts.remove(botName);
                LogManager::log(
                    QString("[%1] Repeated token refresh failures - leaving bot offline").arg(botName),
                    LogManager::Error);
            }
        } else if (shouldAutoRestart) {
            m_tokenRefreshAttempts.remove(botName);
            LogManager::log(QString("[%1] Crashed, auto-restarting...").arg(botName), LogManager::Warning);
            QTimer::singleShot(2000, this, [this, botName]() {
                launchBotByName(botName);
            });
        }
    }
}

void ManagerMainWindow::refreshAccountThenLaunch(const QString &accountProfile,
                                                  const QString &botName)
{
    // If Prism already refreshed this account after our last launch, no need to force it.
    if (m_lastAccountRefreshTime.contains(accountProfile)
        && m_lastBotLaunchTime.contains(botName)
        && m_lastAccountRefreshTime[accountProfile] > m_lastBotLaunchTime[botName]) {
        LogManager::log(
            QString("[%1] Account already refreshed by Prism since last launch - relaunching").arg(botName),
            LogManager::Info);
        m_tokenRefreshAttempts.remove(botName);
        m_lastBotLaunchTime[botName] = QDateTime::currentDateTime();
        launchBotByName(botName);
        return;
    }

    if (PrismLauncherManager::isHookAvailable()) {
        sendHookRefresh(accountProfile, botName, [this, botName](bool success) {
            if (success)
                m_tokenRefreshAttempts.remove(botName);
            else
                LogManager::log(QString("[%1] Refresh failed, relaunching anyway").arg(botName),
                                LogManager::Warning);
            m_lastBotLaunchTime[botName] = QDateTime::currentDateTime();
            launchBotByName(botName);
        });
    } else {
        // No hook: bot stays offline and waits for Prism's background refresh scheduler.
        // Prism can take up to ~1 hour to trigger a refresh, so no timeout is imposed.
        LogManager::log(
            QString("[%1] Hook unavailable - waiting for Prism to refresh account '%2'")
                .arg(botName, accountProfile),
            LogManager::Warning);

        auto* conn = new QMetaObject::Connection;
        *conn = connect(&PrismLauncherManager::instance(),
                        &PrismLauncherManager::accountRefreshSucceeded,
                        this,
                        [this, botName, accountProfile, conn](const QString &name) {
            if (name != accountProfile) return;
            disconnect(*conn);
            delete conn;
            LogManager::log(
                QString("[%1] Prism refreshed account '%2' - relaunching").arg(botName, accountProfile),
                LogManager::Info);
            m_tokenRefreshAttempts.remove(botName);
            m_lastBotLaunchTime[botName] = QDateTime::currentDateTime();
            launchBotByName(botName);
        });
    }
}

void ManagerMainWindow::onAccountRefreshSucceeded(const QString &accountName)
{
    m_lastAccountRefreshTime[accountName] = QDateTime::currentDateTime();
    m_refreshingAccounts.remove(accountName);
    updateStatusDisplay();
}

void ManagerMainWindow::setupConsoleTab()
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->consoleTab->layout());
    if (!layout) {
        layout = new QVBoxLayout(ui->consoleTab);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    QSettings settings("MCBotManager", "MCBotManager");
    bool loggingEnabled = settings.value("Logging/enabled", true).toBool();
    QString logDir = settings.value("Logging/logDir",
        QCoreApplication::applicationDirPath() + "/logs").toString();
    int maxSizeMiB = settings.value("Logging/maxSizeMiB", 10).toInt();
    int maxFiles = settings.value("Logging/maxFiles", 0).toInt();

    for (BotInstance &bot : bots) {
        if (!bot.consoleWidget) {
            bot.consoleWidget = new BotConsoleWidget(this);
            connect(bot.consoleWidget, &BotConsoleWidget::commandEntered,
                    this, &ManagerMainWindow::onConsoleCommandEntered);
            bot.consoleWidget->hide();
            layout->addWidget(bot.consoleWidget);

            if (loggingEnabled)
                bot.consoleWidget->attachLogFile(logDir, bot.name, (qint64)maxSizeMiB * 1024 * 1024, maxFiles);
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

void ManagerMainWindow::setupScriptsTab()
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->scriptsTab->layout());
    if (!layout) {
        layout = new QVBoxLayout(ui->scriptsTab);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (!bot.scriptEngine) {
            bot.scriptEngine = new ScriptEngine(&bot, this);
            bot.scriptEngine->loadScriptsFromDisk();
        }

        if (!bot.scriptsWidget) {
            bot.scriptsWidget = new ScriptsWidget(bot.scriptEngine, this);
            bot.scriptsWidget->hide();
            layout->addWidget(bot.scriptsWidget);
        }
    }
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
        "<p>Copyright © 2025-2026 mankool</p>"
        "<p>Licensed under the GNU General Public License v3.0</p>"
        "<p>This program comes with ABSOLUTELY NO WARRANTY. "
        "This is free software, and you are welcome to redistribute it "
        "under certain conditions.</p>"
        "<p>Built with Qt %1</p>"
    ).arg(QT_VERSION_STR);

    QMessageBox::about(this, "About MC Bot Manager", aboutText);
}

void ManagerMainWindow::setupCodeEditorThemeMenu()
{
    QMenu *themeMenu = new QMenu("Code Editor Theme", this);
    ui->menuTools->insertMenu(ui->actionNetworkStats, themeMenu);

    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    QAction *followSystemAction = new QAction("Follow System", this);
    followSystemAction->setCheckable(true);
    followSystemAction->setData("Follow System");
    themeGroup->addAction(followSystemAction);
    themeMenu->addAction(followSystemAction);

    themeMenu->addSeparator();

    QSettings settings;
    QString currentTheme = settings.value("editor/theme", "Follow System").toString();

    QStringList themes = ScriptsWidget::getAvailableThemes();
    for (const QString &theme : themes) {
        QAction *themeAction = new QAction(theme, this);
        themeAction->setCheckable(true);
        themeAction->setData(theme);
        themeGroup->addAction(themeAction);
        themeMenu->addAction(themeAction);

        if (theme == currentTheme) {
            themeAction->setChecked(true);
        }
    }

    if (currentTheme == "Follow System") {
        followSystemAction->setChecked(true);
    }

    connect(themeGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        QString themeName = action->data().toString();
        onEditorThemeChanged(themeName);
    });
}

void ManagerMainWindow::onEditorThemeChanged(const QString &themeName)
{
    QSettings settings;
    settings.setValue("editor/theme", themeName);

    LogManager::log(QString("Editor theme changed to: %1").arg(themeName), LogManager::Info);

    QVector<BotInstance> &bots = BotManager::getBots();
    for (BotInstance &bot : bots) {
        if (bot.scriptsWidget) {
            bot.scriptsWidget->reloadTheme();
        }
    }
}
