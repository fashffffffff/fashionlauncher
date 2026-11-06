// SPDX-License-Identifier: GPL-3.0-only
#include "SkinsBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlEngine>
#include <QRegularExpression>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "Json.h"
#include "SkinScene.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/Parsers.h"
#include "minecraft/skins/SkinList.h"
#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinRequests.h"
#include "net/RPCSink.h"
#include "net/Request.h"
#include "settings/SettingsObject.h"

static MinecraftAccountPtr findAccount(const QString& id)
{
    auto* list = APPLICATION->accounts();
    for (int i = 0; i < list->count(); i++) {
        if (list->at(i)->internalId() == id)
            return list->at(i);
    }
    return nullptr;
}

SkinsBridge::SkinsBridge(QObject* parent) : QObject(parent)
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        qmlRegisterType<SkinBox>("FFPrism.Skins", 1, 0, "SkinBox");
        qmlRegisterType<SkinTexture>("FFPrism.Skins", 1, 0, "SkinTexture");
    }

    connect(APPLICATION->accounts(), &AccountList::listChanged, this, [this] {
        emit accountsChanged();
        // The selected account may be gone, or its profile (skin, capes) may have been refreshed
        if (!findAccount(m_accountId))
            m_accountId.clear();
        emit accountChanged();
        rebuildSkins();
        rebuildCapes();
    });
    // The library is built on first use (the Skins page), not at startup.
}

SkinsBridge::~SkinsBridge() = default;

QVariantList SkinsBridge::accounts() const
{
    QVariantList out;
    auto* list = APPLICATION->accounts();
    for (int i = 0; i < list->count(); i++) {
        auto a = list->at(i);
        out << QVariantMap{ { "id", a->internalId() },
                            { "name", a->profileName().isEmpty() ? a->displayName() : a->profileName() },
                            { "microsoft", a->accountType() == AccountType::MSA },
                            { "owns", a->ownsMinecraft() } };
    }
    return out;
}

MinecraftAccountPtr SkinsBridge::account() const
{
    auto* list = APPLICATION->accounts();
    if (!m_accountId.isEmpty()) {
        if (auto a = findAccount(m_accountId))
            return a;
    }
    if (auto a = list->defaultAccount())
        return a;
    if (list->count() > 0)
        return list->at(0);
    return nullptr;
}

QString SkinsBridge::accountId() const
{
    auto a = account();
    return a ? a->internalId() : QString();
}

void SkinsBridge::setAccountId(const QString& id)
{
    if (id == accountId())
        return;
    m_accountId = id;
    rebuildList();
    emit accountChanged();
}

QString SkinsBridge::accountName() const
{
    auto a = account();
    return a ? (a->profileName().isEmpty() ? a->displayName() : a->profileName()) : QString();
}

bool SkinsBridge::microsoft() const
{
    auto a = account();
    return a && a->accountType() == AccountType::MSA;
}

bool SkinsBridge::canUpload() const
{
    auto a = account();
    return a && a->accountType() == AccountType::MSA && a->ownsMinecraft() && a->hasProfile();
}

QString SkinsBridge::currentCape() const
{
    auto a = account();
    return a ? a->accountData()->minecraftProfile.currentCape : QString();
}

QString SkinsBridge::skinsFolder() const
{
    return QDir(APPLICATION->settings()->get("SkinsDir").toString()).absolutePath();
}

void SkinsBridge::rebuildList()
{
    auto a = account();
    if (!a) {
        if (!m_fallback)
            m_fallback = MinecraftAccount::createOffline("Player");
        a = m_fallback;
    }
    m_list.reset(new SkinList(nullptr, skinsFolder(), a));
    auto* l = m_list.get();
    connect(l, &QAbstractItemModel::modelReset, this, &SkinsBridge::rebuildSkins);
    connect(l, &QAbstractItemModel::rowsInserted, this, &SkinsBridge::rebuildSkins);
    connect(l, &QAbstractItemModel::rowsRemoved, this, &SkinsBridge::rebuildSkins);
    connect(l, &QAbstractItemModel::dataChanged, this, &SkinsBridge::rebuildSkins);
    l->startWatching();
    rebuildSkins();
    rebuildCapes();
}

void SkinsBridge::load()
{
    if (!m_list)
        rebuildList();
    else
        rebuildCapes();
}

void SkinsBridge::rebuildSkins()
{
    if (!m_list) {
        // First access from QML builds the library
        rebuildList();
        return;
    }
    auto a = account();
    const bool hasAccountSkin = a && !a->accountData()->minecraftProfile.skin.url.isEmpty();
    const int inUse = hasAccountSkin ? m_list->getSelectedAccountSkin() : -1;
    QVariantList out;
    for (int i = 0; i < m_list->rowCount(); i++) {
        auto key = m_list->data(m_list->index(i), Qt::DisplayRole).toString();
        auto* s = m_list->skin(key);
        if (!s)
            continue;
        out << QVariantMap{ { "key", key },
                            { "name", s->name() },
                            { "path", s->getPath() },
                            { "slim", s->getModel() == SkinModel::SLIM },
                            { "capeId", s->getCapeId() },
                            { "inUse", i == inUse } };
    }
    // Account's skin first, then by name
    std::stable_sort(out.begin(), out.end(), [](const QVariant& x, const QVariant& y) {
        auto a = x.toMap(), b = y.toMap();
        if (a["inUse"].toBool() != b["inUse"].toBool())
            return a["inUse"].toBool();
        return a["name"].toString().compare(b["name"].toString(), Qt::CaseInsensitive) < 0;
    });
    m_skins = out;
    emit skinsChanged();
}

void SkinsBridge::rebuildCapes()
{
    QVariantList out;
    auto a = account();
    if (!a || a->accountType() != AccountType::MSA) {
        m_capes = out;
        emit capesChanged();
        return;
    }
    const auto capesDir = FS::PathCombine(skinsFolder(), "capes");
    FS::ensureFolderPathExists(capesDir);
    NetJob::Ptr job;
    for (const auto& cape : a->accountData()->minecraftProfile.capes) {
        const auto path = FS::PathCombine(capesDir, cape.id + ".png");
        if (!QFileInfo::exists(path)) {
            QImage img;
            if (!cape.data.isEmpty() && img.loadFromData(cape.data, "PNG")) {
                img.save(path);
            } else if (!cape.url.isEmpty() && !m_capeJob) {
                if (!job)
                    job.reset(new NetJob(tr("Download capes"), APPLICATION->network()));
                job->addNetAction(Net::Request::makeFile(cape.url, path));
            }
        }
        out << QVariantMap{ { "id", cape.id },
                            { "name", cape.alias.isEmpty() ? tr("Cape") : cape.alias },
                            { "path", QFileInfo::exists(path) ? path : QString() } };
    }
    m_capes = out;
    emit capesChanged();
    if (job) {
        m_capeJob = job;
        connect(job.get(), &Task::finished, this, [this] {
            m_capeJob.reset();
            rebuildCapes();
        });
        job->start();
    }
}

void SkinsBridge::setBusy(const QString& text)
{
    if (text == m_busyText)
        return;
    m_busyText = text;
    emit busyChanged();
}

void SkinsBridge::runJob(NetJob::Ptr job, const QString& busyText, std::function<void(bool, QString)> done)
{
    m_job = job;
    job->setAskRetry(false);
    setBusy(busyText);
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, done, finished](bool ok, const QString& why) {
        if (*finished)
            return;
        *finished = true;
        setBusy({});
        m_job.reset();
        done(ok, why);
    };
    connect(job.get(), &Task::succeeded, this, [finish] { finish(true, {}); });
    connect(job.get(), &Task::failed, this, [finish](const QString& why) { finish(false, why); });
    connect(job.get(), &Task::aborted, this, [finish] { finish(false, {}); });
    job->start();
}

bool SkinsBridge::looksSlim(const QString& path) const
{
    QImage img(path);
    if (img.width() != 64 || img.height() != 64)
        return false;
    // Classic arms paint x 54–55 (the back of the right arm); slim arms leave it empty.
    for (int y = 20; y < 32; y++) {
        if (qAlpha(img.pixel(54, y)) != 0 || qAlpha(img.pixel(55, y)) != 0)
            return false;
    }
    return true;
}

int SkinsBridge::addFiles(const QVariant& urls)
{
    if (!m_list)
        rebuildList();
    QList<QUrl> list;
    if (urls.canConvert<QList<QUrl>>())
        list = urls.value<QList<QUrl>>();
    for (const auto& v : urls.toList())
        list << v.toUrl();
    if (urls.typeId() == QMetaType::QUrl)
        list << urls.toUrl();

    int count = 0;
    QString lastKey;
    for (const auto& url : list) {
        const auto path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        QFileInfo info(path);
        if (!SkinModel(path).isValid()) {
            emit message(tr("“%1” isn't a Minecraft skin. Skins are 64×64 (or old 64×32) PNG images.").arg(info.fileName()), true);
            continue;
        }
        // Copy under a free name, so we know the key the library will give it
        auto base = info.completeBaseName();
        auto name = base;
        for (int n = 2; QFileInfo::exists(FS::PathCombine(skinsFolder(), name + ".png")); n++)
            name = QString("%1 %2").arg(base).arg(n);
        auto err = m_list->installSkin(path, name + ".png");
        if (!err.isEmpty()) {
            emit message(tr("Couldn't add “%1”: %2").arg(info.fileName(), err), true);
            continue;
        }
        SkinModel s(FS::PathCombine(skinsFolder(), name + ".png"));
        s.setModel(looksSlim(path) ? SkinModel::SLIM : SkinModel::CLASSIC);
        m_list->updateSkin(&s);
        lastKey = name;
        count++;
    }
    if (count) {
        rebuildSkins();
        emit message(tr("Added %n skin(s)", "", count), false);
        emit added(lastKey);
    }
    return count;
}

void SkinsBridge::addFromPlayer(const QString& playerName)
{
    const auto user = playerName.trimmed();
    static const QRegularExpression valid("^[A-Za-z0-9_]{1,16}$");
    if (!valid.match(user).hasMatch()) {
        emit message(tr("Player names are 1–16 letters, digits or underscores."), true);
        return;
    }
    if (busy())
        return;
    if (!m_list)
        rebuildList();

    // Same three steps as the classic dialog: name → UUID → profile (skin URL) → PNG
    auto path = FS::PathCombine(skinsFolder(), user + ".png");
    for (int n = 2; QFileInfo::exists(path); n++)
        path = FS::PathCombine(skinsFolder(), QString("%1 %2.png").arg(user).arg(n));
    auto profile = std::make_shared<MinecraftProfile>();

    NetJob::Ptr job{ new NetJob(tr("Download player skin"), APPLICATION->network(), 1) };
    auto downloadSkin = Net::Request::makeFile(QUrl(), path);
    auto [getProfile, a] = Net::RPC::make<bool>({ { .url = { "https://sessionserver.mojang.com/session/minecraft/profile/" } },
                                                  [downloadSkin, profile](const auto& v) -> Result<bool> {
                                                      TRY(Parsers::parseMinecraftProfileMojang(v, *profile))
                                                      if (profile->skin.url.isEmpty())
                                                          return std::unexpected(QString("no skin"));
                                                      downloadSkin->setUrl(profile->skin.url);
                                                      return {};
                                                  } });
    auto [getUUID, b] = Net::RPC::make<bool>({ { .url = "https://api.minecraftservices.com/minecraft/profile/lookup/name/" + user },
                                               [getProfile](const auto& v) -> Result<bool> {
                                                   TRY_INTO(auto root, Json::requireObject(v, "Minecraft profile lookup"))
                                                   auto id = root["id"].toString();
                                                   if (id.isEmpty())
                                                       return std::unexpected(QString("user id is empty"));
                                                   getProfile->setUrl("https://sessionserver.mojang.com/session/minecraft/profile/" + id);
                                                   return {};
                                               } });
    auto step = std::make_shared<int>(0);
    connect(getUUID.get(), &Task::failed, this, [step] { *step = 1; });
    connect(getProfile.get(), &Task::failed, this, [step] { if (*step == 0) *step = 2; });
    job->addNetAction(getUUID);
    job->addNetAction(getProfile);
    job->addNetAction(downloadSkin);

    runJob(job, tr("Looking up %1…").arg(user), [this, path, profile, user, step](bool ok, const QString&) {
        SkinModel s(path);
        if (!ok || !s.isValid()) {
            QFile::remove(path);
            if (*step == 1)
                emit message(tr("There's no Minecraft player called “%1”.").arg(user), true);
            else if (*step == 2 && !profile->id.isEmpty())
                emit message(tr("%1 uses a default skin, so there's nothing to download.").arg(user), true);
            else
                emit message(tr("Couldn't get %1's skin. Check your internet connection and try again.").arg(user), true);
            return;
        }
        s.setModel(profile->skin.variant.toUpper() == "SLIM" ? SkinModel::SLIM : SkinModel::CLASSIC);
        s.setURL(profile->skin.url);
        m_list->updateSkin(&s);
        rebuildSkins();
        emit message(tr("Added %1's skin").arg(user), false);
        emit added(s.name());
    });
}

void SkinsBridge::remove(const QString& key)
{
    if (!m_list)
        return;
    for (const auto& v : m_skins) {
        auto m = v.toMap();
        if (m["key"] == key && m["inUse"].toBool()) {
            emit message(tr("This is the skin you're wearing right now. Switch to another one first."), true);
            return;
        }
    }
    if (!m_list->deleteSkin(key, true) && !m_list->deleteSkin(key, false)) {
        emit message(tr("Couldn't delete the skin file."), true);
        return;
    }
    rebuildSkins();
}

bool SkinsBridge::rename(const QString& key, const QString& newName)
{
    if (!m_list)
        return false;
    auto name = newName.trimmed();
    static const QRegularExpression bad(R"([\\/:*?"<>|])");
    if (name.isEmpty() || name.contains(bad)) {
        emit message(tr("Use a name without / \\ : * ? \" < > |"), true);
        return false;
    }
    int row = m_list->getSkinIndex(key);
    if (row < 0)
        return false;
    if (!m_list->setData(m_list->index(row), name, Qt::EditRole)) {
        emit message(tr("A skin called “%1” already exists.").arg(name), true);
        return false;
    }
    rebuildSkins();
    return true;
}

void SkinsBridge::setSlim(const QString& key, bool slim)
{
    if (!m_list)
        return;
    if (auto* s = m_list->skin(key)) {
        s->setModel(slim ? SkinModel::SLIM : SkinModel::CLASSIC);
        m_list->save();
        rebuildSkins();
    }
}

void SkinsBridge::setCape(const QString& key, const QString& capeId)
{
    if (!m_list)
        return;
    if (auto* s = m_list->skin(key)) {
        s->setCapeId(capeId);
        m_list->save();
        rebuildSkins();
    }
}

void SkinsBridge::use(const QString& key)
{
    auto acct = account();
    if (!m_list || !acct || busy())
        return;
    if (!canUpload()) {
        emit message(tr("Only a Microsoft account that owns Minecraft can change its skin."), true);
        return;
    }
    auto* skin = m_list->skin(key);
    if (!skin || !QFileInfo::exists(skin->getPath())) {
        emit message(tr("The skin file is missing."), true);
        return;
    }
    NetJob::Ptr job{ new NetJob(tr("Change skin"), APPLICATION->network(), 1) };
    job->addNetAction(makeSkinUploadRequest(acct->accessToken(), skin->getPath(), skin->getModelString()));
    const auto cape = skin->getCapeId();
    if (cape != acct->accountData()->minecraftProfile.currentCape)
        job->addNetAction(makeCapeChangeRequest(acct->accessToken(), cape));
    job->addTask(acct->refresh().staticCast<Task>());

    runJob(job, tr("Changing your skin…"), [this, key, acct](bool ok, const QString& why) {
        if (!ok) {
            qWarning() << "Skin upload failed:" << why;
            emit message(tr("Couldn't change the skin. Sign in again in Accounts if this keeps happening."), true);
            return;
        }
        if (auto* s = m_list ? m_list->skin(key) : nullptr) {
            s->setURL(acct->accountData()->minecraftProfile.skin.url);
            m_list->save();
        }
        rebuildSkins();
        emit accountChanged();
        emit message(tr("Skin changed. You'll see it the next time you join a world."), false);
    });
}

void SkinsBridge::resetToDefault()
{
    auto acct = account();
    if (!acct || busy() || !canUpload())
        return;
    NetJob::Ptr job{ new NetJob(tr("Reset skin"), APPLICATION->network(), 1) };
    job->addNetAction(makeSkinDeleteRequest(acct->accessToken()));
    job->addTask(acct->refresh().staticCast<Task>());
    runJob(job, tr("Resetting your skin…"), [this](bool ok, const QString&) {
        rebuildSkins();
        emit accountChanged();
        emit message(ok ? tr("Back to the default skin.") : tr("Couldn't reset the skin."), !ok);
    });
}

void SkinsBridge::openFolder() const
{
    DesktopServices::openPath(skinsFolder(), true);
}
