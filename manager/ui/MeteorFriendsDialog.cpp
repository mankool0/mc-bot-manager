#include "MeteorFriendsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QShortcut>
#include <QKeyEvent>

MeteorFriendsDialog::MeteorFriendsDialog(const QStringList &friends, QWidget *parent)
    : QDialog(parent)
{
    setupUI();

    friendList->addItems(friends);
    updateCountLabel();
    clearButton->setEnabled(!friends.isEmpty());
}

void MeteorFriendsDialog::setupUI()
{
    setWindowTitle("Meteor Friends");
    setMinimumSize(360, 380);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *addLayout = new QHBoxLayout();
    nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText("Player name");
    nameEdit->setClearButtonEnabled(true);
    addLayout->addWidget(nameEdit);

    addButton = new QPushButton("Add", this);
    addButton->setEnabled(false);
    addLayout->addWidget(addButton);
    mainLayout->addLayout(addLayout);

    friendList = new QListWidget(this);
    friendList->setAlternatingRowColors(true);
    friendList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLayout->addWidget(friendList, 1);

    QHBoxLayout *listLayout = new QHBoxLayout();
    countLabel = new QLabel(this);
    listLayout->addWidget(countLabel);
    listLayout->addStretch();

    removeButton = new QPushButton("Remove", this);
    removeButton->setEnabled(false);
    listLayout->addWidget(removeButton);

    clearButton = new QPushButton("Clear", this);
    clearButton->setEnabled(false);
    listLayout->addWidget(clearButton);
    mainLayout->addLayout(listLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    QShortcut *deleteShortcut = new QShortcut(QKeySequence::Delete, friendList);
    deleteShortcut->setContext(Qt::WidgetShortcut);

    connect(nameEdit, &QLineEdit::textChanged, this, &MeteorFriendsDialog::onNameChanged);
    connect(addButton, &QPushButton::clicked, this, &MeteorFriendsDialog::onAddClicked);
    connect(removeButton, &QPushButton::clicked, this, &MeteorFriendsDialog::onRemoveClicked);
    connect(deleteShortcut, &QShortcut::activated, this, &MeteorFriendsDialog::onRemoveClicked);
    connect(clearButton, &QPushButton::clicked, this, &MeteorFriendsDialog::onClearClicked);
    connect(friendList, &QListWidget::itemSelectionChanged, this, &MeteorFriendsDialog::onSelectionChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    nameEdit->setFocus();
}

void MeteorFriendsDialog::keyPressEvent(QKeyEvent *event)
{
    // QLineEdit does not consume Enter, so without this the dialog's default button (OK) would
    // fire on the same keystroke that adds the name and close the dialog.
    if (focusWidget() == nameEdit && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        onAddClicked();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool MeteorFriendsDialog::hasFriend(const QString &name) const
{
    for (int i = 0; i < friendList->count(); ++i) {
        if (friendList->item(i)->text().compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void MeteorFriendsDialog::onNameChanged(const QString &text)
{
    const QString name = text.trimmed();
    const bool duplicate = !name.isEmpty() && hasFriend(name);
    addButton->setEnabled(!name.isEmpty() && !duplicate);
    addButton->setToolTip(duplicate ? "Already a friend" : QString());
}

void MeteorFriendsDialog::onAddClicked()
{
    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty() || hasFriend(name)) {
        return;
    }

    friendList->addItem(name);
    friendList->scrollToBottom();
    nameEdit->clear();
    nameEdit->setFocus();

    updateCountLabel();
    clearButton->setEnabled(true);
}

void MeteorFriendsDialog::onRemoveClicked()
{
    const QList<QListWidgetItem*> selected = friendList->selectedItems();
    for (QListWidgetItem *item : selected) {
        delete item;
    }

    updateCountLabel();
    clearButton->setEnabled(friendList->count() > 0);
    // The name in the edit may have been a duplicate of what was just removed.
    onNameChanged(nameEdit->text());
}

void MeteorFriendsDialog::onClearClicked()
{
    if (friendList->count() == 0) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear Friends",
        "Remove all friends?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        friendList->clear();
        updateCountLabel();
        clearButton->setEnabled(false);
        onNameChanged(nameEdit->text());
    }
}

void MeteorFriendsDialog::onSelectionChanged()
{
    removeButton->setEnabled(!friendList->selectedItems().isEmpty());
}

void MeteorFriendsDialog::updateCountLabel()
{
    const int count = friendList->count();
    if (count == 0) {
        countLabel->setText("No friends");
    } else if (count == 1) {
        countLabel->setText("1 friend");
    } else {
        countLabel->setText(QString("%1 friends").arg(count));
    }
}

QStringList MeteorFriendsDialog::getFriends() const
{
    QStringList result;
    for (int i = 0; i < friendList->count(); ++i) {
        result.append(friendList->item(i)->text());
    }
    return result;
}
