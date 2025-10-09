#ifndef PRISMSETTINGSDIALOG_H
#define PRISMSETTINGSDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

#include "ui_PrismSettingsDialog.h"

class PrismSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PrismSettingsDialog(QWidget *parent = nullptr);
    ~PrismSettingsDialog();

    void setCurrentPath(const QString &path);
    QString getCurrentPath() const;

    void setExecutable(const QString &executable);
    QString getExecutable() const;

    void setInstances(const QStringList &instances);
    void setAccounts(const QStringList &accounts);

    QStringList getInstances() const { return instances; }
    QStringList getAccounts() const { return accounts; }

    // Static utility methods for parsing PrismLauncher data
    static QString detectPrismLauncherPath();
    static QString detectPrismLauncherExecutable(const QString &prismPath);
    static QStringList parsePrismInstances(const QString &prismPath);
    static QStringList parsePrismAccounts(const QString &prismPath);

private slots:
    void onBrowseClicked();
    void onBrowseExeClicked();
    void updateStatistics();

private:
    Ui::PrismSettingsDialog *ui;
    QString currentPath;
    QString currentExecutable;
    QStringList instances;
    QStringList accounts;

    void parsePrismDirectory(const QString &path);
    static bool isFlatpakPath(const QString &path);
};

#endif // PRISMSETTINGSDIALOG_H