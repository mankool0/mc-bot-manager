#include "ScriptFileManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>

QString ScriptFileManager::getBaseScriptDir()
{
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).filePath("scripts");
}

QString ScriptFileManager::getScriptDirectory(const QString &botName)
{
    return QDir(getBaseScriptDir()).filePath(botName);
}

bool ScriptFileManager::ensureScriptDirectoryExists(const QString &botName)
{
    QDir dir(getScriptDirectory(botName));
    if (!dir.exists()) {
        return dir.mkpath(".");
    }
    return true;
}

QString ScriptFileManager::getScriptFilePath(const QString &botName, const QString &filename)
{
    return QDir(getScriptDirectory(botName)).filePath(filename);
}

QStringList ScriptFileManager::listScripts(const QString &botName)
{
    QDir dir(getScriptDirectory(botName));
    QStringList filters;
    filters << "*.py";
    return dir.entryList(filters, QDir::Files, QDir::Name);
}

QString ScriptFileManager::loadScript(const QString &botName,
                                      const QString &filename)
{
    QString path = getScriptFilePath(botName, filename);
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream in(&file);
    return in.readAll();
}

bool ScriptFileManager::saveScript(const QString &botName,
                                   const QString &filename,
                                   const QString &code)
{
    ensureScriptDirectoryExists(botName);
    QString path = getScriptFilePath(botName, filename);
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << code;
    return true;
}

bool ScriptFileManager::deleteScript(const QString &botName, const QString &filename)
{
    return QFile::remove(getScriptFilePath(botName, filename));
}

bool ScriptFileManager::renameScript(const QString &botName, const QString &oldName,
                                     const QString &newName)
{
    return QFile::rename(getScriptFilePath(botName, oldName), getScriptFilePath(botName, newName));
}

QString ScriptFileManager::getMetadataPath(const QString &botName)
{
    return QDir(getScriptDirectory(botName)).filePath(".metadata.json");
}

QMap<QString, ScriptState> ScriptFileManager::loadScriptStates(const QString &botName)
{
    QMap<QString, ScriptState> states;
    QString path = getMetadataPath(botName);
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        return states;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    QJsonArray scripts = root["scripts"].toArray();

    for (const QJsonValue &val : scripts) {
        QJsonObject obj = val.toObject();
        QString filename = obj["filename"].toString();
        ScriptState state;
        state.autorun = obj["autorun"].toBool();
        states[filename] = state;
    }

    return states;
}

void ScriptFileManager::saveScriptStates(const QString &botName,
                                        const QMap<QString, ScriptState> &states)
{
    ensureScriptDirectoryExists(botName);
    QString path = getMetadataPath(botName);
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonArray scripts;
    for (auto it = states.begin(); it != states.end(); ++it) {
        QJsonObject obj;
        obj["filename"] = it.key();
        obj["autorun"] = it.value().autorun;
        scripts.append(obj);
    }

    QJsonObject root;
    root["scripts"] = scripts;

    QJsonDocument doc(root);
    file.write(doc.toJson());
}

QDateTime ScriptFileManager::getLastModified(const QString &botName,
                                             const QString &filename)
{
    QFileInfo info(getScriptFilePath(botName, filename));
    return info.lastModified();
}

bool ScriptFileManager::scriptExists(const QString &botName, const QString &filename)
{
    return QFile::exists(getScriptFilePath(botName, filename));
}
