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
#include <QComboBox>
#include <QMap>

struct BaritoneSettingData;

class BaritoneWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BaritoneWidget(QWidget *parent = nullptr);
    ~BaritoneWidget();

    void updateSettings(const QMap<QString, BaritoneSettingData> &settings);
    void updateSingleSetting(const BaritoneSettingData &setting);
    void clear();

signals:
    void settingChanged(const QString &settingName, const QVariant &value);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void applyFilter();

private:
    enum UserDataRole {
        SettingNameRole = Qt::UserRole,
        SettingTypeRole = Qt::UserRole + 1
    };

    void setupUI();
    void populateTree();
    QTreeWidgetItem* createSettingItem(const BaritoneSettingData &setting);
    void updateSettingWidget(QTreeWidgetItem *settingItem, const BaritoneSettingData &setting);
    QWidget* createSettingEditor(const BaritoneSettingData &setting,
                                   const QString &settingName);

    QVBoxLayout *mainLayout;
    QLineEdit *filterEdit;
    QTreeWidget *settingTree;

    QMap<QString, BaritoneSettingData> allSettings;
    QMap<QString, QTreeWidgetItem*> settingItems;

    bool updatingFromCode; // Flag to prevent signal loops
};

#endif // BARITONEWIDGET_H
