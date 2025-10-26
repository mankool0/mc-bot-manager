#ifndef PRISMSETTINGSDIALOG_H
#define PRISMSETTINGSDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QMap>

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
    void setAccountIdToNameMap(const QMap<QString, QString> &idToNameMap);

    QStringList getInstances() const { return instances; }
    QStringList getAccounts() const;
    QMap<QString, QString> getAccountIdToNameMap() const { return accountIdToNameMap; }

    // Static utility methods for parsing PrismLauncher data
    static QString detectPrismLauncherPath();
    static QString detectPrismLauncherExecutable(const QString &prismPath);
    static QStringList parsePrismInstances(const QString &prismPath);
    static QMap<QString, QString> parsePrismAccounts(const QString &prismPath);

private slots:
    void onBrowseClicked();
    void onBrowseExeClicked();
    void updateStatistics();

private:
    Ui::PrismSettingsDialog *ui;
    QString currentPath;
    QString currentExecutable;
    QStringList instances;
    QMap<QString, QString> accountIdToNameMap;

    void parsePrismDirectory(const QString &path);
    static bool isFlatpakPath(const QString &path);
};

#endif // PRISMSETTINGSDIALOG_H