// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QHash>
#include <QIODevice>
#include <QStringList>

/**
 * Reads single small files out of a zip/jar without walking the whole archive: it parses only the
 * central directory at the end of the file, then seeks straight to the entries it is asked for.
 * Used by the pre-launch checks, where a mods folder with hundreds of jars has to be read in well
 * under a second. Handles stored and deflated entries and zip64 offsets; anything else reads as empty.
 */
class ZipPeek {
   public:
    /// The device must stay open (and seekable) while this object is used.
    bool open(QIODevice* device);

    QStringList names() const { return m_order; }
    bool contains(const QString& name) const { return m_entries.contains(name); }
    /// Contents of one entry, or an empty array when it is missing, too big or unreadable.
    QByteArray read(const QString& name, qint64 maxSize = 16 * 1024 * 1024) const;

   private:
    struct Entry {
        quint16 method = 0;
        quint64 compressed = 0;
        quint64 size = 0;
        quint64 localOffset = 0;
    };
    QIODevice* m_dev = nullptr;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
};
