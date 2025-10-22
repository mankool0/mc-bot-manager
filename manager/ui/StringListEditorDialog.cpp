#include "StringListEditorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QMessageBox>

StringListEditorDialog::StringListEditorDialog(const QString &settingName,
                                               const QStringList &currentItems,
                                               QWidget *parent)
    : QDialog(parent)
    , settingName(settingName)
{
    setupUI();

    for (const QString &item : currentItems) {
        addItemWidget(item);
    }

    updateCountLabel();

    clearButton->setEnabled(!currentItems.isEmpty());
}

StringListEditorDialog::~StringListEditorDialog()
{
}

void StringListEditorDialog::setupUI()
{
    setWindowTitle(QString("Edit %1").arg(settingName));
    setMinimumSize(500, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *addGroup = new QGroupBox("Add New Item", this);
    QHBoxLayout *addLayout = new QHBoxLayout(addGroup);

    newItemEdit = new QLineEdit(this);
    newItemEdit->setPlaceholderText("Enter new item...");
    addLayout->addWidget(newItemEdit);

    addButton = new QPushButton("Add", this);
    addButton->setEnabled(false);
    addButton->setFixedWidth(80);
    addLayout->addWidget(addButton);

    mainLayout->addWidget(addGroup);

    QGroupBox *listGroup = new QGroupBox("Current Items", this);
    QVBoxLayout *listGroupLayout = new QVBoxLayout(listGroup);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    itemsContainer = new QWidget();
    itemsLayout = new QVBoxLayout(itemsContainer);
    itemsLayout->setSpacing(5);
    itemsLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(itemsContainer);
    listGroupLayout->addWidget(scrollArea);

    countLabel = new QLabel(this);
    listGroupLayout->addWidget(countLabel);

    QHBoxLayout *listButtonsLayout = new QHBoxLayout();
    clearButton = new QPushButton("Clear All", this);
    clearButton->setEnabled(false);
    listButtonsLayout->addWidget(clearButton);

    listButtonsLayout->addStretch();
    listGroupLayout->addLayout(listButtonsLayout);

    mainLayout->addWidget(listGroup);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(newItemEdit, &QLineEdit::textChanged, this, &StringListEditorDialog::onNewItemTextChanged);
    connect(newItemEdit, &QLineEdit::returnPressed, this, &StringListEditorDialog::onAddClicked);
    connect(addButton, &QPushButton::clicked, this, &StringListEditorDialog::onAddClicked);
    connect(clearButton, &QPushButton::clicked, this, &StringListEditorDialog::onClearClicked);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    newItemEdit->setFocus();
}

void StringListEditorDialog::onNewItemTextChanged(const QString &text)
{
    addButton->setEnabled(!text.trimmed().isEmpty());
}

void StringListEditorDialog::onAddClicked()
{
    QString newItem = newItemEdit->text().trimmed();
    if (newItem.isEmpty()) {
        return;
    }

    addItemWidget(newItem);
    newItemEdit->clear();
    newItemEdit->setFocus();

    updateCountLabel();
    clearButton->setEnabled(true);
}

void StringListEditorDialog::addItemWidget(const QString &text)
{
    QWidget *itemWidget = new QWidget(itemsContainer);
    QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
    itemLayout->setContentsMargins(0, 0, 0, 0);

    QLineEdit *itemEdit = new QLineEdit(text, itemWidget);
    itemLayout->addWidget(itemEdit);

    QPushButton *removeBtn = new QPushButton("Remove", itemWidget);
    removeBtn->setFixedWidth(80);
    itemLayout->addWidget(removeBtn);
    itemsLayout->addWidget(itemWidget);

    connect(removeBtn, &QPushButton::clicked, this, &StringListEditorDialog::onRemoveItemClicked);
}

void StringListEditorDialog::onRemoveItemClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    QWidget *itemWidget = qobject_cast<QWidget*>(button->parentWidget());
    if (!itemWidget) return;

    itemsLayout->removeWidget(itemWidget);
    itemWidget->deleteLater();

    updateCountLabel();

    clearButton->setEnabled(itemsLayout->count() > 0);
}

void StringListEditorDialog::onClearClicked()
{
    if (itemsLayout->count() == 0) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear All Items",
        "Are you sure you want to remove all items?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        while (QLayoutItem *item = itemsLayout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }

        updateCountLabel();
        clearButton->setEnabled(false);
    }
}

void StringListEditorDialog::updateCountLabel()
{
    countLabel->setText(QString("Total: %1 item(s)").arg(itemsLayout->count()));
}

QStringList StringListEditorDialog::getItems() const
{
    QStringList result;

    for (int i = 0; i < itemsLayout->count(); ++i) {
        QWidget *itemWidget = itemsLayout->itemAt(i)->widget();
        QLineEdit *lineEdit = itemWidget->findChild<QLineEdit*>();
        if (lineEdit) {
            QString text = lineEdit->text().trimmed();
            if (!text.isEmpty()) {
                result.append(text);
            }
        }
    }

    return result;
}
