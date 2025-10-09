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

        if (ui->executableLineEdit->text().isEmpty()) {
            QString detectedExe = detectPrismLauncherExecutable(path);
            if (!detectedExe.isEmpty()) {
                ui->executableLineEdit->setText(detectedExe);
                currentExecutable = detectedExe;
            }
        }
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
    setInstances(parsePrismInstances(path));
    setAccounts(parsePrismAccounts(path));
}

QStringList PrismSettingsDialog::parsePrismInstances(const QString &path)
{
    QStringList instances;

    if (path.isEmpty()) {
        return instances;
    }

    QString instancesPath = path + "/instances";
    QDir instancesDir(instancesPath);

    if (instancesDir.exists()) {
        // Get all subdirectories (each represents an instance)
        QStringList instanceDirs = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QString &instanceName : std::as_const(instanceDirs)) {
            // Verify it's a valid instance by checking for instance.cfg
            QString instanceCfgPath = instancesPath + "/" + instanceName + "/instance.cfg";
            if (QFile::exists(instanceCfgPath)) {
                instances.append(instanceName);
            }
        }

        instances.sort();
    }

    return instances;
}

QStringList PrismSettingsDialog::parsePrismAccounts(const QString &path)
{
    QStringList accounts;

    if (path.isEmpty()) {
        return accounts;
    }

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
                    accounts.append(accountName);
                }
            }

            accounts.sort();
        }
    }

    return accounts;
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

bool PrismSettingsDialog::isFlatpakPath(const QString &path)
{
    return path.contains(".var/app/org.prismlauncher.PrismLauncher");
}

QString PrismSettingsDialog::detectPrismLauncherPath()
{
    QStringList searchPaths;

#ifdef Q_OS_WIN
    // Windows: Check %APPDATA%\PrismLauncher (Roaming - contains instances and accounts.json)
    QString appData = QDir::fromNativeSeparators(qgetenv("APPDATA"));
    if (!appData.isEmpty()) {
        searchPaths << appData + "/PrismLauncher";
    }
#elif defined(Q_OS_LINUX)
    // Linux: Check Flatpak installation first, then native installation
    QString home = QDir::homePath();
    searchPaths << home + "/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher";
    searchPaths << home + "/.local/share/PrismLauncher";
#elif defined(Q_OS_MAC)
    // macOS: Check ~/Library/Application Support/PrismLauncher
    QString home = QDir::homePath();
    searchPaths << home + "/Library/Application Support/PrismLauncher";
#endif

    for (const QString &path : searchPaths) {
        QDir dir(path);
        if (dir.exists()) {
            if (dir.exists("instances") && QFile::exists(path + "/accounts.json")) {
                return path;
            }
        }
    }

    return QString();
}

QString PrismSettingsDialog::detectPrismLauncherExecutable(const QString &prismPath)
{
#ifdef Q_OS_WIN
    // Windows: Check %LOCALAPPDATA%\Programs\PrismLauncher\prismlauncher.exe
    QString localAppData = QDir::fromNativeSeparators(qgetenv("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) {
        QString exePath = localAppData + "/Programs/PrismLauncher/prismlauncher.exe";
        if (QFile::exists(exePath)) {
            return exePath;
        }
    }
#elif defined(Q_OS_LINUX)
    // Linux: Check if this is a Flatpak installation
    if (isFlatpakPath(prismPath)) {
        return "/usr/bin/flatpak run --branch=stable --arch=x86_64 --command=prismlauncher --file-forwarding org.prismlauncher.PrismLauncher";
    }
    // For native Linux installations the user should configure it
#elif defined(Q_OS_MAC)
    // macOS: TODO - need to figure out where PrismLauncher installs on macOS
    // For now, leave blank for user configuration
#endif

    return QString();
}
