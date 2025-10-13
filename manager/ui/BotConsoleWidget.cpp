#include "BotConsoleWidget.h"
#include <QDateTime>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QScrollBar>
#include <QTimer>
#include <QAbstractItemView>

BotConsoleWidget::BotConsoleWidget(QWidget *parent)
    : QWidget(parent)
    , historyIndex(-1)
{
    setupUI();
    setupCompleter();
    initializeCommands();
}

BotConsoleWidget::~BotConsoleWidget()
{
}

void BotConsoleWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    outputEdit = new QPlainTextEdit(this);
    outputEdit->setReadOnly(true);
    outputEdit->setFont(QFont("Consolas", 9));
    outputEdit->setMaximumBlockCount(1000);
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

    mainLayout->addLayout(inputLayout);

    connect(sendButton, &QPushButton::clicked, this, &BotConsoleWidget::onSendCommand);
    connect(inputEdit, &QLineEdit::returnPressed, this, &BotConsoleWidget::onSendCommand);
    connect(inputEdit, &QLineEdit::textChanged, this, &BotConsoleWidget::onInputChanged);

    appendOutput("Console ready. Type a command or start typing for suggestions.", QColor("#0066cc"));
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
    for (const auto &cmd : availableCommands) {
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
    appendOutput(QString("[%1] > %2").arg(timestamp, command), QColor("#006600"));

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
    for (const auto &cmd : availableCommands) {
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

    for (const auto &cmd : availableCommands) {
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

    outputEdit->setTextCursor(cursor);
    outputEdit->ensureCursorVisible();
}

void BotConsoleWidget::appendResponse(bool success, const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QColor color = success ? QColor("#006600") : QColor("#cc0000");
    QString prefix = success ? "✓" : "✗";

    appendOutput(QString("[%1] %2 %3").arg(timestamp, prefix, message), color);
}

void BotConsoleWidget::clearOutput()
{
    outputEdit->clear();
    appendOutput("Console cleared.", QColor("#0066cc"));
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
    for (const auto &cmd : commands) {
        CommandInfo info;
        info.name = "baritone " + cmd.first;
        info.description = cmd.second + " (Baritone)";
        info.syntax = "baritone " + cmd.first + " [args]";
        availableCommands.append(info);
    }

    // Update completer model
    QStringList commandNames;
    for (const auto &cmd : availableCommands) {
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
