#ifndef EMBEDDEDPYTHONLIBS_H
#define EMBEDDEDPYTHONLIBS_H

#include <QString>
#include <QStringList>
#include <QFile>
#include <QDir>
#include <QDebug>

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

    static QStringList getStubModuleNames() {
        return {"bot", "baritone", "meteor", "world", "utils", "server"};
    }

    static void copyStubs(const QString &stubsDir) {
        QDir dir(stubsDir);
        if (!dir.exists())
            dir.mkpath(".");
        for (const QString &name : getStubModuleNames()) {
            QString dest = QDir(stubsDir).filePath(name + ".pyi");
            QFile src(QString(":/stubs/%1.pyi").arg(name));
            if (!src.open(QIODevice::ReadOnly)) {
                qWarning() << "Failed to open stub resource:" << name;
                continue;
            }
            QByteArray srcData = src.readAll();
            QFile existing(dest);
            if (existing.open(QIODevice::ReadOnly) && existing.readAll() == srcData) {
                continue;
            }
            existing.close();
            QFile out(dest);
            if (out.open(QIODevice::WriteOnly))
                out.write(srcData);
        }
    }
};

#endif // EMBEDDEDPYTHONLIBS_H
