#ifndef GLOBALSETTINGSDIALOG_H
#define GLOBALSETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
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

    QSpinBox *consoleMaxLinesSpinBox;
    QCheckBox *consoleUnlimitedCheckBox;
    QSpinBox *consolePendingLinesSpinBox;
    QComboBox *colorSchemeComboBox;

    QDialogButtonBox *buttonBox;
};

#endif // GLOBALSETTINGSDIALOG_H
