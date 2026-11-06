// SPDX-License-Identifier: GPL-3.0-only
// Pack covers for BackgroundsBridge: a managed modpack's gallery picture (Modrinth gallery, CurseForge screenshots),
// downloaded once, decoded small and cached as a JPEG in <data>/cache/qmlui/covers.
#include "BackgroundsBridge.h"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QtConcurrent>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

namespace {
// Wide enough for the instance page header at 2x after the crop; cards use the same file.
constexpr int kCoverWidth = 1280;

QString coverPath(const QString& cacheDir, const QString& instanceId)
{
    QString safe = instanceId;
    safe.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return FS::PathCombine(cacheDir, "covers", safe + ".jpg");
}
}  // namespace

bool BackgroundsBridge::coversEnabled() const
{
    return APPLICATION->settings()->get("QmlPackCovers").toBool();
}

void BackgroundsBridge::setCoversEnabled(bool on)
{
    if (on == coversEnabled()) {
        return;
    }
    APPLICATION->settings()->set("QmlPackCovers", on);
    emit coversEnabledChanged();
}

void BackgroundsBridge::requestCover(const QString& instanceId)
{
    if (instanceId.isEmpty() || m_coverRequested.contains(instanceId)) {
        return;
    }
    m_coverRequested.insert(instanceId);
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst || !inst->isManagedPack()) {
        return;
    }
    const QString path = coverPath(m_cacheDir, instanceId);
    if (QFileInfo::exists(path)) {
        m_covers[instanceId] = QUrl::fromLocalFile(path).toString();
        emit coversChanged();
        return;
    }
    const QString type = inst->getManagedPackType();
    const QString packId = inst->getManagedPackID();
    if (packId.isEmpty()) {
        return;
    }
    QPointer<BackgroundsBridge> self(this);
    if (type == "modrinth") {
        auto* reply = apiGet("https://api.modrinth.com/v2/project/" + packId);
        connect(reply, &QNetworkReply::finished, this, [self, reply, instanceId] {
            reply->deleteLater();
            const auto gallery = QJsonDocument::fromJson(reply->readAll()).object().value("gallery").toArray();
            QString url;
            for (const auto& g : gallery) {
                const auto o = g.toObject();
                if (url.isEmpty() || o.value("featured").toBool()) {
                    // raw_url is the full picture; "url" can be a 350 px preview that looks blurry in the header.
                    url = o.value("raw_url").toString();
                    if (url.isEmpty()) {
                        url = o.value("url").toString();
                    }
                    if (o.value("featured").toBool()) {
                        break;
                    }
                }
            }
            if (self && !url.isEmpty()) {
                self->saveCover(instanceId, url);
            }
        });
    } else if (type == "flame" && !APPLICATION->getFlameAPIKey().isEmpty()) {
        auto* reply = apiGet("https://api.curseforge.com/v1/mods/" + packId, true);
        connect(reply, &QNetworkReply::finished, this, [self, reply, instanceId] {
            reply->deleteLater();
            const auto shots = QJsonDocument::fromJson(reply->readAll()).object().value("data").toObject().value("screenshots").toArray();
            const QString url = shots.isEmpty() ? QString() : shots.first().toObject().value("url").toString();
            if (self && !url.isEmpty()) {
                self->saveCover(instanceId, url);
            }
        });
    }
}

void BackgroundsBridge::saveCover(const QString& instanceId, const QString& imageUrl)
{
    const QString path = coverPath(m_cacheDir, instanceId);
    QPointer<BackgroundsBridge> self(this);
    auto* reply = apiGet(imageUrl);
    connect(reply, &QNetworkReply::finished, this, [self, reply, instanceId, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Pack cover download failed:" << reply->url() << reply->errorString();
            return;
        }
        const QByteArray data = reply->readAll();
        // Decode straight at the small size (webp/png/jpg), off the UI thread.
        auto future = QtConcurrent::run([data, path]() -> bool {
            QBuffer buf;
            buf.setData(data);
            QImageReader reader(&buf);
            reader.setAutoTransform(true);
            const QSize size = reader.size();
            if (size.width() > kCoverWidth) {
                reader.setScaledSize(QSize(kCoverWidth, qMax(1, size.height() * kCoverWidth / size.width())));
            }
            const QImage img = reader.read();
            if (img.isNull()) {
                qWarning() << "Pack cover can't be decoded:" << reader.errorString();
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            return !img.isNull() && img.convertToFormat(QImage::Format_RGB32).save(path, "JPG", 85);
        });
        auto* watcher = new QFutureWatcher<bool>(self);
        connect(watcher, &QFutureWatcher<bool>::finished, self, [self, watcher, instanceId, path] {
            watcher->deleteLater();
            if (self && watcher->result()) {
                self->m_covers[instanceId] = QUrl::fromLocalFile(path).toString();
                emit self->coversChanged();
            }
        });
        watcher->setFuture(future);
    });
}
