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
    static QStringList getBundledModules() {
        return {"crafting"};
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

    static void copyBundledModules(const QString &scriptsDir) {
        QDir dir(scriptsDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        for (const QString &moduleName : getBundledModules()) {
            ensureModuleExists(scriptsDir, moduleName);
        }
    }
};

#endif // EMBEDDEDPYTHONLIBS_H