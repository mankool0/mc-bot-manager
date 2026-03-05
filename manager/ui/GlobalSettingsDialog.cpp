#include "GlobalSettingsDialog.h"
#include <QVBoxLayout>
#include <QGroupBox>
#include <QSettings>
#include <QLabel>
#include <QGuiApplication>
#include <QStyleHints>

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
    resize(500, 300);

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

    QLabel *infoLabel = new QLabel(
        "Note: Changes to console settings will apply to new console instances.\n"
        "Restart the application for changes to take effect on existing consoles.",
        this
    );
    infoLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    infoLabel->setWordWrap(true);
    consoleLayout->addRow(infoLabel);

    mainLayout->addWidget(consoleGroup);

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
