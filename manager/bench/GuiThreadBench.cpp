// The manager's GUI thread under a fleet's traffic, without a game. Fake clients connect over
// the real pipe and send what the client mod sends at its rates: player state and entity
// updates every tick, the view when a bot joins, chunk data and unloads as a bot flies over
// new ground, plugin statuses, block and light updates, chat and Baritone lines from bots
// at work. The real main window runs meanwhile, saving the world as the fleet's does.
//
// Reports how long the GUI thread went without running a 10 ms timer, and how late plugin
// messages reached a _global script: a script watching a plugin's heartbeat reads that delay
// as a silent plugin.
//
// It writes settings, scripts and world saves under HOME, so run it with HOME and every XDG_*
// directory pointed somewhere new, holding a copy of the block registry cache; it refuses to
// run under the real home.
//
//   cmake --build <build dir> --target gui_thread_bench
//   d=$(mktemp -d); mkdir -p $d/cache/mc-bot-manager $d/run
//   cp ~/.cache/mc-bot-manager/block_registry_4671.dat $d/cache/mc-bot-manager/
//   HOME=$d XDG_CONFIG_HOME=$d/config XDG_DATA_HOME=$d/data XDG_CACHE_HOME=$d/cache
//     XDG_RUNTIME_DIR=$d/run QT_QPA_PLATFORM=offscreen <build dir>/gui_thread_bench
//     [--bots 42] [--flying 8] [--seconds 60] [--view 8] [--speed 60] [--entities 8]
//     [--log-modules 1]

#include "AppPaths.h"
#include "bot/BotManager.h"
#include "bot/WorldData.h"
#include "protocol.qpb.h"
#include "saving/WorldAutoSaver.h"
#include "ui/GlobalSettingsDialog.h"
#include "ui/ManagerMainWindow.h"

#include <QApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProtobufSerializer>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <pwd.h>
#include <thread>
#include <unistd.h>

namespace proto = mankool::mcbot::protocol;

namespace {

constexpr int kDataVersion = 4671;
const QString kNether = QStringLiteral("minecraft:the_nether");

struct Params {
    int bots = 42;
    int flying = 8;
    int seconds = 60;
    int view = 8;
    double speed = 60.0;
    int entities = 8;
    int logModules = 1;
};

double now()
{
    return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

// Protobuf by hand for the chunk messages, so a client thread can send a fleet's worth: the
// sections are encoded once, the chunk around them each time.
struct Writer {
    QByteArray out;

    void varint(quint64 v)
    {
        while (v >= 0x80) {
            out.append(char(v | 0x80));
            v >>= 7;
        }
        out.append(char(v));
    }

    void tag(quint32 field, quint32 wire) { varint((quint64(field) << 3) | wire); }

    void delimited(quint32 field, const QByteArray &bytes)
    {
        tag(field, 2);
        varint(quint64(bytes.size()));
        out.append(bytes);
    }

    void int32(quint32 field, qint32 v)
    {
        tag(field, 0);
        varint(quint64(qint64(v)));
    }
};

// Nether sections as flying over new ground meets them: netherrack with runs of ore and lava
// up to the roof, mostly dark; air above it, sent uniform. Terrain that compresses like the
// real thing, or the world saver is timed on noise.
QVector<QByteArray> encodedSections()
{
    QRandomGenerator rng(1);
    QVector<QByteArray> out;
    for (int y = 0; y < 16; ++y) {
        proto::ChunkSection s;
        s.setSectionY(y);
        s.setBiomePalette({QStringLiteral("minecraft:nether_wastes")});
        s.setBiomeUniform(true);
        if (y >= 8) {
            s.setUniform(true);
            s.setPalette({0});
        } else {
            QtProtobuf::uint32List palette;
            for (int i = 0; i < 6; ++i) {
                palette.append(quint32(rng.bounded(29000)));
            }
            s.setPalette(palette);
            QtProtobuf::uint32List idx;
            idx.reserve(4096);
            quint32 run = 1;
            for (int i = 0; i < 4096; ++i) {
                if (rng.bounded(40) == 0) {
                    run = rng.bounded(4) == 0 ? quint32(rng.bounded(6)) : 1;
                }
                idx.append(run);
            }
            s.setBlockIndices(std::move(idx));
            QByteArray light(2048, '\0');
            for (int i = 0; i < 64; ++i) {
                light[rng.bounded(2048)] = char(0x77);
            }
            s.setBlockLight(light);
        }
        QProtobufSerializer ser;
        out.append(s.serialize(&ser));
    }
    return out;
}

QByteArray chunkMessage(const QVector<QByteArray> &sections, int cx, int cz)
{
    Writer chunk;
    chunk.int32(1, cx);
    chunk.int32(2, cz);
    chunk.delimited(3, kNether.toUtf8());
    chunk.int32(5, 255);
    for (const QByteArray &s : sections) {
        chunk.delimited(6, s);
    }
    Writer msg;
    msg.tag(2, 0);
    msg.varint(quint64(QDateTime::currentMSecsSinceEpoch()));
    msg.delimited(18, chunk.out);
    return msg.out;
}

class Client {
public:
    Client(const QString &name, const QString &uuid, int lane, bool flying, const Params &p,
           const QVector<QByteArray> &sections, const std::atomic<bool> &stop)
        : m_name(name), m_uuid(uuid), m_lane(lane), m_flying(flying), m_p(p), m_sections(sections), m_stop(stop)
    {
    }

    void run(const QString &socketPath)
    {
        // Made here, not in the constructor: a socket belongs to the thread that makes it.
        QLocalSocket socket;
        m_socket = &socket;
        m_socket->connectToServer(socketPath);
        if (!m_socket->waitForConnected(5000)) {
            std::printf("%s: could not connect\n", qPrintable(m_name));
            return;
        }
        hello();
        const int z0 = m_lane * 250;  // chunks: each bot on its own road, 4000 blocks apart
        double x = 0.0;
        int frontier = 0;
        // Joining: the whole view at once.
        for (int cx = -m_p.view; cx <= m_p.view; ++cx) {
            for (int cz = z0 - m_p.view; cz <= z0 + m_p.view; ++cz) {
                sendRaw(chunkMessage(m_sections, cx, cz));
            }
        }
        QElapsedTimer clock;
        clock.start();
        for (qint64 tick = 0; !m_stop.load(); ++tick) {
            playerState(x, double(z0 * 16));
            if (m_p.entities > 0) {
                entities(tick, x, double(z0 * 16));
            }
            if (m_flying) {
                x += m_p.speed / 20.0;
                // A new row of the view comes in, the trailing one goes.
                while (int(x) / 16 > frontier) {
                    ++frontier;
                    for (int cz = z0 - m_p.view; cz <= z0 + m_p.view; ++cz) {
                        sendRaw(chunkMessage(m_sections, frontier + m_p.view, cz));
                        unload(frontier - m_p.view - 1, cz);
                    }
                }
                if (tick % 10 == 0) {
                    plugin(QStringLiteral("flight"), tick);
                }
                // The pilot's elytra-fly settings, answered with the whole module each time.
                if (tick % 80 == 40) {
                    moduleResponse();
                }
            } else {
                if (tick % 2 == 0) {
                    blockUpdate(tick, z0);
                }
                if (tick % 10 == 0) {
                    lightUpdate(z0);
                }
                if (tick % 20 == 0) {
                    baritone(tick);
                }
            }
            if (tick % 40 == 0) {
                plugin(QStringLiteral("status"), tick);
                chat(tick);
            }
            drain();
            // 20 ticks a second, as the game runs them.
            const qint64 due = (tick + 1) * 50;
            const qint64 left = due - clock.elapsed();
            if (left > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(left));
            }
        }
        m_socket->disconnectFromServer();
        m_socket = nullptr;
    }

private:
    void sendRaw(const QByteArray &payload)
    {
        QByteArray frame;
        QDataStream ds(&frame, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);  // as PipeConnection.java writes it
        ds << quint32(payload.size());
        frame.append(payload);
        m_socket->write(frame);
        // A client's write blocks while the manager is behind: the backlog waits here, timed
        // from when the message was made.
        while (m_socket->bytesToWrite() > 0 && !m_stop.load()) {
            m_socket->waitForBytesWritten(100);
        }
    }

    void send(proto::ClientToManagerMessage &msg)
    {
        msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        QProtobufSerializer ser;
        sendRaw(msg.serialize(&ser));
    }

    void drain()
    {
        m_socket->waitForReadyRead(0);
        m_socket->readAll();
    }

    void hello()
    {
        proto::ConnectionInfo info;
        info.setClientVersion(QStringLiteral("1.21.11"));
        info.setModVersion(QCoreApplication::applicationVersion());
        info.setPlayerName(m_name);
        info.setPlayerUuid(m_uuid);
        info.setDataVersion(kDataVersion);
        info.setVersionSeries(QStringLiteral("main"));
        info.setMaxMemory(qint64(4096) * 1024 * 1024);
        proto::ClientToManagerMessage a;
        a.setConnectionInfo(info);
        send(a);

        proto::QueryBlockRegistryMessage query;
        query.setDataVersion(kDataVersion);
        proto::ClientToManagerMessage b;
        b.setQueryRegistry(query);
        send(b);

        proto::ServerConnectionStatus status;
        status.setStatus(proto::ServerConnectionStatus_QtProtobufNested::Status::SUCCESSFUL);
        status.setServerAddress(QStringLiteral("bench.invalid"));
        status.setVersionName(QStringLiteral("1.21.11"));
        status.setPlayersOnline(300);
        proto::ClientToManagerMessage c;
        c.setServerStatus(status);
        send(c);
    }

    void playerState(double x, double z)
    {
        proto::Vec3d pos;
        pos.setX(x);
        pos.setY(121.0);
        pos.setZ(z);
        proto::Vec3d vel;
        vel.setX(m_flying ? m_p.speed / 20.0 : 0.0);
        proto::PlayerStateUpdate state;
        state.setUuid(m_uuid);
        state.setName(m_name);
        state.setPosition(pos);
        state.setVelocity(vel);
        state.setYaw(-90.0f);
        state.setHealth(20.0f);
        state.setFoodLevel(20);
        state.setSaturation(5.0f);
        state.setAir(300);
        state.setFlying(m_flying);
        state.setDimension(kNether);
        state.setEntityId(1000 + m_lane);
        proto::ClientToManagerMessage msg;
        msg.setPlayerState(state);
        send(msg);
    }

    void entities(qint64 tick, double x, double z)
    {
        proto::EntityUpdate update;
        update.setDimension(kNether);
        QList<proto::EntityData> list;
        for (int i = 0; i < m_p.entities; ++i) {
            proto::EntityData e;
            e.setEntityId(5000 + i);
            e.setUuid(QStringLiteral("00000000-0000-0000-0000-%1").arg(m_lane * 100 + i, 12, 10, QLatin1Char('0')));
            e.setType(QStringLiteral("minecraft:zombified_piglin"));
            e.setX(x + 10.0 + i + 0.05 * double(tick % 40));
            e.setY(121.0);
            e.setZ(z + 6.0);
            e.setYaw(float(tick % 360));
            e.setIsLiving(true);
            e.setHealth(20.0f);
            e.setMaxHealth(20.0f);
            if ((tick + i) % 200 == 0) {
                e.setNbt(QByteArray(1200, '\x0a'));
            }
            list.append(e);
        }
        update.setUpserted(list);
        proto::ClientToManagerMessage msg;
        msg.setEntityUpdate(update);
        send(msg);
    }

    void unload(int cx, int cz)
    {
        proto::ChunkUnloadMessage u;
        u.setChunkX(cx);
        u.setChunkZ(cz);
        u.setDimension(kNether);
        proto::ClientToManagerMessage msg;
        msg.setChunkUnload(u);
        send(msg);
    }

    void blockUpdate(qint64 tick, int z0)
    {
        proto::BlockPos pos;
        pos.setX(int(tick % 64));
        pos.setY(120);
        pos.setZ(z0 * 16 + int(tick % 7));
        proto::BlockUpdateMessage b;
        b.setPosition(pos);
        b.setStateId(tick % 4 ? 0u : 2201u);
        b.setDimension(kNether);
        proto::ClientToManagerMessage msg;
        msg.setBlockUpdate(b);
        send(msg);
    }

    void lightUpdate(int z0)
    {
        proto::LightSection s;
        s.setSectionY(7);
        s.setData(QByteArray(2048, '\x0f'));
        proto::LightUpdateMessage l;
        l.setChunkX(1);
        l.setChunkZ(z0);
        l.setDimension(kNether);
        l.setBlockSections({s});
        proto::ClientToManagerMessage msg;
        msg.setLightUpdate(l);
        send(msg);
    }

    void baritone(qint64 tick)
    {
        proto::BaritoneLogMessage b;
        b.setContent(QStringLiteral("Building from BetterBlockPos{x=%1,y=120,z=0}").arg(tick));
        b.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        proto::ClientToManagerMessage msg;
        msg.setBaritoneLog(b);
        send(msg);
    }

    void chat(qint64 tick)
    {
        proto::ChatMessage c;
        c.setType(proto::ChatMessage_QtProtobufNested::Type::SYSTEM_MESSAGE);
        c.setContent(QStringLiteral("Player%1 joined the game.").arg(tick % 97));
        c.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        proto::ClientToManagerMessage msg;
        msg.setChat(c);
        send(msg);
    }

    void moduleResponse()
    {
        // elytra-fly as the client reports it: 38 settings in its groups.
        proto::ModuleInfo module;
        module.setName(QStringLiteral("elytra-fly"));
        module.setCategory(QStringLiteral("Movement"));
        module.setEnabled(true);
        module.setDescription(QStringLiteral("Gives you more control over your elytra."));
        QList<proto::SettingInfo> settings;
        for (int i = 0; i < 38; ++i) {
            proto::SettingInfo info;
            info.setName(QStringLiteral("setting-%1").arg(i));
            info.setGroupName(i < 20 ? QStringLiteral("General") : QStringLiteral("Inertia"));
            proto::MeteorSettingValue value;
            if (i % 3 == 0) {
                info.setType(proto::SettingInfo_QtProtobufNested::SettingType::BOOLEAN);
                value.setBoolValue(i % 2 == 0);
            } else if (i % 3 == 1) {
                info.setType(proto::SettingInfo_QtProtobufNested::SettingType::DOUBLE);
                value.setDoubleValue(0.5 * i);
                info.setMinValue(0.0);
                info.setMaxValue(10.0);
            } else {
                info.setType(proto::SettingInfo_QtProtobufNested::SettingType::ENUM);
                value.setStringValue(QStringLiteral("Smart"));
                info.setPossibleValues({QStringLiteral("None"), QStringLiteral("Smart"), QStringLiteral("Always")});
            }
            info.setCurrentValue(value);
            settings.append(info);
        }
        module.setSettings(settings);
        proto::SetModuleConfigResponse response;
        response.setRequestId(QUuid::createUuid().toString(QUuid::WithoutBraces));
        response.setSuccess(true);
        response.setUpdatedModule(module);
        proto::ClientToManagerMessage msg;
        msg.setModuleConfigResponse(response);
        send(msg);
    }

    void plugin(const QString &channel, qint64 tick)
    {
        // `sent`: when it was made, for the probe to time.
        const QString payload = QStringLiteral("{\"steering\":true,\"tick\":%1,\"sent\":%2}")
                                    .arg(tick)
                                    .arg(now(), 0, 'f', 6);
        proto::PluginMessage pm;
        pm.setPlugin(QStringLiteral("bench"));
        pm.setChannel(channel);
        pm.setPayload(payload);
        proto::ClientToManagerMessage msg;
        msg.setPluginMessage(pm);
        send(msg);
    }

    QLocalSocket *m_socket = nullptr;
    QString m_name, m_uuid;
    int m_lane;
    bool m_flying;
    Params m_p;
    const QVector<QByteArray> &m_sections;
    const std::atomic<bool> &m_stop;
};

// A _global script that times each plugin message from when the client made it, and lists
// and exports every bot's changed sections every half second, as an uploading script would.
QString probeScript(const QString &out, const QStringList &names)
{
    return QStringLiteral(R"PY(import json
import threading
import time

_lat = []
_names = %2
_tokens = {}


def _lister():
    while True:
        t0 = time.time()
        for n in _names:
            try:
                ch = world.changed_sections(n, since=_tokens.get(n), dimension="minecraft:the_nether",  # noqa: F821
                                            digest=True, digest_prefix=b"bench")
                _tokens[n] = ch.token
                keys = [(c.chunk_x, c.chunk_z, c.section_y) for c in ch.sections]
                for i in range(0, len(keys), 1024):
                    world.export_sections(keys[i:i + 1024], bot_name=n, dimension="minecraft:the_nether")  # noqa: F821
            except Exception:
                pass
        time.sleep(max(0.0, 0.5 - (time.time() - t0)))


threading.Thread(target=_lister, daemon=True).start()

try:
    @on("plugin_message")  # noqa: F821
    def _probe(message):
        sent = (message.get("data") or {}).get("sent")
        if sent is not None:
            _lat.append((sent, time.time() - sent))
except NameError:
    pass

while True:
    time.sleep(1.0)
    with open(%1, "w") as f:
        json.dump(list(_lat), f)
)PY")
        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{out}).toJson(QJsonDocument::Compact)).mid(1).chopped(1),
             QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(names)).toJson(QJsonDocument::Compact)));
}

double percentile(QVector<double> v, double q)
{
    if (v.isEmpty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[qMin(v.size() - 1, qsizetype(q * double(v.size())))];
}

}

int main(int argc, char **argv)
{
    Params p;
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *k = argv[i];
        const double v = std::atof(argv[i + 1]);
        if (!std::strcmp(k, "--bots")) p.bots = int(v);
        else if (!std::strcmp(k, "--flying")) p.flying = int(v);
        else if (!std::strcmp(k, "--seconds")) p.seconds = int(v);
        else if (!std::strcmp(k, "--view")) p.view = int(v);
        else if (!std::strcmp(k, "--speed")) p.speed = v;
        else if (!std::strcmp(k, "--entities")) p.entities = int(v);
        else if (!std::strcmp(k, "--log-modules")) p.logModules = int(v);
    }

    const passwd *pw = getpwuid(getuid());
    if (pw && QDir(QString::fromLocal8Bit(qgetenv("HOME"))) == QDir(QString::fromLocal8Bit(pw->pw_dir))) {
        std::printf("refusing to run under the real HOME: it writes settings, scripts and saves there\n");
        return 2;
    }

    qRegisterMetaType<ChunkData>();
    qRegisterMetaType<BlockEntityData>();
    qRegisterMetaType<PlayerSaveData>();
    qRegisterMetaType<MapData>();
    qRegisterMetaType<QVector<EntityData>>();
    qRegisterMetaType<QVector<BlockEntityData>>();
    QApplication app(argc, argv);
    app.setApplicationVersion(APP_VERSION);
    // The real binary's name, which every data, cache and log directory is named after.
    app.setApplicationName(QStringLiteral("mc-bot-manager"));
    GlobalSettingsDialog::applyColorScheme();

    if (!QFile::exists(QDir(AppPaths::cacheDir()).filePath(QString("block_registry_%1.dat").arg(kDataVersion)))) {
        std::printf("no block_registry_%d.dat in %s\n", kDataVersion, qPrintable(AppPaths::cacheDir()));
        return 2;
    }

    QStringList names, uuids;
    {
        QSettings settings("MCBotManager", "MCBotManager");
        settings.setValue("Console/logModuleChanges", p.logModules != 0);
        settings.beginGroup("Bots");
        settings.setValue("count", p.bots);
        for (int i = 0; i < p.bots; ++i) {
            names.append(QString("Bench%1").arg(i));
            uuids.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
            settings.beginGroup(QString("Bot_%1").arg(i));
            settings.setValue("name", names.last());
            settings.setValue("instance", names.last());
            settings.setValue("accountId", uuids.last());
            settings.setValue("server", "bench.invalid");
            settings.setValue("autoConnect", false);
            settings.setValue("autoRestart", false);
            settings.setValue("tokenRefresh", false);
            settings.setValue("saveWorldToDisk", true);
            settings.endGroup();
        }
        settings.endGroup();
    }

    const QString globalDir = QDir(AppPaths::scriptsDir()).filePath("_global");
    QDir().mkpath(globalDir);
    const QString probeOut = QDir(globalDir).filePath("probe.json");
    QFile::remove(probeOut);
    {
        QFile f(QDir(globalDir).filePath("probe.py"));
        QFile m(QDir(globalDir).filePath(".metadata.json"));
        if (!f.open(QIODevice::WriteOnly) || !m.open(QIODevice::WriteOnly)) {
            std::printf("cannot write the probe script into %s\n", qPrintable(globalDir));
            return 2;
        }
        f.write(probeScript(probeOut, names).toUtf8());
        m.write(R"({"scripts":[{"filename":"probe.py","autorun":true}]})");
    }

    ManagerMainWindow w;
    w.show();

    for (const QString &name : std::as_const(names)) {
        if (BotInstance *bot = BotManager::getBotByName(name)) {
            bot->status = BotStatus::Starting;
        }
    }

    // How long the GUI thread goes between turns of a 10 ms timer, and when.
    QVector<QPair<double, double>> gaps;
    const double started = now();
    QElapsedTimer sinceTick;
    sinceTick.start();
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&]() {
        gaps.append({now() - started, double(sinceTick.nsecsElapsed()) / 1e6});
        sinceTick.restart();
    });
    ticker.start(10);

    // The world saver's queue, every bot's chunks waiting to be written: it should not grow.
    int saverPeak = 0, saverLast = 0;
    QTimer saverWatch;
    QObject::connect(&saverWatch, &QTimer::timeout, [&]() {
        const BotInstance *first = names.isEmpty() ? nullptr : BotManager::getBotByName(names.first());
        if (first && first->worldAutoSaver) {
            saverLast = first->worldAutoSaver->pendingChunkCount();
            saverPeak = std::max(saverPeak, saverLast);
        }
    });
    saverWatch.start(1000);

    const QString socketPath = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + "/minecraft_manager";
    const QVector<QByteArray> sections = encodedSections();
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<Client>> clients;
    // Started after the event loop is running, one a little after another as the gate lets them in.
    QTimer::singleShot(500, [&]() {
        for (int i = 0; i < p.bots; ++i) {
            clients.push_back(std::make_unique<Client>(names[i], uuids[i], i, i < p.flying, p, sections, stop));
            Client *c = clients.back().get();
            threads.emplace_back([c, socketPath, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * i));
                c->run(socketPath);
            });
        }
    });

    QTimer::singleShot((p.seconds + 3) * 1000, [&]() {
        stop.store(true);
        for (std::thread &t : threads) {
            t.join();
        }
        app.quit();
    });
    app.exec();

    // The first seconds are every bot joining at once, each sent its whole view: reported apart.
    const double settled = 0.5 + 0.05 * p.bots + 10.0;
    QVector<QPair<double, double>> lat;
    QFile f(probeOut);
    if (f.open(QIODevice::ReadOnly)) {
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
            lat.append({v.toArray().at(0).toDouble() - started, v.toArray().at(1).toDouble() * 1000.0});
        }
    }
    const auto after = [](const QVector<QPair<double, double>> &all, double from) {
        QVector<double> out;
        for (const auto &[t, v] : all) {
            if (t >= from) {
                out.append(v);
            }
        }
        return out;
    };
    const auto line = [](const char *what, const QVector<double> &v, bool counted) {
        const double worst = v.isEmpty() ? 0.0 : *std::max_element(v.cbegin(), v.cend());
        std::printf("  %-34s p50 %6.1f ms, p99 %6.1f ms, worst %7.1f ms", what, percentile(v, 0.5), percentile(v, 0.99),
                    worst);
        if (counted) {
            std::printf(", %lld of them, %lld over 2 s", static_cast<long long>(v.size()),
                        static_cast<long long>(std::count_if(v.cbegin(), v.cend(), [](double x) { return x > 2000.0; })));
        }
        std::printf("\n");
    };
    std::printf("%d bots, %d flying at %.0f blocks/s, view %d, %d entities each, %ds, module changes %s\n", p.bots,
                p.flying, p.speed, p.view, p.entities, p.seconds, p.logModules ? "logged" : "not logged");
    line("GUI thread, 10 ms timer, all:", after(gaps, 0.0), false);
    line("GUI thread, after joining:", after(gaps, settled), false);
    line("plugin messages to the script, all:", after(lat, 0.0), true);
    line("plugin messages, after joining:", after(lat, settled), true);
    std::printf("  world saver: %d chunks waiting at the end, %d at the most\n", saverLast, saverPeak);
    return 0;
}
