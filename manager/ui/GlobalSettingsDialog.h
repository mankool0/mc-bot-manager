#ifndef GLOBALSETTINGSDIALOG_H
#define GLOBALSETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>

class GlobalSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalSettingsDialog(QWidget *parent = nullptr);
    ~GlobalSettingsDialog();

private slots:
    void saveSettings();
    void loadSettings();

private:
    void setupUI();

    QSpinBox *consoleMaxLinesSpinBox;
    QCheckBox *consoleUnlimitedCheckBox;

    QDialogButtonBox *buttonBox;
};

#endif // GLOBALSETTINGSDIALOG_H
