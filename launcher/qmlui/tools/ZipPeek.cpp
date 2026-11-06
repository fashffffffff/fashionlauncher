// SPDX-License-Identifier: GPL-3.0-only
#include "ZipPeek.h"

#include <zlib.h>

namespace {
quint16 u16(const char* p)
{
    return static_cast<quint16>(static_cast<quint8>(p[0]) | (static_cast<quint8>(p[1]) << 8));
}
quint32 u32(const char* p)
{
    return static_cast<quint32>(u16(p)) | (static_cast<quint32>(u16(p + 2)) << 16);
}
quint64 u64(const char* p)
{
    return static_cast<quint64>(u32(p)) | (static_cast<quint64>(u32(p + 4)) << 32);
}
}  // namespace

bool ZipPeek::open(QIODevice* device)
{
    m_dev = device;
    m_entries.clear();
    m_order.clear();
    if (!m_dev || !m_dev->isOpen() || m_dev->isSequential()) {
        return false;
    }
    const qint64 fileSize = m_dev->size();
    if (fileSize < 22) {
        return false;
    }
    // End of central directory: within the last 64 KiB + 22 bytes (the comment can be up to 64 KiB)
    const qint64 tailSize = qMin<qint64>(fileSize, 65535 + 22);
    if (!m_dev->seek(fileSize - tailSize)) {
        return false;
    }
    const QByteArray tail = m_dev->read(tailSize);
    if (tail.size() != tailSize) {
        return false;
    }
    qint64 eocd = -1;
    for (qint64 i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail.constData() + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return false;
    }
    const char* e = tail.constData() + eocd;
    quint64 count = u16(e + 10);
    quint64 cdSize = u32(e + 12);
    quint64 cdOffset = u32(e + 16);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF) {
        // zip64: the locator sits right before the classic record
        if (eocd < 20 || u32(e - 20) != 0x07064b50) {
            return false;
        }
        const quint64 recordOffset = u64(e - 20 + 8);
        if (!m_dev->seek(static_cast<qint64>(recordOffset))) {
            return false;
        }
        const QByteArray rec = m_dev->read(56);
        if (rec.size() != 56 || u32(rec.constData()) != 0x06064b50) {
            return false;
        }
        count = u64(rec.constData() + 32);
        cdSize = u64(rec.constData() + 40);
        cdOffset = u64(rec.constData() + 48);
    }
    if (cdSize > 256ull * 1024 * 1024 || cdOffset + cdSize > static_cast<quint64>(fileSize) || !m_dev->seek(static_cast<qint64>(cdOffset))) {
        return false;
    }
    const QByteArray cd = m_dev->read(static_cast<qint64>(cdSize));
    if (static_cast<quint64>(cd.size()) != cdSize) {
        return false;
    }
    const char* p = cd.constData();
    const char* end = p + cd.size();
    m_entries.reserve(static_cast<qsizetype>(qMin<quint64>(count, 100000)));
    while (p + 46 <= end && u32(p) == 0x02014b50) {
        const quint16 nameLen = u16(p + 28);
        const quint16 extraLen = u16(p + 30);
        const quint16 commentLen = u16(p + 32);
        if (p + 46 + nameLen + extraLen + commentLen > end) {
            break;
        }
        Entry entry;
        entry.method = u16(p + 10);
        entry.compressed = u32(p + 20);
        entry.size = u32(p + 24);
        entry.localOffset = u32(p + 42);
        // zip64 extra field: only the values that overflowed are present, in this order
        const char* x = p + 46 + nameLen;
        const char* xEnd = x + extraLen;
        while (x + 4 <= xEnd) {
            const quint16 id = u16(x);
            const quint16 len = u16(x + 2);
            const char* d = x + 4;
            if (d + len > xEnd) {
                break;
            }
            if (id == 0x0001) {
                const char* q = d;
                if (entry.size == 0xFFFFFFFF && q + 8 <= d + len) {
                    entry.size = u64(q);
                    q += 8;
                }
                if (entry.compressed == 0xFFFFFFFF && q + 8 <= d + len) {
                    entry.compressed = u64(q);
                    q += 8;
                }
                if (entry.localOffset == 0xFFFFFFFF && q + 8 <= d + len) {
                    entry.localOffset = u64(q);
                }
            }
            x = d + len;
        }
        const QString name = QString::fromUtf8(p + 46, nameLen);
        if (!m_entries.contains(name)) {
            m_order.append(name);
        }
        m_entries.insert(name, entry);
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray ZipPeek::read(const QString& name, qint64 maxSize) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd() || !m_dev) {
        return {};
    }
    const Entry& entry = *it;
    if (entry.size > static_cast<quint64>(maxSize) || entry.compressed > static_cast<quint64>(maxSize)) {
        return {};
    }
    if (!m_dev->seek(static_cast<qint64>(entry.localOffset))) {
        return {};
    }
    const QByteArray local = m_dev->read(30);
    if (local.size() != 30 || u32(local.constData()) != 0x04034b50) {
        return {};
    }
    const qint64 dataStart = static_cast<qint64>(entry.localOffset) + 30 + u16(local.constData() + 26) + u16(local.constData() + 28);
    if (!m_dev->seek(dataStart)) {
        return {};
    }
    const QByteArray raw = m_dev->read(static_cast<qint64>(entry.compressed));
    if (static_cast<quint64>(raw.size()) != entry.compressed) {
        return {};
    }
    if (entry.method == 0) {
        return raw;
    }
    if (entry.method != 8) {
        return {};
    }
    QByteArray out;
    out.resize(static_cast<qsizetype>(entry.size));
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return {};
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
    zs.avail_in = static_cast<uInt>(raw.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) {
        return {};
    }
    out.resize(static_cast<qsizetype>(zs.total_out));
    return out;
}
