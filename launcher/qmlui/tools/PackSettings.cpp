// SPDX-License-Identifier: GPL-3.0-only
#include "PackSettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include "BaseInstance.h"
#include "HardwareInfo.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"
#include "settings/SettingsObject.h"

#include "ZipPeek.h"

namespace PackSettings {
namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("ToolsBridge", text);
}
}  // namespace

QJsonObject readFromArchive(const QString& archivePath)
{
    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    ZipPeek zip;
    if (!zip.open(&file) || !zip.contains(FileName)) {
        return {};
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(zip.read(FileName, 256 * 1024), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[tools]" << FileName << "in" << archivePath << "isn't valid JSON:" << err.errorString();
        return {};
    }
    return doc.object();
}

QString formatMemory(int mib)
{
    if (mib < 1024) {
        return tr("%1 MB").arg(mib);
    }
    const double gb = mib / 1024.0;
    return tr("%1 GB").arg(QString::number(gb, 'f', qFuzzyCompare(gb, std::round(gb)) ? 0 : 1));
}

QString apply(BaseInstance* inst, const QJsonObject& s)
{
    if (!inst || s.isEmpty()) {
        return {};
    }
    auto* settings = inst->settings();
    QStringList said;
    QStringList noteLines;

    const QJsonObject memory = s.value("memory").toObject();
    int maxMiB = memory.value("max").toInt(0);
    int minMiB = memory.value("min").toInt(0);
    if (maxMiB > 0) {
        // Never ask for more than the computer can give (same limit Prism uses for CurseForge packs)
        const int cap = static_cast<int>(static_cast<double>(HardwareInfo::totalRamMiB()) * 0.9);
        bool capped = false;
        if (cap > 0 && maxMiB > cap) {
            maxMiB = cap;
            capped = true;
        }
        maxMiB = qMax(maxMiB, 512);
        minMiB = minMiB > 0 ? qBound(256, minMiB, maxMiB) : qMin(512, maxMiB);
        settings->set("OverrideMemory", true);
        settings->set("MinMemAlloc", minMiB);
        settings->set("MaxMemAlloc", maxMiB);
        said << (capped ? tr("This modpack asked for more memory than this computer has, so it got %1.").arg(formatMemory(maxMiB))
                        : tr("This modpack set %1 of memory.").arg(formatMemory(maxMiB)));
        noteLines << tr("Memory: %1 (set by the modpack)").arg(formatMemory(maxMiB));
    }

    const QString jvmArgs = s.value("jvmArgs").toString().simplified();
    if (!jvmArgs.isEmpty() && jvmArgs.size() <= 4096) {
        settings->set("OverrideJavaArgs", true);
        settings->set("JvmArgs", jvmArgs);
        said << tr("It also added its own Java arguments.");
        noteLines << tr("Java arguments: %1 (set by the modpack)").arg(jvmArgs);
    }

    const QString notes = s.value("notes").toString().trimmed();
    if (!notes.isEmpty()) {
        noteLines.prepend(notes.left(8000));
    }
    if (!noteLines.isEmpty()) {
        const QString old = inst->notes();
        inst->setNotes(old.isEmpty() ? noteLines.join('\n') : old + "\n\n" + noteLines.join('\n'));
    }
    return said.join(' ');
}

QJsonObject fromInstance(BaseInstance* inst)
{
    QJsonObject out;
    if (!inst) {
        return out;
    }
    auto* settings = inst->settings();
    if (settings->get("OverrideMemory").toBool()) {
        out["memory"] = QJsonObject{ { "min", settings->get("MinMemAlloc").toInt() }, { "max", settings->get("MaxMemAlloc").toInt() } };
    }
    if (settings->get("OverrideJavaArgs").toBool()) {
        const QString args = settings->get("JvmArgs").toString().simplified();
        if (!args.isEmpty()) {
            out["jvmArgs"] = args;
        }
    }
    return out;
}

QString writeIntoArchive(const QString& archivePath, const QJsonObject& settings)
{
    if (!QFileInfo::exists(archivePath)) {
        return tr("The file %1 doesn't exist.").arg(archivePath);
    }
    const QString temp = archivePath + ".fashion-tmp";
    QFile::remove(temp);
    {
        MMCZip::ArchiveWriter writer(temp);
        if (auto r = writer.open(); !r) {
            return r.error();
        }
        MMCZip::ArchiveReader reader(archivePath);
        auto copied = reader.parse([&writer](MMCZip::ArchiveReader::File* f) -> Result<> {
            if (f->filename() == FileName) {
                return f->skip();
            }
            return writer.addFile(f);
        });
        if (!copied) {
            [[maybe_unused]] const auto _ = writer.close();
            QFile::remove(temp);
            return copied.error();
        }
        if (auto r = writer.addFile(FileName, QJsonDocument(settings).toJson(QJsonDocument::Indented)); !r) {
            [[maybe_unused]] const auto _ = writer.close();
            QFile::remove(temp);
            return r.error();
        }
        if (auto r = writer.close(); !r) {
            QFile::remove(temp);
            return r.error();
        }
    }
    QFile::remove(archivePath);
    if (!QFile::rename(temp, archivePath)) {
        return tr("Couldn't replace %1.").arg(archivePath);
    }
    return {};
}

}  // namespace PackSettings
