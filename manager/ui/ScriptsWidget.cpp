#include "ScriptsWidget.h"
#include "ZubanClient.h"
#include "scripting/ScriptEngine.h"
#include "scripting/ScriptContext.h"
#include "scripting/ScriptFileManager.h"
#include "bot/BotManager.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QGroupBox>
#include <QSettings>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPainter>
#include <QStyledItemDelegate>

enum ScriptItemState { StateNormal = 0, StateRunning = 1, StateError = 2 };
static const int ScriptStateRole = Qt::UserRole + 1;

class ScriptItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);

        int state = index.data(ScriptStateRole).toInt();
        if (state == StateNormal)
            return;

        QColor tint = (state == StateRunning) ? QColor(76, 175, 80, 70) : QColor(244, 67, 54, 70);
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRect(option.rect);
        painter->restore();
    }
};

ScriptsWidget::ScriptsWidget(ScriptEngine *engine, QWidget *parent)
    : QWidget(parent)
    , scriptEngine(engine)
    , isModified(false)
{
    setupUI();
    refreshScriptList();

    connect(scriptEngine, &ScriptEngine::scriptStarted,
            this, &ScriptsWidget::onScriptStarted);
    connect(scriptEngine, &ScriptEngine::scriptStopped,
            this, &ScriptsWidget::onScriptStopped);
    connect(scriptEngine, &ScriptEngine::scriptError,
            this, &ScriptsWidget::onScriptError);
}

ScriptsWidget::~ScriptsWidget()
{
}

void ScriptsWidget::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    QWidget *leftPanel = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *scriptsLabel = new QLabel("Scripts");
    scriptsLabel->setStyleSheet("font-weight: bold; font-size: 11pt;");
    leftLayout->addWidget(scriptsLabel);

    scriptList = new QListWidget();
    scriptList->setSelectionMode(QAbstractItemView::SingleSelection);
    scriptList->setItemDelegate(new ScriptItemDelegate(scriptList));
    leftLayout->addWidget(scriptList);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    newButton = new QPushButton("New");
    renameButton = new QPushButton("Rename");
    deleteButton = new QPushButton("Delete");
    buttonLayout->addWidget(newButton);
    buttonLayout->addWidget(renameButton);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addStretch();
    leftLayout->addLayout(buttonLayout);

    leftPanel->setLayout(leftLayout);
    splitter->addWidget(leftPanel);

    QWidget *rightPanel = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *editorLabel = new QLabel("Script Editor");
    editorLabel->setStyleSheet("font-weight: bold; font-size: 11pt;");
    rightLayout->addWidget(editorLabel);

    setupEditor();
    rightLayout->addWidget(codeEditor, 1);

    QHBoxLayout *editorButtonLayout = new QHBoxLayout();
    saveButton = new QPushButton("Save");
    runButton = new QPushButton("Run");
    stopButton = new QPushButton("Stop");
    editorButtonLayout->addWidget(saveButton);
    editorButtonLayout->addWidget(runButton);
    editorButtonLayout->addWidget(stopButton);
    editorButtonLayout->addStretch();
    rightLayout->addLayout(editorButtonLayout);

    statusLabel = new QLabel("");
    statusLabel->setStyleSheet("color: gray; font-style: italic;");
    editorButtonLayout->addWidget(statusLabel);

    rightPanel->setLayout(rightLayout);
    splitter->addWidget(rightPanel);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    mainLayout->addWidget(splitter);

    connect(scriptList, &QListWidget::itemSelectionChanged,
            this, &ScriptsWidget::onScriptSelectionChanged);
    connect(scriptList, &QListWidget::itemChanged,
            this, &ScriptsWidget::onScriptItemChanged);
    connect(newButton, &QPushButton::clicked,
            this, &ScriptsWidget::onNewScript);
    connect(renameButton, &QPushButton::clicked,
            this, &ScriptsWidget::onRenameScript);
    connect(deleteButton, &QPushButton::clicked,
            this, &ScriptsWidget::onDeleteScript);
    connect(saveButton, &QPushButton::clicked,
            this, &ScriptsWidget::onSaveScript);
    connect(runButton, &QPushButton::clicked,
            this, &ScriptsWidget::onRunScript);
    connect(stopButton, &QPushButton::clicked,
            this, &ScriptsWidget::onStopScript);
    connect(codeEditor, &MonacoWidget::textChanged,
            this, [this]() { isModified = true; updateButtons(); });

    updateButtons();
}

void ScriptsWidget::refreshScriptList()
{
    scriptList->clear();

    if (!scriptEngine) {
        return;
    }

    QStringList scripts = scriptEngine->getScriptNames();
    for (const QString &scriptName : std::as_const(scripts)) {
        QListWidgetItem *item = new QListWidgetItem(scriptName);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        bool enabled = scriptEngine->isScriptEnabled(scriptName);
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

        item->setToolTip("Check to auto-run this script when the manager starts");

        if (scriptEngine->isScriptRunning(scriptName))
            item->setData(ScriptStateRole, StateRunning);

        scriptList->addItem(item);
    }
}

void ScriptsWidget::loadScript(const QString &filename)
{
    if (!scriptEngine) {
        return;
    }

    ScriptContext *ctx = scriptEngine->getScript(filename);
    if (ctx) {
        currentScript = filename;
        codeEditor->setText(ctx->code);
        isModified = false;
        statusLabel->setText(QString("Loaded: %1").arg(filename));
        updateButtons();
    }
}

void ScriptsWidget::onScriptSelectionChanged()
{
    QList<QListWidgetItem*> selected = scriptList->selectedItems();
    if (selected.isEmpty()) {
        currentScript.clear();
        codeEditor->clear();
        statusLabel->clear();
        updateButtons();
        return;
    }

    QString scriptName = selected.first()->text();
    loadScript(scriptName);
}

void ScriptsWidget::onNewScript()
{
    QString name = QInputDialog::getText(this, "New Script", "Script name:");
    if (name.isEmpty()) {
        return;
    }

    if (!name.endsWith(".py")) {
        name += ".py";
    }

    if (scriptEngine->getScript(name)) {
        QMessageBox::warning(this, "Script Exists", "A script with this name already exists.");
        return;
    }

    QString defaultCode = "# " + name + "\n";
    if (scriptEngine->loadScript(name, defaultCode)) {
        QString botName = scriptEngine->getBotName();
        if (ScriptFileManager::saveScript(botName, name, defaultCode)) {
            refreshScriptList();

            for (int i = 0; i < scriptList->count(); ++i) {
                if (scriptList->item(i)->text() == name) {
                    scriptList->setCurrentRow(i);
                    break;
                }
            }

            statusLabel->setText(QString("Created: %1").arg(name));
        } else {
            scriptEngine->unloadScript(name);
            QMessageBox::warning(this, "Save Failed", "Failed to save script to disk.");
        }
    }
}

void ScriptsWidget::onRenameScript()
{
    if (currentScript.isEmpty()) {
        return;
    }

    QString newName = QInputDialog::getText(this, "Rename Script", "New name:", QLineEdit::Normal, currentScript);
    if (newName.isEmpty() || newName == currentScript) {
        return;
    }

    if (!newName.endsWith(".py")) {
        newName += ".py";
    }

    if (scriptEngine->getScript(newName)) {
        QMessageBox::warning(this, "Script Exists", "A script with this name already exists.");
        return;
    }

    ScriptContext *ctx = scriptEngine->getScript(currentScript);
    if (!ctx) {
        return;
    }

    QString code = ctx->code;
    bool autorun = scriptEngine->isScriptEnabled(currentScript);
    QString botName = scriptEngine->getBotName();

    if (!ScriptFileManager::renameScript(botName, currentScript, newName)) {
        QMessageBox::warning(this, "Rename Failed", "Failed to rename script file on disk.");
        return;
    }

    scriptEngine->loadScript(newName, code);
    scriptEngine->enableScript(newName, autorun);
    scriptEngine->unloadScript(currentScript);

    currentScript = newName;
    refreshScriptList();

    for (int i = 0; i < scriptList->count(); ++i) {
        if (scriptList->item(i)->text() == newName) {
            scriptList->setCurrentRow(i);
            break;
        }
    }

    statusLabel->setText(QString("Renamed to: %1").arg(newName));
}

void ScriptsWidget::onDeleteScript()
{
    if (currentScript.isEmpty()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Delete Script",
        QString("Are you sure you want to delete '%1'?").arg(currentScript),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        scriptEngine->unloadScript(currentScript);
        QString botName = scriptEngine->getBotName();
        ScriptFileManager::deleteScript(botName, currentScript);
        refreshScriptList();
        currentScript.clear();
        codeEditor->clear();
        statusLabel->setText("Script deleted");
    }
}

void ScriptsWidget::onSaveScript()
{
    if (currentScript.isEmpty()) {
        return;
    }

    QString code = codeEditor->getText();
    QString botName = scriptEngine->getBotName();

    if (scriptEngine->loadScript(currentScript, code) &&
        ScriptFileManager::saveScript(botName, currentScript, code)) {
        isModified = false;
        statusLabel->setText(QString("Saved: %1").arg(currentScript));
        updateButtons();
        emit scriptSaved(currentScript);
    } else {
        QMessageBox::warning(this, "Save Failed", "Failed to save script.");
    }
}

void ScriptsWidget::onRunScript()
{
    if (currentScript.isEmpty()) {
        return;
    }

    if (isModified) {
        onSaveScript();
    }

    if (scriptEngine->runScript(currentScript)) {
        statusLabel->setText(QString("Running: %1").arg(currentScript));
        updateButtons();
    } else {
        QString error = scriptEngine->getScriptError(currentScript);
        statusLabel->setText(QString("Error: %1").arg(error));
    }
}

void ScriptsWidget::onStopScript()
{
    if (currentScript.isEmpty()) {
        return;
    }

    scriptEngine->stopScript(currentScript);
    statusLabel->setText(QString("Stopped: %1").arg(currentScript));
    updateButtons();
}

void ScriptsWidget::onScriptItemChanged(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    QString scriptName = item->text();
    bool autorun = (item->checkState() == Qt::Checked);

    scriptEngine->enableScript(scriptName, autorun);

    QMap<QString, ScriptState> states;
    QStringList scriptNames = scriptEngine->getScriptNames();
    for (const QString &name : std::as_const(scriptNames)) {
        ScriptState state;
        state.autorun = scriptEngine->isScriptEnabled(name);
        states[name] = state;
    }

    QString botName = scriptEngine->getBotName();
    ScriptFileManager::saveScriptStates(botName, states);
}

void ScriptsWidget::onScriptStarted(const QString &filename)
{
    if (filename == currentScript) {
        statusLabel->setText(QString("Running: %1").arg(filename));
        updateButtons();
    }

    for (int i = 0; i < scriptList->count(); ++i) {
        QListWidgetItem *item = scriptList->item(i);
        if (item->text() == filename) {
            item->setData(ScriptStateRole, StateRunning);
            break;
        }
    }
}

void ScriptsWidget::onScriptStopped(const QString &filename)
{
    if (filename == currentScript) {
        statusLabel->setText(QString("Stopped: %1").arg(filename));
        updateButtons();
    }

    for (int i = 0; i < scriptList->count(); ++i) {
        QListWidgetItem *item = scriptList->item(i);
        if (item->text() == filename) {
            if (item->data(ScriptStateRole).toInt() == StateRunning)
                item->setData(ScriptStateRole, StateNormal);
            break;
        }
    }
}

void ScriptsWidget::onScriptError(const QString &filename, const QString &error)
{
    (void)error;

    for (int i = 0; i < scriptList->count(); ++i) {
        QListWidgetItem *item = scriptList->item(i);
        if (item->text() == filename) {
            item->setData(ScriptStateRole, StateError);
            break;
        }
    }
}

void ScriptsWidget::updateButtons()
{
    bool hasScript = !currentScript.isEmpty();
    bool isRunning = hasScript && scriptEngine && scriptEngine->isScriptRunning(currentScript);

    renameButton->setEnabled(hasScript && !isRunning);
    deleteButton->setEnabled(hasScript && !isRunning);
    saveButton->setEnabled(hasScript && isModified);
    runButton->setEnabled(hasScript && !isRunning);
    stopButton->setEnabled(hasScript && isRunning);
    codeEditor->setReadOnly(isRunning);
}

void ScriptsWidget::reloadTheme()
{
    if (!codeEditor) {
        return;
    }

    QSettings settings;
    QString theme = settings.value("editor/theme", "Follow System").toString();
    bool dark;
    if (theme == "Follow System") {
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    } else {
        dark = (theme == "Dark");
    }
    codeEditor->setDarkMode(dark);
}

QStringList ScriptsWidget::getAvailableThemes()
{
    return {"Dark", "Light"};
}

void ScriptsWidget::setupEditor()
{
    QSettings settings;
    QString theme = settings.value("editor/theme", "Follow System").toString();
    bool dark;
    if (theme == "Follow System") {
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    } else {
        dark = (theme == "Dark");
    }

    codeEditor = new MonacoWidget(this);
    codeEditor->setDarkMode(dark);

    if (scriptEngine) {
        codeEditor->loadEventData(scriptEngine->loadEventData());

        zubanClient = new ZubanClient(this);
        connect(zubanClient, &ZubanClient::diagnosticsReceived,
                codeEditor, &MonacoWidget::setDiagnostics);

        QString scriptsDir = ScriptFileManager::getBaseScriptDir();
        QString stubsDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stubs";
        QString pylibsDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/pylibs";
        zubanClient->start(scriptsDir, stubsDir, pylibsDir);

        codeEditor->setCompletionProvider([zuban = zubanClient](const QString &code, int line, int col) -> QString {
            return zuban->complete(code, line, col);
        });
        codeEditor->setSignatureProvider([zuban = zubanClient](const QString &code, int line, int col) -> QString {
            return zuban->signatureHelp(code, line, col);
        });
        codeEditor->setHoverProvider([zuban = zubanClient](const QString &code, int line, int col) -> QString {
            return zuban->hover(code, line, col);
        });
    }
}
