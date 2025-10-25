#include "MapEditorDialog.h"
#include "ListEditorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>

MapEditorDialog::MapEditorDialog(const QString &settingName,
                                 const QStringList &possibleKeys,
                                 const QStringList &possibleValues,
                                 const QMap<QString, QStringList> &currentMap,
                                 QWidget *parent)
    : QDialog(parent)
    , settingName(settingName)
    , possibleKeys(possibleKeys)
    , possibleValues(possibleValues)
    , mapData(currentMap)
{
    setupUI();
    populateTable();
}

MapEditorDialog::~MapEditorDialog()
{
}

void MapEditorDialog::setupUI()
{
    setWindowTitle(QString("Edit Map: %1").arg(settingName));
    setMinimumSize(700, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *filterLayout = new QHBoxLayout();
    QLabel *filterLabel = new QLabel("Filter:", this);
    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Search keys or values...");
    filterLayout->addWidget(filterLabel);
    filterLayout->addWidget(filterEdit);
    mainLayout->addLayout(filterLayout);

    table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Key", "Values (comma-separated)"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, 200);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(table);

    countLabel = new QLabel(this);
    mainLayout->addWidget(countLabel);

    QHBoxLayout *buttonsLayout = new QHBoxLayout();
    addButton = new QPushButton("Add Entry", this);
    removeButton = new QPushButton("Remove Entry", this);
    editKeyButton = new QPushButton("Edit Key", this);
    editButton = new QPushButton("Edit Values...", this);
    clearAllButton = new QPushButton("Clear All", this);

    buttonsLayout->addWidget(addButton);
    buttonsLayout->addWidget(removeButton);
    buttonsLayout->addWidget(editKeyButton);
    buttonsLayout->addWidget(editButton);
    buttonsLayout->addWidget(clearAllButton);
    buttonsLayout->addStretch();
    mainLayout->addLayout(buttonsLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(addButton, &QPushButton::clicked, this, &MapEditorDialog::onAddClicked);
    connect(removeButton, &QPushButton::clicked, this, &MapEditorDialog::onRemoveClicked);
    connect(editKeyButton, &QPushButton::clicked, this, &MapEditorDialog::onEditKeyClicked);
    connect(editButton, &QPushButton::clicked, this, &MapEditorDialog::onEditValuesClicked);
    connect(clearAllButton, &QPushButton::clicked, this, &MapEditorDialog::onClearAllClicked);
    connect(table, &QTableWidget::cellDoubleClicked, this, &MapEditorDialog::onTableDoubleClicked);
    connect(filterEdit, &QLineEdit::textChanged, this, &MapEditorDialog::onFilterChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MapEditorDialog::populateTable()
{
    table->setRowCount(0);

    for (auto it = mapData.constBegin(); it != mapData.constEnd(); ++it) {
        int row = table->rowCount();
        table->insertRow(row);

        QTableWidgetItem *keyItem = new QTableWidgetItem(it.key());
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, keyItem);

        QTableWidgetItem *valuesItem = new QTableWidgetItem(it.value().join(", "));
        valuesItem->setFlags(valuesItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 1, valuesItem);
    }

    updateTable();
}

void MapEditorDialog::updateTable()
{
    QString filterText = filterEdit->text();
    int visibleCount = 0;

    for (int i = 0; i < table->rowCount(); ++i) {
        QTableWidgetItem *keyItem = table->item(i, 0);
        QTableWidgetItem *valueItem = table->item(i, 1);

        if (!keyItem) continue;

        bool matches = filterText.isEmpty() ||
                      keyItem->text().contains(filterText, Qt::CaseInsensitive) ||
                      (valueItem && valueItem->text().contains(filterText, Qt::CaseInsensitive));

        table->setRowHidden(i, !matches);
        if (matches) visibleCount++;
    }

    if (filterText.isEmpty()) {
        countLabel->setText(QString("Total entries: %1").arg(table->rowCount()));
    } else {
        countLabel->setText(QString("Showing: %1 / Total: %2").arg(visibleCount).arg(table->rowCount()));
    }
}

void MapEditorDialog::onAddClicked()
{
    if (possibleKeys.isEmpty()) {
        QMessageBox::information(this, "No Keys Available", "No valid keys are available to add.");
        return;
    }

    QStringList availableKeys;
    for (const QString &key : possibleKeys) {
        if (!mapData.contains(key)) {
            availableKeys.append(key);
        }
    }

    if (availableKeys.isEmpty()) {
        QMessageBox::information(this, "All Keys Used", "All available keys have already been added.");
        return;
    }

    availableKeys.sort(Qt::CaseInsensitive);

    QDialog inputDialog(this);
    inputDialog.setWindowTitle("Add New Entry");
    inputDialog.setMinimumSize(400, 300);

    QVBoxLayout *layout = new QVBoxLayout(&inputDialog);

    QLabel *label = new QLabel("Select key:", &inputDialog);
    layout->addWidget(label);

    QLineEdit *filterEdit = new QLineEdit(&inputDialog);
    filterEdit->setPlaceholderText("Search keys...");
    layout->addWidget(filterEdit);

    QListWidget *keyList = new QListWidget(&inputDialog);
    keyList->addItems(availableKeys);
    keyList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(keyList);

    QLabel *countLabel = new QLabel(&inputDialog);
    countLabel->setText(QString("Total: %1").arg(availableKeys.size()));
    layout->addWidget(countLabel);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &inputDialog);
    layout->addWidget(buttonBox);

    auto updateFilter = [keyList, filterEdit, countLabel, availableKeys]() {
        QString filterText = filterEdit->text();
        int visibleCount = 0;

        for (int i = 0; i < keyList->count(); ++i) {
            QListWidgetItem *item = keyList->item(i);
            if (!item) continue;

            bool matches = filterText.isEmpty() ||
                          item->text().contains(filterText, Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches) visibleCount++;
        }

        if (filterText.isEmpty()) {
            countLabel->setText(QString("Total: %1").arg(availableKeys.size()));
        } else {
            countLabel->setText(QString("Showing: %1 / Total: %2")
                               .arg(visibleCount).arg(availableKeys.size()));
        }
    };

    connect(filterEdit, &QLineEdit::textChanged, updateFilter);
    connect(buttonBox, &QDialogButtonBox::accepted, &inputDialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &inputDialog, &QDialog::reject);
    connect(keyList, &QListWidget::itemDoubleClicked, &inputDialog, &QDialog::accept);

    if (inputDialog.exec() == QDialog::Accepted) {
        QListWidgetItem *selectedItem = keyList->currentItem();
        if (selectedItem) {
            QString newKey = selectedItem->text();
            mapData[newKey] = QStringList();
            populateTable();
        }
    }
}

void MapEditorDialog::onRemoveClicked()
{
    int currentRow = table->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "No Selection", "Please select an entry to remove.");
        return;
    }

    QTableWidgetItem *keyItem = table->item(currentRow, 0);
    if (!keyItem) return;

    QString key = keyItem->text();
    mapData.remove(key);
    populateTable();
}

void MapEditorDialog::onEditKeyClicked()
{
    int currentRow = table->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "No Selection", "Please select an entry to edit the key.");
        return;
    }

    QTableWidgetItem *keyItem = table->item(currentRow, 0);
    if (!keyItem) return;

    QString oldKey = keyItem->text();

    if (possibleKeys.isEmpty()) {
        QMessageBox::information(this, "No Keys Available", "No valid keys are available.");
        return;
    }

    QStringList availableKeys;
    for (const QString &key : possibleKeys) {
        if (!mapData.contains(key) || key == oldKey) {
            availableKeys.append(key);
        }
    }

    if (availableKeys.isEmpty()) {
        QMessageBox::information(this, "All Keys Used", "All available keys have already been added.");
        return;
    }

    availableKeys.sort(Qt::CaseInsensitive);

    QDialog inputDialog(this);
    inputDialog.setWindowTitle("Edit Key");
    inputDialog.setMinimumSize(400, 300);

    QVBoxLayout *layout = new QVBoxLayout(&inputDialog);

    QLabel *label = new QLabel("Select new key:", &inputDialog);
    layout->addWidget(label);

    QLineEdit *filterEdit = new QLineEdit(&inputDialog);
    filterEdit->setPlaceholderText("Search keys...");
    layout->addWidget(filterEdit);

    QListWidget *keyList = new QListWidget(&inputDialog);
    keyList->addItems(availableKeys);
    keyList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(keyList);

    // Pre-select current key
    for (int i = 0; i < keyList->count(); ++i) {
        if (keyList->item(i)->text() == oldKey) {
            keyList->setCurrentRow(i);
            break;
        }
    }

    QLabel *countLabel = new QLabel(&inputDialog);
    countLabel->setText(QString("Total: %1").arg(availableKeys.size()));
    layout->addWidget(countLabel);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &inputDialog);
    layout->addWidget(buttonBox);

    auto updateFilter = [keyList, filterEdit, countLabel, availableKeys]() {
        QString filterText = filterEdit->text();
        int visibleCount = 0;

        for (int i = 0; i < keyList->count(); ++i) {
            QListWidgetItem *item = keyList->item(i);
            if (!item) continue;

            bool matches = filterText.isEmpty() ||
                          item->text().contains(filterText, Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches) visibleCount++;
        }

        if (filterText.isEmpty()) {
            countLabel->setText(QString("Total: %1").arg(availableKeys.size()));
        } else {
            countLabel->setText(QString("Showing: %1 / Total: %2")
                               .arg(visibleCount).arg(availableKeys.size()));
        }
    };

    connect(filterEdit, &QLineEdit::textChanged, updateFilter);
    connect(buttonBox, &QDialogButtonBox::accepted, &inputDialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &inputDialog, &QDialog::reject);
    connect(keyList, &QListWidget::itemDoubleClicked, &inputDialog, &QDialog::accept);

    if (inputDialog.exec() == QDialog::Accepted) {
        QListWidgetItem *selectedItem = keyList->currentItem();
        if (selectedItem) {
            QString newKey = selectedItem->text();

            if (newKey != oldKey) {
                // Move the values to the new key
                QStringList values = mapData[oldKey];
                mapData.remove(oldKey);
                mapData[newKey] = values;
                populateTable();
            }
        }
    }
}

void MapEditorDialog::onEditValuesClicked()
{
    int currentRow = table->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "No Selection", "Please select an entry to edit.");
        return;
    }

    QTableWidgetItem *keyItem = table->item(currentRow, 0);
    if (!keyItem) return;

    QString key = keyItem->text();
    QStringList currentValues = mapData.value(key);

    ListEditorDialog dialog(QString("Values for '%1'").arg(key),
                           possibleValues,
                           currentValues,
                           this);

    if (dialog.exec() == QDialog::Accepted) {
        mapData[key] = dialog.getSelectedItems();
        populateTable();
    }
}

void MapEditorDialog::onTableDoubleClicked(int row, int column)
{
    if (row < 0) return;

    if (column == 1) {
        table->setCurrentCell(row, column);
        onEditValuesClicked();
    }
}

void MapEditorDialog::onClearAllClicked()
{
    if (mapData.isEmpty()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear All",
        QString("Remove all %1 entries?").arg(mapData.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        mapData.clear();
        populateTable();
    }
}

void MapEditorDialog::onFilterChanged(const QString &)
{
    updateTable();
}

QMap<QString, QStringList> MapEditorDialog::getMap() const
{
    return mapData;
}
