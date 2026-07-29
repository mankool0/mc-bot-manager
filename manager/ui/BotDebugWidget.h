#ifndef BOTDEBUGWIDGET_H
#define BOTDEBUGWIDGET_H

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

struct BotInstance;

class BotDebugWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BotDebugWidget(BotInstance *bot, QWidget *parent = nullptr);

    void populate();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    BotInstance *m_bot;
    QTreeWidget *m_tree;
    QTimer *m_timer;
    QPushButton *m_refreshButton;
    QPushButton *m_autoRefreshButton;

    // Query tab
    QComboBox *m_funcCombo;
    QStackedWidget *m_paramsStack;
    QPlainTextEdit *m_resultEdit;
    QPushButton *m_runButton;
    bool m_queryTabReady = false;
    bool m_stateModulesReady = false;
    // (module name -> its __debug_state__ function names), discovered once on the first refresh.
    QList<QPair<QString, QStringList>> m_stateSpec;
    // A gather can be slow, so these keep refreshes and queries from stacking up.
    bool m_refreshInFlight = false;
    bool m_queryInFlight = false;

    struct QueryParam {
        QString name;
        QString type;
        QString placeholder;
        QString enumClass;      // for type == "enum": the enum class name in the module
        QStringList enumMembers; // for type == "enum": selectable member names
    };
    struct QueryFuncInfo {
        QString module;
        QString fn;
        QString botKwarg;
        QList<QueryParam> params;
        QList<QWidget *> inputs; // QLineEdit, or QComboBox for enum params
    };
    QList<QueryFuncInfo> m_queryFuncs;

    void initStateTab(QWidget *tab);
    void initQueryTab(QWidget *tab);
    void initQueryFunctions();
    void runQuery();
    static QList<QueryParam> parseQuerySignature(const QString &doc, const QString &botKwarg,
                                                 const pybind11::object &mod);

    static QString stableKey(const QString &title);
    static void saveExpandState(QTreeWidgetItem *item, const QString &prefix, QMap<QString, bool> &state);
    static void restoreExpandState(QTreeWidgetItem *item, const QString &prefix,
                                   const QMap<QString, bool> &state);
};

#endif // BOTDEBUGWIDGET_H
