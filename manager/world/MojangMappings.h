#ifndef MOJANGMAPPINGS_H
#define MOJANGMAPPINGS_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

// Mojang's client mappings for a game version, which the client mod needs to compile plugins on a
// game that runs under intermediary names. Downloaded once per machine into the manager's cache and
// handed to clients over the pipe.
namespace MojangMappings {

// The mappings for `version` to `done` on the main thread: the file's bytes, or none and why.
// Callers asking for a version already on its way share that download.
void fetch(const QString &version, QObject *context,
           std::function<void(const QByteArray &mappings, const QString &error)> done);

}

#endif // MOJANGMAPPINGS_H
