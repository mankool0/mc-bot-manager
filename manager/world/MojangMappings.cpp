#include "MojangMappings.h"
#include "AppPaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QVector>

namespace {

const QString kManifest = QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");

using Done = std::function<void(const QByteArray &, const QString &)>;

struct Waiter {
    QPointer<QObject> context;
    Done done;
};

QNetworkAccessManager *network()
{
    static QPointer<QNetworkAccessManager> manager;
    if (!manager) {
        manager = new QNetworkAccessManager(QCoreApplication::instance());
    }
    return manager;
}

QHash<QString, QVector<Waiter>> &waiting()
{
    static QHash<QString, QVector<Waiter>> waiters;
    return waiters;
}

QString cachePath(const QString &version)
{
    return QDir(AppPaths::cacheDir()).filePath(QStringLiteral("mojang-mappings/%1/client.txt").arg(version));
}

void finish(const QString &version, const QByteArray &mappings, const QString &error)
{
    const QVector<Waiter> waiters = waiting().take(version);
    for (const Waiter &w : waiters) {
        if (w.context) w.done(mappings, error);
    }
}

// One GET: its body to `next`, or the failure to everyone waiting on `version`.
void get(const QString &version, const QUrl &url, std::function<void(const QByteArray &)> next)
{
    QNetworkReply *reply = network()->get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, network(), [reply, version, next]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish(version, {}, QStringLiteral("could not download %1: %2").arg(reply->url().toString(), reply->errorString()));
            return;
        }
        next(reply->readAll());
    });
}

} // namespace

void MojangMappings::fetch(const QString &version, QObject *context, Done done)
{
    QFile cached(cachePath(version));
    if (cached.open(QIODevice::ReadOnly)) {
        const QByteArray mappings = cached.readAll();
        // Still asynchronous, so the caller sees one completion path.
        QMetaObject::invokeMethod(context, [done, mappings]() { done(mappings, QString()); }, Qt::QueuedConnection);
        return;
    }

    auto &all = waiting();
    const bool underway = all.contains(version);
    all[version].append({QPointer<QObject>(context), std::move(done)});
    if (underway) return;

    get(version, QUrl(kManifest), [version](const QByteArray &body) {
        QString versionUrl;
        for (const QJsonValue entry : QJsonDocument::fromJson(body).object().value("versions").toArray()) {
            if (entry.toObject().value("id").toString() == version) {
                versionUrl = entry.toObject().value("url").toString();
                break;
            }
        }
        if (versionUrl.isEmpty()) {
            finish(version, {}, QStringLiteral("Mojang lists no version %1").arg(version));
            return;
        }
        get(version, QUrl(versionUrl), [version](const QByteArray &body) {
            const QJsonObject download = QJsonDocument::fromJson(body).object()
                .value("downloads").toObject().value("client_mappings").toObject();
            if (download.isEmpty()) {
                finish(version, {}, QStringLiteral("Mojang publishes no mappings for %1").arg(version));
                return;
            }
            const QByteArray sha1 = download.value("sha1").toString().toLatin1();
            get(version, QUrl(download.value("url").toString()), [version, sha1](const QByteArray &mappings) {
                if (QCryptographicHash::hash(mappings, QCryptographicHash::Sha1).toHex() != sha1) {
                    finish(version, {}, QStringLiteral("Mojang's mappings for %1 do not match their checksum").arg(version));
                    return;
                }
                QDir().mkpath(QFileInfo(cachePath(version)).absolutePath());
                QSaveFile file(cachePath(version));
                // A file that does not save is only downloaded again next time.
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(mappings);
                    file.commit();
                }
                finish(version, mappings, QString());
            });
        });
    });
}
