#ifndef EMBEDDEDPYTHONLIBS_H
#define EMBEDDEDPYTHONLIBS_H

#include <QString>
#include <QStringList>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <pybind11/embed.h>

namespace py = pybind11;

class EmbeddedPythonLibs {
public:
    static QStringList getAllModuleNames() {
        return {"_events", "crafting", "enchanting"};
    }

    static bool ensureModuleExists(const QString &scriptsDir, const QString &moduleName) {
        QString modulePath = QDir(scriptsDir).filePath(moduleName + ".py");

        if (QFile::exists(modulePath)) {
            return true;
        }

        QString resourcePath = QString(":/pythonlibs/pythonlibs/%1.py").arg(moduleName);
        QFile resourceFile(resourcePath);

        if (!resourceFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open resource:" << resourcePath;
            return false;
        }

        QFile targetFile(modulePath);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to create module file:" << modulePath;
            return false;
        }

        targetFile.write(resourceFile.readAll());
        qDebug() << "Copied bundled module to:" << modulePath;
        return true;
    }

    static void copyPublicModules(const QString &scriptsDir) {
        QDir dir(scriptsDir);
        if (!dir.exists())
            dir.mkpath(".");
        for (const QString &m : QStringList{"crafting", "enchanting"})
            ensureModuleExists(scriptsDir, m);
    }

    static void copyInternalModules(const QString &internalDir) {
        QDir dir(internalDir);
        if (!dir.exists())
            dir.mkpath(".");
        ensureModuleExists(internalDir, "_events");
    }

    static void ensureJediAvailable(const QString &internalDir) {
        QString jediLibsDir = QDir(internalDir).filePath("jedi_libs");
        QDir dir(jediLibsDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        // Only extract if jedi package is missing (first run or after clean)
        if (!QFile::exists(QDir(jediLibsDir).filePath("jedi/__init__.py"))) {
            py::module_ zipfile = py::module_::import("zipfile");
            py::module_ io = py::module_::import("io");

            for (const char* whl : {"parso", "jedi"}) {
                QFile res(QString(":/pythonlibs/%1.whl").arg(whl));
                if (!res.open(QIODevice::ReadOnly)) {
                    qWarning() << "Failed to open wheel resource:" << whl;
                    continue;
                }
                QByteArray data = res.readAll();
                try {
                    py::bytes whlBytes(data.constData(), data.size());
                    py::object bytesIO = io.attr("BytesIO")(whlBytes);
                    py::object zf = zipfile.attr("ZipFile")(bytesIO);
                    zf.attr("extractall")(jediLibsDir.toStdString());
                    zf.attr("close")();
                    qDebug() << "Extracted" << whl << "to" << jediLibsDir;
                } catch (py::error_already_set &e) {
                    qWarning() << "Failed to extract wheel" << whl << ":" << e.what();
                }
            }
        }

        std::string jediLibsStd = jediLibsDir.toStdString();
        py::module_ sys = py::module_::import("sys");
        py::list path = sys.attr("path");
        bool found = false;
        for (auto p : path) {
            if (p.cast<std::string>() == jediLibsStd) { found = true; break; }
        }
        if (!found) {
            path.append(jediLibsStd);
        }
    }
};

#endif // EMBEDDEDPYTHONLIBS_H
