#ifndef STRINGLISTEDITORDIALOG_H
#define STRINGLISTEDITORDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>
#include <QScrollArea>

class StringListEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StringListEditorDialog(const QString &settingName,
                                    const QStringList &currentItems,
                                    QWidget *parent = nullptr);
    ~StringListEditorDialog();

    QStringList getItems() const;

private slots:
    void onAddClicked();
    void onRemoveItemClicked();
    void onClearClicked();
    void onNewItemTextChanged(const QString &text);

private:
    void setupUI();
    void addItemWidget(const QString &text);
    void updateCountLabel();

    QString settingName;

    QLineEdit *newItemEdit;
    QPushButton *addButton;
    QVBoxLayout *itemsLayout;
    QWidget *itemsContainer;
    QPushButton *clearButton;
    QLabel *countLabel;
};

#endif // STRINGLISTEDITORDIALOG_H
