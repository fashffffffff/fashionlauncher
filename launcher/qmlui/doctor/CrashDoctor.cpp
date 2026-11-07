// SPDX-License-Identifier: GPL-3.0-only
#include "CrashDoctor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include "Application.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "ui/GuiUtil.h"
#include "InstanceList.h"
#include "LaunchController.h"
#include "launch/LaunchTask.h"
#include "launch/LogModel.h"
#include "minecraft/MinecraftInstance.h"
#include "modplatform/helpers/HashUtils.h"
#include "modplatform/packwiz/Packwiz.h"
#include "net/ApiRequest.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"
#include "qmlui/doctor/CrashRules.h"
#include "qmlui/jobs/TaskCenter.h"
#include "qmlui/resources/SearchModel.h"

namespace {

MinecraftInstance* findInstance(const QString& id)
{
    return dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById(id));
}

QString readFile(const QString& path, qint64 maxBytes = 24 * 1024 * 1024)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Huge logs: the end is where the crash is.
    if (f.size() > maxBytes) {
        f.seek(f.size() - maxBytes);
    }
    return QString::fromUtf8(f.readAll());
}

/// The newest crash report written at or after `since`, "" if none
QString freshCrashReport(MinecraftInstance* inst, const QDateTime& since)
{
    const auto reports = QDir(FS::PathCombine(inst->gameRoot(), "crash-reports")).entryInfoList({ "*.txt" }, QDir::Files, QDir::Time);
    if (!reports.isEmpty() && reports.first().lastModified() >= since) {
        return reports.first().absoluteFilePath();
    }
    return {};
}

/// Loader-provided or built-in ids that are not installable mods
bool isPlatformId(const QString& id)
{
    static const QStringList ids = { "minecraft", "java", "forge", "neoforge", "fml", "fabricloader", "fabric-loader", "quilt_loader",
                                     "quilt-loader", "javafml", "lowcodefml", "mcp" };
    return ids.contains(id);
}

/// Metadata (.index/<slug>.pw.toml) of the mod whose file is `fileName`
Packwiz::V1::Mod metadataFor(MinecraftInstance* inst, const QString& fileName)
{
    const QDir indexDir(QDir(inst->modsRoot()).filePath(".index"));
    for (const auto& fi : indexDir.entryInfoList({ "*.pw.toml" }, QDir::Files)) {
        QString slug = fi.fileName();
        slug.chop(8);
        auto mod = Packwiz::V1::getIndexForMod(indexDir, slug);
        if (mod.filename == fileName && mod.url.isValid()) {
            return mod;
        }
    }
    return {};
}

}  // namespace

CrashDoctor* CrashDoctor::instance()
{
    static QPointer<CrashDoctor> s;
    if (!s) {
        s = new CrashDoctor(QCoreApplication::instance());
    }
    return s;
}

CrashDoctor::CrashDoctor(QObject* parent) : QObject(parent)
{
    connect(APPLICATION, &Application::launchStarted, this, &CrashDoctor::watch);
}

void CrashDoctor::watch(MinecraftInstance* instance, LaunchController* controller)
{
    if (!instance || !controller) {
        return;
    }
    const QString id = instance->id();
    const QDateTime started = QDateTime::currentDateTime().addSecs(-2);
    connect(instance, &BaseInstance::launchTaskChanged, controller, [this, id, started](LaunchTask* task) {
        if (!task) {
            return;
        }
        connect(task, &Task::failed, this, [this, id, started, task](const QString&) {
            auto log = task->getLogModel();
            if (!log) {
                return;
            }
            const QString text = log->toPlainText();
            // Only a game that was really running: failures before it (accounts, downloads) have their own messages.
            if (!text.contains(QStringLiteral("Minecraft process ID: "))) {
                return;
            }
            // Stopped from the launcher: not a crash.
            // (variables, so lupdate doesn't copy core's string into the QML shell's translations)
            static const char* ctx = "LoggedProcess";
            static const char* msg = "Process was killed by user.";
            const QString killed = QCoreApplication::translate(ctx, msg);
            if (text.contains(killed) || text.contains(QStringLiteral("Process was killed by user."))) {
                return;
            }
            QString full = text;
            QString logFile;
            if (auto* inst = findInstance(id)) {
                logFile = freshCrashReport(inst, started);
                if (!logFile.isEmpty()) {
                    full += '\n' + readFile(logFile);
                }
            }
            m_pending = build(id, full, "crash", logFile);
            emit pendingChanged();
            emit crashed(id);
        });
    });
}

QString CrashDoctor::liveLog(const QString& instanceId) const
{
    auto inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        return {};
    }
    if (auto* task = inst->getLaunchTask()) {
        if (auto log = task->getLogModel()) {
            return log->toPlainText();
        }
    }
    return {};
}

bool CrashDoctor::takePending()
{
    if (m_pending.isEmpty()) {
        return false;
    }
    m_report = m_pending;
    m_pending.clear();
    emit reportChanged();
    emit pendingChanged();
    return true;
}

bool CrashDoctor::analyzeInstance(const QString& instanceId)
{
    auto* inst = findInstance(instanceId);
    if (!inst) {
        return false;
    }
    QString text = liveLog(instanceId);
    QString logFile;
    const QFileInfo latest(FS::PathCombine(inst->gameRoot(), "logs/latest.log"));
    if (text.isEmpty() && latest.exists()) {
        text = readFile(latest.absoluteFilePath());
        logFile = latest.absoluteFilePath();
    }
    // A crash report from the same session adds the "Description" and full trace.
    const QString report = freshCrashReport(inst, latest.exists() ? latest.lastModified().addSecs(-120) : QDateTime());
    if (!report.isEmpty()) {
        text += '\n' + readFile(report);
        logFile = report;
    }
    if (text.trimmed().isEmpty()) {
        return false;
    }
    m_report = build(instanceId, text, "log", logFile);
    emit reportChanged();
    return true;
}

bool CrashDoctor::analyzeFile(const QString& instanceId, const QString& path)
{
    const QString text = readFile(path);
    if (text.isEmpty()) {
        return false;
    }
    m_report = build(instanceId, text, "crash", path);
    emit reportChanged();
    return true;
}

QVariantMap CrashDoctor::build(const QString& instanceId, const QString& text, const QString& source, const QString& logFile) const
{
    const auto diagnosis = CrashRules::diagnose(text);
    auto* inst = findInstance(instanceId);
    const QDir mods(inst ? inst->modsRoot() : QString());

    QVariantList findings;
    for (const auto& f : diagnosis.findings) {
        QVariantMap m = CrashRules::toVariant(f);
        QString fix;
        if (f.kind == "missing-dependency") {
            fix = isPlatformId(f.dependency) ? "" : "install";
        } else if (f.kind == "wrong-dependency" || f.kind == "incompatible" || f.kind == "duplicate") {
            fix = f.dependency == "minecraft" ? "" : "mods";
        } else if (f.kind.startsWith("memory")) {
            fix = "memory";
        } else if (f.kind == "java") {
            fix = "java";
        } else if (f.kind == "corrupt-file" && inst && !f.files.isEmpty()) {
            const QString file = f.files.first();
            const bool exists = mods.exists(file);
            m["fileExists"] = exists;
            if (exists) {
                fix = metadataFor(inst, file).url.isValid() ? "reinstall" : "trash";
            }
        }
        m["fix"] = fix;
        findings << m;
    }
    QVariantMap out = CrashRules::toVariant(diagnosis);
    out["findings"] = findings;
    out["instanceId"] = instanceId;
    out["instanceName"] = inst ? inst->name() : instanceId;
    out["source"] = source;
    out["logFile"] = logFile;
    out["time"] = QDateTime::currentDateTime();
    return out;
}

void CrashDoctor::copyText(const QString& text) const
{
    GuiUtil::setClipboardText(text);
}

// ---- fixes -----------------------------------------------------------------------------------

void CrashDoctor::installDependency(const QString& instanceId, const QString& modId)
{
    const QString key = "install:" + modId;
    if (!findInstance(instanceId) || modId.isEmpty() || isPlatformId(modId)) {
        emit fixFinished(key, false, tr("This can't be installed automatically."));
        return;
    }
    // Reuse the Mods tab's search + install (instance's Minecraft version and loader, dependencies included).
    // Only a project whose slug IS the id the loader named counts: a similar name is a guess.
    auto* search = new SearchModel(this);
    search->setInstanceId(instanceId);
    search->setKind("mods");
    search->setPlatform("modrinth");
    search->setQuery(modId);
    QStringList slugs = { modId.toLower() };
    if (modId.contains('_')) {
        slugs << modId.toLower().replace('_', '-');
    }
    auto installing = std::make_shared<bool>(false);
    connect(search, &SearchModel::busyChanged, this, [this, search, slugs, key, modId, installing] {
        if (search->busy() || *installing) {
            return;
        }
        if (!search->error().isEmpty()) {
            emit fixFinished(key, false, search->error());
            search->deleteLater();
            return;
        }
        int row = -1;
        for (int i = 0; i < search->rowCount() && row < 0; i++) {
            const QString url = search->data(search->index(i), SearchModel::UrlRole).toString();
            for (const auto& slug : slugs) {
                if (url == "https://modrinth.com/mod/" + slug) {
                    row = i;
                }
            }
        }
        if (row < 0) {
            emit fixFinished(key, false, tr("Modrinth has no mod with the id “%1”. Search for it in the mods tab.").arg(modId));
            search->deleteLater();
            return;
        }
        *installing = true;
        connect(search, &SearchModel::message, this, [this, search, key](const QString& text, bool isError) {
            emit fixFinished(key, !isError, text);
            search->deleteLater();
        });
        search->install(row);
    });
    search->search();
}

void CrashDoctor::reinstallFile(const QString& instanceId, const QString& fileName)
{
    const QString key = "reinstall:" + fileName;
    auto* inst = findInstance(instanceId);
    if (!inst) {
        emit fixFinished(key, false, tr("The instance is gone."));
        return;
    }
    const auto meta = metadataFor(inst, fileName);
    if (!meta.url.isValid()) {
        emit fixFinished(key, false, tr("There is no download address for this file."));
        return;
    }
    const QString path = QDir(inst->modsRoot()).absoluteFilePath(fileName);
    auto job = NetJob::Ptr(new NetJob(tr("Reinstall %1").arg(fileName), APPLICATION->network()));
    auto action = Net::ApiRequest::makeFile(meta.url, path);
    QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha1;
    bool check = !meta.hash.isEmpty();
    switch (Hashing::algorithmFromString(meta.hashFormat)) {
        case Hashing::Algorithm::Md5:
            algorithm = QCryptographicHash::Md5;
            break;
        case Hashing::Algorithm::Sha1:
            algorithm = QCryptographicHash::Sha1;
            break;
        case Hashing::Algorithm::Sha256:
            algorithm = QCryptographicHash::Sha256;
            break;
        case Hashing::Algorithm::Sha512:
            algorithm = QCryptographicHash::Sha512;
            break;
        default:
            check = false;  // murmur2 (CurseForge fingerprints) can't be checked by a stream validator
            break;
    }
    if (check) {
        action->addValidator(new Net::ChecksumValidator(algorithm, meta.hash));
    }
    job->addNetAction(action);
    connect(job.get(), &Task::succeeded, this, [this, key, fileName] { emit fixFinished(key, true, tr("Downloaded “%1” again.").arg(fileName)); });
    connect(job.get(), &Task::failed, this, [this, key](const QString& reason) { emit fixFinished(key, false, reason); });
    TaskCenter::instance()->track(job, tr("Reinstalling %1").arg(meta.name.isEmpty() ? fileName : meta.name), "resources", instanceId);
    job->start();
}

bool CrashDoctor::trashFile(const QString& instanceId, const QString& fileName)
{
    auto* inst = findInstance(instanceId);
    if (!inst || fileName.contains('/') || fileName.contains('\\')) {
        return false;
    }
    const QString path = QDir(inst->modsRoot()).absoluteFilePath(fileName);
    return QFileInfo::exists(path) && (FS::trash(path) || QFile::remove(path));
}
