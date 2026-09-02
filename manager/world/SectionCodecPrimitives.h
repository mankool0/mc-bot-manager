#ifndef SECTIONCODECPRIMITIVES_H
#define SECTIONCODECPRIMITIVES_H

#include <QByteArray>

// Little-endian primitives for SectionCodec's encodings. The append* forms grow
// a QByteArray; the put* forms write through a raw pointer into a pre-sized one
// (QByteArray::append per byte costs more than hashing the result, see
// encodeBlob). Signed fields are written as two's complement, which is
// byte-identical to the unsigned write - appendI32le exists so the code reads
// like the format spec rather than leaving the reader to work that out.

constexpr int kSectionCells = 4096;  // 16x16x16

inline void appendU16le(QByteArray &out, quint16 v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
}

inline void appendU32le(QByteArray &out, quint32 v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
    out.append(static_cast<char>((v >> 16) & 0xff));
    out.append(static_cast<char>((v >> 24) & 0xff));
}

inline void appendI32le(QByteArray &out, qint32 v)
{
    appendU32le(out, static_cast<quint32>(v));
}

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

#endif // SECTIONCODECPRIMITIVES_H
