#ifndef SCRIPTFILEMANAGER_H
#define SCRIPTFILEMANAGER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QDateTime>

struct ScriptState {
    bool autorun = false;
};

class ScriptFileManager
{
public:
    static QString getBaseScriptDir();
    static QString getScriptDirectory(const QString &botName);
    static bool ensureScriptDirectoryExists(const QString &botName);

    static QStringList listScripts(const QString &botName);
    static QString loadScript(const QString &botName, const QString &filename);
    static bool saveScript(const QString &botName, const QString &filename,
                          const QString &code);
    static bool deleteScript(const QString &botName, const QString &filename);
    static bool renameScript(const QString &botName, const QString &oldName,
                            const QString &newName);

    static QMap<QString, ScriptState> loadScriptStates(const QString &botName);
    static void saveScriptStates(const QString &botName,
                                const QMap<QString, ScriptState> &states);

    static QDateTime getLastModified(const QString &botName,
                                    const QString &filename);
    static bool scriptExists(const QString &botName, const QString &filename);

private:
    static QString getScriptFilePath(const QString &botName, const QString &filename);
    static QString getMetadataPath(const QString &botName);
};

#endif // SCRIPTFILEMANAGER_H
