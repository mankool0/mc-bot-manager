#include "ESPBlockDataMapEditorDialog.h"
#include "SettingEditorFactory.h"
#include "bot/BotManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QColorDialog>
#include <QEvent>
#include <QComboBox>
#include <QCheckBox>

ESPBlockDataMapEditorDialog::ESPBlockDataMapEditorDialog(const QString &settingName,
                                                           const QStringList &possibleBlockNames,
                                                           const ESPBlockDataMap &currentMap,
                                                           QWidget *parent)
    : QDialog(parent)
    , settingName(settingName)
    , possibleBlockNames(possibleBlockNames)
    , mapData(currentMap)
{
    setupUI();
    populateTable();
}

ESPBlockDataMapEditorDialog::~ESPBlockDataMapEditorDialog()
{
}

void ESPBlockDataMapEditorDialog::setupUI()
{
    setWindowTitle(QString("Edit ESP Block Map: %1").arg(settingName));
    setMinimumSize(800, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *filterLayout = new QHBoxLayout();
    QLabel *filterLabel = new QLabel("Filter:", this);
    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Search block names...");
    filterLayout->addWidget(filterLabel);
    filterLayout->addWidget(filterEdit);
    mainLayout->addLayout(filterLayout);

    table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Block Name", "ESP Settings"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, 250);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(table);

    countLabel = new QLabel(this);
    mainLayout->addWidget(countLabel);

    QHBoxLayout *buttonsLayout = new QHBoxLayout();
    addButton = new QPushButton("Add Block", this);
    removeButton = new QPushButton("Remove Block", this);
    editButton = new QPushButton("Edit Settings...", this);
    clearAllButton = new QPushButton("Clear All", this);

    buttonsLayout->addWidget(addButton);
    buttonsLayout->addWidget(removeButton);
    buttonsLayout->addWidget(editButton);
    buttonsLayout->addWidget(clearAllButton);
    buttonsLayout->addStretch();
    mainLayout->addLayout(buttonsLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(addButton, &QPushButton::clicked, this, &ESPBlockDataMapEditorDialog::onAddClicked);
    connect(removeButton, &QPushButton::clicked, this, &ESPBlockDataMapEditorDialog::onRemoveClicked);
    connect(editButton, &QPushButton::clicked, this, &ESPBlockDataMapEditorDialog::onEditClicked);
    connect(clearAllButton, &QPushButton::clicked, this, &ESPBlockDataMapEditorDialog::onClearAllClicked);
    connect(table, &QTableWidget::cellDoubleClicked, this, &ESPBlockDataMapEditorDialog::onTableDoubleClicked);
    connect(filterEdit, &QLineEdit::textChanged, this, &ESPBlockDataMapEditorDialog::onFilterChanged);
    connect(table, &QTableWidget::itemSelectionChanged, this, &ESPBlockDataMapEditorDialog::onSelectionChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Initial button state
    onSelectionChanged();
}

void ESPBlockDataMapEditorDialog::populateTable()
{
    updateTable();
}

void ESPBlockDataMapEditorDialog::updateTable()
{
    table->setRowCount(0);
    filteredBlockNames.clear();

    QString filterText = filterEdit->text().toLower();

    QStringList blockNames = mapData.keys();
    std::sort(blockNames.begin(), blockNames.end());

    for (const QString &blockName : blockNames) {
        if (!filterText.isEmpty() && !blockName.toLower().contains(filterText)) {
            continue;
        }

        filteredBlockNames.append(blockName);

        int row = table->rowCount();
        table->insertRow(row);

        // Block name column (read-only)
        QTableWidgetItem *nameItem = new QTableWidgetItem(blockName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, nameItem);

        // ESP settings preview column - use widget for visual preview
        QWidget *previewWidget = createPreviewWidget(mapData[blockName]);
        table->setCellWidget(row, 1, previewWidget);
    }

    updateCountLabel();
}

void ESPBlockDataMapEditorDialog::updateCountLabel()
{
    int totalBlocks = mapData.size();
    int visibleBlocks = filteredBlockNames.size();

    if (totalBlocks == visibleBlocks) {
        countLabel->setText(QString("%1 block(s) configured").arg(totalBlocks));
    } else {
        countLabel->setText(QString("Showing %1 of %2 block(s)").arg(visibleBlocks).arg(totalBlocks));
    }
}

QString ESPBlockDataMapEditorDialog::createPreviewText(const ESPBlockData &data) const
{
    QString shapeText;
    switch (data.shapeMode) {
        case ESPBlockData::Lines: shapeText = "Lines"; break;
        case ESPBlockData::Sides: shapeText = "Sides"; break;
        case ESPBlockData::Both: shapeText = "Both"; break;
    }

    return QString("Shape: %1, Tracer: %2").arg(shapeText, data.tracer ? "Yes" : "No");
}

QWidget* ESPBlockDataMapEditorDialog::createPreviewWidget(const ESPBlockData &data)
{
    QWidget *container = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    QString shapeText;
    switch (data.shapeMode) {
        case ESPBlockData::Lines: shapeText = "Lines"; break;
        case ESPBlockData::Sides: shapeText = "Sides"; break;
        case ESPBlockData::Both: shapeText = "Both"; break;
    }
    QLabel *shapeLabel = new QLabel("Shape: " + shapeText, container);
    layout->addWidget(shapeLabel);

    auto createColorSwatch = [container](const RGBAColor &color, const QString &label) -> QWidget* {
        QWidget *colorWidget = new QWidget(container);
        QHBoxLayout *colorLayout = new QHBoxLayout(colorWidget);
        colorLayout->setContentsMargins(0, 0, 0, 0);
        colorLayout->setSpacing(4);

        QLabel *labelWidget = new QLabel(label + ":", colorWidget);
        colorLayout->addWidget(labelWidget);

        QLabel *colorSwatch = new QLabel(colorWidget);
        colorSwatch->setFixedSize(40, 20);
        colorSwatch->setFrameStyle(QFrame::Box);
        colorSwatch->setLineWidth(1);

        QPixmap colorPixmap(40, 20);
        QPainter painter(&colorPixmap);

        QColor lightGray(204, 204, 204);
        QColor darkGray(255, 255, 255);
        for (int y = 0; y < 20; y += 10) {
            for (int x = 0; x < 40; x += 10) {
                bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
            }
        }

        painter.fillRect(0, 0, 40, 20, QColor(color.red, color.green, color.blue, color.alpha));
        painter.end();

        colorSwatch->setPixmap(colorPixmap);
        colorLayout->addWidget(colorSwatch);

        return colorWidget;
    };

    layout->addWidget(createColorSwatch(data.lineColor, "Line"));
    layout->addWidget(createColorSwatch(data.sideColor, "Side"));

    QLabel *tracerLabel = new QLabel(QString("Tracer: %1").arg(data.tracer ? "Yes" : "No"), container);
    layout->addWidget(tracerLabel);

    if (data.tracer) {
        layout->addWidget(createColorSwatch(data.tracerColor, "Color"));
    }

    layout->addStretch();

    return container;
}

void ESPBlockDataMapEditorDialog::editBlockSettings(const QString &blockName, const ESPBlockData &currentData)
{
    QDialog *editDialog = new QDialog(this);
    editDialog->setWindowTitle(QString("Edit ESP Settings: %1").arg(blockName));
    editDialog->setMinimumWidth(400);

    QVBoxLayout *layout = new QVBoxLayout(editDialog);

    QLabel *infoLabel = new QLabel(QString("Block: <b>%1</b>").arg(blockName), editDialog);
    layout->addWidget(infoLabel);

    SettingEditorContext context;
    context.name = blockName;
    context.parent = editDialog;

    ESPBlockData editableData = currentData;
    ESPBlockData *resultData = new ESPBlockData();

    auto onChange = [resultData](const QVariant &value) {
        *resultData = value.value<ESPBlockData>();
    };

    QWidget *editorWidget = SettingEditorFactory::instance().createEditor(
        SettingSystemType::Meteor,
        static_cast<int>(mankool::mcbot::protocol::SettingInfo::SettingType::ESP_BLOCK_DATA),
        QVariant::fromValue(editableData),
        context,
        onChange
    );

    if (editorWidget) {
        layout->addWidget(editorWidget);

        // Install event filters on nested color labels so they're clickable
        QList<QLabel*> colorLabels = editorWidget->findChildren<QLabel*>();
        for (QLabel* colorLabel : colorLabels) {
            if (colorLabel->property("colorR").isValid()) {
                colorLabel->installEventFilter(this);
            }
        }
    }

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, editDialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, editDialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, editDialog, &QDialog::reject);

    if (editDialog->exec() == QDialog::Accepted) {
        mapData[blockName] = *resultData;
        updateTable();
    }

    delete resultData;
    delete editDialog;
}

void ESPBlockDataMapEditorDialog::addNewBlock()
{
    QString blockName;

    if (!possibleBlockNames.isEmpty()) {
        // Filter out blocks that are already in the map
        QStringList availableBlocks;
        for (const QString &block : possibleBlockNames) {
            if (!mapData.contains(block)) {
                availableBlocks.append(block);
            }
        }

        if (availableBlocks.isEmpty()) {
            QMessageBox::information(this, "All Blocks Used", "All available blocks have already been configured.");
            return;
        }

        availableBlocks.sort(Qt::CaseInsensitive);

        QDialog inputDialog(this);
        inputDialog.setWindowTitle("Add New Block");
        inputDialog.setMinimumSize(400, 300);

        QVBoxLayout *layout = new QVBoxLayout(&inputDialog);

        QLabel *label = new QLabel("Select block:", &inputDialog);
        layout->addWidget(label);

        QLineEdit *filterEdit = new QLineEdit(&inputDialog);
        filterEdit->setPlaceholderText("Search blocks...");
        layout->addWidget(filterEdit);

        QListWidget *blockList = new QListWidget(&inputDialog);
        blockList->addItems(availableBlocks);
        blockList->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(blockList);

        QLabel *countLabel = new QLabel(&inputDialog);
        countLabel->setText(QString("Total: %1").arg(availableBlocks.size()));
        layout->addWidget(countLabel);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &inputDialog);
        layout->addWidget(buttonBox);

        auto updateFilter = [blockList, filterEdit, countLabel, availableBlocks]() {
            QString filterText = filterEdit->text();
            int visibleCount = 0;

            for (int i = 0; i < blockList->count(); ++i) {
                QListWidgetItem *item = blockList->item(i);
                if (!item) continue;

                bool matches = filterText.isEmpty() ||
                              item->text().contains(filterText, Qt::CaseInsensitive);
                item->setHidden(!matches);
                if (matches) visibleCount++;
            }

            if (filterText.isEmpty()) {
                countLabel->setText(QString("Total: %1").arg(availableBlocks.size()));
            } else {
                countLabel->setText(QString("Showing: %1 / Total: %2")
                                   .arg(visibleCount).arg(availableBlocks.size()));
            }
        };

        connect(filterEdit, &QLineEdit::textChanged, updateFilter);
        connect(buttonBox, &QDialogButtonBox::accepted, &inputDialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &inputDialog, &QDialog::reject);
        connect(blockList, &QListWidget::itemDoubleClicked, &inputDialog, &QDialog::accept);

        if (inputDialog.exec() == QDialog::Accepted) {
            QListWidgetItem *selectedItem = blockList->currentItem();
            if (selectedItem) {
                blockName = selectedItem->text();
            } else {
                return;
            }
        } else {
            return;
        }
    } else {
        // Free-form text entry
        bool ok;
        blockName = QInputDialog::getText(this, "Add Block",
                                          "Enter block name:",
                                          QLineEdit::Normal,
                                          "",
                                          &ok);
        if (!ok || blockName.isEmpty()) {
            return;
        }
    }

    if (mapData.contains(blockName)) {
        QMessageBox::warning(this, "Block Exists",
                            QString("Block '%1' is already configured. Use Edit to modify it.").arg(blockName));
        return;
    }

    ESPBlockData defaultData;
    defaultData.shapeMode = ESPBlockData::Both;
    defaultData.lineColor = {255, 255, 255, 255};
    defaultData.sideColor = {255, 255, 255, 50};
    defaultData.tracer = false;
    defaultData.tracerColor = {255, 255, 255, 255};

    mapData[blockName] = defaultData;

    editBlockSettings(blockName, defaultData);

    updateTable();
}

void ESPBlockDataMapEditorDialog::onAddClicked()
{
    addNewBlock();
}

void ESPBlockDataMapEditorDialog::onRemoveClicked()
{
    int currentRow = table->currentRow();
    if (currentRow < 0 || currentRow >= filteredBlockNames.size()) {
        return;
    }

    QString blockName = filteredBlockNames[currentRow];

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Remove Block",
        QString("Remove ESP settings for '%1'?").arg(blockName),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        mapData.remove(blockName);
        updateTable();
    }
}

void ESPBlockDataMapEditorDialog::onEditClicked()
{
    int currentRow = table->currentRow();
    if (currentRow < 0 || currentRow >= filteredBlockNames.size()) {
        return;
    }

    QString blockName = filteredBlockNames[currentRow];
    editBlockSettings(blockName, mapData[blockName]);
}

void ESPBlockDataMapEditorDialog::onClearAllClicked()
{
    if (mapData.isEmpty()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear All",
        QString("Remove all %1 ESP block configurations?").arg(mapData.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        mapData.clear();
        updateTable();
    }
}

void ESPBlockDataMapEditorDialog::onTableDoubleClicked(int row, int column)
{
    Q_UNUSED(column);

    if (row >= 0 && row < filteredBlockNames.size()) {
        QString blockName = filteredBlockNames[row];
        editBlockSettings(blockName, mapData[blockName]);
    }
}

void ESPBlockDataMapEditorDialog::onFilterChanged(const QString &text)
{
    Q_UNUSED(text);
    updateTable();
}

void ESPBlockDataMapEditorDialog::onSelectionChanged()
{
    bool hasSelection = table->currentRow() >= 0;
    removeButton->setEnabled(hasSelection);
    editButton->setEnabled(hasSelection);
}

bool ESPBlockDataMapEditorDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label && label->property("colorR").isValid()) {
            int r = label->property("colorR").toInt();
            int g = label->property("colorG").toInt();
            int b = label->property("colorB").toInt();
            int a = label->property("colorA").toInt();

            QColor currentColor(r, g, b, a);
            QColor newColor = QColorDialog::getColor(currentColor, this, "Select Color",
                                                     QColorDialog::ShowAlphaChannel);

            if (newColor.isValid()) {
                QPixmap colorPixmap(60, 25);
                QPainter painter(&colorPixmap);

                QColor lightGray(204, 204, 204);
                QColor darkGray(255, 255, 255);
                for (int y = 0; y < 25; y += 10) {
                    for (int x = 0; x < 60; x += 10) {
                        bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                        painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
                    }
                }

                painter.fillRect(0, 0, 60, 25, newColor);
                painter.end();

                label->setPixmap(colorPixmap);
                label->setProperty("colorR", newColor.red());
                label->setProperty("colorG", newColor.green());
                label->setProperty("colorB", newColor.blue());
                label->setProperty("colorA", newColor.alpha());

                // Find parent ESP container and trigger its onChange callback
                QWidget *parent = label->parentWidget();
                while (parent) {
                    if (parent->property("espChangeCallback").isValid()) {
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

                            auto callback = parent->property("espChangeCallback").value<SettingEditorFactory::ChangeCallback>();
                            if (callback) {
                                callback(QVariant::fromValue(newData));
                            }
                        }
                        break;
                    }
                    parent = parent->parentWidget();
                }
            }
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

ESPBlockDataMap ESPBlockDataMapEditorDialog::getMap() const
{
    return mapData;
}
