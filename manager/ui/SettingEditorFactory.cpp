#include "SettingEditorFactory.h"
#include "ListEditorDialog.h"
#include "StringListEditorDialog.h"
#include "bot/BotManager.h"
#include "logging/LogManager.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QPainter>
#include <QPixmap>
#include <limits>

static constexpr int MAX_DISPLAY_LENGTH = 50;
static constexpr int TRUNCATE_LENGTH = 47;

SettingEditorFactory::SettingEditorFactory()
{
    registerAllTypes();
}

SettingEditorFactory::~SettingEditorFactory()
{
}

SettingEditorFactory& SettingEditorFactory::instance()
{
    static SettingEditorFactory instance;
    return instance;
}

void SettingEditorFactory::registerType(SettingSystemType systemType, int settingType,
                                        CreateEditorFunc creator, UpdateWidgetFunc updater)
{
    QPair<SettingSystemType, int> key(systemType, settingType);
    creators[key] = creator;
    updaters[key] = updater;
}

QWidget* SettingEditorFactory::createEditor(
    SettingSystemType systemType,
    int settingType,
    const QVariant& value,
    const SettingEditorContext& context,
    ChangeCallback onChange)
{
    QPair<SettingSystemType, int> key(systemType, settingType);
    if (creators.contains(key)) {
        return creators[key](value, context, onChange);
    }

    return nullptr;
}

void SettingEditorFactory::updateWidget(
    SettingSystemType systemType,
    int settingType,
    QWidget* widget,
    const QVariant& value,
    const SettingEditorContext& context)
{
    QPair<SettingSystemType, int> key(systemType, settingType);

    if (updaters.contains(key) && widget) {
        updaters[key](widget, value, context);
    }
}


void SettingEditorFactory::registerAllTypes()
{
    using BaritoneSettingType = mankool::mcbot::protocol::BaritoneSettingInfo::SettingType;
    using MeteorSettingType = mankool::mcbot::protocol::SettingInfo::SettingType;

    // Common Types (shared by both Baritone and Meteor)

    // BOOLEAN handler
    auto boolCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QCheckBox* checkBox = new QCheckBox(context.parent);
        checkBox->setChecked(value.toBool());
        QObject::connect(checkBox, &QCheckBox::toggled, [onChange](bool checked) {
            onChange(QVariant(checked));
        });
        return checkBox;
    };

    auto boolUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QCheckBox* checkBox = qobject_cast<QCheckBox*>(widget)) {
            checkBox->setChecked(value.toBool());
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::BOOLEAN), boolCreator, boolUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::BOOLEAN), boolCreator, boolUpdater);

    // INTEGER handler
    auto intCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QSpinBox* spinBox = new QSpinBox(context.parent);
        spinBox->setMinimum(context.hasMin ? static_cast<int>(context.minValue) : std::numeric_limits<int>::min());
        spinBox->setMaximum(context.hasMax ? static_cast<int>(context.maxValue) : std::numeric_limits<int>::max());
        spinBox->setValue(value.toInt());
        QObject::connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), [onChange](int val) {
            onChange(QVariant(val));
        });
        return spinBox;
    };

    auto intUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QSpinBox* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setValue(value.toInt());
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::INTEGER), intCreator, intUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::INTEGER), intCreator, intUpdater);

    // DOUBLE/FLOAT handler
    auto doubleCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QDoubleSpinBox* spinBox = new QDoubleSpinBox(context.parent);
        spinBox->setMinimum(context.hasMin ? context.minValue : std::numeric_limits<double>::min());
        spinBox->setMaximum(context.hasMax ? context.maxValue : std::numeric_limits<double>::max());
        spinBox->setDecimals(4);
        spinBox->setSingleStep(0.1);
        spinBox->setValue(value.toDouble());
        QObject::connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [onChange](double val) {
            onChange(QVariant(val));
        });
        return spinBox;
    };

    auto doubleUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QDoubleSpinBox* spinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            spinBox->setValue(value.toDouble());
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::DOUBLE), doubleCreator, doubleUpdater);
    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::FLOAT), doubleCreator, doubleUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::DOUBLE), doubleCreator, doubleUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::FLOAT), doubleCreator, doubleUpdater);

    // LONG handler (limited to int range in QSpinBox)
    // TODO: Create a new class from QAbstractSpinBox to support 64bit ints
    auto longCreator = [](const QVariant &value,
                          const SettingEditorContext &context,
                          ChangeCallback onChange) -> QWidget * {
        QSpinBox* spinBox = new QSpinBox(context.parent);
        spinBox->setMinimum(context.hasMin ? static_cast<int>(context.minValue)
                                           : std::numeric_limits<int>::min());
        spinBox->setMaximum(context.hasMax ? static_cast<int>(context.maxValue)
                                           : std::numeric_limits<int>::max());
        spinBox->setValue(static_cast<int>(value.toLongLong()));
        QObject::connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), [onChange](int val) {
            onChange(QVariant(static_cast<qint64>(val)));
        });
        return spinBox;
    };

    auto longUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QSpinBox* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setValue(static_cast<int>(value.toLongLong()));
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::LONG), longCreator, longUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::LONG), longCreator, longUpdater);

    // STRING handler (also used for KEYBIND, POTION, GENERIC in Meteor)
    auto stringCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        LogManager::log(QString("STRING creator: value.isValid()=%1, type=%2, value='%3'")
            .arg(value.isValid()).arg(value.typeName()).arg(value.toString()), LogManager::Debug);
        QLineEdit* lineEdit = new QLineEdit(context.parent);
        QString displayValue = value.toString();
        lineEdit->setText(displayValue);
        lineEdit->setProperty("originalValue", displayValue);
        QObject::connect(lineEdit, &QLineEdit::editingFinished, [lineEdit, onChange]() {
            QString newValue = lineEdit->text();
            QString originalValue = lineEdit->property("originalValue").toString();
            if (newValue != originalValue) {
                lineEdit->setProperty("originalValue", newValue);
                onChange(QVariant(newValue));
            }
        });
        LogManager::log("STRING creator: Returning QLineEdit", LogManager::Debug);
        return lineEdit;
    };

    auto stringUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            QString displayValue = value.toString();
            lineEdit->setText(displayValue);
            lineEdit->setProperty("originalValue", displayValue);
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::STRING), stringCreator, stringUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::STRING), stringCreator, stringUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::KEYBIND), stringCreator, stringUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::POTION), stringCreator, stringUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::GENERIC), stringCreator, stringUpdater);



    // Baritone-Specific Types

    // Non-editable types (LIST, MAP, CONSUMER, BI_CONSUMER) - read-only labels
    // These return empty QVariant from the mod, so we just show them as text
    auto readOnlyCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback) -> QWidget* {
        QLabel* label = new QLabel(context.parent);
        QString displayValue = value.toString();

        if (displayValue.length() > MAX_DISPLAY_LENGTH) {
            label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
            label->setToolTip(displayValue + "\n\n(Read-only - unimplemented type)");
        } else {
            label->setText(displayValue.isEmpty() ? "(empty)" : displayValue);
            label->setToolTip("Read-only - unimplemented type");
        }

        label->setStyleSheet("QLabel { padding: 2px; }");
        return label;
    };

    auto readOnlyUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            QString displayValue = value.toString();

            if (displayValue.length() > MAX_DISPLAY_LENGTH) {
                label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
                label->setToolTip(displayValue + "\n\n(Read-only - unimplemented type)");
            } else {
                label->setText(displayValue.isEmpty() ? "(empty)" : displayValue);
                label->setToolTip("Read-only - unimplemented type");
            }
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::LIST), readOnlyCreator, readOnlyUpdater);
    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::MAP), readOnlyCreator, readOnlyUpdater);
    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::BI_CONSUMER), readOnlyCreator, readOnlyUpdater);
    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::CONSUMER), readOnlyCreator, readOnlyUpdater);

    // COLOR (RGB) handler - uses QLabel with pixmap
    auto colorRGBCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        RGBColor color = value.value<RGBColor>();

        QLabel* colorLabel = new QLabel(context.parent);
        colorLabel->setFixedSize(60, 25);
        colorLabel->setFrameStyle(QFrame::Box);
        colorLabel->setLineWidth(1);

        // Create pixmap with solid color (no alpha/checkerboard for RGB)
        QPixmap colorPixmap(60, 25);
        colorPixmap.fill(QColor(color.red, color.green, color.blue));

        colorLabel->setPixmap(colorPixmap);
        colorLabel->setCursor(Qt::PointingHandCursor);

        colorLabel->setProperty("colorR", color.red);
        colorLabel->setProperty("colorG", color.green);
        colorLabel->setProperty("colorB", color.blue);
        colorLabel->setProperty("isBaritoneRGB", true);  // Flag to distinguish from RGBA
        colorLabel->setProperty("changeCallback", QVariant::fromValue(onChange));

        return colorLabel;
    };

    auto colorRGBUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QLabel* colorLabel = qobject_cast<QLabel*>(widget)) {
            RGBColor color = value.value<RGBColor>();

            // Redraw pixmap with new color
            QPixmap colorPixmap(60, 25);
            colorPixmap.fill(QColor(color.red, color.green, color.blue));

            colorLabel->setPixmap(colorPixmap);
            colorLabel->setProperty("colorR", color.red);
            colorLabel->setProperty("colorG", color.green);
            colorLabel->setProperty("colorB", color.blue);
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::COLOR), colorRGBCreator, colorRGBUpdater);

    // VEC3I handler
    auto vec3iCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        Vec3i vec = value.value<Vec3i>();

        QWidget* container = new QWidget(context.parent);
        QHBoxLayout* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        QLabel *xLabel = new QLabel("X:", container);
        QSpinBox* xSpinBox = new QSpinBox(container);
        xSpinBox->setMinimum(std::numeric_limits<int>::min());
        xSpinBox->setMaximum(std::numeric_limits<int>::max());
        xSpinBox->setValue(vec.x);

        QLabel *yLabel = new QLabel("Y:", container);
        QSpinBox* ySpinBox = new QSpinBox(container);
        ySpinBox->setMinimum(std::numeric_limits<int>::min());
        ySpinBox->setMaximum(std::numeric_limits<int>::max());
        ySpinBox->setValue(vec.y);

        QLabel *zLabel = new QLabel("Z:", container);
        QSpinBox* zSpinBox = new QSpinBox(container);
        zSpinBox->setMinimum(std::numeric_limits<int>::min());
        zSpinBox->setMaximum(std::numeric_limits<int>::max());
        zSpinBox->setValue(vec.z);

        layout->addWidget(xLabel);
        layout->addWidget(xSpinBox);
        layout->addWidget(yLabel);
        layout->addWidget(ySpinBox);
        layout->addWidget(zLabel);
        layout->addWidget(zSpinBox);
        layout->addStretch();

        auto emitChange = [xSpinBox, ySpinBox, zSpinBox, onChange]() {
            Vec3i vec{xSpinBox->value(), ySpinBox->value(), zSpinBox->value()};
            onChange(QVariant::fromValue(vec));
        };

        QObject::connect(xSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), emitChange);
        QObject::connect(ySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), emitChange);
        QObject::connect(zSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), emitChange);

        return container;
    };

    auto vec3iUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        Vec3i vec = value.value<Vec3i>();
        QList<QSpinBox*> spinBoxes = widget->findChildren<QSpinBox*>();
        if (spinBoxes.size() >= 3) {
            spinBoxes[0]->setValue(vec.x);
            spinBoxes[1]->setValue(vec.y);
            spinBoxes[2]->setValue(vec.z);
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::VEC3I), vec3iCreator, vec3iUpdater);

    // BLOCK_ROTATION handler
    auto blockRotationCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QComboBox* comboBox = new QComboBox(context.parent);
        comboBox->addItem("None", static_cast<int>(BlockRotation::None));
        comboBox->addItem("Clockwise 90°", static_cast<int>(BlockRotation::Clockwise90));
        comboBox->addItem("Clockwise 180°", static_cast<int>(BlockRotation::Clockwise180));
        comboBox->addItem("Counter-Clockwise 90°", static_cast<int>(BlockRotation::CounterClockwise90));

        BlockRotation currentRotation = value.value<BlockRotation>();
        comboBox->setCurrentIndex(comboBox->findData(static_cast<int>(currentRotation)));

        QObject::connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [comboBox, onChange](int index) {
            int rotationValue = comboBox->itemData(index).toInt();
            onChange(QVariant::fromValue(static_cast<BlockRotation>(rotationValue)));
        });
        return comboBox;
    };

    auto blockRotationUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QComboBox* comboBox = qobject_cast<QComboBox*>(widget)) {
            BlockRotation rotation = value.value<BlockRotation>();
            int index = comboBox->findData(static_cast<int>(rotation));
            if (index >= 0) {
                comboBox->setCurrentIndex(index);
            }
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::BLOCK_ROTATION), blockRotationCreator, blockRotationUpdater);

    // BLOCK_MIRROR handler
    auto blockMirrorCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QComboBox* comboBox = new QComboBox(context.parent);
        comboBox->addItem("None", static_cast<int>(BlockMirror::None));
        comboBox->addItem("Left-Right", static_cast<int>(BlockMirror::LeftRight));
        comboBox->addItem("Front-Back", static_cast<int>(BlockMirror::FrontBack));

        BlockMirror currentMirror = value.value<BlockMirror>();
        comboBox->setCurrentIndex(comboBox->findData(static_cast<int>(currentMirror)));

        QObject::connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [comboBox, onChange](int index) {
            int mirrorValue = comboBox->itemData(index).toInt();
            onChange(QVariant::fromValue(static_cast<BlockMirror>(mirrorValue)));
        });
        return comboBox;
    };

    auto blockMirrorUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QComboBox* comboBox = qobject_cast<QComboBox*>(widget)) {
            BlockMirror mirror = value.value<BlockMirror>();
            int index = comboBox->findData(static_cast<int>(mirror));
            if (index >= 0) {
                comboBox->setCurrentIndex(index);
            }
        }
    };

    registerType(SettingSystemType::Baritone, static_cast<int>(BaritoneSettingType::BLOCK_MIRROR), blockMirrorCreator, blockMirrorUpdater);



    // Meteor-Specific Types

    // ENUM handler
    auto enumCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QComboBox* comboBox = new QComboBox(context.parent);
        for (const QString& option : context.possibleValues) {
            comboBox->addItem(option);
        }
        comboBox->setCurrentText(value.toString());
        QObject::connect(comboBox, &QComboBox::currentTextChanged, [onChange](const QString& text) {
            onChange(QVariant(text));
        });
        return comboBox;
    };

    auto enumUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QComboBox* comboBox = qobject_cast<QComboBox*>(widget)) {
            comboBox->setCurrentText(value.toString());
        }
    };

    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::ENUM), enumCreator, enumUpdater);

    // COLOR (RGBA) handler - uses QLabel with pixmap and event filter
    // Note: This needs special handling with event filter, so we return a QLabel
    // The actual color picking will be handled by the widget's event filter
    auto colorRGBACreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        RGBAColor color = value.value<RGBAColor>();

        QLabel* colorLabel = new QLabel(context.parent);
        colorLabel->setFixedSize(60, 25);
        colorLabel->setFrameStyle(QFrame::Box);
        colorLabel->setLineWidth(1);

        // Create pixmap with checkerboard pattern
        QPixmap colorPixmap(60, 25);
        QPainter painter(&colorPixmap);

        // Draw checkerboard background
        QColor lightGray(204, 204, 204);
        QColor darkGray(255, 255, 255);
        for (int y = 0; y < 25; y += 10) {
            for (int x = 0; x < 60; x += 10) {
                bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
            }
        }

        // Draw the color with alpha on top
        painter.fillRect(0, 0, 60, 25, QColor(color.red, color.green, color.blue, color.alpha));
        painter.end();

        colorLabel->setPixmap(colorPixmap);
        colorLabel->setCursor(Qt::PointingHandCursor);

        colorLabel->setProperty("colorR", color.red);
        colorLabel->setProperty("colorG", color.green);
        colorLabel->setProperty("colorB", color.blue);
        colorLabel->setProperty("colorA", color.alpha);
        colorLabel->setProperty("changeCallback", QVariant::fromValue(onChange));

        return colorLabel;
    };

    auto colorRGBAUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QLabel* colorLabel = qobject_cast<QLabel*>(widget)) {
            RGBAColor color = value.value<RGBAColor>();

            // Redraw pixmap with new color
            QPixmap colorPixmap(60, 25);
            QPainter painter(&colorPixmap);

            // Draw checkerboard background
            QColor lightGray(204, 204, 204);
            QColor darkGray(255, 255, 255);
            for (int y = 0; y < 25; y += 10) {
                for (int x = 0; x < 60; x += 10) {
                    bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                    painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
                }
            }

            // Draw the color with alpha on top
            painter.fillRect(0, 0, 60, 25, QColor(color.red, color.green, color.blue, color.alpha));
            painter.end();

            colorLabel->setPixmap(colorPixmap);
            colorLabel->setProperty("colorR", color.red);
            colorLabel->setProperty("colorG", color.green);
            colorLabel->setProperty("colorB", color.blue);
            colorLabel->setProperty("colorA", color.alpha);
        }
    };

    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::COLOR), colorRGBACreator, colorRGBAUpdater);

    // VECTOR3D handler
    auto vector3dCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        Vector3d vec = value.value<Vector3d>();

        QWidget* container = new QWidget(context.parent);
        QHBoxLayout* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        QDoubleSpinBox* xBox = new QDoubleSpinBox(container);
        xBox->setPrefix("X: ");
        xBox->setMinimum(std::numeric_limits<double>::min());
        xBox->setMaximum(std::numeric_limits<double>::max());
        xBox->setDecimals(2);
        xBox->setValue(vec.x);

        QDoubleSpinBox* yBox = new QDoubleSpinBox(container);
        yBox->setPrefix("Y: ");
        yBox->setMinimum(std::numeric_limits<double>::min());
        yBox->setMaximum(std::numeric_limits<double>::max());
        yBox->setDecimals(2);
        yBox->setValue(vec.y);

        QDoubleSpinBox* zBox = new QDoubleSpinBox(container);
        zBox->setPrefix("Z: ");
        zBox->setMinimum(std::numeric_limits<double>::min());
        zBox->setMaximum(std::numeric_limits<double>::max());
        zBox->setDecimals(2);
        zBox->setValue(vec.z);

        layout->addWidget(xBox);
        layout->addWidget(yBox);
        layout->addWidget(zBox);

        auto emitChange = [xBox, yBox, zBox, onChange]() {
            Vector3d vec;
            vec.x = xBox->value();
            vec.y = yBox->value();
            vec.z = zBox->value();
            onChange(QVariant::fromValue(vec));
        };

        QObject::connect(xBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);
        QObject::connect(yBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);
        QObject::connect(zBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);

        return container;
    };

    auto vector3dUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QWidget* container = qobject_cast<QWidget*>(widget)) {
            Vector3d vec = value.value<Vector3d>();
            QList<QDoubleSpinBox*> spinBoxes = container->findChildren<QDoubleSpinBox*>();
            if (spinBoxes.size() >= 3) {
                spinBoxes[0]->setValue(vec.x);
                spinBoxes[1]->setValue(vec.y);
                spinBoxes[2]->setValue(vec.z);
            }
        }
    };

    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::VECTOR3D), vector3dCreator, vector3dUpdater);

    // All list types with possible values (uses ListEditorDialog via event filter)
    auto listWithValuesCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QLabel* label = new QLabel(context.parent);

        QStringList items = value.toStringList();
        QString displayValue = items.join(", ");

        if (displayValue.length() > MAX_DISPLAY_LENGTH) {
            label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
            label->setToolTip(displayValue + "\n\nClick to edit");
        } else {
            label->setText(displayValue.isEmpty() ? "(empty - click to add)" : displayValue);
            label->setToolTip("Click to edit");
        }

        label->setStyleSheet("QLabel { padding: 2px; }");
        label->setCursor(Qt::PointingHandCursor);
        label->setFrameStyle(QFrame::Box);
        label->setLineWidth(1);

        // Store properties for event filter handling
        label->setProperty("isListWithPossibleValues", true);
        label->setProperty("possibleValues", context.possibleValues);
        label->setProperty("settingName", context.name);
        label->setProperty("changeCallback", QVariant::fromValue(onChange));

        return label;
    };

    auto listWithValuesUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            QStringList items = value.toStringList();
            QString displayValue = items.join(", ");

            if (displayValue.length() > MAX_DISPLAY_LENGTH) {
                label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
                label->setToolTip(displayValue + "\n\nClick to edit");
            } else {
                label->setText(displayValue.isEmpty() ? "(empty - click to add)" : displayValue);
                label->setToolTip("Click to edit");
            }
        }
    };

    // Register all list types that use ListEditorDialog
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::BLOCK_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::ITEM_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::ENTITY_TYPE_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::ENCHANTMENT_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::STORAGE_BLOCK_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::SOUND_EVENT_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::STATUS_EFFECT_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::PARTICLE_TYPE_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::MODULE_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::PACKET_LIST), listWithValuesCreator, listWithValuesUpdater);
    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::SCREEN_HANDLER_LIST), listWithValuesCreator, listWithValuesUpdater);

    // Free-form string list (uses StringListEditorDialog via event filter)
    auto stringListCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        QLabel* label = new QLabel(context.parent);

        QStringList items = value.toStringList();
        QString displayValue = items.join(", ");

        if (displayValue.length() > MAX_DISPLAY_LENGTH) {
            label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
            label->setToolTip(displayValue + "\n\nClick to edit");
        } else {
            label->setText(displayValue.isEmpty() ? "(empty - click to add)" : displayValue);
            label->setToolTip("Click to edit");
        }

        label->setStyleSheet("QLabel { padding: 2px; }");
        label->setCursor(Qt::PointingHandCursor);
        label->setFrameStyle(QFrame::Box);
        label->setLineWidth(1);

        // Store properties for event filter handling
        label->setProperty("isFreeFormStringList", true);
        label->setProperty("settingName", context.name);
        label->setProperty("changeCallback", QVariant::fromValue(onChange));

        return label;
    };

    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::STRING_LIST), stringListCreator, listWithValuesUpdater);

    // BLOCK_ESP_CONFIG_MAP - read-only (for now)
    auto espMapCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback) -> QWidget* {
        ESPBlockDataMap configMap = value.value<ESPBlockDataMap>();
        QLabel* label = new QLabel(context.parent);
        QString displayValue;
        if (configMap.isEmpty()) {
            displayValue = "(empty map)";
        } else {
            QStringList blockNames;
            for (auto it = configMap.constBegin(); it != configMap.constEnd(); ++it) {
                blockNames.append(it.key());
            }
            displayValue = QString("%1 block(s): %2").arg(configMap.size()).arg(blockNames.join(", "));
        }

        if (displayValue.length() > MAX_DISPLAY_LENGTH) {
            label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
            label->setToolTip(displayValue + "\n\n(View/edit this setting in-game - complex map type)");
        } else {
            label->setText(displayValue);
            label->setToolTip("View/edit this setting in-game - complex map type");
        }

        label->setStyleSheet("QLabel { padding: 2px; }");
        return label;
    };

    auto espMapUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            ESPBlockDataMap configMap = value.value<ESPBlockDataMap>();
            QString displayValue;
            if (configMap.isEmpty()) {
                displayValue = "(empty map)";
            } else {
                QStringList blockNames;
                for (auto it = configMap.constBegin(); it != configMap.constEnd(); ++it) {
                    blockNames.append(it.key());
                }
                displayValue = QString("%1 block(s): %2").arg(configMap.size()).arg(blockNames.join(", "));
            }

            if (displayValue.length() > MAX_DISPLAY_LENGTH) {
                label->setText(displayValue.left(TRUNCATE_LENGTH) + "...");
                label->setToolTip(displayValue + "\n\n(View/edit this setting in-game - complex map type)");
            } else {
                label->setText(displayValue);
            }
        }
    };

    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::BLOCK_ESP_CONFIG_MAP), espMapCreator, espMapUpdater);

    // ESP_BLOCK_DATA - complex composite widget (needs event filter for nested color labels)
    auto espBlockDataCreator = [](const QVariant& value, const SettingEditorContext& context, ChangeCallback onChange) -> QWidget* {
        ESPBlockData data = value.value<ESPBlockData>();

        QWidget* container = new QWidget(context.parent);
        QVBoxLayout* mainLayout = new QVBoxLayout(container);
        mainLayout->setContentsMargins(0, 2, 0, 2);
        mainLayout->setSpacing(4);

        // Shape mode row
        QHBoxLayout* shapeLayout = new QHBoxLayout();
        shapeLayout->setSpacing(4);
        QLabel* shapeLabel = new QLabel("Shape:", container);
        shapeLabel->setFixedWidth(50);
        QComboBox* shapeCombo = new QComboBox(container);
        shapeCombo->addItem("Lines", ESPBlockData::Lines);
        shapeCombo->addItem("Sides", ESPBlockData::Sides);
        shapeCombo->addItem("Both", ESPBlockData::Both);
        shapeCombo->setCurrentIndex(static_cast<int>(data.shapeMode));
        shapeLayout->addWidget(shapeLabel);
        shapeLayout->addWidget(shapeCombo, 1);
        mainLayout->addLayout(shapeLayout);

        // Helper lambda to create color labels
        auto createColorLabel = [container](const RGBAColor& color) -> QLabel* {
            QLabel* colorLabel = new QLabel(container);
            colorLabel->setFixedSize(60, 25);
            colorLabel->setFrameStyle(QFrame::Box);
            colorLabel->setLineWidth(1);

            QPixmap colorPixmap(60, 25);
            QPainter painter(&colorPixmap);

            QColor lightGray(204, 204, 204);
            QColor darkGray(255, 255, 255);
            for (int y = 0; y < 25; y += 10) {
                for (int x = 0; x < 60; x += 10) {
                    bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                    painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
                }
            }

            painter.fillRect(0, 0, 60, 25, QColor(color.red, color.green, color.blue, color.alpha));
            painter.end();

            colorLabel->setPixmap(colorPixmap);
            colorLabel->setCursor(Qt::PointingHandCursor);

            colorLabel->setProperty("colorR", color.red);
            colorLabel->setProperty("colorG", color.green);
            colorLabel->setProperty("colorB", color.blue);
            colorLabel->setProperty("colorA", color.alpha);

            return colorLabel;
        };

        // Line color row
        QHBoxLayout* lineColorLayout = new QHBoxLayout();
        lineColorLayout->setSpacing(4);
        QLabel* lineLabel = new QLabel("Line:", container);
        lineLabel->setFixedWidth(50);
        QLabel* lineColorLabel = createColorLabel(data.lineColor);
        lineColorLabel->setObjectName("lineColor");
        lineColorLayout->addWidget(lineLabel);
        lineColorLayout->addWidget(lineColorLabel);
        lineColorLayout->addStretch();
        mainLayout->addLayout(lineColorLayout);

        // Side color row
        QHBoxLayout* sideColorLayout = new QHBoxLayout();
        sideColorLayout->setSpacing(4);
        QLabel* sideLabel = new QLabel("Side:", container);
        sideLabel->setFixedWidth(50);
        QLabel* sideColorLabel = createColorLabel(data.sideColor);
        sideColorLabel->setObjectName("sideColor");
        sideColorLayout->addWidget(sideLabel);
        sideColorLayout->addWidget(sideColorLabel);
        sideColorLayout->addStretch();
        mainLayout->addLayout(sideColorLayout);

        // Tracer row
        QHBoxLayout* tracerLayout = new QHBoxLayout();
        tracerLayout->setSpacing(4);
        QCheckBox* tracerCheck = new QCheckBox("Tracer", container);
        tracerCheck->setChecked(data.tracer);
        QLabel* tracerColorLabel = createColorLabel(data.tracerColor);
        tracerColorLabel->setObjectName("tracerColor");
        tracerColorLabel->setEnabled(data.tracer);
        tracerLayout->addWidget(tracerCheck);
        tracerLayout->addWidget(tracerColorLabel);
        tracerLayout->addStretch();
        mainLayout->addLayout(tracerLayout);

        // Connect signals - this emits the entire ESPBlockData when any part changes
        auto emitESPChange = [container, shapeCombo, tracerCheck, onChange]() {
            ESPBlockData newData;
            newData.shapeMode = static_cast<ESPBlockData::ShapeMode>(shapeCombo->currentData().toInt());

            QLabel* lineColorLabel = container->findChild<QLabel*>("lineColor");
            if (lineColorLabel) {
                newData.lineColor.red = lineColorLabel->property("colorR").toInt();
                newData.lineColor.green = lineColorLabel->property("colorG").toInt();
                newData.lineColor.blue = lineColorLabel->property("colorB").toInt();
                newData.lineColor.alpha = lineColorLabel->property("colorA").toInt();
            }

            QLabel* sideColorLabel = container->findChild<QLabel*>("sideColor");
            if (sideColorLabel) {
                newData.sideColor.red = sideColorLabel->property("colorR").toInt();
                newData.sideColor.green = sideColorLabel->property("colorG").toInt();
                newData.sideColor.blue = sideColorLabel->property("colorB").toInt();
                newData.sideColor.alpha = sideColorLabel->property("colorA").toInt();
            }

            newData.tracer = tracerCheck->isChecked();

            QLabel* tracerColorLabel = container->findChild<QLabel*>("tracerColor");
            if (tracerColorLabel) {
                newData.tracerColor.red = tracerColorLabel->property("colorR").toInt();
                newData.tracerColor.green = tracerColorLabel->property("colorG").toInt();
                newData.tracerColor.blue = tracerColorLabel->property("colorB").toInt();
                newData.tracerColor.alpha = tracerColorLabel->property("colorA").toInt();
            }

            onChange(QVariant::fromValue(newData));
        };

        QObject::connect(shapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), emitESPChange);
        QObject::connect(tracerCheck, &QCheckBox::toggled, [tracerColorLabel, emitESPChange](bool checked) {
            tracerColorLabel->setEnabled(checked);
            emitESPChange();
        });

        // Store the change callback on the container for color label event filter access
        container->setProperty("espChangeCallback", QVariant::fromValue(onChange));

        return container;
    };

    auto espBlockDataUpdater = [](QWidget* widget, const QVariant& value, const SettingEditorContext&) {
        if (QWidget* container = qobject_cast<QWidget*>(widget)) {
            ESPBlockData data = value.value<ESPBlockData>();

            QComboBox* shapeCombo = container->findChild<QComboBox*>();
            if (shapeCombo) {
                shapeCombo->setCurrentIndex(static_cast<int>(data.shapeMode));
            }

            auto updateColorLabel = [](QLabel* label, const RGBAColor& color) {
                if (!label) return;

                QPixmap colorPixmap(60, 25);
                QPainter painter(&colorPixmap);
                QColor lightGray(204, 204, 204);
                QColor darkGray(255, 255, 255);
                for (int y = 0; y < 25; y += 10) {
                    for (int x = 0; x < 60; x += 10) {
                        bool isLight = ((x / 10) + (y / 10)) % 2 == 0;
                        painter.fillRect(x, y, 10, 10, isLight ? lightGray : darkGray);
                    }
                }
                painter.fillRect(0, 0, 60, 25, QColor(color.red, color.green, color.blue, color.alpha));
                painter.end();
                label->setPixmap(colorPixmap);
                label->setProperty("colorR", color.red);
                label->setProperty("colorG", color.green);
                label->setProperty("colorB", color.blue);
                label->setProperty("colorA", color.alpha);
            };

            updateColorLabel(container->findChild<QLabel*>("lineColor"), data.lineColor);
            updateColorLabel(container->findChild<QLabel*>("sideColor"), data.sideColor);
            updateColorLabel(container->findChild<QLabel*>("tracerColor"), data.tracerColor);

            QCheckBox* tracerCheck = container->findChild<QCheckBox*>();
            if (tracerCheck) {
                tracerCheck->setChecked(data.tracer);
            }

            QLabel* tracerColorLabel = container->findChild<QLabel*>("tracerColor");
            if (tracerColorLabel) {
                tracerColorLabel->setEnabled(data.tracer);
            }
        }
    };

    registerType(SettingSystemType::Meteor, static_cast<int>(MeteorSettingType::ESP_BLOCK_DATA), espBlockDataCreator, espBlockDataUpdater);
}
