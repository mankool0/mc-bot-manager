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
#include <QCheckBox>
#include <QKeyEvent>
#include <QTimer>
#include <QMutex>
#include <QVector>

class BotConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BotConsoleWidget(QWidget *parent = nullptr);
    ~BotConsoleWidget();

    // Called from main thread only (direct insert, no buffering)
    void appendOutput(const QString &text, const QColor &color = Qt::black);
    void appendResponse(bool success, const QString &message);
    void clearOutput();

    // Called from any thread - writes into ring buffer, flushed every 50ms
    void pushLogLine(const QString &text, const QColor &color);

    // Called from main thread - resizes ring buffer, preserving most recent contents
    void setRingCapacity(int capacity);

    // Called from main thread - updates max line count (0 = unlimited), trims immediately
    void setMaxLines(int maxLines);

    QStringList getCommandHistory() const { return commandHistory; }
    void setCommandHistory(const QStringList &history);

    void addBaritoneCommands(const QVector<QPair<QString, QString>> &commands);

signals:
    void commandEntered(const QString &command);

private slots:
    void onSendCommand();
    void onInputChanged(const QString &text);
    void flushPendingOutput();

private:
    void setupUI();
    void setupCompleter();
    void updateCommandHint(const QString &command);

    QPlainTextEdit *outputEdit;
    QLineEdit *inputEdit;
    QPushButton *sendButton;
    QCheckBox *autoScrollCheckBox;
    QLabel *hintLabel;

    QCompleter *completer;
    QStringListModel *completerModel;

    QStringList commandHistory;
    int historyIndex;
    QString currentInput;

    // Ring buffer for script log output (written from any thread, drained on main thread)
    struct PendingLine { QString text; QColor color; };
    QVector<PendingLine> m_ring;
    int m_ringCapacity;
    int m_ringHead;      // index of oldest item
    int m_ringTail;      // index where next write goes
    int m_ringCount;     // items currently stored
    int m_droppedCount;  // items dropped since last flush
    QMutex m_ringMutex;

    QTimer *m_flushTimer;

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
