#include "PrismSettingsDialog.h"
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>

PrismSettingsDialog::PrismSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PrismSettingsDialog)
{
    ui->setupUi(this);

    connect(ui->browseButton, &QPushButton::clicked, this, &PrismSettingsDialog::onBrowseClicked);
    connect(ui->browseExeButton, &QPushButton::clicked, this, &PrismSettingsDialog::onBrowseExeClicked);

    // Set initial labels for lists
    ui->instancesList->addItem("Instances:");
    ui->accountsList->addItem("Accounts:");

    updateStatistics();
}

PrismSettingsDialog::~PrismSettingsDialog()
{
    delete ui;
}

void PrismSettingsDialog::setCurrentPath(const QString &path)
{
    currentPath = path;
    ui->pathLineEdit->setText(path);

    if (!path.isEmpty()) {
        parsePrismDirectory(path);
    }

    updateStatistics();
}

QString PrismSettingsDialog::getCurrentPath() const
{
    return currentPath;
}

void PrismSettingsDialog::setExecutable(const QString &executable)
{
    currentExecutable = executable;
    ui->executableLineEdit->setText(executable);
}

QString PrismSettingsDialog::getExecutable() const
{
    return ui->executableLineEdit->text();
}

void PrismSettingsDialog::setInstances(const QStringList &inst)
{
    instances = inst;

    ui->instancesList->clear();
    ui->instancesList->addItem(QString("Instances (%1):").arg(instances.size()));
    for (const QString &instance : std::as_const(instances)) {
        ui->instancesList->addItem("  • " + instance);
    }
}

void PrismSettingsDialog::setAccounts(const QStringList &acc)
{
    accounts = acc;

    ui->accountsList->clear();
    ui->accountsList->addItem(QString("Accounts (%1):").arg(accounts.size()));
    for (const QString &account : std::as_const(accounts)) {
        ui->accountsList->addItem("  • " + account);
    }
}

void PrismSettingsDialog::onBrowseClicked()
{
    QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        "Select PrismLauncher Directory",
        currentPath.isEmpty() ? QDir::homePath() : currentPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!selectedPath.isEmpty()) {
        QDir dir(selectedPath);
        if (dir.exists("instances") && dir.exists("accounts.json")) {
            setCurrentPath(selectedPath);
        } else {
            QMessageBox::warning(this, "Invalid Directory",
                "The selected directory does not appear to be a valid PrismLauncher directory.\n"
                "Please select the directory containing 'instances' folder and 'accounts.json' file.");
        }
    }
}

void PrismSettingsDialog::updateStatistics()
{
    if (currentPath.isEmpty()) {
        ui->statusValueLabel->setText("Not configured");
        ui->statusValueLabel->setStyleSheet("color: #9E9E9E; font-weight: bold;");
        ui->instancesCountLabel->setText("0");
        ui->accountsCountLabel->setText("0");
    } else {
        ui->statusValueLabel->setText("Configured");
        ui->statusValueLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");
        ui->instancesCountLabel->setText(QString::number(instances.size()));
        ui->accountsCountLabel->setText(QString::number(accounts.size()));
    }
}

void PrismSettingsDialog::parsePrismDirectory(const QString &path)
{
    parsePrismInstances(path);
    parsePrismAccounts(path);
}

void PrismSettingsDialog::parsePrismInstances(const QString &path)
{
    QStringList newInstances;

    QString instancesPath = path + "/instances";
    QDir instancesDir(instancesPath);

    if (instancesDir.exists()) {
        // Get all subdirectories (each represents an instance)
        QStringList instanceDirs = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QString &instanceName : std::as_const(instanceDirs)) {
            // Verify it's a valid instance by checking for instance.cfg
            QString instanceCfgPath = instancesPath + "/" + instanceName + "/instance.cfg";
            if (QFile::exists(instanceCfgPath)) {
                newInstances.append(instanceName);
            }
        }

        newInstances.sort();
    }

    setInstances(newInstances);
}

void PrismSettingsDialog::parsePrismAccounts(const QString &path)
{
    QStringList newAccounts;

    QString accountsPath = path + "/accounts.json";
    QFile accountsFile(accountsPath);

    if (accountsFile.open(QIODevice::ReadOnly)) {
        QByteArray data = accountsFile.readAll();
        accountsFile.close();

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);

        if (error.error == QJsonParseError::NoError) {
            QJsonObject rootObj = doc.object();
            QJsonArray accountsArray = rootObj["accounts"].toArray();

            for (const QJsonValue &value : std::as_const(accountsArray)) {
                QJsonObject accountObj = value.toObject();
                QJsonObject profileObj = accountObj["profile"].toObject();
                QString accountName = profileObj["name"].toString();

                if (!accountName.isEmpty()) {
                    newAccounts.append(accountName);
                }
            }

            newAccounts.sort();
        }
    }

    setAccounts(newAccounts);
}

void PrismSettingsDialog::onBrowseExeClicked()
{
    QString filter;
#ifdef Q_OS_WIN
    filter = "Executable Files (*.exe);;All Files (*)";
#else
    filter = "All Files (*)";
#endif

    QString selectedExe = QFileDialog::getOpenFileName(
        this,
        "Select PrismLauncher Executable",
        currentExecutable.isEmpty() ? QDir::homePath() : QFileInfo(currentExecutable).path(),
        filter
    );

    if (!selectedExe.isEmpty()) {
        ui->executableLineEdit->setText(selectedExe);
        currentExecutable = selectedExe;
    }
}
