#include "BaritoneWidget.h"
#include "SettingEditorFactory.h"
#include "bot/BotManager.h"
#include "logging/LogManager.h"
#include <QHeaderView>
#include <QColorDialog>
#include <QEvent>
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

    connect(filterEdit, &QLineEdit::textChanged, this, &BaritoneWidget::applyFilter);
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

        QTreeWidgetItem *item = createSettingItem(setting);
        settingTree->addTopLevelItem(item);
        settingItems[settingName] = item;

        QWidget *editor = createSettingEditor(setting, settingName);
        if (editor) {
            settingTree->setItemWidget(item, 1, editor);
        } else {
            // No editor widget - display value as text in the tree item
            item->setText(1, setting.currentValue.toString());
        }
    }

    applyFilter();
}

QTreeWidgetItem* BaritoneWidget::createSettingItem(const BaritoneSettingData &setting)
{
    QTreeWidgetItem *settingItem = new QTreeWidgetItem();

    QString settingName = setting.name.trimmed();
    settingItem->setText(0, settingName);
    settingItem->setData(0, SettingNameRole, settingName);
    settingItem->setData(0, SettingTypeRole, static_cast<int>(setting.type));

    QString tooltip = settingName;
    if (!setting.description.isEmpty()) {
        tooltip += "\n" + setting.description;
    }
    if (setting.defaultValue.isValid() && !setting.defaultValue.isNull()) {
        tooltip += "\nDefault: " + setting.defaultValue.toString();
    }
    settingItem->setToolTip(0, tooltip);

    return settingItem;
}

void BaritoneWidget::updateSettingWidget(QTreeWidgetItem *settingItem,
                                          const BaritoneSettingData &setting)
{
    QWidget *widget = settingTree->itemWidget(settingItem, 1);
    if (!widget) {
        settingItem->setText(1, setting.currentValue.toString());
        return;
    }

    SettingEditorContext context;
    context.name = setting.name;
    context.description = setting.description;
    context.defaultValue = setting.defaultValue;
    context.parent = this;

    SettingEditorFactory::instance().updateWidget(
        SettingSystemType::Baritone,
        static_cast<int>(setting.type),
        widget,
        setting.currentValue,
        context
    );
}

QWidget* BaritoneWidget::createSettingEditor(const BaritoneSettingData &setting,
                                               const QString &settingName)
{
    SettingEditorContext context;
    context.name = setting.name;
    context.description = setting.description;
    context.defaultValue = setting.defaultValue;
    context.parent = this;

    auto onChange = [this, settingName](const QVariant& value) {
        if (!updatingFromCode) {
            emit settingChanged(settingName, value);
        }
    };

    QWidget* widget = SettingEditorFactory::instance().createEditor(
        SettingSystemType::Baritone,
        static_cast<int>(setting.type),
        setting.currentValue,
        context,
        onChange
    );

    if (widget) {
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            if (label->property("isBaritoneRGB").toBool()) {
                label->setProperty("settingName", settingName);
                label->installEventFilter(this);
            }
        }
    }

    return widget;
}

void BaritoneWidget::applyFilter()
{
    QString filterText = filterEdit->text().toLower();

    for (auto it = settingItems.constBegin(); it != settingItems.constEnd(); ++it) {
        const QString &settingName = it.key();
        QTreeWidgetItem *item = it.value();

        bool matches = filterText.isEmpty() || settingName.toLower().contains(filterText);
        item->setHidden(!matches);
    }
}

bool BaritoneWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label && label->property("isBaritoneRGB").toBool()) {
            // Get current color from properties
            int r = label->property("colorR").toInt();
            int g = label->property("colorG").toInt();
            int b = label->property("colorB").toInt();

            QColor currentColor(r, g, b);
            QColor newColor = QColorDialog::getColor(currentColor, this, "Select Color");

            if (newColor.isValid()) {
                // Update the pixmap with new color
                QPixmap colorPixmap(60, 25);
                colorPixmap.fill(newColor);

                label->setPixmap(colorPixmap);
                label->setProperty("colorR", newColor.red());
                label->setProperty("colorG", newColor.green());
                label->setProperty("colorB", newColor.blue());

                auto callback = label->property("changeCallback").value<SettingEditorFactory::ChangeCallback>();
                if (callback) {
                    RGBColor rgbColor{
                        static_cast<uint32_t>(newColor.red()),
                        static_cast<uint32_t>(newColor.green()),
                        static_cast<uint32_t>(newColor.blue())
                    };
                    callback(QVariant::fromValue(rgbColor));
                }
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
