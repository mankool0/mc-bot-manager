#ifndef SCRIPTSWIDGET_H
#define SCRIPTSWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QSet>

namespace Qutepart {
    class Qutepart;
}

class ScriptEngine;

class ScriptsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptsWidget(ScriptEngine *engine, QWidget *parent = nullptr);
    ~ScriptsWidget();

    void refreshScriptList();
    void loadScript(const QString &filename);
    void reloadTheme();

    static QStringList getAvailableThemes();

signals:
    void scriptLoaded(const QString &filename);
    void scriptSaved(const QString &filename);

private slots:
    void onScriptSelectionChanged();
    void onNewScript();
    void onDeleteScript();
    void onSaveScript();
    void onRunScript();
    void onStopScript();
    void onScriptItemChanged(QListWidgetItem *item);
    void onScriptStarted(const QString &filename);
    void onScriptStopped(const QString &filename);
    void onScriptError(const QString &filename, const QString &error);

private:
    void setupUI();
    void updateButtons();
    void setupEditor();
    QSet<QString> getCompletions();
    QString getEditorThemePath();

    ScriptEngine *scriptEngine;

    QListWidget *scriptList;
    Qutepart::Qutepart *codeEditor;
    QPushButton *newButton;
    QPushButton *deleteButton;
    QPushButton *saveButton;
    QPushButton *runButton;
    QPushButton *stopButton;
    QLabel *statusLabel;

    QString currentScript;
    bool isModified;
};

#endif // SCRIPTSWIDGET_H
