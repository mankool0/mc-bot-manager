#ifndef METEORFRIENDSDIALOG_H
#define METEORFRIENDSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QStringList>

class MeteorFriendsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MeteorFriendsDialog(const QStringList &friends, QWidget *parent = nullptr);

    QStringList getFriends() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onNameChanged(const QString &text);
    void onAddClicked();
    void onRemoveClicked();
    void onClearClicked();
    void onSelectionChanged();

private:
    void setupUI();
    bool hasFriend(const QString &name) const;
    void updateCountLabel();

    QLineEdit *nameEdit;
    QPushButton *addButton;
    QListWidget *friendList;
    QLabel *countLabel;
    QPushButton *removeButton;
    QPushButton *clearButton;
};

#endif // METEORFRIENDSDIALOG_H
