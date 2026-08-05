#include "BotDebugWidget.h"
#include "bot/BotManager.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

#include <QDebug>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHideEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QtConcurrent>

namespace {

class CurrentPageStack : public QStackedWidget
{
public:
    explicit CurrentPageStack(QWidget *parent = nullptr) : QStackedWidget(parent)
    {
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }

    QSize sizeHint() const override
    {
        QWidget *w = currentWidget();
        return w ? w->sizeHint() : QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        QWidget *w = currentWidget();
        return w ? w->minimumSizeHint() : QStackedWidget::minimumSizeHint();
    }
};

// Split a parameter list on top-level commas, ignoring commas nested inside
// brackets (e.g. List[str, int] or an enum default like <Foo.BAR: 0>).
QStringList splitTopLevel(const QString &s)
{
    QStringList out;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (c == '[' || c == '(' || c == '{' || c == '<') depth++;
        else if (c == ']' || c == ')' || c == '}' || c == '>') depth--;
        else if (c == ',' && depth == 0) {
            out.append(s.mid(start, i - start).trimmed());
            start = i + 1;
        }
    }
    QString last = s.mid(start).trimmed();
    if (!last.isEmpty())
        out.append(last);
    return out;
}

// Index of the first top-level occurrence of ch (not nested in brackets), or -1.
int indexOfTopLevel(const QString &s, QChar ch)
{
    int depth = 0;
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (c == '[' || c == '(' || c == '{' || c == '<') depth++;
        else if (c == ']' || c == ')' || c == '}' || c == '>') depth--;
        else if (depth == 0 && c == ch) return i;
    }
    return -1;
}

// Returns all built-in embedded modules that expose the given attribute, sorted by name.
// Imports each entry in sys.builtin_module_names so pybind11 embedded modules
// (registered via PyImport_AppendInittab) are found even if not yet imported.
QList<py::module_> modulesWithAttr(const char *attr)
{
    QList<py::module_> result;
    py::object sys = py::module_::import("sys");
    for (auto nameObj : sys.attr("builtin_module_names").cast<py::tuple>()) {
        try {
            py::module_ mod = py::module_::import(nameObj.cast<std::string>().c_str());
            if (py::hasattr(mod, attr))
                result.append(mod);
        } catch (...) {}
    }
    std::sort(result.begin(), result.end(), [](const py::module_ &a, const py::module_ &b) {
        return std::string(py::str(a.attr("__name__"))) < std::string(py::str(b.attr("__name__")));
    });
    return result;
}

// A py-free tree row, so state gathered on a worker thread can be rendered on the UI thread
// without any py::object crossing between them.
struct DebugNode {
    QString col0;
    QString col1;
    bool bold = false;
    bool spanned = false;
    QVector<DebugNode> children;
};

// Must be called with the GIL held.
DebugNode pyToNode(const QString &key, const py::object &value, const py::object &builtins)
{
    DebugNode node;
    node.col0 = key;

    if (value.is_none()) { node.col1 = "None"; return node; }

    // bool must be checked before int (bool is a subclass of int in Python)
    if (py::isinstance<py::bool_>(value)) { node.col1 = value.cast<bool>() ? "true" : "false"; return node; }
    if (py::isinstance<py::int_>(value)) { node.col1 = QString::number(value.cast<long long>()); return node; }
    if (py::isinstance<py::float_>(value)) { node.col1 = QString::number(value.cast<double>(), 'f', 4); return node; }
    if (py::isinstance<py::str>(value)) { node.col1 = QString::fromStdString(value.cast<std::string>()); return node; }

    if (py::isinstance<py::dict>(value)) {
        py::dict d = value.cast<py::dict>();
        if (d.empty()) { node.col1 = "{}"; return node; }
        for (auto [k, v] : d)
            node.children.append(pyToNode(QString::fromStdString(py::str(k).cast<std::string>()),
                                          v.cast<py::object>(), builtins));
        return node;
    }

    if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value)) {
        auto size = static_cast<Py_ssize_t>(py::len(value));

        // Small all-primitive sequences: show as a single repr row
        if (size <= 4) {
            bool allPrim = true;
            for (Py_ssize_t i = 0; i < size && allPrim; ++i) {
                py::object item = value.attr("__getitem__")(py::int_(i));
                allPrim = py::isinstance<py::bool_>(item) || py::isinstance<py::int_>(item)
                       || py::isinstance<py::float_>(item) || py::isinstance<py::str>(item)
                       || item.is_none();
            }
            if (allPrim) { node.col1 = QString::fromStdString(py::repr(value).cast<std::string>()); return node; }
        }

        node.col0 = QString("%1 (%2)").arg(key).arg(size);
        constexpr Py_ssize_t MAX_ITEMS = 100;
        Py_ssize_t show = std::min(size, MAX_ITEMS);
        for (Py_ssize_t i = 0; i < show; ++i)
            node.children.append(pyToNode(QString("[%1]").arg(i),
                                          value.attr("__getitem__")(py::int_(i)).cast<py::object>(), builtins));
        if (size > MAX_ITEMS) {
            DebugNode more;
            more.col0 = QString("... (%1 more)").arg(size - MAX_ITEMS);
            more.spanned = true;
            node.children.append(more);
        }
        return node;
    }

    // pybind11 enums expose every other enumerator as an attribute of each value, so the generic
    // dir() expansion below would recurse forever. Render them as a single leaf instead.
    if (py::hasattr(py::type::of(value), "__members__")) {
        node.col1 = QString::fromStdString(py::repr(value).cast<std::string>());
        return node;
    }

    try {
        py::list attrs = builtins.attr("dir")(value);
        QList<std::pair<QString, py::object>> pubAttrs;
        for (auto a : attrs) {
            std::string name = a.cast<std::string>();
            if (name.empty() || name[0] == '_') continue;
            try {
                py::object attr = value.attr(name.c_str());
                if (!builtins.attr("callable")(attr).cast<bool>())
                    pubAttrs.append({QString::fromStdString(name), attr});
            } catch (...) {}
        }
        if (!pubAttrs.isEmpty()) {
            for (auto &[k, v] : pubAttrs)
                node.children.append(pyToNode(k, v, builtins));
            return node;
        }
    } catch (...) {}

    node.col1 = QString::fromStdString(py::repr(value).cast<std::string>());
    return node;
}

// What one worker-thread refresh hands back to the UI thread.
struct GatherResult {
    QList<QPair<QString, QStringList>> spec;
    bool specReady = false;
    QVector<DebugNode> sections;
    QString error;
};

// UI thread only.
void makeItem(QTreeWidget *tree, QTreeWidgetItem *parent, const DebugNode &n)
{
    QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, n.col0);
    if (!n.col1.isEmpty())
        item->setText(1, n.col1);
    if (n.bold) {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
    }
    if (n.spanned)
        item->setFirstColumnSpanned(true);
    for (const DebugNode &c : n.children)
        makeItem(tree, item, c);
}

} // namespace

BotDebugWidget::BotDebugWidget(BotInstance *bot, QWidget *parent)
    : QWidget(parent)
    , m_bot(bot)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *tabs = new QTabWidget(this);
    mainLayout->addWidget(tabs);

    auto *stateWidget = new QWidget();
    initStateTab(stateWidget);
    tabs->addTab(stateWidget, "State");

    auto *queryWidget = new QWidget();
    initQueryTab(queryWidget);
    tabs->addTab(queryWidget, "Query");

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &BotDebugWidget::populate);
    connect(m_refreshButton, &QPushButton::clicked, this, &BotDebugWidget::populate);
    connect(m_autoRefreshButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked && isVisible())
            m_timer->start();
        else
            m_timer->stop();
    });
}

void BotDebugWidget::initStateTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(4, 4, 4, 0);
    m_refreshButton = new QPushButton("Refresh", tab);
    m_autoRefreshButton = new QPushButton("Auto-refresh", tab);
    m_autoRefreshButton->setCheckable(true);
    m_autoRefreshButton->setChecked(true);
    toolbarLayout->addWidget(m_refreshButton);
    toolbarLayout->addWidget(m_autoRefreshButton);
    toolbarLayout->addStretch();
    layout->addLayout(toolbarLayout);

    m_tree = new QTreeWidget(tab);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"Field", "Value"});
    m_tree->setRootIsDecorated(true);
    m_tree->header()->setStretchLastSection(true);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this] {
        m_tree->resizeColumnToContents(0);
    });
    layout->addWidget(m_tree);
}

void BotDebugWidget::initQueryTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *funcRow = new QHBoxLayout();
    funcRow->addWidget(new QLabel("Function:", tab));
    m_funcCombo = new QComboBox(tab);
    funcRow->addWidget(m_funcCombo, 1);
    m_runButton = new QPushButton("Run", tab);
    auto *clearBtn = new QPushButton("Clear", tab);
    funcRow->addWidget(m_runButton);
    funcRow->addWidget(clearBtn);
    layout->addLayout(funcRow);

    m_paramsStack = new CurrentPageStack(tab);
    m_paramsStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    layout->addWidget(m_paramsStack);

    connect(m_funcCombo, &QComboBox::currentIndexChanged,
            m_paramsStack, &QStackedWidget::setCurrentIndex);

    layout->addWidget(new QLabel("Result:", tab));
    m_resultEdit = new QPlainTextEdit(tab);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_resultEdit, 1);

    connect(m_runButton, &QPushButton::clicked, this, &BotDebugWidget::runQuery);
    connect(clearBtn, &QPushButton::clicked, this, [this] { m_resultEdit->clear(); });
    connect(m_funcCombo, &QComboBox::currentIndexChanged,
            this, [this] { m_resultEdit->clear(); });
}

QList<BotDebugWidget::QueryParam>
BotDebugWidget::parseQuerySignature(const QString &doc, const QString &botKwarg,
                                    const py::object &mod)
{
    QList<QueryParam> params;

    // pybind11 renders the signature as the first line of __doc__, e.g.
    // "get_block(x: int, y: int, use_disk: bool = False, bot: str = '') -> Optional[str]"
    QString firstLine = doc.section('\n', 0, 0);
    int open = firstLine.indexOf('(');
    if (open < 0)
        return params;

    // Extract the balanced parenthesized argument list.
    QString argList;
    int depth = 0;
    for (int i = open; i < firstLine.size(); ++i) {
        QChar c = firstLine[i];
        if (c == '(') depth++;
        else if (c == ')' && --depth == 0) {
            argList = firstLine.mid(open + 1, i - open - 1);
            break;
        }
    }

    for (const QString &raw : splitTopLevel(argList)) {
        QString p = raw.trimmed();
        if (p.isEmpty() || p == "/" || p.startsWith('*'))
            continue; // skip positional-only marker and *args/**kwargs

        QString left = p;
        QString defaultText;
        int eq = indexOfTopLevel(p, '=');
        bool hasDefault = eq >= 0;
        if (hasDefault) {
            left = p.left(eq).trimmed();
            defaultText = p.mid(eq + 1).trimmed();
        }

        QString name = left;
        QString annotation;
        int colon = indexOfTopLevel(left, ':');
        if (colon >= 0) {
            name = left.left(colon).trimmed();
            annotation = left.mid(colon + 1).trimmed();
        }

        if (name.isEmpty() || name == botKwarg)
            continue;

        // pybind11 renders C++ int/float params as typing.SupportsInt /
        // typing.SupportsFloat in the docstring signature, so match those too.
        QString type = "str";
        QString enumClass;
        QStringList enumMembers;
        if (annotation == "int" || annotation.endsWith("SupportsInt")) type = "int";
        else if (annotation == "float" || annotation.endsWith("SupportsFloat")) type = "float";
        else if (annotation == "bool") type = "bool";
        else if (annotation.startsWith("List") || annotation.startsWith("list")
                 || annotation.startsWith("Sequence") || annotation.contains("Sequence[")) type = "strlist";
        else if (!annotation.isEmpty()) {
            // A pybind11 enum param renders its type as module.Qualname (e.g.
            // "world.BlockFace"); the enum class is an attribute of the owning
            // module exposing __members__. Use the last dotted segment.
            QString simple = annotation.section('.', -1);
            std::string cls = simple.toStdString();
            if (py::hasattr(mod, cls.c_str())) {
                py::object obj = mod.attr(cls.c_str());
                if (py::hasattr(obj, "__members__")) {
                    type = "enum";
                    enumClass = simple;
                    for (auto m : obj.attr("__members__").cast<py::dict>())
                        enumMembers.append(QString::fromStdString(py::str(m.first).cast<std::string>()));
                }
            }
        }
        if (hasDefault) type += "?";

        // Use the default value as a placeholder, except for uninformative ones.
        QString placeholder;
        if (hasDefault && !defaultText.isEmpty()
            && defaultText != "''" && defaultText != "\"\""
            && defaultText != "False" && defaultText != "True" && defaultText != "None")
            placeholder = defaultText;

        params.append({name, type, placeholder, enumClass, enumMembers});
    }

    return params;
}

void BotDebugWidget::initQueryFunctions()
{
    py::gil_scoped_acquire gil;

    try {
        QString botKwargQ = QStringLiteral("bot_name");

        for (const py::module_ &mod : modulesWithAttr("__debug_query__")) {
            QString modName = QString::fromStdString(py::str(mod.attr("__name__")).cast<std::string>());

            for (auto fnNameObj : mod.attr("__debug_query__").cast<py::list>()) {
                std::string fnName = fnNameObj.cast<std::string>();
                py::object fn = mod.attr(fnName.c_str());

                QueryFuncInfo info;
                info.module = modName;
                info.fn = QString::fromStdString(fnName);
                info.botKwarg = botKwargQ;

                // pybind11 functions are not introspectable via inspect.signature();
                // parse the signature out of the docstring instead.
                QString doc;
                if (py::hasattr(fn, "__doc__")) {
                    py::object d = fn.attr("__doc__");
                    if (!d.is_none())
                        doc = QString::fromStdString(d.cast<std::string>());
                }

                QStringList paramLabels;
                for (const QueryParam &p : parseQuerySignature(doc, botKwargQ, mod)) {
                    info.params.append(p);
                    paramLabels.append(p.name);
                }

                QString label = QString("%1.%2(%3)")
                    .arg(modName, info.fn, paramLabels.join(", "));
                m_funcCombo->addItem(label);

                auto *page = new QWidget();
                auto *form = new QFormLayout(page);
                form->setContentsMargins(0, 0, 0, 0);
                for (const QueryParam &p : info.params) {
                    QString rowLabel = p.type.endsWith('?') ? p.name + " (opt):" : p.name + ":";
                    if (p.type.startsWith("enum")) {
                        auto *combo = new QComboBox(page);
                        combo->addItems(p.enumMembers);
                        // Select the member named in the default (placeholder holds
                        // the raw default repr, e.g. "<Direction.UP: 1>").
                        for (const QString &member : p.enumMembers) {
                            if (p.placeholder.contains(member)) {
                                combo->setCurrentText(member);
                                break;
                            }
                        }
                        form->addRow(rowLabel, combo);
                        info.inputs.append(combo);
                    } else {
                        auto *edit = new QLineEdit(page);
                        if (!p.placeholder.isEmpty())
                            edit->setPlaceholderText(p.placeholder);
                        form->addRow(rowLabel, edit);
                        connect(edit, &QLineEdit::returnPressed, this, &BotDebugWidget::runQuery);
                        info.inputs.append(edit);
                    }
                }
                m_paramsStack->addWidget(page);
                m_queryFuncs.append(info);
            }
        }
    } catch (const py::error_already_set &e) {
        qWarning() << "BotDebugWidget: failed to build query functions:" << e.what();
    } catch (const std::exception &e) {
        qWarning() << "BotDebugWidget: failed to build query functions:" << e.what();
    }
}

void BotDebugWidget::runQuery()
{
    if (m_queryInFlight)
        return;

    int idx = m_funcCombo->currentIndex();
    if (idx < 0 || idx >= m_queryFuncs.size())
        return;

    const QueryFuncInfo &info = m_queryFuncs[idx];

    // Snapshot the widget text here; everything Python happens on the worker, since a query
    // function may block on a client round-trip.
    QStringList rawValues;
    rawValues.reserve(info.params.size());
    for (int i = 0; i < info.params.size(); ++i) {
        if (auto *combo = qobject_cast<QComboBox *>(info.inputs[i]))
            rawValues.append(combo->currentText());
        else if (auto *edit = qobject_cast<QLineEdit *>(info.inputs[i]))
            rawValues.append(edit->text().trimmed());
        else
            rawValues.append(QString());
    }

    const QString module = info.module;
    const QString fnName = info.fn;
    const QString botKwarg = info.botKwarg;
    const QList<QueryParam> params = info.params;
    const std::string botName = m_bot ? m_bot->name.toStdString() : "";

    m_queryInFlight = true;
    m_runButton->setEnabled(false);
    m_resultEdit->setPlainText("(running...)");

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        m_queryInFlight = false;
        m_runButton->setEnabled(true);
        m_resultEdit->setPlainText(watcher->result());
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(
            [module, fnName, botKwarg, params, rawValues, botName]() -> QString {
        // Acquire the GIL outside the try so it is still held in the catch handlers.
        py::gil_scoped_acquire gil;
        try {
            py::dict kwargs;
            py::object mod = py::module_::import(module.toStdString().c_str());

            for (int i = 0; i < params.size(); ++i) {
                const QueryParam &p = params[i];
                QString type = p.type;
                bool optional = type.endsWith('?');
                if (optional) type.chop(1);

                const QString &text = rawValues[i];

                if (type == "enum") {
                    // Combobox always has a selection; pass the enum member object.
                    kwargs[p.name.toStdString().c_str()] = mod.attr(p.enumClass.toStdString().c_str())
                                                              .attr(text.toStdString().c_str());
                    continue;
                }

                if (text.isEmpty()) {
                    if (!optional)
                        return QString("(need value for %1)").arg(p.name);
                    continue;
                }

                std::string key = p.name.toStdString();
                if (type == "int") {
                    bool ok; int v = text.toInt(&ok);
                    if (!ok) return QString("(need integer for %1)").arg(p.name);
                    kwargs[key.c_str()] = py::int_(v);
                } else if (type == "float") {
                    bool ok; double v = text.toDouble(&ok);
                    if (!ok) return QString("(need number for %1)").arg(p.name);
                    kwargs[key.c_str()] = py::float_(v);
                } else if (type == "bool") {
                    kwargs[key.c_str()] = py::bool_(text == "true" || text == "1");
                } else if (type == "strlist") {
                    // Comma splitting can only produce a list of strings, which is useless for
                    // params that want tuples or objects. Accept a Python literal when the text
                    // looks like one, so e.g. "[(1,2,3),(4,5,6)]" works.
                    const QString trimmed = text.trimmed();
                    if (trimmed.startsWith('[') || trimmed.startsWith('(')) {
                        kwargs[key.c_str()] = py::module_::import("ast")
                                                  .attr("literal_eval")(trimmed.toStdString());
                    } else {
                        py::list lst;
                        for (const QString &t : text.split(','))
                            if (!t.trimmed().isEmpty())
                                lst.append(py::str(t.trimmed().toStdString()));
                        kwargs[key.c_str()] = lst;
                    }
                } else {
                    kwargs[key.c_str()] = py::str(text.toStdString());
                }
            }

            kwargs[botKwarg.toStdString().c_str()] = py::str(botName);
            py::object fn = mod.attr(fnName.toStdString().c_str());
            py::object ret = fn(**kwargs);
            return QString::fromStdString(py::repr(ret).cast<std::string>());
        } catch (const py::error_already_set &e) {
            return QString("Python error: %1").arg(e.what());
        } catch (const std::exception &e) {
            return QString("Error: %1").arg(e.what());
        } catch (...) {
            return QStringLiteral("Error: unknown failure");
        }
    }));
}

void BotDebugWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_queryTabReady) {
        initQueryFunctions();
        m_queryTabReady = true;
    }
    if (m_autoRefreshButton->isChecked())
        m_timer->start();
    populate();
}

void BotDebugWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

void BotDebugWidget::populate()
{
    // A previous refresh may still be gathering; skip this tick rather than stacking up.
    if (m_refreshInFlight)
        return;

    QMap<QString, bool> expandState;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        saveExpandState(m_tree->topLevelItem(i), "", expandState);

    QList<QList<int>> selectedPaths;
    for (QTreeWidgetItem *sel : m_tree->selectedItems()) {
        QList<int> path;
        QTreeWidgetItem *cur = sel;
        while (cur) {
            QTreeWidgetItem *par = cur->parent();
            path.prepend(par ? par->indexOfChild(cur) : m_tree->indexOfTopLevelItem(cur));
            cur = par;
        }
        selectedPaths.append(path);
    }

    // Preserved across the rebuild so the view doesn't jump while the user is reading.
    int scrollV = m_tree->verticalScrollBar()->value();
    int scrollH = m_tree->horizontalScrollBar()->value();

    const QString botName = m_bot ? m_bot->name : QString();
    const auto spec = m_stateSpec;
    const bool specReady = m_stateModulesReady;

    // Everything that touches Python runs on a worker thread, including the one-time module scan:
    // a state function can block on a client round-trip, and even the cheap ones queue for the GIL
    // behind any running script.
    m_refreshInFlight = true;
    auto *watcher = new QFutureWatcher<GatherResult>(this);
    connect(watcher, &QFutureWatcher<GatherResult>::finished, this,
            [this, watcher, expandState, selectedPaths, scrollV, scrollH]() {
        m_refreshInFlight = false;
        const GatherResult gathered = watcher->result();
        watcher->deleteLater();

        if (!m_stateModulesReady && gathered.specReady) {
            m_stateSpec = gathered.spec;
            m_stateModulesReady = true;
        }

        m_tree->clear();

        if (!gathered.error.isEmpty()) {
            auto *errItem = new QTreeWidgetItem(m_tree);
            errItem->setText(0, gathered.error);
            return;
        }

        for (const DebugNode &section : gathered.sections)
            makeItem(m_tree, nullptr, section);

        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            restoreExpandState(m_tree->topLevelItem(i), "", expandState);

        for (const QList<int> &path : selectedPaths) {
            QTreeWidgetItem *item = m_tree->topLevelItem(path.value(0, -1));
            for (int i = 1; i < path.size() && item; ++i)
                item = item->child(path[i]);
            if (item)
                item->setSelected(true);
        }

        m_tree->resizeColumnToContents(0);
        m_tree->verticalScrollBar()->setValue(scrollV);
        m_tree->horizontalScrollBar()->setValue(scrollH);
    });

    watcher->setFuture(QtConcurrent::run([botName, spec, specReady]() -> GatherResult {
        // Every exception stays inside the lambda; nothing may propagate through the QFuture to
        // result() on the UI thread.
        GatherResult out;
        out.spec = spec;
        out.specReady = specReady;
        const std::string bn = botName.toStdString();

        // Discover which modules expose __debug_state__ (first refresh only).
        if (!out.specReady) {
            try {
                py::gil_scoped_acquire gil;
                for (const py::module_ &mod : modulesWithAttr("__debug_state__")) {
                    QString modName = QString::fromStdString(py::str(mod.attr("__name__")).cast<std::string>());
                    QStringList fns;
                    for (auto f : mod.attr("__debug_state__").cast<py::list>())
                        fns.append(QString::fromStdString(f.cast<std::string>()));
                    out.spec.append({modName, fns});
                }
                out.specReady = true;
            } catch (const std::exception &e) {
                out.error = QString("Error: %1").arg(e.what());
                return out;
            } catch (...) {
                out.error = QStringLiteral("Error: failed to enumerate debug state modules");
                return out;
            }
        }

        for (const auto &entry : out.spec) {
            DebugNode section;
            section.col0 = entry.first;
            section.bold = true;

            for (const QString &fnName : entry.second) {
                DebugNode child;
                // Taken per state function and dropped in between, so a long refresh never starves
                // script threads. Every py::object stays inside this scope.
                try {
                    py::gil_scoped_acquire gil;
                    py::object builtins = py::module_::import("builtins");
                    py::module_ mod = py::module_::import(entry.first.toStdString().c_str());

                    py::object result;
                    try {
                        py::object fn = mod.attr(fnName.toStdString().c_str());
                        result = fn(py::arg("bot_name") = py::str(bn));
                    } catch (const py::error_already_set &e) {
                        result = py::str(std::string("(error: ") + e.what() + ")");
                    } catch (const std::exception &e) {
                        result = py::str(std::string("(error: ") + e.what() + ")");
                    }
                    child = pyToNode(fnName, result, builtins);
                } catch (const std::exception &e) {
                    child = DebugNode{};
                    child.col0 = fnName;
                    child.col1 = QString("(error: %1)").arg(e.what());
                } catch (...) {
                    child = DebugNode{};
                    child.col0 = fnName;
                    child.col1 = "(error)";
                }
                section.children.append(child);
            }
            out.sections.append(section);
        }
        return out;
    }));
}

QString BotDebugWidget::stableKey(const QString &title)
{
    static const QRegularExpression countSuffix(R"(\s+\([^)]*\)\s*$)");
    QString key = title;
    return key.remove(countSuffix);
}

void BotDebugWidget::saveExpandState(QTreeWidgetItem *item, const QString &prefix,
                                     QMap<QString, bool> &state)
{
    if (item->childCount() == 0)
        return;
    QString key = prefix + stableKey(item->text(0));
    state[key] = item->isExpanded();
    for (int i = 0; i < item->childCount(); ++i)
        saveExpandState(item->child(i), key + "/", state);
}

void BotDebugWidget::restoreExpandState(QTreeWidgetItem *item, const QString &prefix,
                                        const QMap<QString, bool> &state)
{
    if (item->childCount() == 0)
        return;
    QString key = prefix + stableKey(item->text(0));
    auto it = state.find(key);
    if (it != state.end())
        item->setExpanded(it.value());
    for (int i = 0; i < item->childCount(); ++i)
        restoreExpandState(item->child(i), key + "/", state);
}

