// SPDX-License-Identifier: GPL-3.0-only
#include "ResourcesBridge.h"

#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QUrl>

#include "InstalledModel.h"
#include "ResourceCommon.h"
#include "SearchModel.h"
#include "minecraft/mod/DataPack.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ResourceFolderModel.h"

namespace {
/// image://resicon/<instanceId>/<kind>/<percent-encoded resource id>
class ResourceIconProvider : public QQuickImageProvider {
   public:
    ResourceIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const QStringList parts = id.section('?', 0, 0).split('/');
        QPixmap pix;
        if (parts.size() >= 3) {
            auto* folder = QmlResources::folderFor(QmlResources::findInstance(parts[0]), parts[1]);
            const QString resId = QUrl::fromPercentEncoding(parts.mid(2).join('/').toLatin1());
            if (auto res = folder ? folder->find(resId) : nullptr) {
                if (const auto* mod = dynamic_cast<const Mod*>(res.get())) {
                    pix = mod->icon(QSize(64, 64), Qt::KeepAspectRatio);
                } else if (const auto* pack = dynamic_cast<const DataPack*>(res.get())) {
                    pix = pack->image(QSize(64, 64), Qt::KeepAspectRatio);
                }
            }
        }
        if (!pix.isNull() && requestedSize.isValid()) {
            pix = pix.scaled(requestedSize, Qt::KeepAspectRatio, pix.width() < requestedSize.width() ? Qt::FastTransformation
                                                                                                       : Qt::SmoothTransformation);
        }
        if (size) {
            *size = pix.size();
        }
        return pix;
    }
};
}  // namespace

ResourcesBridge::ResourcesBridge(QObject* parent) : QObject(parent)
{
    static bool registered = false;
    if (!registered) {
        qmlRegisterType<InstalledModel>("FFPrism.Resources", 1, 0, "InstalledModel");
        qmlRegisterType<SearchModel>("FFPrism.Resources", 1, 0, "SearchModel");
        registered = true;
    }
    // The shell creates its engine before the feature bridges, as a sibling child.
    if (parent) {
        if (auto* engine = parent->findChild<QQmlEngine*>(QString(), Qt::FindDirectChildrenOnly)) {
            engine->addImageProvider("resicon", new ResourceIconProvider);
        }
    }
}

QVariantMap ResourcesBridge::instanceInfo(const QString& id) const
{
    auto* inst = QmlResources::findInstance(id);
    return {
        { "valid", inst != nullptr },
        { "gameVersion", QmlResources::gameVersion(inst) },
        { "loader", QmlResources::loaderNames(inst) },
        { "hasModLoader", QmlResources::hasModLoader(inst) },
    };
}
