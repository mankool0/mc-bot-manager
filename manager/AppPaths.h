#ifndef APPPATHS_H
#define APPPATHS_H

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace AppPaths {

inline QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

inline QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

inline QString scriptsDir()
{
    return QDir(dataDir()).filePath("scripts");
}

inline QString logsDir()
{
    return QDir(dataDir()).filePath("logs");
}

inline QString worldSavesDir()
{
    return QDir(dataDir()).filePath("worldSaves");
}

inline QString pylibsDir()
{
    return QDir(dataDir()).filePath("pylibs");
}

inline QString stubsDir()
{
    return QDir(dataDir()).filePath("stubs");
}

} // namespace AppPaths

#endif // APPPATHS_H
