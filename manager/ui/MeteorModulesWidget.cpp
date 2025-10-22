#include "MeteorModulesWidget.h"
#include "SettingEditorFactory.h"
#include "ListEditorDialog.h"
#include "StringListEditorDialog.h"
#include "bot/BotManager.h"
#include <QHeaderView>
#include <QGroupBox>
#include <QColorDialog>
#include <QPainter>
#include <QPixmap>
#include <QEvent>
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

    connect(filterEdit, &QLineEdit::textChanged, this, &MeteorModulesWidget::applyFilters);
    connect(categoryFilter, &QComboBox::currentTextChanged, this, &MeteorModulesWidget::applyFilters);
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
    QMap<QString, bool> moduleExpansion;
    QMap<QString, QMap<QString, bool>> groupExpansion;

    for (auto it = moduleItems.constBegin(); it != moduleItems.constEnd(); ++it) {
        QString moduleName = it.key();
        QTreeWidgetItem *moduleItem = it.value();
        moduleExpansion[moduleName] = moduleItem->isExpanded();

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

    QVector<MeteorModuleData> sortedModules;
    for (auto it = allModules.constBegin(); it != allModules.constEnd(); ++it) {
        sortedModules.append(it.value());
    }
    std::sort(sortedModules.begin(), sortedModules.end(),
              [](const MeteorModuleData &a, const MeteorModuleData &b) {
                  return a.name.toLower() < b.name.toLower();
              });

    for (const auto &module : sortedModules) {
        QTreeWidgetItem *item = createModuleItem(module);
        moduleTree->addTopLevelItem(item);
        moduleItems[module.name] = item;

        if (moduleExpansion.contains(module.name)) {
            item->setExpanded(moduleExpansion[module.name]);
        }

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

    applyFilters();
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
                settingItem->setText(1, setting.currentValue.toString());
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
        settingItem->setText(1, setting.currentValue.toString());
        return;
    }

    SettingEditorContext context;
    context.name = setting.name;
    context.description = setting.description;
    context.minValue = setting.minValue;
    context.maxValue = setting.maxValue;
    context.hasMin = setting.hasMin;
    context.hasMax = setting.hasMax;
    context.possibleValues = setting.possibleValues;
    context.parent = this;

    SettingEditorFactory::instance().updateWidget(
        SettingSystemType::Meteor,
        static_cast<int>(setting.type),
        widget,
        setting.currentValue,
        context
    );
}

QWidget* MeteorModulesWidget::createSettingEditor(const MeteorSettingData &setting,
                                                    const QString &moduleName,
                                                    const QString &settingPath)
{
    SettingEditorContext context;
    context.name = setting.name;
    context.description = setting.description;
    context.minValue = setting.minValue;
    context.maxValue = setting.maxValue;
    context.hasMin = setting.hasMin;
    context.hasMax = setting.hasMax;
    context.possibleValues = setting.possibleValues;
    context.parent = this;

    // Create the change callback that respects updatingFromCode flag
    auto onChange = [this, moduleName, settingPath](const QVariant& value) {
        if (!updatingFromCode) {
            emit settingChanged(moduleName, settingPath, value);
        }
    };

    QWidget* widget = SettingEditorFactory::instance().createEditor(
        SettingSystemType::Meteor,
        static_cast<int>(setting.type),
        setting.currentValue,
        context,
        onChange
    );

    // For widgets that need event filtering (labels with dialogs), add module/setting context
    if (widget) {
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            label->setProperty("moduleName", moduleName);
            label->setProperty("settingPath", settingPath);

            label->installEventFilter(this);
        } else if (widget->property("espChangeCallback").isValid()) {
            // ESP_BLOCK_DATA container - add module/setting context
            widget->setProperty("moduleName", moduleName);
            widget->setProperty("settingPath", settingPath);

            // Find all nested color labels and install event filters
            QList<QLabel*> colorLabels = widget->findChildren<QLabel*>();
            for (QLabel* colorLabel : colorLabels) {
                if (colorLabel->property("colorR").isValid()) {
                    colorLabel->installEventFilter(this);
                }
            }
        }
    }

    return widget;
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

void MeteorModulesWidget::applyFilters()
{
    QString filterText = filterEdit->text().toLower();
    QString selectedCategory = categoryFilter->currentText();

    for (auto it = moduleItems.constBegin(); it != moduleItems.constEnd(); ++it) {
        const QString &moduleName = it.key();
        QTreeWidgetItem *item = it.value();
        const MeteorModuleData &module = allModules[moduleName];

        bool matchesText = filterText.isEmpty() || module.name.toLower().contains(filterText);
        bool matchesCategory = selectedCategory == "All" || module.category == selectedCategory;

        item->setHidden(!matchesText || !matchesCategory);
    }
}

QLabel* MeteorModulesWidget::createColorLabel(const RGBAColor &color, QWidget *parent)
{
    QLabel *colorLabel = new QLabel(parent);
    colorLabel->setFixedSize(60, 25);
    colorLabel->setFrameStyle(QFrame::Box);
    colorLabel->setLineWidth(1);

    // Create pixmap with checkerboard pattern
    QPixmap colorPixmap(60, 25);
    QPainter painter(&colorPixmap);

    // Draw checkerboard background
    QColor lightGray(204, 204, 204);
    QColor darkGray(255, 255, 255);
    for (int y = 0; y < 25; y += 10) {
        for (int x = 0; x < 60; x += 10) {
            bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
            painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
        }
    }

    // Draw the color with alpha on top
    painter.fillRect(0, 0, 60, 25, QColor(color.red, color.green, color.blue, color.alpha));
    painter.end();

    colorLabel->setPixmap(colorPixmap);
    colorLabel->setCursor(Qt::PointingHandCursor);

    // Store color for click handling
    colorLabel->setProperty("colorR", color.red);
    colorLabel->setProperty("colorG", color.green);
    colorLabel->setProperty("colorB", color.blue);
    colorLabel->setProperty("colorA", color.alpha);

    // Install event filter for click handling
    colorLabel->installEventFilter(this);

    return colorLabel;
}

bool MeteorModulesWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label) {
            // Check if this is a list with possible values (uses ListEditorDialog)
            if (label->property("isListWithPossibleValues").toBool()) {
                QString moduleName = label->property("moduleName").toString();
                QString settingPath = label->property("settingPath").toString();
                QString settingName = label->property("settingName").toString();
                QStringList possibleValues = label->property("possibleValues").toStringList();

                if (!possibleValues.isEmpty()) {
                    // Parse current selected items from the label text
                    QString currentText = label->text();
                    QStringList currentItems;
                    if (!currentText.isEmpty() && currentText != "(empty - click to add)") {
                        // Remove the "..." suffix if truncated
                        if (currentText.endsWith("...")) {
                            // Get the full text from tooltip
                            QString tooltip = label->toolTip();
                            int newlinePos = tooltip.indexOf('\n');
                            if (newlinePos > 0) {
                                currentText = tooltip.left(newlinePos);
                            }
                        }
                        // Parse the comma-separated list
                        currentItems = currentText.split(", ", Qt::SkipEmptyParts);
                    }

                    // Open the list editor dialog
                    ListEditorDialog dialog(settingName, possibleValues, currentItems, this);
                    if (dialog.exec() == QDialog::Accepted) {
                        QStringList newItems = dialog.getSelectedItems();

                        // Update the label
                        QString newText = newItems.join(", ");
                        if (newText.length() > MAX_DISPLAY_LENGTH) {
                            label->setText(newText.left(TRUNCATE_LENGTH) + "...");
                            label->setToolTip(newText + "\n\nClick to edit");
                        } else {
                            label->setText(newText.isEmpty() ? "(empty - click to add)" : newText);
                            label->setToolTip("Click to edit");
                        }

                        // Emit setting changed signal
                        emit settingChanged(moduleName, settingPath, QVariant(newItems));
                    }
                }

                return true;
            }

            // Check if this is a free-form string list (uses StringListEditorDialog)
            if (label->property("isFreeFormStringList").toBool()) {
                QString moduleName = label->property("moduleName").toString();
                QString settingPath = label->property("settingPath").toString();
                QString settingName = label->property("settingName").toString();

                // Parse current items from the label text
                QString currentText = label->text();
                QStringList currentItems;
                if (!currentText.isEmpty() && currentText != "(empty - click to add)") {
                    // Remove the "..." suffix if truncated
                    if (currentText.endsWith("...")) {
                        // Get the full text from tooltip
                        QString tooltip = label->toolTip();
                        int newlinePos = tooltip.indexOf('\n');
                        if (newlinePos > 0) {
                            currentText = tooltip.left(newlinePos);
                        }
                    }
                    // Parse the comma-separated list
                    currentItems = currentText.split(", ", Qt::SkipEmptyParts);
                }

                // Open the string list editor dialog
                StringListEditorDialog dialog(settingName, currentItems, this);
                if (dialog.exec() == QDialog::Accepted) {
                    QStringList newItems = dialog.getItems();

                    // Update the label
                    QString newText = newItems.join(", ");
                    if (newText.length() > MAX_DISPLAY_LENGTH) {
                        label->setText(newText.left(TRUNCATE_LENGTH) + "...");
                        label->setToolTip(newText + "\n\nClick to edit");
                    } else {
                        label->setText(newText.isEmpty() ? "(empty - click to add)" : newText);
                        label->setToolTip("Click to edit");
                    }

                    // Emit setting changed signal
                    emit settingChanged(moduleName, settingPath, QVariant(newItems));
                }

                return true;
            }

            // Check if this is a color label (must have colorR property set)
            if (label->property("colorR").isValid()) {
                int r = label->property("colorR").toInt();
                int g = label->property("colorG").toInt();
                int b = label->property("colorB").toInt();
                int a = label->property("colorA").toInt();

                QColor currentColor(r, g, b, a);
                QColor newColor = QColorDialog::getColor(currentColor, this, "Select Color",
                                                         QColorDialog::ShowAlphaChannel);

                if (newColor.isValid()) {
                    // Update the pixmap with new color
                    QPixmap colorPixmap(60, 25);
                    QPainter painter(&colorPixmap);

                    // Draw checkerboard background
                    QColor lightGray(204, 204, 204);
                    QColor darkGray(255, 255, 255);
                    for (int y = 0; y < 25; y += 10) {
                        for (int x = 0; x < 60; x += 10) {
                            bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                            painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
                        }
                    }

                    // Draw the color with alpha on top
                    painter.fillRect(0, 0, 60, 25, newColor);
                    painter.end();

                    label->setPixmap(colorPixmap);
                    label->setProperty("colorR", newColor.red());
                    label->setProperty("colorG", newColor.green());
                    label->setProperty("colorB", newColor.blue());
                    label->setProperty("colorA", newColor.alpha());

                    // Check if this is a standalone color setting or part of ESPBlockData
                    if (label->property("moduleName").isValid()) {
                        // Standalone color setting
                        QString moduleName = label->property("moduleName").toString();
                        QString settingPath = label->property("settingPath").toString();

                        RGBAColor rgbaColor;
                        rgbaColor.red = newColor.red();
                        rgbaColor.green = newColor.green();
                        rgbaColor.blue = newColor.blue();
                        rgbaColor.alpha = newColor.alpha();

                        emit settingChanged(moduleName, settingPath, QVariant::fromValue(rgbaColor));
                    } else {
                        // Part of ESPBlockData - find parent container and emit change directly
                        QWidget *parent = label->parentWidget();
                        while (parent) {
                            // Check if this is an ESPBlockData container by looking for stored properties
                            if (parent->property("moduleName").isValid() && parent->property("settingPath").isValid()) {
                                QString moduleName = parent->property("moduleName").toString();
                                QString settingPath = parent->property("settingPath").toString();

                                // Collect all ESP data from the container
                                QComboBox *shapeCombo = parent->findChild<QComboBox*>();
                                QCheckBox *tracerCheck = parent->findChild<QCheckBox*>();
                                QLabel *lineColorLabel = parent->findChild<QLabel*>("lineColor");
                                QLabel *sideColorLabel = parent->findChild<QLabel*>("sideColor");
                                QLabel *tracerColorLabel = parent->findChild<QLabel*>("tracerColor");

                                if (shapeCombo && tracerCheck && lineColorLabel && sideColorLabel && tracerColorLabel) {
                                    ESPBlockData newData;
                                    newData.shapeMode = static_cast<ESPBlockData::ShapeMode>(shapeCombo->currentData().toInt());

                                    newData.lineColor.red = lineColorLabel->property("colorR").toInt();
                                    newData.lineColor.green = lineColorLabel->property("colorG").toInt();
                                    newData.lineColor.blue = lineColorLabel->property("colorB").toInt();
                                    newData.lineColor.alpha = lineColorLabel->property("colorA").toInt();

                                    newData.sideColor.red = sideColorLabel->property("colorR").toInt();
                                    newData.sideColor.green = sideColorLabel->property("colorG").toInt();
                                    newData.sideColor.blue = sideColorLabel->property("colorB").toInt();
                                    newData.sideColor.alpha = sideColorLabel->property("colorA").toInt();

                                    newData.tracer = tracerCheck->isChecked();

                                    newData.tracerColor.red = tracerColorLabel->property("colorR").toInt();
                                    newData.tracerColor.green = tracerColorLabel->property("colorG").toInt();
                                    newData.tracerColor.blue = tracerColorLabel->property("colorB").toInt();
                                    newData.tracerColor.alpha = tracerColorLabel->property("colorA").toInt();

                                    emit settingChanged(moduleName, settingPath, QVariant::fromValue(newData));
                                }
                                break;
                            }
                            parent = parent->parentWidget();
                        }
                    }
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
