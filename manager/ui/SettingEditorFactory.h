#ifndef SETTINGEDITORFACTORY_H
#define SETTINGEDITORFACTORY_H

#include <QWidget>
#include <QVariant>
#include <QMap>
#include <functional>

enum class SettingSystemType {
    Baritone,
    Meteor
};

struct SettingEditorContext {
    SettingSystemType systemType = SettingSystemType::Baritone;
    QString name;
    QString description;
    QVariant defaultValue;
    double minValue = 0.0;
    double maxValue = 0.0;
    bool hasMin = false;
    bool hasMax = false;
    QStringList possibleValues;
    QWidget* parent = nullptr;
};

class SettingEditorFactory
{
public:
    // Callback when a setting value changes
    using ChangeCallback = std::function<void(const QVariant&)>;

    using CreateEditorFunc = std::function<QWidget*(
        const QVariant& value,
        const SettingEditorContext& context,
        ChangeCallback onChange
    )>;

    using UpdateWidgetFunc = std::function<void(
        QWidget* widget,
        const QVariant& value,
        const SettingEditorContext& context
    )>;

    static SettingEditorFactory& instance();

    void registerType(SettingSystemType systemType, int settingType,
                     CreateEditorFunc creator, UpdateWidgetFunc updater);

    QWidget* createEditor(
        SettingSystemType systemType,
        int settingType,
        const QVariant& value,
        const SettingEditorContext& context,
        ChangeCallback onChange
    );

    void updateWidget(
        SettingSystemType systemType,
        int settingType,
        QWidget* widget,
        const QVariant& value,
        const SettingEditorContext& context
    );

private:
    SettingEditorFactory();
    ~SettingEditorFactory();

    SettingEditorFactory(const SettingEditorFactory&) = delete;
    SettingEditorFactory& operator=(const SettingEditorFactory&) = delete;

    void registerAllTypes();

    QMap<QPair<SettingSystemType, int>, CreateEditorFunc> creators;
    QMap<QPair<SettingSystemType, int>, UpdateWidgetFunc> updaters;
};

#endif // SETTINGEDITORFACTORY_H
