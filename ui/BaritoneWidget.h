#ifndef BARITONEWIDGET_H
#define BARITONEWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QMap>
#include "baritone.qpb.h"

class BaritoneWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BaritoneWidget(QWidget *parent = nullptr);
    ~BaritoneWidget();

    void updateSettings(const QVector<mankool::mcbot::protocol::BaritoneSettingInfo> &settings);
    void updateSingleSetting(const mankool::mcbot::protocol::BaritoneSettingInfo &setting);
    void clear();

signals:
    void settingChanged(const QString &settingName, const QString &value);

private slots:
    void onFilterTextChanged(const QString &text);

private:
    enum UserDataRole {
        SettingNameRole = Qt::UserRole,
        SettingTypeRole = Qt::UserRole + 1
    };

    void setupUI();
    void populateTree();
    QTreeWidgetItem* createSettingItem(const mankool::mcbot::protocol::BaritoneSettingInfo &setting);
    void updateSettingWidget(QTreeWidgetItem *settingItem, const mankool::mcbot::protocol::BaritoneSettingInfo &setting);
    QWidget* createSettingEditor(const mankool::mcbot::protocol::BaritoneSettingInfo &setting,
                                   const QString &settingName);

    QVBoxLayout *mainLayout;
    QLineEdit *filterEdit;
    QTreeWidget *settingTree;

    QVector<mankool::mcbot::protocol::BaritoneSettingInfo> allSettings;
    QMap<QString, QTreeWidgetItem*> settingItems;

    bool updatingFromCode; // Flag to prevent signal loops
};

#endif // BARITONEWIDGET_H
