#include "ListEditorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>

ListEditorDialog::ListEditorDialog(const QString &settingName,
                                   const QStringList &availableItems,
                                   const QStringList &currentlySelected,
                                   QWidget *parent)
    : QDialog(parent)
    , settingName(settingName)
    , allItems(availableItems)
    , selectedItems(currentlySelected.begin(), currentlySelected.end())
{
    setupUI();
    populateAvailableList();
}

ListEditorDialog::~ListEditorDialog()
{
}

void ListEditorDialog::setupUI()
{
    setWindowTitle(QString("Edit %1").arg(settingName));
    setMinimumSize(800, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *listsLayout = new QHBoxLayout();

    QGroupBox *availableGroup = new QGroupBox("Available Items", this);
    QVBoxLayout *availableLayout = new QVBoxLayout(availableGroup);

    availableFilterEdit = new QLineEdit(this);
    availableFilterEdit->setPlaceholderText("Search available items...");
    availableLayout->addWidget(availableFilterEdit);

    availableList = new QListWidget(this);
    availableList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    availableList->setSortingEnabled(true);
    availableLayout->addWidget(availableList);

    availableCountLabel = new QLabel(this);
    availableLayout->addWidget(availableCountLabel);

    listsLayout->addWidget(availableGroup, 1);

    QVBoxLayout *buttonsLayout = new QVBoxLayout();
    buttonsLayout->addStretch();

    addButton = new QPushButton(">", this);
    addButton->setToolTip("Add selected items");
    addButton->setFixedWidth(50);
    buttonsLayout->addWidget(addButton);

    addAllButton = new QPushButton(">>", this);
    addAllButton->setToolTip("Add all items");
    addAllButton->setFixedWidth(50);
    buttonsLayout->addWidget(addAllButton);

    removeButton = new QPushButton("<", this);
    removeButton->setToolTip("Remove selected items");
    removeButton->setFixedWidth(50);
    buttonsLayout->addWidget(removeButton);

    removeAllButton = new QPushButton("<<", this);
    removeAllButton->setToolTip("Remove all items");
    removeAllButton->setFixedWidth(50);
    buttonsLayout->addWidget(removeAllButton);

    buttonsLayout->addStretch();
    listsLayout->addLayout(buttonsLayout);

    QGroupBox *selectedGroup = new QGroupBox("Selected Items", this);
    QVBoxLayout *selectedLayout = new QVBoxLayout(selectedGroup);

    selectedFilterEdit = new QLineEdit(this);
    selectedFilterEdit->setPlaceholderText("Search selected items...");
    selectedLayout->addWidget(selectedFilterEdit);

    selectedList = new QListWidget(this);
    selectedList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    selectedList->setSortingEnabled(true);
    selectedLayout->addWidget(selectedList);

    selectedCountLabel = new QLabel(this);
    selectedLayout->addWidget(selectedCountLabel);

    listsLayout->addWidget(selectedGroup, 1);

    mainLayout->addLayout(listsLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(addButton, &QPushButton::clicked, this, &ListEditorDialog::onAddClicked);
    connect(removeButton, &QPushButton::clicked, this, &ListEditorDialog::onRemoveClicked);
    connect(addAllButton, &QPushButton::clicked, this, &ListEditorDialog::onAddAllClicked);
    connect(removeAllButton, &QPushButton::clicked, this, &ListEditorDialog::onRemoveAllClicked);
    connect(availableFilterEdit, &QLineEdit::textChanged, this, &ListEditorDialog::updateAvailableList);
    connect(selectedFilterEdit, &QLineEdit::textChanged, this, &ListEditorDialog::onSelectedFilterChanged);
    connect(availableList, &QListWidget::itemDoubleClicked, this, &ListEditorDialog::onAvailableDoubleClicked);
    connect(selectedList, &QListWidget::itemDoubleClicked, this, &ListEditorDialog::onSelectedDoubleClicked);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (const QString &item : allItems) {
        availableList->addItem(item);
        selectedList->addItem(item);
    }

    auto updateCounts = [this]() {
        availableCountLabel->setText(QString("Total: %1").arg(availableList->count()));
        selectedCountLabel->setText(QString("Total: %1").arg(selectedList->count()));
    };
    connect(availableList->model(), &QAbstractItemModel::rowsInserted, updateCounts);
    connect(availableList->model(), &QAbstractItemModel::rowsRemoved, updateCounts);
    connect(selectedList->model(), &QAbstractItemModel::rowsInserted, updateCounts);
    connect(selectedList->model(), &QAbstractItemModel::rowsRemoved, updateCounts);
    updateCounts();
}

void ListEditorDialog::populateAvailableList()
{
    for (int i = 0; i < availableList->count(); ++i) {
        QListWidgetItem *item = availableList->item(i);
        if (item) {
            item->setHidden(selectedItems.contains(item->text()));
        }
    }

    for (int i = 0; i < selectedList->count(); ++i) {
        QListWidgetItem *item = selectedList->item(i);
        if (item) {
            item->setHidden(!selectedItems.contains(item->text()));
        }
    }

    updateAvailableList();
    onSelectedFilterChanged(selectedFilterEdit->text());
}

void ListEditorDialog::updateAvailableList()
{
    QString filterText = availableFilterEdit->text();

    int visibleCount = 0;
    int totalAvailable = 0;

    for (int i = 0; i < availableList->count(); ++i) {
        QListWidgetItem *item = availableList->item(i);
        if (!item) continue;

        QString itemText = item->text();
        bool isSelected = selectedItems.contains(itemText);
        bool matchesFilter = filterText.isEmpty() || itemText.contains(filterText, Qt::CaseInsensitive);

        bool shouldShow = !isSelected && matchesFilter;
        item->setHidden(!shouldShow);

        if (!isSelected) totalAvailable++;
        if (shouldShow) visibleCount++;
    }

    if (filterText.isEmpty()) {
        availableCountLabel->setText(QString("Total: %1").arg(totalAvailable));
    } else {
        availableCountLabel->setText(QString("Showing: %1 / Total: %2").arg(visibleCount).arg(totalAvailable));
    }
}

void ListEditorDialog::onAddClicked()
{
    QList<QListWidgetItem*> items = availableList->selectedItems();
    for (QListWidgetItem *item : items) {
        selectedItems.insert(item->text());
    }
    updateAvailableList();
    onSelectedFilterChanged(selectedFilterEdit->text());
}

void ListEditorDialog::onRemoveClicked()
{
    QList<QListWidgetItem*> items = selectedList->selectedItems();
    for (QListWidgetItem *item : items) {
        selectedItems.remove(item->text());
    }
    updateAvailableList();
    onSelectedFilterChanged(selectedFilterEdit->text());
}

void ListEditorDialog::onAddAllClicked()
{
    for (int i = 0; i < availableList->count(); ++i) {
        QListWidgetItem *item = availableList->item(i);
        if (item && !item->isHidden()) {
            selectedItems.insert(item->text());
        }
    }
    updateAvailableList();
    onSelectedFilterChanged(selectedFilterEdit->text());
}

void ListEditorDialog::onRemoveAllClicked()
{
    for (int i = 0; i < selectedList->count(); ++i) {
        QListWidgetItem *item = selectedList->item(i);
        if (item && !item->isHidden()) {
            selectedItems.remove(item->text());
        }
    }
    updateAvailableList();
    onSelectedFilterChanged(selectedFilterEdit->text());
}

void ListEditorDialog::onSelectedFilterChanged(const QString &text)
{
    QString filterText = text;

    int visibleCount = 0;
    int totalSelected = 0;

    for (int i = 0; i < selectedList->count(); ++i) {
        QListWidgetItem *item = selectedList->item(i);
        if (!item) continue;

        QString itemText = item->text();
        bool isSelected = selectedItems.contains(itemText);
        bool matchesFilter = filterText.isEmpty() || itemText.contains(filterText, Qt::CaseInsensitive);

        bool shouldShow = isSelected && matchesFilter;
        item->setHidden(!shouldShow);

        if (isSelected) totalSelected++;
        if (shouldShow) visibleCount++;
    }

    if (filterText.isEmpty()) {
        selectedCountLabel->setText(QString("Total: %1").arg(totalSelected));
    } else {
        selectedCountLabel->setText(QString("Showing: %1 / Total: %2").arg(visibleCount).arg(totalSelected));
    }
}

void ListEditorDialog::onAvailableDoubleClicked(QListWidgetItem *item)
{
    if (item) {
        selectedItems.insert(item->text());
        updateAvailableList();
        onSelectedFilterChanged(selectedFilterEdit->text());
    }
}

void ListEditorDialog::onSelectedDoubleClicked(QListWidgetItem *item)
{
    if (item) {
        selectedItems.remove(item->text());
        updateAvailableList();
        onSelectedFilterChanged(selectedFilterEdit->text());
    }
}

QStringList ListEditorDialog::getSelectedItems() const
{
    return QStringList(selectedItems.begin(), selectedItems.end());
}
