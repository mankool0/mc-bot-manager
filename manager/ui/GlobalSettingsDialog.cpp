#include "GlobalSettingsDialog.h"
#include "BotConsoleWidget.h"
#include "AppColors.h"
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
#include <QScrollArea>
#include <QColorDialog>
#include <QToolButton>

GlobalSettingsDialog::GlobalSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    loadSettings();
}

GlobalSettingsDialog::~GlobalSettingsDialog()
{
}

void GlobalSettingsDialog::setButtonColor(QPushButton *btn, const QColor &color)
{
    btn->setProperty("selectedColor", color);
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid #888;").arg(color.name()));
}

void GlobalSettingsDialog::addColorRow(QFormLayout *layout, const QString &label,
                                        const QString &key, const QColor &defaultColor)
{
    QWidget *container = new QWidget(this);
    QHBoxLayout *rowLayout = new QHBoxLayout(container);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    QPushButton *colorBtn = new QPushButton(container);
    colorBtn->setFixedSize(60, 22);
    setButtonColor(colorBtn, defaultColor);
    connect(colorBtn, &QPushButton::clicked, this, [this, colorBtn]() {
        QColor current = colorBtn->property("selectedColor").value<QColor>();
        QColor chosen = QColorDialog::getColor(current, this, "Select Color");
        if (chosen.isValid())
            setButtonColor(colorBtn, chosen);
    });

    QToolButton *resetBtn = new QToolButton(container);
    resetBtn->setText("↺");
    resetBtn->setFixedSize(22, 22);
    resetBtn->setToolTip("Reset to default");
    connect(resetBtn, &QToolButton::clicked, this, [this, colorBtn, defaultColor]() {
        setButtonColor(colorBtn, defaultColor);
    });

    rowLayout->addWidget(colorBtn);
    rowLayout->addWidget(resetBtn);
    rowLayout->addStretch();

    m_colorEntries.append({key, defaultColor, colorBtn});
    layout->addRow(label + ":", container);
}

static QScrollArea *makeScrollArea(QWidget *parent)
{
    QScrollArea *sa = new QScrollArea(parent);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    return sa;
}

void GlobalSettingsDialog::setupUI()
{
    setWindowTitle("Global Settings");
    setModal(true);
    resize(520, 520);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    tabWidget = new QTabWidget(this);

    // ---- Console tab ----
    {
        QScrollArea *sa = makeScrollArea(this);
        QWidget *contents = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(contents);

        QGroupBox *consoleGroup = new QGroupBox("Console Settings");
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

        layout->addWidget(consoleGroup);
        layout->addStretch();
        sa->setWidget(contents);
        tabWidget->addTab(sa, "Console");
    }

    // ---- Logging tab ----
    {
        QScrollArea *sa = makeScrollArea(this);
        QWidget *contents = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(contents);

        QGroupBox *loggingGroup = new QGroupBox("File Logging");
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

        layout->addWidget(loggingGroup);
        layout->addStretch();
        sa->setWidget(contents);
        tabWidget->addTab(sa, "Logging");
    }

    // ---- Colors tab ----
    {
        QScrollArea *sa = makeScrollArea(this);
        QWidget *contents = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(contents);

        QGroupBox *consoleColorsGroup = new QGroupBox("Console Colors");
        QFormLayout *consoleColorsLayout = new QFormLayout(consoleColorsGroup);
        const auto &d = AppColors::defaults();
        addColorRow(consoleColorsLayout, "Ready / Cleared",   "Colors/Console/ready",   d.consoleReady);
        addColorRow(consoleColorsLayout, "User Input",        "Colors/Console/input",   d.consoleInput);
        addColorRow(consoleColorsLayout, "Success Response",  "Colors/Console/success", d.consoleSuccess);
        addColorRow(consoleColorsLayout, "Error Response",    "Colors/Console/error",   d.consoleError);
        addColorRow(consoleColorsLayout, "Dropped Messages",  "Colors/Console/dropped", d.consoleDropped);
        layout->addWidget(consoleColorsGroup);

        QGroupBox *scriptColorsGroup = new QGroupBox("Script Colors");
        QFormLayout *scriptColorsLayout = new QFormLayout(scriptColorsGroup);
        addColorRow(scriptColorsLayout, "Script Completed",    "Colors/Script/success", d.scriptSuccess);
        addColorRow(scriptColorsLayout, "Script Stopped",      "Colors/Script/stopped", d.scriptStopped);
        addColorRow(scriptColorsLayout, "Script Error",        "Colors/Script/error",   d.scriptError);
        addColorRow(scriptColorsLayout, "utils.log() output",  "Colors/Script/log",     d.scriptLog);
        layout->addWidget(scriptColorsGroup);

        QGroupBox *statusColorsGroup = new QGroupBox("Bot Status Colors");
        QFormLayout *statusColorsLayout = new QFormLayout(statusColorsGroup);
        addColorRow(statusColorsLayout, "Online",  "Colors/Status/online",  d.statusOnline);
        addColorRow(statusColorsLayout, "Offline", "Colors/Status/offline", d.statusOffline);
        addColorRow(statusColorsLayout, "Error",   "Colors/Status/error",   d.statusError);
        addColorRow(statusColorsLayout, "Other",   "Colors/Status/other",   d.statusOther);
        layout->addWidget(statusColorsGroup);

        QGroupBox *logColorsGroup = new QGroupBox("Log Colors");
        QFormLayout *logColorsLayout = new QFormLayout(logColorsGroup);
        addColorRow(logColorsLayout, "Timestamp", "Colors/Log/timestamp", d.logTimestamp);
        addColorRow(logColorsLayout, "Debug",     "Colors/Log/debug",     d.logDebug);
        addColorRow(logColorsLayout, "Info",      "Colors/Log/info",      d.logInfo);
        addColorRow(logColorsLayout, "Warning",   "Colors/Log/warning",   d.logWarning);
        addColorRow(logColorsLayout, "Error",     "Colors/Log/error",     d.logError);
        addColorRow(logColorsLayout, "Success",   "Colors/Log/success",   d.logSuccess);
        layout->addWidget(logColorsGroup);

        QHBoxLayout *resetAllLayout = new QHBoxLayout();
        resetAllLayout->addStretch();
        QPushButton *resetAllBtn = new QPushButton("Reset All to Defaults", this);
        connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
            for (auto &entry : m_colorEntries)
                setButtonColor(entry.button, entry.defaultColor);
        });
        resetAllLayout->addWidget(resetAllBtn);
        layout->addLayout(resetAllLayout);

        layout->addStretch();
        sa->setWidget(contents);
        tabWidget->addTab(sa, "Colors");
    }

    // ---- Appearance tab ----
    {
        QScrollArea *sa = makeScrollArea(this);
        QWidget *contents = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(contents);

        QGroupBox *appearanceGroup = new QGroupBox("Appearance");
        QFormLayout *appearanceLayout = new QFormLayout(appearanceGroup);

        colorSchemeComboBox = new QComboBox(this);
        colorSchemeComboBox->addItem("Follow System", static_cast<int>(Qt::ColorScheme::Unknown));
        colorSchemeComboBox->addItem("Light", static_cast<int>(Qt::ColorScheme::Light));
        colorSchemeComboBox->addItem("Dark", static_cast<int>(Qt::ColorScheme::Dark));
        appearanceLayout->addRow("Color Scheme:", colorSchemeComboBox);

        layout->addWidget(appearanceGroup);
        layout->addStretch();
        sa->setWidget(contents);
        tabWidget->addTab(sa, "Appearance");
    }

    mainLayout->addWidget(tabWidget);

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

    for (auto &entry : m_colorEntries) {
        QColor color = settings.value(entry.key, entry.defaultColor).value<QColor>();
        setButtonColor(entry.button, color);
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

    for (const auto &entry : m_colorEntries) {
        QColor color = entry.button->property("selectedColor").value<QColor>();
        settings.setValue(entry.key, color);
    }

    settings.sync();
    AppColors::reload();
    applyColorScheme();
    accept();
}

void GlobalSettingsDialog::applyColorScheme()
{
    QSettings settings("MCBotManager", "MCBotManager");
    int scheme = settings.value("Appearance/colorScheme", static_cast<int>(Qt::ColorScheme::Unknown)).toInt();
    QGuiApplication::styleHints()->setColorScheme(static_cast<Qt::ColorScheme>(scheme));
}
