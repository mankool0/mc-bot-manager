#include "ClientModInstaller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

#include "bot/BotManager.h"

namespace {

// Jars this project produces are named <base>-<mcVersion>-<modVersion>.jar.
const QString kJarPrefix = QStringLiteral("mc-bot-client-");

// Matches the installed jar whatever its version, including the copy Prism
// leaves behind when a mod is disabled from its mod list.
const QStringList kInstalledJarFilters = {
    QStringLiteral("mc-bot-client*.jar"),
    QStringLiteral("mc-bot-client*.jar.disabled"),
};

QString gameRoot(const QString &instanceRoot)
{
    // Prism's own rule (MinecraftInstance::gameRoot): the dotted directory is
    // only used by instances that already have one.
    const QString dotted = instanceRoot + "/.minecraft";
    const QString plain = instanceRoot + "/minecraft";
    if (QFileInfo::exists(dotted) && !QFileInfo::exists(plain)) {
        return dotted;
    }
    return plain;
}

struct PackInfo {
    bool valid = false;
    QString mcVersion;
    bool hasFabricLoader = false;
};

PackInfo readPack(const QString &instanceRoot)
{
    PackInfo info;
    QFile packFile(instanceRoot + "/mmc-pack.json");
    if (!packFile.open(QIODevice::ReadOnly)) {
        return info;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(packFile.readAll());
    const QJsonArray components = doc.object()["components"].toArray();
    for (const QJsonValue &value : components) {
        const QJsonObject component = value.toObject();
        const QString uid = component["uid"].toString();
        if (uid == "net.minecraft") {
            info.mcVersion = component["version"].toString();
        } else if (uid == "net.fabricmc.fabric-loader") {
            info.hasFabricLoader = true;
        }
    }
    info.valid = !info.mcVersion.isEmpty();
    return info;
}

QByteArray hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result();
}

QByteArray bundledJarHash(const QString &path)
{
    static QHash<QString, QByteArray> cache;
    auto it = cache.constFind(path);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QByteArray hash = hashFile(path);
    if (!hash.isEmpty()) {
        cache.insert(path, hash);
    }
    return hash;
}

} // namespace

namespace ClientModInstaller {

bool isOk(Result result)
{
    return result == Result::UpToDate || result == Result::Installed || result == Result::Replaced;
}

QString bundledJarDir()
{
    return QCoreApplication::applicationDirPath() + "/client-mods";
}

QHash<QString, QString> bundledJars()
{
    QHash<QString, QString> jars;
    // Only jars built from this manager's version can be installed: any other
    // would be turned away by the version handshake anyway.
    const QString suffix = QString("-%1.jar").arg(QCoreApplication::applicationVersion());

    const QDir dir(bundledJarDir());
    const QFileInfoList entries = dir.entryInfoList({kJarPrefix + "*" + suffix}, QDir::Files);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        const QString mcVersion =
            name.mid(kJarPrefix.size(), name.size() - kJarPrefix.size() - suffix.size());
        if (mcVersion.isEmpty()) continue;
        jars.insert(mcVersion, entry.absoluteFilePath());
    }
    return jars;
}

Outcome syncInstance(const QString &prismPath, const QString &instanceId)
{
    Outcome outcome;
    outcome.instance = instanceId;

    const QString instanceRoot = prismPath + "/instances/" + instanceId;
    if (!QFileInfo::exists(instanceRoot)) {
        outcome.result = Result::NoInstanceDir;
        outcome.detail = QString("instance '%1' is not in %2/instances").arg(instanceId, prismPath);
        return outcome;
    }

    const PackInfo pack = readPack(instanceRoot);
    if (!pack.valid) {
        outcome.result = Result::NotFabric;
        outcome.detail = QString("instance '%1' has no readable mmc-pack.json").arg(instanceId);
        return outcome;
    }
    outcome.mcVersion = pack.mcVersion;

    if (!pack.hasFabricLoader) {
        outcome.result = Result::NotFabric;
        outcome.detail = QString("instance '%1' is not a Fabric instance").arg(instanceId);
        return outcome;
    }

    const QHash<QString, QString> jars = bundledJars();
    if (jars.isEmpty()) {
        outcome.result = Result::NoBundledJars;
        outcome.detail = QString("no client mod jars for manager %1 in %2")
                             .arg(QCoreApplication::applicationVersion(), bundledJarDir());
        return outcome;
    }

    const QString sourceJar = jars.value(pack.mcVersion);
    if (sourceJar.isEmpty()) {
        QStringList supported = jars.keys();
        supported.sort();
        outcome.result = Result::UnsupportedVersion;
        outcome.detail = QString("instance '%1' runs Minecraft %2, which this manager has no client mod for (has: %3)")
                             .arg(instanceId, pack.mcVersion, supported.join(", "));
        return outcome;
    }

    const QString modsDir = gameRoot(instanceRoot) + "/mods";
    if (!QDir().mkpath(modsDir)) {
        outcome.result = Result::Failed;
        outcome.detail = QString("could not create %1").arg(modsDir);
        return outcome;
    }

    const QString targetPath = modsDir + "/" + QFileInfo(sourceJar).fileName();
    const QByteArray wantedHash = bundledJarHash(sourceJar);
    if (wantedHash.isEmpty()) {
        outcome.result = Result::Failed;
        outcome.detail = QString("could not read %1").arg(sourceJar);
        return outcome;
    }

    // QDir::System catches symlinks whose target is gone: those are still ours
    // to clear out. Instances that share one jar through symlinks are common,
    // so links are unlinked and replaced with a real file rather than written
    // through, which would rewrite the jar of every instance sharing it.
    const QFileInfoList installed = QDir(modsDir).entryInfoList(
        kInstalledJarFilters, QDir::Files | QDir::System | QDir::NoDotAndDotDot);

    QStringList stale;
    bool wantedIsInstalled = false;
    for (const QFileInfo &entry : installed) {
        const bool isTarget = entry.absoluteFilePath() == targetPath;
        if (isTarget && !entry.isSymLink() && hashFile(entry.absoluteFilePath()) == wantedHash) {
            wantedIsInstalled = true;
            continue;
        }
        stale.append(entry.absoluteFilePath());
    }

    if (wantedIsInstalled && stale.isEmpty()) {
        outcome.result = Result::UpToDate;
        return outcome;
    }

    QStringList removedNames;
    for (const QString &path : std::as_const(stale)) {
        if (!QFile::remove(path)) {
            outcome.result = Result::Failed;
            outcome.detail = QString("could not remove %1").arg(path);
            return outcome;
        }
        removedNames.append(QFileInfo(path).fileName());
    }

    if (!wantedIsInstalled) {
        // Copy aside and rename so a half written jar is never left in mods/.
        const QString partPath = targetPath + ".mcbm-part";
        QFile::remove(partPath);
        if (!QFile::copy(sourceJar, partPath) || !QFile::rename(partPath, targetPath)) {
            QFile::remove(partPath);
            outcome.result = Result::Failed;
            outcome.detail = QString("could not write %1").arg(targetPath);
            return outcome;
        }
    }

    const QString jarName = QFileInfo(sourceJar).fileName();
    if (removedNames.isEmpty()) {
        outcome.result = Result::Installed;
        outcome.detail = QString("installed %1").arg(jarName);
    } else if (removedNames == QStringList{jarName}) {
        // Same name, different bytes: a locally rebuilt mod, or a symlink that
        // pointed at the right version and is now a file of its own.
        outcome.result = Result::Replaced;
        outcome.detail = QString("refreshed %1").arg(jarName);
    } else {
        outcome.result = Result::Replaced;
        outcome.detail = QString("installed %1 (replaced %2)").arg(jarName, removedNames.join(", "));
    }
    return outcome;
}

QVector<Outcome> syncUsedInstances(const QString &prismPath)
{
    QVector<Outcome> outcomes;
    if (prismPath.isEmpty()) {
        return outcomes;
    }

    QStringList seen;
    for (const BotInstance *bot : std::as_const(BotManager::getBots())) {
        if (bot->instance.isEmpty() || seen.contains(bot->instance)) continue;
        seen.append(bot->instance);

        // Replacing a jar the running game has open fails on Windows, and would
        // swap the mod out from under a bot that is already using it. Stopping
        // counts, since the JVM is still on its way down.
        bool inUse = false;
        for (const BotInstance *other : std::as_const(BotManager::getBots())) {
            if (other->instance != bot->instance) continue;
            if (other->status == BotStatus::Starting || other->status == BotStatus::Online
                || other->status == BotStatus::Stopping) {
                inUse = true;
                break;
            }
        }

        if (inUse) {
            Outcome busy;
            busy.instance = bot->instance;
            busy.result = Result::Busy;
            busy.detail = QString("instance '%1' is in use").arg(bot->instance);
            outcomes.append(busy);
            continue;
        }

        outcomes.append(syncInstance(prismPath, bot->instance));
    }
    return outcomes;
}

} // namespace ClientModInstaller
