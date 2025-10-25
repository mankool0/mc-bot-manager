#ifndef ESPBLOCKDATAMAPEDITORDIALOG_H
#define ESPBLOCKDATAMAPEDITORDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QStringList>
#include <QMap>

struct ESPBlockData;
using ESPBlockDataMap = QMap<QString, ESPBlockData>;

class ESPBlockDataMapEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ESPBlockDataMapEditorDialog(const QString &settingName,
                                          const QStringList &possibleBlockNames,
                                          const ESPBlockDataMap &currentMap,
                                          QWidget *parent = nullptr);
    ~ESPBlockDataMapEditorDialog();

    ESPBlockDataMap getMap() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onEditClicked();
    void onClearAllClicked();
    void onTableDoubleClicked(int row, int column);
    void onFilterChanged(const QString &text);
    void onSelectionChanged();

private:
    void setupUI();
    void populateTable();
    void updateTable();
    void updateCountLabel();
    QString createPreviewText(const ESPBlockData &data) const;
    QWidget* createPreviewWidget(const ESPBlockData &data);
    void editBlockSettings(const QString &blockName, const ESPBlockData &currentData);
    void addNewBlock();

    QString settingName;
    QStringList possibleBlockNames;
    ESPBlockDataMap mapData;

    QLineEdit *filterEdit;
    QTableWidget *table;
    QPushButton *addButton;
    QPushButton *removeButton;
    QPushButton *editButton;
    QPushButton *clearAllButton;
    QLabel *countLabel;

    QStringList filteredBlockNames;
};

#endif // ESPBLOCKDATAMAPEDITORDIALOG_H
