#ifndef GLOBALSETTINGSDIALOG_H
#define GLOBALSETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QColor>
#include <QVector>
#include <Qt>

class GlobalSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalSettingsDialog(QWidget *parent = nullptr);
    ~GlobalSettingsDialog();

    static void applyColorScheme();

private slots:
    void saveSettings();
    void loadSettings();

private:
    void setupUI();
    void addColorRow(QFormLayout *layout, const QString &label,
                     const QString &key, const QColor &defaultColor);
    static void setButtonColor(QPushButton *btn, const QColor &color);

    QTabWidget *tabWidget;

    QSpinBox *consoleMaxLinesSpinBox;
    QCheckBox *consoleUnlimitedCheckBox;
    QSpinBox *consolePendingLinesSpinBox;
    QComboBox *colorSchemeComboBox;

    QCheckBox *loggingEnabledCheckBox;
    QLineEdit *logDirEdit;
    QPushButton *logDirBrowseButton;
    QSpinBox *logMaxSizeMiBSpinBox;
    QSpinBox *logMaxFilesSpinBox;

    QDialogButtonBox *buttonBox;

    struct ColorEntry {
        QString key;
        QColor defaultColor;
        QPushButton *button = nullptr;
    };
    QVector<ColorEntry> m_colorEntries;
};

#endif // GLOBALSETTINGSDIALOG_H
