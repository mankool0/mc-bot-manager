#ifndef CLIENTMODINSTALLER_H
#define CLIENTMODINSTALLER_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ClientModInstaller {

enum class Result {
    UpToDate,           // the right jar is already there
    Installed,          // no client mod was present, one was added
    Replaced,           // a different client mod was present and was swapped out
    NoInstanceDir,      // the instance is gone from disk
    NotFabric,          // no Fabric loader component, so the mod cannot load
    UnsupportedVersion, // no bundled jar for the instance's Minecraft version
    NoBundledJars,      // nothing was bundled with this manager build
    Busy,               // the instance is running, its jar may be locked
    Failed,
};

struct Outcome {
    Result result = Result::Failed;
    QString instance;
    QString mcVersion;
    // Human readable, already scoped to the instance. Empty for UpToDate.
    QString detail;
};

// UpToDate, Installed and Replaced. Everything else left the instance as it was.
bool isOk(Result result);

QString bundledJarDir();

// Minecraft version -> jar path
QHash<QString, QString> bundledJars();

Outcome syncInstance(const QString &prismPath, const QString &instanceId);

// Every instance a configured bot uses, deduplicated.
QVector<Outcome> syncUsedInstances(const QString &prismPath);

} // namespace ClientModInstaller

#endif // CLIENTMODINSTALLER_H
