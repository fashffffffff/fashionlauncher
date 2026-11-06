// SPDX-License-Identifier: GPL-3.0-only
#include "JavaInstaller.h"

#include <algorithm>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "SysInfo.h"
#include "java/download/ArchiveDownloadTask.h"
#include "java/download/ManifestDownloadTask.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "qmlui/jobs/TaskCenter.h"

#if defined(Q_OS_MACOS)
#include "java/download/SymlinkTask.h"
#include "tasks/SequentialTask.h"
#endif

namespace {
QString majorLabel(QString id)
{
    return id.startsWith("java") ? "Java " + id.mid(4) : id;
}
}  // namespace

JavaInstaller::JavaInstaller(QObject* parent) : QObject(parent) {}

QVariantList JavaInstaller::providers() const
{
    return {
        QVariantMap{ { "uid", "net.minecraft.java" }, { "name", tr("Mojang") } },
        QVariantMap{ { "uid", "net.adoptium.java" }, { "name", tr("Adoptium") } },
        QVariantMap{ { "uid", "com.azul.java" }, { "name", tr("Azul Zulu") } },
        QVariantMap{ { "uid", "com.ibm.java" }, { "name", tr("IBM Semeru") } },
    };
}

void JavaInstaller::setLoading(const QString& what, const QString& error)
{
    m_loading = what;
    m_error = error;
    emit loadingChanged();
}

QStringList JavaInstaller::recommendedMajors() const
{
    QStringList out;
    if (auto* inst = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById(m_instanceId))) {
        if (auto mc = inst->getPackProfile()->getComponent("net.minecraft")) {
            if (auto file = mc->getVersionFile()) {
                for (int major : file->compatibleJavaMajors) {
                    out << QString("java%1").arg(major);
                }
            }
        }
    }
    if (out.isEmpty()) {
        // Every Java that some Minecraft version uses (Mojang's list)
        if (auto mojang = APPLICATION->metadataIndex()->get("net.minecraft.java"); mojang && mojang->isLoaded()) {
            for (const auto& v : mojang->versions()) {
                out << v->version();
            }
        }
    }
    return out;
}

void JavaInstaller::loadMajors(const QString& uid, const QString& instanceId)
{
    m_uid = uid;
    m_instanceId = instanceId;
    m_majors.clear();
    m_builds.clear();
    m_buildList.clear();
    emit majorsChanged();
    emit buildsChanged();
    const int gen = ++m_generation;

    auto list = APPLICATION->metadataIndex()->get(uid);
    if (!list) {
        setLoading({}, tr("No Java versions are currently available in the meta."));
        return;
    }
    // Recommendations without an instance come from Mojang's list, so make sure it is there too.
    if (instanceId.isEmpty() && uid != "net.minecraft.java") {
        if (auto mojang = APPLICATION->metadataIndex()->get("net.minecraft.java"); mojang && !mojang->isLoaded()) {
            if (auto t = mojang->getLoadTask()) {
                connect(t.get(), &Task::succeeded, this, [this, gen] {
                    if (gen == m_generation) {
                        fillMajors();
                    }
                });
                if (!t->isRunning()) {
                    t->start();
                }
            }
        }
    }
    if (list->isLoaded()) {
        fillMajors();
        return;
    }
    setLoading("majors");
    m_loadTask = list->getLoadTask();
    connect(m_loadTask.get(), &Task::succeeded, this, [this, gen] {
        if (gen == m_generation) {
            setLoading({});
            fillMajors();
        }
    });
    connect(m_loadTask.get(), &Task::failed, this, [this, gen](const QString& reason) {
        if (gen == m_generation) {
            setLoading({}, tr("Couldn't load the Java version list: %1").arg(reason));
        }
    });
    if (!m_loadTask->isRunning()) {
        m_loadTask->start();
    }
}

void JavaInstaller::fillMajors()
{
    auto list = APPLICATION->metadataIndex()->get(m_uid);
    if (!list) {
        return;
    }
    const QStringList recommended = recommendedMajors();
    QVariantList out;
    for (const auto& v : list->versions()) {
        out << QVariantMap{ { "id", v->version() }, { "label", majorLabel(v->version()) }, { "recommended", recommended.contains(v->version()) } };
    }
    // Newest first
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["id"].toString().mid(4).toInt() > b.toMap()["id"].toString().mid(4).toInt();
    });
    m_majors = out;
    emit majorsChanged();
    if (out.isEmpty()) {
        setLoading({}, tr("No Java versions are currently available in the meta."));
    }
}

void JavaInstaller::loadBuilds(const QString& majorId)
{
    m_majorId = majorId;
    m_builds.clear();
    m_buildList.clear();
    emit buildsChanged();
    const int gen = ++m_generation;

    auto list = APPLICATION->metadataIndex()->get(m_uid);
    Meta::Version::Ptr version = list ? list->getVersion(majorId) : nullptr;
    if (!version) {
        setLoading({}, tr("This Java version is no longer available."));
        return;
    }
    if (version->isLoaded()) {
        setLoading({});
        fillBuilds();
        return;
    }
    setLoading("builds");
    m_loadTask = version->loadTask(Net::Mode::Online);
    connect(m_loadTask.get(), &Task::succeeded, this, [this, gen] {
        if (gen == m_generation) {
            setLoading({});
            fillBuilds();
        }
    });
    connect(m_loadTask.get(), &Task::failed, this, [this, gen](const QString& reason) {
        if (gen == m_generation) {
            setLoading({}, tr("Couldn't load the builds: %1").arg(reason));
        }
    });
    if (!m_loadTask->isRunning()) {
        m_loadTask->start();
    }
}

void JavaInstaller::fillBuilds()
{
    auto list = APPLICATION->metadataIndex()->get(m_uid);
    auto version = list ? list->getVersion(m_majorId) : nullptr;
    if (!version || !version->data()) {
        return;
    }
    const QString arch = SysInfo::getSupportedJavaArchitecture();
    m_buildList.clear();
    for (const auto& rt : version->data()->runtimes) {
        if (rt->runtimeOS == arch) {
            m_buildList << rt;
        }
    }
    std::sort(m_buildList.begin(), m_buildList.end(), [](const Java::MetadataPtr& a, const Java::MetadataPtr& b) { return *b < *a; });
    QVariantList out;
    for (int i = 0; i < m_buildList.size(); i++) {
        const auto& rt = m_buildList[i];
        out << QVariantMap{ { "index", i },
                            { "name", rt->m_name },
                            { "version", rt->version.toString() },
                            { "vendor", rt->vendor },
                            { "type", rt->packageType },
                            { "date", rt->releaseTime.date().toString(Qt::ISODate) } };
    }
    m_builds = out;
    emit buildsChanged();
    if (out.isEmpty()) {
        setLoading({}, tr("No builds of this Java for your computer (%1).").arg(arch));
    }
}

bool JavaInstaller::install(int index)
{
    if (index < 0 || index >= m_buildList.size()) {
        return false;
    }
    const auto meta = m_buildList[index];
    const QString finalPath = FS::PathCombine(APPLICATION->javaPath(), meta->m_name);
    Task::Ptr task;
    switch (meta->downloadType) {
        case Java::DownloadType::Manifest:
            task = makeShared<Java::ManifestDownloadTask>(meta->url, finalPath, meta->checksumType, meta->checksumHash);
            break;
        case Java::DownloadType::Archive:
            task = makeShared<Java::ArchiveDownloadTask>(meta->url, finalPath, meta->checksumType, meta->checksumHash);
            break;
        case Java::DownloadType::Unknown:
            emit installFinished(false, tr("Could not determine Java download type!"));
            return false;
    }
#if defined(Q_OS_MACOS)
    auto seq = makeShared<SequentialTask>(tr("Install Java"));
    seq->addTask(task);
    seq->addTask(makeShared<Java::SymlinkTask>(finalPath));
    task = seq;
#endif
    const QString title = tr("Installing %1").arg(meta->m_name);
    connect(task.get(), &Task::finished, this, [this, task = task.get(), finalPath, name = meta->m_name] {
        m_installing--;
        emit installingChanged();
        if (task->wasSuccessful()) {
            emit installFinished(true, tr("%1 is installed.").arg(name));
        } else {
            FS::deletePath(finalPath);
            emit installFinished(false, task->getState() == Task::State::AbortedByUser
                                            ? tr("Java download cancelled.")
                                            : tr("Java download failed: %1").arg(task->failReason()));
        }
    });
    m_installing++;
    emit installingChanged();
    TaskCenter::instance()->track(task, title, "java");
    task->start();
    return true;
}
