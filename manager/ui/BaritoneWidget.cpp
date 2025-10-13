#include "BaritoneWidget.h"
#include "bot/BotManager.h"
#include <QHeaderView>
#include <algorithm>

BaritoneWidget::BaritoneWidget(QWidget *parent)
    : QWidget(parent)
    , updatingFromCode(false)
{
    setupUI();
}

BaritoneWidget::~BaritoneWidget()
{
}

void BaritoneWidget::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    QHBoxLayout *filterLayout = new QHBoxLayout();

    QLabel *filterLabel = new QLabel("Filter:", this);
    filterLayout->addWidget(filterLabel);

    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Search settings...");
    filterLayout->addWidget(filterEdit);

    mainLayout->addLayout(filterLayout);

    settingTree = new QTreeWidget(this);
    settingTree->setHeaderLabels({"Setting", "Value"});
    settingTree->setColumnWidth(0, 300);
    settingTree->setColumnWidth(1, 200);
    settingTree->header()->setStretchLastSection(true);
    settingTree->setAlternatingRowColors(true);
    settingTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    settingTree->setIndentation(0);
    settingTree->setRootIsDecorated(false);
    mainLayout->addWidget(settingTree);

    connect(filterEdit, &QLineEdit::textChanged, this, &BaritoneWidget::onFilterTextChanged);
}

void BaritoneWidget::updateSettings(const QMap<QString, BaritoneSettingData> &settings)
{
    updatingFromCode = true;
    allSettings = settings;
    populateTree();
    updatingFromCode = false;
}

void BaritoneWidget::clear()
{
    settingTree->clear();
    settingItems.clear();
    allSettings.clear();
}

void BaritoneWidget::updateSingleSetting(const BaritoneSettingData &setting)
{
    updatingFromCode = true;

    QString settingName = setting.name.trimmed();

    // Update the setting in our stored data
    allSettings[settingName] = setting;

    QTreeWidgetItem *existingItem = settingItems.value(settingName, nullptr);
    if (existingItem) {
        updateSettingWidget(existingItem, setting);
    }

    updatingFromCode = false;
}

void BaritoneWidget::populateTree()
{
    settingTree->clear();
    settingItems.clear();

    QString filterText = filterEdit->text().toLower();

    // Convert map to sorted vector for display
    QVector<BaritoneSettingData> sortedSettings;
    for (auto it = allSettings.constBegin(); it != allSettings.constEnd(); ++it) {
        sortedSettings.append(it.value());
    }
    std::sort(sortedSettings.begin(), sortedSettings.end(),
              [](const BaritoneSettingData &a, const BaritoneSettingData &b) {
                  return a.name.trimmed().toLower() < b.name.trimmed().toLower();
              });

    for (const auto &setting : sortedSettings) {
        QString settingName = setting.name.trimmed();
        if (!filterText.isEmpty() && !settingName.toLower().contains(filterText)) {
            continue;
        }

        QTreeWidgetItem *item = createSettingItem(setting);
        settingTree->addTopLevelItem(item);
        settingItems[settingName] = item;

        QWidget *editor = createSettingEditor(setting, settingName);
        if (editor) {
            settingTree->setItemWidget(item, 1, editor);
        }
    }
}

QTreeWidgetItem* BaritoneWidget::createSettingItem(const BaritoneSettingData &setting)
{
    QTreeWidgetItem *settingItem = new QTreeWidgetItem();

    QString settingName = setting.name.trimmed();
    settingItem->setText(0, settingName);
    settingItem->setData(0, SettingNameRole, settingName);
    settingItem->setData(0, SettingTypeRole, setting.type);

    QString tooltip = settingName;
    if (!setting.description.isEmpty()) {
        tooltip += "\n" + setting.description;
    }
    if (!setting.defaultValue.isEmpty()) {
        tooltip += "\nDefault: " + setting.defaultValue;
    }
    settingItem->setToolTip(0, tooltip);

    return settingItem;
}

void BaritoneWidget::updateSettingWidget(QTreeWidgetItem *settingItem,
                                          const BaritoneSettingData &setting)
{
    QWidget *widget = settingTree->itemWidget(settingItem, 1);
    if (!widget) {
        settingItem->setText(1, setting.currentValue);
        return;
    }

    QString type = setting.type.toLower();

    if (type == "boolean" || type == "java.lang.boolean") {
        if (QCheckBox *checkBox = qobject_cast<QCheckBox*>(widget)) {
            checkBox->setChecked(setting.currentValue.toLower() == "true");
        }
    } else if (type == "integer" || type == "java.lang.integer" || type == "int") {
        if (QSpinBox *spinBox = qobject_cast<QSpinBox*>(widget)) {
            bool ok;
            int value = setting.currentValue.toInt(&ok);
            if (ok) {
                spinBox->setValue(value);
            }
        }
    } else if (type == "double" || type == "java.lang.double" || type == "float" || type == "java.lang.float") {
        if (QDoubleSpinBox *spinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            bool ok;
            double value = setting.currentValue.toDouble(&ok);
            if (ok) {
                spinBox->setValue(value);
            }
        }
    } else if (type == "long" || type == "java.lang.long") {
        if (QSpinBox *spinBox = qobject_cast<QSpinBox*>(widget)) {
            bool ok;
            int value = setting.currentValue.toInt(&ok);
            if (ok) {
                spinBox->setValue(value);
            }
        }
    } else {
        // String or other types
        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setText(setting.currentValue);
            lineEdit->setProperty("originalValue", setting.currentValue);
        }
    }
}

QWidget* BaritoneWidget::createSettingEditor(const BaritoneSettingData &setting,
                                               const QString &settingName)
{
    QString type = setting.type.toLower();

    if (type == "boolean" || type == "java.lang.boolean") {
        QCheckBox *checkBox = new QCheckBox(this);
        checkBox->setChecked(setting.currentValue.toLower() == "true");
        connect(checkBox, &QCheckBox::toggled, this, [this, settingName](bool checked) {
            if (!updatingFromCode) {
                emit settingChanged(settingName, checked ? "true" : "false");
            }
        });
        return checkBox;
    } else if (type == "integer" || type == "java.lang.integer" || type == "int") {
        QSpinBox *spinBox = new QSpinBox(this);
        spinBox->setMinimum(std::numeric_limits<int>::min());
        spinBox->setMaximum(std::numeric_limits<int>::max());
        bool ok;
        int value = setting.currentValue.toInt(&ok);
        if (ok) {
            spinBox->setValue(value);
        }
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, settingName](int value) {
            if (!updatingFromCode) {
                emit settingChanged(settingName, QString::number(value));
            }
        });
        return spinBox;
    } else if (type == "double" || type == "java.lang.double" || type == "float" || type == "java.lang.float") {
        QDoubleSpinBox *spinBox = new QDoubleSpinBox(this);
        spinBox->setMinimum(-999999.0);
        spinBox->setMaximum(999999.0);
        spinBox->setDecimals(4);
        spinBox->setSingleStep(0.1);
        bool ok;
        double value = setting.currentValue.toDouble(&ok);
        if (ok) {
            spinBox->setValue(value);
        }
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, settingName](double value) {
            if (!updatingFromCode) {
                emit settingChanged(settingName, QString::number(value));
            }
        });
        return spinBox;
    } else if (type == "long" || type == "java.lang.long") {
        // Use QSpinBox for long values (limited to int range)
        QSpinBox *spinBox = new QSpinBox(this);
        spinBox->setMinimum(std::numeric_limits<int>::min());
        spinBox->setMaximum(std::numeric_limits<int>::max());
        bool ok;
        int value = setting.currentValue.toInt(&ok);
        if (ok) {
            spinBox->setValue(value);
        }
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, settingName](int value) {
            if (!updatingFromCode) {
                emit settingChanged(settingName, QString::number(value));
            }
        });
        return spinBox;
    } else {
        // String or other types - use QLineEdit
        QLineEdit *lineEdit = new QLineEdit(this);
        lineEdit->setText(setting.currentValue);
        lineEdit->setProperty("originalValue", setting.currentValue);
        connect(lineEdit, &QLineEdit::editingFinished, this, [this, lineEdit, settingName]() {
            if (!updatingFromCode) {
                QString newValue = lineEdit->text();
                QString originalValue = lineEdit->property("originalValue").toString();
                if (newValue != originalValue) {
                    lineEdit->setProperty("originalValue", newValue);
                    emit settingChanged(settingName, newValue);
                }
            }
        });
        return lineEdit;
    }
}

void BaritoneWidget::onFilterTextChanged(const QString &text)
{
    Q_UNUSED(text);
    populateTree();
}
