#ifndef BOTCONSOLEWIDGET_H
#define BOTCONSOLEWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCompleter>
#include <QStringListModel>
#include <QLabel>
#include <QKeyEvent>

class BotConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BotConsoleWidget(QWidget *parent = nullptr);
    ~BotConsoleWidget();

    void appendOutput(const QString &text, const QColor &color = Qt::black);
    void appendResponse(bool success, const QString &message);
    void clearOutput();

    QStringList getCommandHistory() const { return commandHistory; }
    void setCommandHistory(const QStringList &history);

    void addBaritoneCommands(const QVector<QPair<QString, QString>> &commands);

signals:
    void commandEntered(const QString &command);

private slots:
    void onSendCommand();
    void onInputChanged(const QString &text);

private:
    void setupUI();
    void setupCompleter();
    void updateCommandHint(const QString &command);

    QPlainTextEdit *outputEdit;
    QLineEdit *inputEdit;
    QPushButton *sendButton;
    QLabel *hintLabel;

    QCompleter *completer;
    QStringListModel *completerModel;

    QStringList commandHistory;
    int historyIndex;
    QString currentInput;

    struct CommandInfo {
        QString name;
        QString description;
        QString syntax;
    };

    QList<CommandInfo> availableCommands;

    void initializeCommands();
    QString findCommandHelp(const QString &commandName);

    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif // BOTCONSOLEWIDGET_H
