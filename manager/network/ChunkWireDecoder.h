#ifndef CHUNKWIREDECODER_H
#define CHUNKWIREDECODER_H

#include "world.qpb.h"

#include <QByteArray>
#include <optional>

// A ClientToManagerMessage carrying chunk_data, read straight off the wire. Every bot's
// messages are decoded on the GUI thread, and QProtobufSerializer's per-field reflection
// costs 2-3 ms a chunk there: a few bots flying over new ground fill the thread, and every
// bot's other messages wait behind their chunks. This reads the same message in a fraction
// of that, into the same ChunkDataMessage the serializer would build.
namespace ChunkWireDecoder {

// The chunk_data payload, or nullopt for any other message and for anything not read
// exactly as the format says (unknown fields, wire types the schema does not give a field,
// truncation): the caller hands those to QProtobufSerializer, so nullopt never loses one.
std::optional<mankool::mcbot::protocol::ChunkDataMessage> decode(const QByteArray &message);

}

#endif // CHUNKWIREDECODER_H
