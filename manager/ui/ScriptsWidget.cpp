#include "ScriptsWidget.h"
#include "scripting/ScriptEngine.h"
#include "scripting/ScriptContext.h"
#include "scripting/ScriptFileManager.h"
#include "bot/BotManager.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QGroupBox>
#include <QSettings>
#include <QGuiApplication>
#include <QStyleHints>
#include <QDirIterator>
#include <QFileInfo>
#include <qutepart/qutepart.h>
#include <qutepart/theme.h>

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
    leftLayout->addWidget(scriptList);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    newButton = new QPushButton("New");
    deleteButton = new QPushButton("Delete");
    buttonLayout->addWidget(newButton);
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
    rightLayout->addWidget(codeEditor);

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
    connect(deleteButton, &QPushButton::clicked,
            this, &ScriptsWidget::onDeleteScript);
    connect(saveButton, &QPushButton::clicked,
            this, &ScriptsWidget::onSaveScript);
    connect(runButton, &QPushButton::clicked,
            this, &ScriptsWidget::onRunScript);
    connect(stopButton, &QPushButton::clicked,
            this, &ScriptsWidget::onStopScript);
    connect(codeEditor, &Qutepart::Qutepart::textChanged,
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
    for (const QString &scriptName : scripts) {
        QListWidgetItem *item = new QListWidgetItem(scriptName);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        bool enabled = scriptEngine->isScriptEnabled(scriptName);
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

        // Add tooltip explaining the checkbox
        item->setToolTip("Check to auto-run this script when the manager starts");

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
        codeEditor->setPlainText(ctx->code);
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

    QString code = codeEditor->toPlainText();
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
    for (const QString &name : scriptNames) {
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
            item->setBackground(QColor(200, 255, 200));
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
            item->setBackground(Qt::transparent);
            break;
        }
    }
}

void ScriptsWidget::onScriptError(const QString &filename, const QString &error)
{
    (void)error;  // Mark as intentionally unused

    for (int i = 0; i < scriptList->count(); ++i) {
        QListWidgetItem *item = scriptList->item(i);
        if (item->text() == filename) {
            item->setBackground(QColor(255, 200, 200));
            break;
        }
    }
}

void ScriptsWidget::updateButtons()
{
    bool hasScript = !currentScript.isEmpty();
    bool isRunning = hasScript && scriptEngine && scriptEngine->isScriptRunning(currentScript);

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

    Qutepart::Theme *theme = new Qutepart::Theme();
    theme->loadTheme(getEditorThemePath());
    codeEditor->setTheme(theme);
}

void ScriptsWidget::setupEditor()
{
    codeEditor = new Qutepart::Qutepart(this);

    Qutepart::Theme *theme = new Qutepart::Theme();
    theme->loadTheme(getEditorThemePath());
    codeEditor->setTheme(theme);

    codeEditor->setIndentWidth(4);
    codeEditor->setIndentUseTabs(false);
    codeEditor->setDrawIndentations(true);
    codeEditor->setLineLengthEdge(88);
    codeEditor->setDrawAnyWhitespace(false);
    codeEditor->setDrawIncorrectIndentation(true);
    codeEditor->setDrawSolidEdge(true);

    Qutepart::LangInfo langInfo = Qutepart::chooseLanguage(
        QString(),
        QString(),
        "script.py"
    );

    if (langInfo.isValid()) {
        codeEditor->setHighlighter(langInfo.id);
        codeEditor->setIndentAlgorithm(langInfo.indentAlg);
    }

    codeEditor->setCompletionEnabled(true);
    codeEditor->setCompletionThreshold(2);
    codeEditor->setCompletionCallback([this](const QString &) {
        return getCompletions();
    });
}

QStringList ScriptsWidget::getAvailableThemes()
{
    QStringList themes;
    QDirIterator it(":/qutepart/themes", QStringList() << "*.theme", QDir::Files);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);
        // Extract just the theme name without path and extension
        themes.append(fileInfo.baseName());
    }

    themes.sort(Qt::CaseInsensitive);
    return themes;
}

QString ScriptsWidget::getEditorThemePath()
{
    QSettings settings;
    QString themePref = settings.value("editor/theme", "Follow System").toString();

    if (themePref == "Follow System") {
        auto colorScheme = QGuiApplication::styleHints()->colorScheme();

        if (colorScheme == Qt::ColorScheme::Dark) {
            return ":/qutepart/themes/breeze-dark.theme";
        } else {
            return ":/qutepart/themes/breeze-light.theme";
        }
    }

    // User selected a specific theme
    return QString(":/qutepart/themes/%1.theme").arg(themePref);
}

QSet<QString> ScriptsWidget::getCompletions()
{
    QSet<QString> completions;

    // Module imports
    completions << "import bot" << "import baritone" << "import meteor" << "import utils";

    // Module names
    completions << "bot" << "baritone" << "meteor" << "utils";

    // bot module - state queries
    completions << "bot.position()" << "bot.health()" << "bot.hunger()";
    completions << "bot.saturation()" << "bot.air()";
    completions << "bot.experience_level()" << "bot.experience_progress()";
    completions << "bot.selected_slot()";
    completions << "bot.server()" << "bot.account()" << "bot.uptime()";
    completions << "bot.dimension()" << "bot.is_online()" << "bot.status()";
    completions << "bot.inventory()" << "bot.network_stats()" << "bot.list_all()";

    // bot module - control
    completions << "bot.start(" << "bot.stop(" << "bot.restart(";
    completions << "bot.chat(" << "bot.manager_command(";

    // baritone module
    completions << "baritone.goto(" << "baritone.follow(" << "baritone.cancel()";
    completions << "baritone.mine(" << "baritone.farm()" << "baritone.command(";
    completions << "baritone.set_setting(" << "baritone.get_setting(";

    // meteor module
    completions << "meteor.toggle(" << "meteor.enable(" << "meteor.disable(";
    completions << "meteor.set_setting(" << "meteor.get_setting(";
    completions << "meteor.get_module(" << "meteor.list_modules()";

    // utils module
    completions << "utils.log(" << "utils.error(";

    // Event decorator and event names
    completions << "@on(";
    completions << "chat_message" << "health_change" << "hunger_change";
    completions << "player_state" << "inventory_update";

    return completions;
}
