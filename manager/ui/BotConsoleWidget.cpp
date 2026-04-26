#include "BotConsoleWidget.h"
#include "AppColors.h"
#include "logging/LogFileSink.h"
#include <QDateTime>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QScrollBar>
#include <QTimer>
#include <QAbstractItemView>
#include <QSettings>
#include <QCheckBox>

BotConsoleWidget::BotConsoleWidget(QWidget *parent)
    : QWidget(parent)
    , historyIndex(-1)
    , m_ringCapacity(0)
    , m_ringHead(0)
    , m_ringTail(0)
    , m_ringCount(0)
    , m_droppedCount(0)
    , m_flushTimer(new QTimer(this))
{
    QSettings settings("MCBotManager", "MCBotManager");
    m_ringCapacity = settings.value("Console/maxPendingLines", 500).toInt();
    m_ring.resize(m_ringCapacity);

    m_flushTimer->setInterval(50);
    m_flushTimer->setSingleShot(false);
    connect(m_flushTimer, &QTimer::timeout, this, &BotConsoleWidget::flushPendingOutput);
    m_flushTimer->start();

    setupUI();
    setupCompleter();
    initializeCommands();
}

BotConsoleWidget::~BotConsoleWidget()
{
    delete m_fileSink;
}

void BotConsoleWidget::attachLogFile(const QString &logDir, const QString &botName, qint64 maxSizeBytes, int maxFiles)
{
    QMutexLocker locker(&m_ringMutex);
    delete m_fileSink;
    m_fileSink = new LogFileSink();
    m_fileSink->setMaxSizeBytes(maxSizeBytes);
    m_fileSink->setMaxFiles(maxFiles);
    // Unlock before open() to avoid holding mutex during I/O
    locker.unlock();
    m_fileSink->open(logDir + "/bots/" + botName, botName);
}

void BotConsoleWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    outputEdit = new QPlainTextEdit(this);
    outputEdit->setReadOnly(true);
    outputEdit->setFont(QFont("Consolas", 9));

    QSettings settings("MCBotManager", "MCBotManager");
    int lineLimit = settings.value("Console/maxLines", 10000).toInt();
    if (lineLimit > 0) {
        outputEdit->setMaximumBlockCount(lineLimit);
    }
    // If lineLimit is 0, don't set any limit (Qt default is unlimited)

    mainLayout->addWidget(outputEdit);

    hintLabel = new QLabel(this);
    hintLabel->setStyleSheet("QLabel { color: #666; font-style: italic; padding: 2px; }");
    hintLabel->setWordWrap(true);
    hintLabel->setVisible(false);
    mainLayout->addWidget(hintLabel);

    QHBoxLayout *inputLayout = new QHBoxLayout();

    inputEdit = new QLineEdit(this);
    inputEdit->setPlaceholderText("Enter command...");
    inputEdit->installEventFilter(this);
    inputLayout->addWidget(inputEdit);

    sendButton = new QPushButton("Send", this);
    inputLayout->addWidget(sendButton);

    QPushButton *clearButton = new QPushButton("Clear", this);
    inputLayout->addWidget(clearButton);

    autoScrollCheckBox = new QCheckBox("Auto-scroll", this);
    autoScrollCheckBox->setChecked(true);
    inputLayout->addWidget(autoScrollCheckBox);

    mainLayout->addLayout(inputLayout);

    connect(clearButton, &QPushButton::clicked, this, &BotConsoleWidget::clearOutput);
    connect(sendButton, &QPushButton::clicked, this, &BotConsoleWidget::onSendCommand);
    connect(inputEdit, &QLineEdit::returnPressed, this, &BotConsoleWidget::onSendCommand);
    connect(inputEdit, &QLineEdit::textChanged, this, &BotConsoleWidget::onInputChanged);

    appendOutput(QString("[%1] Console ready. Type a command or start typing for suggestions.").arg(QDateTime::currentDateTime().toString("HH:mm:ss")), AppColors::consoleReady());
}

void BotConsoleWidget::setupCompleter()
{
    completerModel = new QStringListModel(this);
    completer = new QCompleter(completerModel, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    inputEdit->setCompleter(completer);

    if (completer->popup()) {
        completer->popup()->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    }

    inputEdit->installEventFilter(this);
}

void BotConsoleWidget::initializeCommands()
{
    availableCommands = {
        {"connect", "Connect to a Minecraft server", "connect <server_address>"},
        {"disconnect", "Disconnect from current server", "disconnect [reason]"},
        {"chat", "Send a chat message", "chat <message>"},
        {"move", "Move to coordinates", "move <x> <y> <z>"},
        {"lookat", "Look at position or entity", "lookat <x> <y> <z> | lookat entity <id>"},
        {"rotate", "Set rotation", "rotate <yaw> <pitch>"},
        {"hotbar", "Switch hotbar slot", "hotbar <slot>"},
        {"use", "Use item in hand", "use [main|offhand]"},
        {"drop", "Drop item", "drop [all]"},
        {"shutdown", "Shutdown the bot", "shutdown [reason]"},
        {"meteor", "Manage Meteor client modules", "meteor list [category] | meteor toggle <module> | meteor set <module> <\"setting\"|enabled> <value>"}
    };

    QStringList commandNames;
    for (const auto &cmd : std::as_const(availableCommands)) {
        commandNames << cmd.name;
    }
    completerModel->setStringList(commandNames);
}

void BotConsoleWidget::onSendCommand()
{
    QString command = inputEdit->text().trimmed();
    if (command.isEmpty()) {
        return;
    }

    commandHistory.append(command);
    historyIndex = commandHistory.size();

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    appendOutput(QString("[%1] > %2").arg(timestamp, command), AppColors::consoleInput());

    inputEdit->clear();
    hintLabel->setVisible(false);

    emit commandEntered(command);
}

void BotConsoleWidget::onInputChanged(const QString &text)
{
    if (text.isEmpty()) {
        hintLabel->setVisible(false);
        return;
    }

    updateCommandHint(text);
}

void BotConsoleWidget::updateCommandHint(const QString &commandName)
{
    QString help = findCommandHelp(commandName);
    if (!help.isEmpty()) {
        hintLabel->setText(help);
        hintLabel->setVisible(true);
    } else {
        hintLabel->setVisible(false);
    }
}

QString BotConsoleWidget::findCommandHelp(const QString &commandName)
{
    QString lowerInput = commandName.toLower().trimmed();

    // First, try to find an exact match (case-insensitive)
    for (const auto &cmd : std::as_const(availableCommands)) {
        if (cmd.name.toLower() == lowerInput) {
            // Convert \n to <br/> for HTML rendering
            QString htmlDescription = cmd.description;
            htmlDescription.replace("\n", "<br/>");
            return QString("<b>%1</b>: %2<br/>Usage: <tt>%3</tt>")
                .arg(cmd.name, htmlDescription, cmd.syntax);
        }
    }

    // If no exact match, find the first command that starts with the input
    // Only show hint if the input is more specific than just "baritone "
    if (lowerInput == "baritone" || lowerInput == "baritone ") {
        // Don't show hint for just "baritone" - let user see the full dropdown
        return QString();
    }

    for (const auto &cmd : std::as_const(availableCommands)) {
        if (cmd.name.startsWith(lowerInput, Qt::CaseInsensitive)) {
            // Convert \n to <br/> for HTML rendering
            QString htmlDescription = cmd.description;
            htmlDescription.replace("\n", "<br/>");
            return QString("<b>%1</b>: %2<br/>Usage: <tt>%3</tt>")
                .arg(cmd.name, htmlDescription, cmd.syntax);
        }
    }

    return QString();
}

void BotConsoleWidget::appendOutput(const QString &text, const QColor &color)
{
    QTextCursor cursor = outputEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    cursor.insertText(text + "\n", format);
    if (autoScrollCheckBox->isChecked()) {
        outputEdit->setTextCursor(cursor);
        outputEdit->ensureCursorVisible();
    }
}

void BotConsoleWidget::setMaxLines(int maxLines)
{
    outputEdit->setMaximumBlockCount(maxLines);
}

void BotConsoleWidget::setRingCapacity(int capacity)
{
    QMutexLocker locker(&m_ringMutex);

    // Copy existing items into a flat list (oldest first)
    int keep = qMin(m_ringCount, capacity);
    int drop = m_ringCount - keep;
    QVector<PendingLine> preserved(keep);
    for (int i = 0; i < keep; i++)
        preserved[i] = m_ring[(m_ringHead + drop + i) % m_ringCapacity];

    m_ringCapacity = capacity;
    m_ring.resize(capacity);

    // Write preserved items back sequentially from index 0
    for (int i = 0; i < keep; i++)
        m_ring[i] = preserved[i];

    m_ringHead = 0;
    m_ringTail = keep % capacity;
    m_ringCount = keep;
    // Don't reset m_droppedCount - items lost during resize count as dropped
    m_droppedCount += drop;
}

void BotConsoleWidget::pushLogLine(const QString &text, const QColor &color)
{
    QMutexLocker locker(&m_ringMutex);
    if (m_ringCount == m_ringCapacity) {
        // Buffer full - overwrite oldest entry
        m_ringHead = (m_ringHead + 1) % m_ringCapacity;
        m_droppedCount++;
    } else {
        m_ringCount++;
    }
    m_ring[m_ringTail] = {text, color};
    m_ringTail = (m_ringTail + 1) % m_ringCapacity;

    if (m_fileSink)
        m_fileSink->write(text);
}

void BotConsoleWidget::flushPendingOutput()
{
    QVector<PendingLine> batch;
    int dropped = 0;
    {
        QMutexLocker locker(&m_ringMutex);
        if (m_ringCount == 0 && m_droppedCount == 0)
            return;
        batch.resize(m_ringCount);
        for (int i = 0; i < m_ringCount; i++)
            batch[i] = m_ring[(m_ringHead + i) % m_ringCapacity];
        dropped = m_droppedCount;
        m_ringHead = 0;
        m_ringTail = 0;
        m_ringCount = 0;
        m_droppedCount = 0;
    }

    QTextCursor cursor = outputEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();

    if (dropped > 0) {
        QTextCharFormat fmt;
        fmt.setForeground(AppColors::consoleDropped());
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
        cursor.insertText(QString("[%1] [...%2 messages dropped...]\n").arg(ts).arg(dropped), fmt);
    }
    for (const auto &line : std::as_const(batch)) {
        QTextCharFormat fmt;
        fmt.setForeground(line.color);
        cursor.insertText(line.text + "\n", fmt);
    }

    cursor.endEditBlock();

    if (autoScrollCheckBox->isChecked()) {
        outputEdit->setTextCursor(cursor);
        outputEdit->ensureCursorVisible();
    }
}

void BotConsoleWidget::appendResponse(bool success, const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QColor color = success ? AppColors::consoleSuccess()
                           : AppColors::consoleError();
    appendOutput(QString("[%1] %2").arg(timestamp, message), color);
}

void BotConsoleWidget::clearOutput()
{
    outputEdit->clear();
    appendOutput(QString("[%1] Console cleared.").arg(QDateTime::currentDateTime().toString("HH:mm:ss")), AppColors::consoleReady());
}

void BotConsoleWidget::setCommandHistory(const QStringList &history)
{
    commandHistory = history;
    historyIndex = commandHistory.size();
}

void BotConsoleWidget::addBaritoneCommands(const QVector<QPair<QString, QString>> &commands)
{
    // Remove old baritone commands
    availableCommands.erase(
        std::remove_if(availableCommands.begin(), availableCommands.end(),
                      [](const CommandInfo &cmd) {
                          return cmd.name.startsWith("baritone ");
                      }),
        availableCommands.end());

    // Add new baritone commands with "baritone " prefix
    for (const auto &cmd : std::as_const(commands)) {
        CommandInfo info;
        info.name = "baritone " + cmd.first;
        info.description = cmd.second + " (Baritone)";
        info.syntax = "baritone " + cmd.first + " [args]";
        availableCommands.append(info);
    }

    // Update completer model
    QStringList commandNames;
    for (const auto &cmd : std::as_const(availableCommands)) {
        commandNames << cmd.name;
    }
    completerModel->setStringList(commandNames);
}

bool BotConsoleWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == inputEdit) {
        if (event->type() == QEvent::MouseButtonPress) {
            if (inputEdit->text().isEmpty() && inputEdit->hasFocus()) {
                QTimer::singleShot(0, this, [this]() {
                    completer->setCompletionPrefix("");
                    completer->complete();
                });
            }
        }
        else if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Up) {
                if (!commandHistory.isEmpty() && historyIndex > 0) {
                    if (historyIndex == commandHistory.size()) {
                        currentInput = inputEdit->text();
                    }
                    historyIndex--;
                    inputEdit->setText(commandHistory[historyIndex]);
                }
                return true;
            }
            else if (keyEvent->key() == Qt::Key_Down) {
                if (!commandHistory.isEmpty() && historyIndex < commandHistory.size()) {
                    historyIndex++;
                    if (historyIndex == commandHistory.size()) {
                        inputEdit->setText(currentInput);
                    } else {
                        inputEdit->setText(commandHistory[historyIndex]);
                    }
                }
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}
