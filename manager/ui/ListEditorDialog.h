#ifndef LISTEDITORDIALOG_H
#define LISTEDITORDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QStringList>
#include <QSet>

class ListEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ListEditorDialog(const QString &settingName,
                              const QStringList &availableItems,
                              const QStringList &selectedItems,
                              QWidget *parent = nullptr);
    ~ListEditorDialog();

    QStringList getSelectedItems() const;

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onAddAllClicked();
    void onRemoveAllClicked();
    void updateAvailableList();
    void onSelectedFilterChanged(const QString &text);
    void onAvailableDoubleClicked(QListWidgetItem *item);
    void onSelectedDoubleClicked(QListWidgetItem *item);

private:
    void setupUI();
    void populateAvailableList();

    QString settingName;
    QStringList allItems;
    QSet<QString> selectedItems;

    QLineEdit *availableFilterEdit;
    QLineEdit *selectedFilterEdit;
    QListWidget *availableList;
    QListWidget *selectedList;
    QPushButton *addButton;
    QPushButton *removeButton;
    QPushButton *addAllButton;
    QPushButton *removeAllButton;
    QLabel *availableCountLabel;
    QLabel *selectedCountLabel;
};

#endif // LISTEDITORDIALOG_H
