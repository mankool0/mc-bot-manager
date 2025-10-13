#include "MeteorModulesWidget.h"
#include "bot/BotManager.h"
#include <QHeaderView>
#include <QGroupBox>
#include <algorithm>
#include <functional>

MeteorModulesWidget::MeteorModulesWidget(QWidget *parent)
    : QWidget(parent)
    , updatingFromCode(false)
{
    setupUI();
}

MeteorModulesWidget::~MeteorModulesWidget()
{
}

void MeteorModulesWidget::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Filter section
    QHBoxLayout *filterLayout = new QHBoxLayout();

    QLabel *filterLabel = new QLabel("Filter:", this);
    filterLayout->addWidget(filterLabel);

    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Search modules...");
    filterLayout->addWidget(filterEdit);

    QLabel *categoryLabel = new QLabel("Category:", this);
    filterLayout->addWidget(categoryLabel);

    categoryFilter = new QComboBox(this);
    categoryFilter->addItem("All");
    categoryFilter->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    filterLayout->addWidget(categoryFilter);

    mainLayout->addLayout(filterLayout);

    moduleTree = new QTreeWidget(this);
    moduleTree->setHeaderLabels({"Module / Setting", "Value"});
    moduleTree->setColumnWidth(0, DEFAULT_COLUMN_WIDTH);
    moduleTree->setAlternatingRowColors(true);
    moduleTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(moduleTree);

    connect(filterEdit, &QLineEdit::textChanged, this, &MeteorModulesWidget::onFilterTextChanged);
    connect(categoryFilter, &QComboBox::currentTextChanged, this, &MeteorModulesWidget::onCategoryFilterChanged);
    connect(moduleTree, &QTreeWidget::itemChanged, this, &MeteorModulesWidget::onItemChanged);
}

void MeteorModulesWidget::updateModules(const QMap<QString, MeteorModuleData> &modules)
{
    updatingFromCode = true;
    allModules = modules;

    // Update category filter
    QSet<QString> categories;
    for (auto it = modules.constBegin(); it != modules.constEnd(); ++it) {
        const MeteorModuleData &module = it.value();
        if (!module.category.isEmpty()) {
            categories.insert(module.category);
        }
    }

    QString currentCategory = categoryFilter->currentText();
    categoryFilter->clear();
    categoryFilter->addItem("All");
    for (const QString &cat : categories) {
        categoryFilter->addItem(cat);
    }

    int index = categoryFilter->findText(currentCategory);
    if (index >= 0) {
        categoryFilter->setCurrentIndex(index);
    }

    populateTree();
    updatingFromCode = false;
}

void MeteorModulesWidget::clear()
{
    moduleTree->clear();
    moduleItems.clear();
    allModules.clear();
}

void MeteorModulesWidget::updateSingleModule(const MeteorModuleData &module)
{
    updatingFromCode = true;

    // Update the module in our stored data
    allModules[module.name] = module;

    // Find the existing tree item for this module
    QTreeWidgetItem *existingItem = moduleItems.value(module.name, nullptr);
    if (existingItem) {
        // Save scroll position
        int scrollValue = moduleTree->verticalScrollBar()->value();

        // Update module's checkbox state in-place
        existingItem->setCheckState(0, module.enabled ? Qt::Checked : Qt::Unchecked);

        // Update settings in-place
        updateModuleSettings(existingItem, module);

        // Restore scroll position
        moduleTree->verticalScrollBar()->setValue(scrollValue);
    }

    updatingFromCode = false;
}

void MeteorModulesWidget::populateTree()
{
    // Save expansion state before clearing
    QMap<QString, bool> moduleExpansion;
    QMap<QString, QMap<QString, bool>> groupExpansion; // module name -> group name -> expanded state

    for (auto it = moduleItems.constBegin(); it != moduleItems.constEnd(); ++it) {
        QString moduleName = it.key();
        QTreeWidgetItem *moduleItem = it.value();
        moduleExpansion[moduleName] = moduleItem->isExpanded();

        // Save expansion state of groups within this module
        QMap<QString, bool> groups;
        for (int i = 0; i < moduleItem->childCount(); ++i) {
            QTreeWidgetItem *child = moduleItem->child(i);
            if (child->data(0, ItemTypeRole).toString() == "group") {
                groups[child->text(0)] = child->isExpanded();
            }
        }
        groupExpansion[moduleName] = groups;
    }

    moduleTree->clear();
    moduleItems.clear();

    QString filterText = filterEdit->text().toLower();
    QString selectedCategory = categoryFilter->currentText();

    // Convert map to sorted vector for display
    QVector<MeteorModuleData> sortedModules;
    for (auto it = allModules.constBegin(); it != allModules.constEnd(); ++it) {
        sortedModules.append(it.value());
    }
    std::sort(sortedModules.begin(), sortedModules.end(),
              [](const MeteorModuleData &a, const MeteorModuleData &b) {
                  return a.name.toLower() < b.name.toLower();
              });

    for (const auto &module : sortedModules) {
        if (!filterText.isEmpty() && !module.name.toLower().contains(filterText)) {
            continue;
        }

        if (selectedCategory != "All" && module.category != selectedCategory) {
            continue;
        }

        QTreeWidgetItem *item = createModuleItem(module);
        moduleTree->addTopLevelItem(item);
        moduleItems[module.name] = item;

        // Restore expansion state for this module
        if (moduleExpansion.contains(module.name)) {
            item->setExpanded(moduleExpansion[module.name]);
        }

        // Restore group expansion states
        if (groupExpansion.contains(module.name)) {
            const QMap<QString, bool> &groups = groupExpansion[module.name];
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem *child = item->child(i);
                if (child->data(0, ItemTypeRole).toString() == "group") {
                    QString groupName = child->text(0);
                    if (groups.contains(groupName)) {
                        child->setExpanded(groups[groupName]);
                    }
                }
            }
        }
    }
}

QTreeWidgetItem* MeteorModulesWidget::createModuleItem(const MeteorModuleData &module)
{
    QTreeWidgetItem *moduleItem = new QTreeWidgetItem();

    moduleItem->setText(0, QString("%1 (%2)").arg(module.name, module.category));
    moduleItem->setCheckState(0, module.enabled ? Qt::Checked : Qt::Unchecked);
    moduleItem->setFlags(moduleItem->flags() | Qt::ItemIsUserCheckable);
    moduleItem->setData(0, ItemDataRole, module.name);
    moduleItem->setData(0, ItemTypeRole, "module");

    if (!module.description.isEmpty()) {
        moduleItem->setToolTip(0, module.description);
    }

    QFont font = moduleItem->font(0);
    font.setBold(true);
    moduleItem->setFont(0, font);

    // Group settings by group name
    QMap<QString, QVector<QString>> groupedSettings; // groupName -> settingPaths
    for (auto it = module.settings.constBegin(); it != module.settings.constEnd(); ++it) {
        const QString &settingPath = it.key();
        const MeteorSettingData &setting = it.value();
        QString group = setting.groupName;
        groupedSettings[group].append(settingPath);
    }

    for (auto it = groupedSettings.constBegin(); it != groupedSettings.constEnd(); ++it) {
        const QString &groupName = it.key();
        const auto &settingPaths = it.value();

        QTreeWidgetItem *parentItem = moduleItem;

        if (!groupName.isEmpty()) {
            QTreeWidgetItem *groupItem = new QTreeWidgetItem(moduleItem);
            groupItem->setText(0, groupName);
            groupItem->setData(0, ItemTypeRole, "group");

            QFont groupFont = groupItem->font(0);
            groupFont.setItalic(true);
            groupItem->setFont(0, groupFont);

            parentItem = groupItem;
        }

        for (const QString &settingPath : settingPaths) {
            const MeteorSettingData &setting = module.settings[settingPath];
            QTreeWidgetItem *settingItem = new QTreeWidgetItem(parentItem);

            settingItem->setText(0, setting.name);
            settingItem->setData(0, ItemDataRole, settingPath);
            settingItem->setData(0, ItemTypeRole, "setting");
            settingItem->setData(0, ModuleNameRole, module.name);

            if (!setting.description.isEmpty()) {
                settingItem->setToolTip(0, setting.description);
            }

            QWidget *editor = createSettingEditor(setting, module.name, settingPath);
            if (editor) {
                moduleTree->setItemWidget(settingItem, 1, editor);
            } else {
                settingItem->setText(1, setting.currentValue);
            }
        }
    }

    return moduleItem;
}

void MeteorModulesWidget::updateModuleSettings(QTreeWidgetItem *moduleItem, const MeteorModuleData &module)
{
    std::function<void(QTreeWidgetItem*)> updateChildren = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            QString itemType = child->data(0, ItemTypeRole).toString();

            if (itemType == "setting") {
                QString settingPath = child->data(0, ItemDataRole).toString();
                if (module.settings.contains(settingPath)) {
                    const MeteorSettingData &setting = module.settings[settingPath];
                    updateSettingWidget(child, setting);
                }
            } else if (itemType == "group") {
                updateChildren(child);
            }
        }
    };

    updateChildren(moduleItem);
}

void MeteorModulesWidget::updateSettingWidget(QTreeWidgetItem *settingItem, const MeteorSettingData &setting)
{
    QWidget *widget = moduleTree->itemWidget(settingItem, 1);
    if (!widget) {
        settingItem->setText(1, setting.currentValue);
        return;
    }

    using SettingType = mankool::mcbot::protocol::SettingInfo::SettingType;

    switch (setting.type) {
        case SettingType::BOOLEAN: {
            if (QCheckBox *checkBox = qobject_cast<QCheckBox*>(widget)) {
                checkBox->setChecked(setting.currentValue.toLower() == "true");
            }
            break;
        }

        case SettingType::INTEGER: {
            if (QSpinBox *spinBox = qobject_cast<QSpinBox*>(widget)) {
                bool ok;
                int value = setting.currentValue.toInt(&ok);
                if (ok) {
                    spinBox->setValue(value);
                }
            }
            break;
        }

        case SettingType::DOUBLE: {
            if (QDoubleSpinBox *spinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
                bool ok;
                double value = setting.currentValue.toDouble(&ok);
                if (ok) {
                    spinBox->setValue(value);
                }
            }
            break;
        }

        case SettingType::ENUM: {
            if (QComboBox *comboBox = qobject_cast<QComboBox*>(widget)) {
                comboBox->setCurrentText(setting.currentValue);
            }
            break;
        }

        case SettingType::BLOCK:
        case SettingType::ITEM: {
            if (QLabel *label = qobject_cast<QLabel*>(widget)) {
                QString displayValue = setting.currentValue;
                if (displayValue.length() > MAX_DISPLAY_LENGTH) {
                    label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
                    label->setToolTip(displayValue + "\n\n(Edit this setting in-game - complex format)");
                } else {
                    label->setText(displayValue);
                }
            }
            break;
        }

        case SettingType::STRING:
        case SettingType::COLOR:
        case SettingType::KEYBIND:
        default: {
            if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(widget)) {
                lineEdit->setText(setting.currentValue);
                lineEdit->setProperty("originalValue", setting.currentValue);
            }
            break;
        }
    }
}

QWidget* MeteorModulesWidget::createSettingEditor(const MeteorSettingData &setting,
                                                    const QString &moduleName,
                                                    const QString &settingPath)
{
    using SettingType = mankool::mcbot::protocol::SettingInfo::SettingType;

    switch (setting.type) {
        case SettingType::BOOLEAN: {
            QCheckBox *checkBox = new QCheckBox(this);
            checkBox->setChecked(setting.currentValue.toLower() == "true");
            connect(checkBox, &QCheckBox::toggled, this, [this, moduleName, settingPath](bool checked) {
                if (!updatingFromCode) {
                    emit settingChanged(moduleName, settingPath, checked ? "true" : "false");
                }
            });
            return checkBox;
        }

        case SettingType::INTEGER: {
            QSpinBox *spinBox = new QSpinBox(this);
            if (setting.hasMin) {
                spinBox->setMinimum(static_cast<int>(setting.minValue));
            } else {
                spinBox->setMinimum(std::numeric_limits<int>::min());
            }
            if (setting.hasMax) {
                spinBox->setMaximum(static_cast<int>(setting.maxValue));
            } else {
                spinBox->setMaximum(std::numeric_limits<int>::max());
            }
            bool ok;
            int value = setting.currentValue.toInt(&ok);
            if (ok) {
                spinBox->setValue(value);
            }
            connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this, moduleName, settingPath](int value) {
                if (!updatingFromCode) {
                    emit settingChanged(moduleName, settingPath, QString::number(value));
                }
            });
            return spinBox;
        }

        case SettingType::DOUBLE: {
            QDoubleSpinBox *spinBox = new QDoubleSpinBox(this);
            if (setting.hasMin) {
                spinBox->setMinimum(setting.minValue);
            } else {
                spinBox->setMinimum(-999999.0);
            }
            if (setting.hasMax) {
                spinBox->setMaximum(setting.maxValue);
            } else {
                spinBox->setMaximum(999999.0);
            }
            spinBox->setDecimals(2);
            spinBox->setSingleStep(0.1);
            bool ok;
            double value = setting.currentValue.toDouble(&ok);
            if (ok) {
                spinBox->setValue(value);
            }
            connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this, moduleName, settingPath](double value) {
                if (!updatingFromCode) {
                    emit settingChanged(moduleName, settingPath, QString::number(value));
                }
            });
            return spinBox;
        }

        case SettingType::ENUM: {
            QComboBox *comboBox = new QComboBox(this);
            for (const QString &option : setting.possibleValues) {
                comboBox->addItem(option);
            }
            comboBox->setCurrentText(setting.currentValue);
            connect(comboBox, &QComboBox::currentTextChanged, this,
                    [this, moduleName, settingPath](const QString &text) {
                if (!updatingFromCode) {
                    emit settingChanged(moduleName, settingPath, text);
                }
            });
            return comboBox;
        }

        case SettingType::BLOCK:
        case SettingType::ITEM: {
            QLabel *label = new QLabel(this);
            QString displayValue = setting.currentValue;

            if (displayValue.length() > MAX_DISPLAY_LENGTH) {
                label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
                label->setToolTip(displayValue + "\n\n(Edit this setting in-game - complex format)");
            } else {
                label->setText(displayValue);
                label->setToolTip("Edit this setting in-game - complex format");
            }

            label->setStyleSheet("QLabel { padding: 2px; }");
            return label;
        }

        case SettingType::STRING:
        case SettingType::COLOR:
        case SettingType::KEYBIND:
        default: {
            QLineEdit *lineEdit = new QLineEdit(this);
            lineEdit->setText(setting.currentValue);
            lineEdit->setProperty("originalValue", setting.currentValue);
            connect(lineEdit, &QLineEdit::editingFinished, this, [this, lineEdit, moduleName, settingPath]() {
                if (!updatingFromCode) {
                    QString newValue = lineEdit->text();
                    QString originalValue = lineEdit->property("originalValue").toString();
                    if (newValue != originalValue) {
                        lineEdit->setProperty("originalValue", newValue);
                        emit settingChanged(moduleName, settingPath, newValue);
                    }
                }
            });
            return lineEdit;
        }
    }
}

void MeteorModulesWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (updatingFromCode || column != 0) {
        return;
    }

    QString itemType = item->data(0, ItemTypeRole).toString();

    if (itemType == "module") {
        QString moduleName = item->data(0, ItemDataRole).toString();
        bool enabled = item->checkState(0) == Qt::Checked;
        emit moduleToggled(moduleName, enabled);
    }
}

void MeteorModulesWidget::onFilterTextChanged(const QString &text)
{
    Q_UNUSED(text);
    populateTree();
}

void MeteorModulesWidget::onCategoryFilterChanged(const QString &category)
{
    Q_UNUSED(category);
    populateTree();
}
