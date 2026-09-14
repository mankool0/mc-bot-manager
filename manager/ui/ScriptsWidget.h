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
#include <QTimer>
#include <memory>

#include "ui/MonacoWidget.h"

class ScriptEngine;
class ZubanClient;

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

    // Call on the top-level window that will host editors, before it is first shown.
    static void reserveEditorSurface(QWidget *topLevel);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

signals:
    void scriptLoaded(const QString &filename);
    void scriptSaved(const QString &filename);

private slots:
    void onScriptSelectionChanged();
    void onNewScript();
    void onRenameScript();
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
    // The editor is a Chromium renderer plus a language server, about 200 MB per widget,
    // and there is one widget per bot. It exists only while its tab is or was recently
    // on screen: created on the first show, released after a long spell hidden.
    void ensureEditor();
    void releaseEditor();
    void setScriptModified(const QString &filename, bool modified);

    ScriptEngine *scriptEngine;
    // Shared with the completion lambdas, which run on a thread pool and may still be
    // inside a request when the editor is released; the last holder posts the delete.
    std::shared_ptr<ZubanClient> zubanClient;

    QListWidget *scriptList;
    QVBoxLayout *editorHostLayout = nullptr;
    QLabel *editorPlaceholder = nullptr;
    MonacoWidget *codeEditor = nullptr;
    bool editorCreating = false;
    QTimer *editorIdleTimer = nullptr;
    QPushButton *newButton;
    QPushButton *renameButton;
    QPushButton *deleteButton;
    QPushButton *saveButton;
    QPushButton *runButton;
    QPushButton *stopButton;
    QLabel *statusLabel;

    QString currentScript;
    QSet<QString> modifiedScripts;
};

#endif // SCRIPTSWIDGET_H
