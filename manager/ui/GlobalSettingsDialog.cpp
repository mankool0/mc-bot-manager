#include "GlobalSettingsDialog.h"
#include "BotConsoleWidget.h"
#include "bot/BotManager.h"
#include "logging/LogManager.h"
#include <QVBoxLayout>
#include <QGroupBox>
#include <QSettings>
#include <QGuiApplication>
#include <QStyleHints>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHBoxLayout>

GlobalSettingsDialog::GlobalSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    loadSettings();
}

GlobalSettingsDialog::~GlobalSettingsDialog()
{
}

void GlobalSettingsDialog::setupUI()
{
    setWindowTitle("Global Settings");
    setModal(true);
    resize(500, 450);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *consoleGroup = new QGroupBox("Console Settings", this);
    QFormLayout *consoleLayout = new QFormLayout(consoleGroup);

    QHBoxLayout *maxLinesLayout = new QHBoxLayout();
    consoleMaxLinesSpinBox = new QSpinBox(this);
    consoleMaxLinesSpinBox->setRange(100, 1000000);
    consoleMaxLinesSpinBox->setValue(10000);
    consoleMaxLinesSpinBox->setSingleStep(1000);
    consoleMaxLinesSpinBox->setToolTip("Maximum number of lines to keep in console output");

    consoleUnlimitedCheckBox = new QCheckBox("Unlimited", this);
    consoleUnlimitedCheckBox->setToolTip("Set to 0 for unlimited console output");

    maxLinesLayout->addWidget(consoleMaxLinesSpinBox);
    maxLinesLayout->addWidget(consoleUnlimitedCheckBox);
    maxLinesLayout->addStretch();

    consoleLayout->addRow("Max Lines:", maxLinesLayout);

    consolePendingLinesSpinBox = new QSpinBox(this);
    consolePendingLinesSpinBox->setRange(100, 100000);
    consolePendingLinesSpinBox->setValue(500);
    consolePendingLinesSpinBox->setSingleStep(100);
    consolePendingLinesSpinBox->setToolTip("Max lines buffered per bot between UI flushes. "
                                           "Excess lines are dropped with a notice.");
    consoleLayout->addRow("Max Buffered Lines:", consolePendingLinesSpinBox);


    mainLayout->addWidget(consoleGroup);

    QGroupBox *loggingGroup = new QGroupBox("File Logging", this);
    QFormLayout *loggingLayout = new QFormLayout(loggingGroup);

    loggingEnabledCheckBox = new QCheckBox("Enable file logging", this);
    loggingLayout->addRow(loggingEnabledCheckBox);

    QHBoxLayout *logDirLayout = new QHBoxLayout();
    logDirEdit = new QLineEdit(this);
    logDirEdit->setReadOnly(true);
    logDirEdit->setToolTip("Directory where log files are saved");
    logDirBrowseButton = new QPushButton("Browse...", this);
    logDirLayout->addWidget(logDirEdit);
    logDirLayout->addWidget(logDirBrowseButton);
    loggingLayout->addRow("Log Directory:", logDirLayout);

    logMaxSizeMiBSpinBox = new QSpinBox(this);
    logMaxSizeMiBSpinBox->setRange(1, 1000);
    logMaxSizeMiBSpinBox->setValue(10);
    logMaxSizeMiBSpinBox->setSuffix(" MiB");
    logMaxSizeMiBSpinBox->setToolTip("Maximum log file size before rollover");
    loggingLayout->addRow("Max File Size:", logMaxSizeMiBSpinBox);

    logMaxFilesSpinBox = new QSpinBox(this);
    logMaxFilesSpinBox->setRange(0, 10000);
    logMaxFilesSpinBox->setSpecialValueText("Unlimited");
    logMaxFilesSpinBox->setToolTip("Total number of log files to keep per bot (0 = unlimited)");
    loggingLayout->addRow("Max Files:", logMaxFilesSpinBox);

    mainLayout->addWidget(loggingGroup);

    connect(loggingEnabledCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        logDirEdit->setEnabled(checked);
        logDirBrowseButton->setEnabled(checked);
        logMaxSizeMiBSpinBox->setEnabled(checked);
        logMaxFilesSpinBox->setEnabled(checked);
    });
    connect(logDirBrowseButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Log Directory", logDirEdit->text());
        if (!dir.isEmpty())
            logDirEdit->setText(dir);
    });

    QGroupBox *appearanceGroup = new QGroupBox("Appearance", this);
    QFormLayout *appearanceLayout = new QFormLayout(appearanceGroup);

    colorSchemeComboBox = new QComboBox(this);
    colorSchemeComboBox->addItem("Follow System", static_cast<int>(Qt::ColorScheme::Unknown));
    colorSchemeComboBox->addItem("Light", static_cast<int>(Qt::ColorScheme::Light));
    colorSchemeComboBox->addItem("Dark", static_cast<int>(Qt::ColorScheme::Dark));
    appearanceLayout->addRow("Color Scheme:", colorSchemeComboBox);

    mainLayout->addWidget(appearanceGroup);

    mainLayout->addStretch();

    // Buttons
    buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this
    );
    mainLayout->addWidget(buttonBox);

    // Connections
    connect(buttonBox, &QDialogButtonBox::accepted, this, &GlobalSettingsDialog::saveSettings);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(consoleUnlimitedCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        consoleMaxLinesSpinBox->setEnabled(!checked);
        if (checked) {
            consoleMaxLinesSpinBox->setValue(0);
        } else if (consoleMaxLinesSpinBox->value() == 0) {
            consoleMaxLinesSpinBox->setValue(10000);
        }
    });
}

void GlobalSettingsDialog::loadSettings()
{
    QSettings settings("MCBotManager", "MCBotManager");

    int maxLines = settings.value("Console/maxLines", 10000).toInt();

    if (maxLines == 0) {
        consoleUnlimitedCheckBox->setChecked(true);
        consoleMaxLinesSpinBox->setValue(10000);
        consoleMaxLinesSpinBox->setEnabled(false);
    } else {
        consoleUnlimitedCheckBox->setChecked(false);
        consoleMaxLinesSpinBox->setValue(maxLines);
        consoleMaxLinesSpinBox->setEnabled(true);
    }

    consolePendingLinesSpinBox->setValue(settings.value("Console/maxPendingLines", 500).toInt());

    bool loggingEnabled = settings.value("Logging/enabled", true).toBool();
    loggingEnabledCheckBox->setChecked(loggingEnabled);
    QString defaultLogDir = QCoreApplication::applicationDirPath() + "/logs";
    logDirEdit->setText(settings.value("Logging/logDir", defaultLogDir).toString());
    logMaxSizeMiBSpinBox->setValue(settings.value("Logging/maxSizeMiB", 10).toInt());
    logMaxFilesSpinBox->setValue(settings.value("Logging/maxFiles", 0).toInt());
    logDirEdit->setEnabled(loggingEnabled);
    logDirBrowseButton->setEnabled(loggingEnabled);
    logMaxSizeMiBSpinBox->setEnabled(loggingEnabled);
    logMaxFilesSpinBox->setEnabled(loggingEnabled);

    int scheme = settings.value("Appearance/colorScheme", static_cast<int>(Qt::ColorScheme::Unknown)).toInt();
    for (int i = 0; i < colorSchemeComboBox->count(); ++i) {
        if (colorSchemeComboBox->itemData(i).toInt() == scheme) {
            colorSchemeComboBox->setCurrentIndex(i);
            break;
        }
    }
}

void GlobalSettingsDialog::saveSettings()
{
    QSettings settings("MCBotManager", "MCBotManager");

    int maxLines = consoleUnlimitedCheckBox->isChecked() ? 0 : consoleMaxLinesSpinBox->value();
    settings.setValue("Console/maxLines", maxLines);
    int pendingLines = consolePendingLinesSpinBox->value();
    settings.setValue("Console/maxPendingLines", pendingLines);

    for (BotInstance *bot : std::as_const(BotManager::getBots())) {
        if (bot->consoleWidget) {
            bot->consoleWidget->setMaxLines(maxLines);
            bot->consoleWidget->setRingCapacity(pendingLines);
        }
    }

    bool loggingEnabled = loggingEnabledCheckBox->isChecked();
    QString logDir = logDirEdit->text().trimmed();
    int logMaxSizeMiB = logMaxSizeMiBSpinBox->value();
    int logMaxFiles = logMaxFilesSpinBox->value();
    settings.setValue("Logging/enabled", loggingEnabled);
    settings.setValue("Logging/logDir", logDir);
    settings.setValue("Logging/maxSizeMiB", logMaxSizeMiB);
    settings.setValue("Logging/maxFiles", logMaxFiles);

    qint64 maxSizeBytes = (qint64)logMaxSizeMiB * 1024 * 1024;
    if (loggingEnabled) {
        LogManager::initFileSink(logDir, maxSizeBytes, logMaxFiles);
        for (BotInstance *bot : std::as_const(BotManager::getBots())) {
            if (bot->consoleWidget)
                bot->consoleWidget->attachLogFile(logDir, bot->name, maxSizeBytes, logMaxFiles);
        }
    } else {
        LogManager::closeFileSink();
    }

    int scheme = colorSchemeComboBox->currentData().toInt();
    settings.setValue("Appearance/colorScheme", scheme);

    settings.sync();
    applyColorScheme();
    accept();
}

void GlobalSettingsDialog::applyColorScheme()
{
    QSettings settings("MCBotManager", "MCBotManager");
    int scheme = settings.value("Appearance/colorScheme", static_cast<int>(Qt::ColorScheme::Unknown)).toInt();
    QGuiApplication::styleHints()->setColorScheme(static_cast<Qt::ColorScheme>(scheme));
}
