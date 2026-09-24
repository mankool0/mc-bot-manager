#ifndef SECTIONCODECPRIMITIVES_H
#define SECTIONCODECPRIMITIVES_H

#include <QtGlobal>

// Little-endian primitives for SectionCodec's encodings, written through a raw
// pointer into a pre-sized buffer (QByteArray::append per byte costs more than
// hashing the result, see encodeBlob). Signed fields are written as two's
// complement, which is byte-identical to the unsigned write - putI32le exists
// so the code reads like the format spec rather than leaving the reader to work
// that out.

constexpr int kSectionCells = 4096;  // 16x16x16

inline char *putU16le(char *p, quint16 v)
{
    p[0] = static_cast<char>(v & 0xff);
    p[1] = static_cast<char>((v >> 8) & 0xff);
    return p + 2;
}

inline char *putU32le(char *p, quint32 v)
{
    p[0] = static_cast<char>(v & 0xff);
    p[1] = static_cast<char>((v >> 8) & 0xff);
    p[2] = static_cast<char>((v >> 16) & 0xff);
    p[3] = static_cast<char>((v >> 24) & 0xff);
    return p + 4;
}

inline char *putI32le(char *p, qint32 v)
{
    return putU32le(p, static_cast<quint32>(v));
}

#endif // SECTIONCODECPRIMITIVES_H
