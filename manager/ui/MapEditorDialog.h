#ifndef MAPEDITORDIALOG_H
#define MAPEDITORDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QStringList>
#include <QMap>

class MapEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MapEditorDialog(const QString &settingName,
                             const QStringList &possibleKeys,
                             const QStringList &possibleValues,
                             const QMap<QString, QStringList> &currentMap,
                             QWidget *parent = nullptr);
    ~MapEditorDialog();

    QMap<QString, QStringList> getMap() const;

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onEditKeyClicked();
    void onEditValuesClicked();
    void onClearAllClicked();
    void onTableDoubleClicked(int row, int column);
    void onFilterChanged(const QString &text);

private:
    void setupUI();
    void populateTable();
    void updateTable();

    QString settingName;
    QStringList possibleKeys;
    QStringList possibleValues;
    QMap<QString, QStringList> mapData;

    QLineEdit *filterEdit;
    QTableWidget *table;
    QPushButton *addButton;
    QPushButton *removeButton;
    QPushButton *editKeyButton;
    QPushButton *editButton;
    QPushButton *clearAllButton;
    QLabel *countLabel;
};

#endif // MAPEDITORDIALOG_H
