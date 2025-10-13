#ifndef METEORMODULESWIDGET_H
#define METEORMODULESWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QScrollBar>
#include <QMap>

struct MeteorModuleData;
struct MeteorSettingData;

class MeteorModulesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MeteorModulesWidget(QWidget *parent = nullptr);
    ~MeteorModulesWidget();

    void updateModules(const QMap<QString, MeteorModuleData> &modules);
    void updateSingleModule(const MeteorModuleData &module);
    void clear();

signals:
    void moduleToggled(const QString &moduleName, bool enabled);
    void settingChanged(const QString &moduleName, const QString &settingPath, const QString &value);

private slots:
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onFilterTextChanged(const QString &text);
    void onCategoryFilterChanged(const QString &category);

private:
    enum UserDataRole {
        ItemDataRole = Qt::UserRole,
        ItemTypeRole = Qt::UserRole + 1,
        ModuleNameRole = Qt::UserRole + 2
    };

    static constexpr int DEFAULT_COLUMN_WIDTH = 300;
    static constexpr int MAX_DISPLAY_LENGTH = 50;
    static constexpr int TRUNCATE_LENGTH = 47;

    void setupUI();
    void populateTree();
    QTreeWidgetItem* createModuleItem(const MeteorModuleData &module);
    void updateModuleSettings(QTreeWidgetItem *moduleItem, const MeteorModuleData &module);
    void updateSettingWidget(QTreeWidgetItem *settingItem, const MeteorSettingData &setting);
    QWidget* createSettingEditor(const MeteorSettingData &setting,
                                   const QString &moduleName,
                                   const QString &settingPath);

    QVBoxLayout *mainLayout;
    QLineEdit *filterEdit;
    QComboBox *categoryFilter;
    QTreeWidget *moduleTree;

    QMap<QString, MeteorModuleData> allModules;
    QMap<QString, QTreeWidgetItem*> moduleItems;

    bool updatingFromCode; // Flag to prevent signal loops
};

#endif // METEORMODULESWIDGET_H
