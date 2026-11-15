// Checks and timing for network/ChunkWireDecoder: it must build exactly what
// QProtobufSerializer builds from the same bytes, or decline the message.
//
//   cmake --build <build dir> --target chunk_decode_bench
//   <build dir>/chunk_decode_bench [--checks-only] [--vectors DIR]
//
// --vectors DIR: every *.bin in DIR is a ClientToManagerMessage from another encoder (protoc
// --encode writes them the way the client's protobuf-java does), checked the same way.

#include "network/ChunkWireDecoder.h"
#include "protocol.qpb.h"
#include "world.qpb.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProtobufSerializer>
#include <cstdio>
#include <cstring>

using namespace mankool::mcbot::protocol;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

struct Rng {
    quint64 state;

    explicit Rng(quint64 seed) : state(seed) {}

    quint64 next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    int below(int n) { return int(next() % quint64(n)); }
};

QByteArray serialize(const ClientToManagerMessage &msg)
{
    QProtobufSerializer serializer;
    return msg.serialize(&serializer);
}

std::optional<ClientToManagerMessage> reference(const QByteArray &data)
{
    QProtobufSerializer serializer;
    ClientToManagerMessage msg;
    if (!msg.deserialize(&serializer, data)) {
        return std::nullopt;
    }
    return msg;
}

// What the decoder returned against what the serializer reads: equal, or declined.
enum class Verdict { Same, Declined, Differs };

Verdict compare(const QByteArray &data)
{
    const std::optional<ChunkDataMessage> fast = ChunkWireDecoder::decode(data);
    if (!fast) {
        return Verdict::Declined;
    }
    const std::optional<ClientToManagerMessage> ref = reference(data);
    if (!ref || !ref->hasChunkData() || !(ref->chunkData() == *fast)) {
        return Verdict::Differs;
    }
    return Verdict::Same;
}

// A chunk with every field in play: block ids past one varint byte, uniform sections, sections
// without light, negative coordinates, block entities, biomes uniform and not.
ChunkDataMessage randomChunk(Rng &rng, bool overworld)
{
    ChunkDataMessage chunk;
    chunk.setChunkX(rng.below(2000001) - 1000000);
    chunk.setChunkZ(rng.below(2000001) - 1000000);
    chunk.setDimension(overworld ? QStringLiteral("minecraft:overworld") : QStringLiteral("minecraft:the_nether"));
    chunk.setMinY(overworld ? -64 : 0);
    chunk.setMaxY(overworld ? 319 : 255);
    QList<ChunkSection> sections;
    const int count = rng.below(5) == 0 ? rng.below(3) : (overworld ? 24 : 16);
    for (int s = 0; s < count; ++s) {
        ChunkSection section;
        section.setSectionY(overworld ? s - 4 : s);
        const bool uniform = rng.below(4) == 0;
        section.setUniform(uniform);
        QtProtobuf::uint32List palette;
        const int paletteSize = uniform ? 1 : 1 + rng.below(40);
        for (int i = 0; i < paletteSize; ++i) {
            palette.append(quint32(rng.below(30000)));
        }
        section.setPalette(palette);
        if (!uniform) {
            QtProtobuf::uint32List indices;
            indices.reserve(4096);
            for (int i = 0; i < 4096; ++i) {
                indices.append(quint32(rng.below(paletteSize)));
            }
            section.setBlockIndices(std::move(indices));
        }
        const bool biomeUniform = rng.below(2) == 0;
        section.setBiomeUniform(biomeUniform);
        QStringList biomes{QStringLiteral("minecraft:nether_wastes")};
        if (!biomeUniform) {
            biomes.append(QStringLiteral("minecraft:crimson_forest"));
            QtProtobuf::uint32List biomeIndices;
            for (int i = 0; i < 64; ++i) {
                biomeIndices.append(quint32(rng.below(2)));
            }
            section.setBiomeIndices(std::move(biomeIndices));
        }
        section.setBiomePalette(biomes);
        if (rng.below(5) != 0) {
            QByteArray light(2048, Qt::Uninitialized);
            for (char &c : light) {
                c = char(rng.below(256));
            }
            section.setBlockLight(light);
        }
        if (overworld && rng.below(5) != 0) {
            section.setSkyLight(QByteArray(2048, char(0xff)));
        }
        sections.append(section);
    }
    chunk.setSections(sections);
    QByteArrayList nbt;
    for (int i = rng.below(4); i > 0; --i) {
        nbt.append(QByteArray(1 + rng.below(300), char(rng.below(256))));
    }
    chunk.setBlockEntityNbt(nbt);
    return chunk;
}

QByteArray wrap(const ChunkDataMessage &chunk, Rng &rng)
{
    ClientToManagerMessage msg;
    msg.setMessageId(QStringLiteral("m-%1").arg(rng.next()));
    msg.setTimestamp(qint64(rng.next() >> 23));
    msg.setChunkData(chunk);
    return serialize(msg);
}

// Hand-written encodings the serializer does not produce but a parser must take.
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
        varint(quint64(qint64(v)));  // negative: ten bytes, sign extended, as protobuf writes it
    }
};

QByteArray handSection(bool packed)
{
    Writer s;
    s.int32(1, -3);
    // Palette with ids past one byte, then the indices; packed or one by one.
    if (packed) {
        Writer p;
        p.varint(0);
        p.varint(300);
        p.varint(70000);
        s.delimited(2, p.out);
    } else {
        s.tag(2, 0);
        s.varint(0);
        s.tag(2, 0);
        s.varint(300);
        s.tag(2, 0);
        s.varint(70000);
    }
    Writer idx;
    for (int i = 0; i < 4096; ++i) {
        idx.varint(quint64((i + 1) % 3));  // the last one not 0, which a short copy would leave
    }
    s.delimited(3, idx.out);
    s.delimited(5, "minecraft:plains");
    s.delimited(8, QByteArray(2048, '\x11'));
    return s.out;
}

QByteArray handChunk(const QByteArray &section, bool shuffled)
{
    Writer c;
    if (shuffled) {
        // Fields out of order, a scalar twice (the last one stands), a section before the
        // coordinates.
        c.delimited(6, section);
        c.int32(2, 5);
        c.int32(1, 99);
        c.delimited(3, "minecraft:the_nether");
        c.int32(1, -7);
        c.delimited(6, section);
        c.int32(5, 255);
    } else {
        c.int32(1, -7);
        c.int32(2, 5);
        c.delimited(3, "minecraft:the_nether");
        c.int32(5, 255);
        c.delimited(6, section);
        c.delimited(6, section);
    }
    c.delimited(7, QByteArray("\x0a\x00", 2));
    return c.out;
}

QByteArray outer(const QByteArray &chunk, bool idFirst = true)
{
    Writer m;
    if (idFirst) {
        m.delimited(1, "id");
    }
    m.tag(2, 0);
    m.varint(1793475198000ull);
    m.delimited(18, chunk);
    if (!idFirst) {
        m.delimited(1, "id");
    }
    return m.out;
}

void checkEquivalence()
{
    std::printf("equivalence with QProtobufSerializer\n");
    Rng rng(7);
    int same = 0, other = 0;
    for (int i = 0; i < 400; ++i) {
        const QByteArray data = wrap(randomChunk(rng, i % 2 == 1), rng);
        (compare(data) == Verdict::Same ? same : other)++;
    }
    check(other == 0, "400 random chunks, both dimensions, decode to what the serializer builds");
    std::printf("      (%d same, %d declined or different)\n", same, other);

    ClientToManagerMessage empty;
    empty.setChunkData(ChunkDataMessage());
    check(compare(serialize(empty)) == Verdict::Same, "an empty chunk_data, every field at its default");
}

void checkHandWritten()
{
    std::printf("encodings a parser must take\n");
    check(compare(outer(handChunk(handSection(true), false))) == Verdict::Same,
          "packed palette with multi-byte ids, negative section y and chunk x");
    check(compare(outer(handChunk(handSection(false), false))) == Verdict::Same,
          "a repeated uint32 written one value at a time, not packed");
    check(compare(outer(handChunk(handSection(true), true))) == Verdict::Same,
          "fields out of order and a scalar written twice: the last stands");
    check(compare(outer(handChunk(handSection(true), false), false)) == Verdict::Same,
          "message_id after the payload");

    Writer again;
    again.out = handSection(true);
    again.delimited(8, QByteArray(2048, '\x22'));
    again.delimited(5, "minecraft:desert");
    Writer chunk;
    chunk.out = handChunk(again.out, false);
    chunk.delimited(3, "minecraft:overworld");
    check(compare(outer(chunk.out)) == Verdict::Same,
          "a string and a bytes field written twice, and a second biome: the last string stands, "
          "the biomes add up");
}

void checkDeclines()
{
    std::printf("what it hands back to the serializer\n");
    const QByteArray good = outer(handChunk(handSection(true), false));

    ClientToManagerMessage plugin;
    PluginMessage pm;
    pm.setPlugin(QStringLiteral("bench"));
    pm.setChannel(QStringLiteral("flight"));
    pm.setPayload(QStringLiteral("{\"steering\":true}"));
    plugin.setPluginMessage(pm);
    check(!ChunkWireDecoder::decode(serialize(plugin)), "a plugin message is not a chunk");

    Writer twice;
    twice.out = good;
    twice.delimited(18, handChunk(handSection(true), false));
    check(compare(twice.out) == Verdict::Declined, "chunk_data twice, which the format merges");

    Writer then;
    then.out = good;
    then.delimited(49, QByteArray());
    check(compare(then.out) == Verdict::Declined, "another payload after it, which the oneof takes");

    // The format appends a second packed run; the serializer keeps only the last.
    Writer runs;
    runs.out = handSection(true);
    Writer more;
    more.varint(7);
    runs.delimited(3, more.out);
    check(compare(outer(handChunk(runs.out, false))) == Verdict::Declined,
          "a packed field written twice, which the serializer reads its own way");
    Writer after;
    after.out = handSection(true);
    after.tag(3, 0);
    after.varint(7);
    check(compare(outer(handChunk(after.out, false))) == Verdict::Declined,
          "a value written alone after the field's packed run");

    Writer unknownSection;
    unknownSection.out = handSection(true);
    unknownSection.int32(15, 1);
    check(compare(outer(handChunk(unknownSection.out, false))) == Verdict::Declined,
          "a field this schema does not have");

    Writer wrongWire;
    wrongWire.tag(1, 2);  // section_y as bytes
    wrongWire.varint(1);
    wrongWire.out.append('x');
    check(compare(outer(handChunk(wrongWire.out, false))) == Verdict::Declined,
          "a field with a wire type its type cannot have");

    check(compare(good.left(good.size() - 1)) == Verdict::Declined, "the last byte missing");
    check(compare(QByteArray()) == Verdict::Declined, "nothing at all");
}

// Truncated and corrupted chunks must never decode to something the serializer reads otherwise.
void checkFuzz()
{
    std::printf("fuzz\n");
    Rng rng(11);
    const QByteArray base = wrap(randomChunk(rng, false), rng);
    const QByteArray hand = outer(handChunk(handSection(true), true));
    const QByteArray unpacked = outer(handChunk(handSection(false), false));
    int differs = 0, decoded = 0;
    for (int i = 0; i < 60000; ++i) {
        QByteArray data = i % 3 == 0 ? base : (i % 3 == 1 ? hand : unpacked);
        switch (rng.below(4)) {
        case 0:
            data.truncate(rng.below(int(data.size())));
            break;
        case 1:
            for (int k = 1 + rng.below(4); k > 0; --k) {
                data[rng.below(int(data.size()))] = char(rng.below(256));
            }
            break;
        case 2:
            // Flips in the first bytes, where the tags and lengths are.
            data[rng.below(qMin<int>(64, int(data.size())))] ^= char(1 << rng.below(8));
            break;
        default:
            data[rng.below(int(data.size()))] ^= char(1 << rng.below(8));
            break;
        }
        const Verdict v = compare(data);
        differs += v == Verdict::Differs;
        decoded += v == Verdict::Same;
    }
    check(differs == 0, "60000 truncated or corrupted messages: each declined or read as the serializer reads it");
    std::printf("      (%d of them still decoded, all the same)\n", decoded);
}

void checkVectors(const QString &dir)
{
    std::printf("vectors from %s\n", qPrintable(dir));
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.bin")}, QDir::Files, QDir::Name);
    int same = 0;
    for (const QString &name : files) {
        QFile f(QDir(dir).filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            check(false, qPrintable(QStringLiteral("could not read ") + name));
            continue;
        }
        const Verdict v = compare(f.readAll());
        if (v != Verdict::Same) {
            check(false, qPrintable(name + (v == Verdict::Declined ? QStringLiteral(" declined")
                                                                    : QStringLiteral(" differs"))));
        } else {
            ++same;
        }
    }
    check(!files.isEmpty() && same == files.size(), "every vector decodes to what the serializer builds");
    std::printf("      (%d of %d)\n", same, int(files.size()));
}

void timing()
{
    std::printf("timing, per chunk message\n");
    for (bool overworld : {false, true}) {
        Rng rng(3);
        QVector<QByteArray> wire;
        for (int i = 0; i < 300; ++i) {
            ChunkDataMessage chunk = randomChunk(rng, overworld);
            // Full chunks of mostly one block, as flying over new ground sends them.
            QList<ChunkSection> sections;
            for (int s = 0; s < (overworld ? 24 : 16); ++s) {
                ChunkSection section;
                section.setSectionY(s);
                section.setPalette({0, 1012, 2201, 3305, 4410, 5120});
                QtProtobuf::uint32List idx;
                for (int k = 0; k < 4096; ++k) {
                    idx.append(rng.below(10) < 7 ? 1 : quint32(rng.below(6)));
                }
                section.setBlockIndices(std::move(idx));
                section.setBiomePalette({QStringLiteral("minecraft:nether_wastes")});
                section.setBiomeUniform(true);
                section.setBlockLight(QByteArray(2048, '\x0f'));
                if (overworld) {
                    section.setSkyLight(QByteArray(2048, char(0xff)));
                }
                sections.append(section);
            }
            chunk.setSections(sections);
            wire.append(wrap(chunk, rng));
        }
        QElapsedTimer t;
        t.start();
        for (const QByteArray &data : wire) {
            if (!reference(data)) {
                std::printf("reference decode failed\n");
                ++g_failures;
            }
        }
        const double serializerMs = double(t.nsecsElapsed()) / 1e6 / wire.size();
        t.start();
        for (const QByteArray &data : wire) {
            if (!ChunkWireDecoder::decode(data)) {
                std::printf("fast decode failed\n");
                ++g_failures;
            }
        }
        const double fastMs = double(t.nsecsElapsed()) / 1e6 / wire.size();
        std::printf("  %s (%d sections, %lld KB): serializer %.3f ms, decoder %.3f ms, %.0fx\n",
                    overworld ? "overworld" : "nether", overworld ? 24 : 16,
                    static_cast<long long>(wire.first().size() / 1024), serializerMs, fastMs,
                    serializerMs / fastMs);
    }
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool checksOnly = false;
    QString vectors;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--checks-only") == 0) {
            checksOnly = true;
        } else if (std::strcmp(argv[i], "--vectors") == 0 && i + 1 < argc) {
            vectors = QString::fromLocal8Bit(argv[++i]);
        }
    }
    checkEquivalence();
    checkHandWritten();
    checkDeclines();
    checkFuzz();
    if (!vectors.isEmpty()) {
        checkVectors(vectors);
    }
    if (!checksOnly) {
        timing();
    }
    std::printf("%s\n", g_failures ? "FAILED" : "all checks passed");
    return g_failures ? 1 : 0;
}
