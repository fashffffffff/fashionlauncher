// SPDX-License-Identifier: GPL-3.0-only
#include "PanoramaTools.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "archive/ArchiveReader.h"

namespace PanoramaTools {

QString packFacePath(int i)
{
    return QString("assets/minecraft/textures/gui/title/background/panorama_%1.png").arg(i);
}

static bool complete(const QList<QImage>& faces)
{
    // Tiny faces are placeholders (modern client jars ship 1x1 ones).
    return faces.size() == 6 &&
           std::all_of(faces.begin(), faces.end(), [](const QImage& f) { return !f.isNull() && f.width() >= 16 && f.height() >= 16; });
}

QList<QImage> facesFromAssetIndex(const QString& indexJson, const QString& objectsDir)
{
    QFile f(indexJson);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto objects = QJsonDocument::fromJson(f.readAll()).object().value("objects").toObject();
    QList<QImage> faces;
    for (int i = 0; i < 6; i++) {
        const QString hash =
            objects.value(QString("minecraft/textures/gui/title/background/panorama_%1.png").arg(i)).toObject().value("hash").toString();
        if (hash.size() < 2) {
            return {};
        }
        faces.append(QImage(QDir(objectsDir).filePath(hash.left(2) + "/" + hash)));
    }
    return complete(faces) ? faces : QList<QImage>{};
}

QList<QImage> facesFromPack(const QString& path)
{
    QList<QImage> faces;
    const QFileInfo fi(path);
    if (fi.isDir()) {
        return facesFromDir(QDir(path).filePath("assets/minecraft/textures/gui/title/background"));
    }
    if (!fi.isFile()) {
        return {};
    }
    // One pass over the archive: client jars hold thousands of entries.
    QList<QByteArray> data(6);
    int found = 0;
    MMCZip::ArchiveReader zip(path);
    const QString prefix = "assets/minecraft/textures/gui/title/background/panorama_";
    auto res = zip.parse([&](MMCZip::ArchiveReader::File* f) -> Result<bool> {
        const QString name = f->filename();
        if (name.startsWith(prefix) && name.endsWith(".png") && name.size() == prefix.size() + 5) {
            const int i = name.at(prefix.size()).digitValue();
            if (i >= 0 && i < 6 && data[i].isEmpty()) {
                if (auto bytes = f->readAll()) {
                    data[i] = *bytes;
                    found++;
                }
                return found >= 6;  // true stops the scan
            }
        }
        (void)f->skip();
        return false;
    });
    if (!res || found < 6) {
        return {};
    }
    for (const auto& d : data) {
        faces.append(QImage::fromData(d));
    }
    return complete(faces) ? faces : QList<QImage>{};
}

QList<QImage> facesFromDir(const QString& dir)
{
    QList<QImage> faces;
    for (int i = 0; i < 6; i++) {
        faces.append(QImage(QDir(dir).filePath(QString("panorama_%1.png").arg(i))));
    }
    return complete(faces) ? faces : QList<QImage>{};
}

QList<QImage> facesFromFiles(const QStringList& files)
{
    if (files.size() != 6) {
        return {};
    }
    QList<QImage> faces(6);
    static const QRegularExpression numbered("(\\d)\\.[a-zA-Z]+$");
    bool allNumbered = true;
    for (const auto& f : files) {
        const auto m = numbered.match(f);
        const int i = m.hasMatch() ? m.captured(1).toInt() : -1;
        if (i < 0 || i > 5 || !faces[i].isNull()) {
            allNumbered = false;
            break;
        }
        faces[i] = QImage(f);
    }
    if (!allNumbered) {
        QStringList sorted = files;
        sorted.sort(Qt::CaseInsensitive);
        faces.clear();
        for (const auto& f : sorted) {
            faces.append(QImage(f));
        }
    }
    return complete(faces) ? faces : QList<QImage>{};
}

QList<QImage> facesFromEquirect(const QImage& source)
{
    if (source.isNull()) {
        return {};
    }
    const QImage img = source.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const int n = std::clamp(w / 4, 64, kMaxFace);
    // Direction for face pixel (a right, b up, both -1..1). Camera looks at -Z, +Y is up, +X right;
    // these match the quads in PanoramaView.qml and Minecraft's face order.
    auto dir = [](int face, double a, double b, double& x, double& y, double& z) {
        switch (face) {
            case 0: x = a, y = b, z = -1; break;    // front
            case 1: x = 1, y = b, z = a; break;     // right
            case 2: x = -a, y = b, z = 1; break;    // back
            case 3: x = -1, y = b, z = -a; break;   // left
            case 4: x = a, y = 1, z = b; break;     // up: image bottom meets the front face
            default: x = a, y = -1, z = -b; break;  // down: image top meets the front face
        }
    };
    auto sample = [&](double u, double v) {
        // Bilinear, wrapping horizontally.
        const double fx = u * w - 0.5, fy = std::clamp(v * h - 0.5, 0.0, h - 1.0);
        const int x0 = int(std::floor(fx)), y0 = int(fy);
        const double tx = fx - x0, ty = fy - y0;
        const int x1 = x0 + 1, y1 = std::min(y0 + 1, h - 1);
        auto px = [&](int x, int y) { return img.pixel(((x % w) + w) % w, y); };
        const QRgb p00 = px(x0, y0), p10 = px(x1, y0), p01 = px(x0, y1), p11 = px(x1, y1);
        auto ch = [&](int shift) {
            auto c = [&](QRgb p) { return double((p >> shift) & 0xff); };
            return int(std::lround((c(p00) * (1 - tx) + c(p10) * tx) * (1 - ty) + (c(p01) * (1 - tx) + c(p11) * tx) * ty));
        };
        return qRgb(ch(16), ch(8), ch(0));
    };
    QList<QImage> faces;
    for (int f = 0; f < 6; f++) {
        QImage face(n, n, QImage::Format_RGB32);
        for (int py = 0; py < n; py++) {
            auto* line = reinterpret_cast<QRgb*>(face.scanLine(py));
            const double b = 1.0 - 2.0 * (py + 0.5) / n;
            for (int px = 0; px < n; px++) {
                const double a = 2.0 * (px + 0.5) / n - 1.0;
                double x, y, z;
                dir(f, a, b, x, y, z);
                const double lon = std::atan2(x, -z);
                const double lat = std::atan2(y, std::sqrt(x * x + z * z));
                line[px] = sample(0.5 + lon / (2 * M_PI), 0.5 - lat / M_PI);
            }
        }
        faces.append(face);
    }
    return faces;
}

QString newestPanoramaDir(const QStringList& roots)
{
    QString best;
    QDateTime bestTime;
    for (const auto& root : roots) {
        if (!QFileInfo(root).isDir()) {
            continue;
        }
        QDirIterator it(root, { "panorama_5.png" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            // Two levels at most (screenshots/<stamp>/panorama_N.png).
            if (QDir(root).relativeFilePath(fi.absoluteFilePath()).count('/') > 2) {
                continue;
            }
            const QString dir = fi.absolutePath();
            if (hasFaces(dir) && (bestTime.isNull() || fi.lastModified() > bestTime)) {
                best = dir;
                bestTime = fi.lastModified();
            }
        }
    }
    return best;
}

bool saveFaces(const QList<QImage>& faces, const QString& dir)
{
    if (!complete(faces) || !QDir().mkpath(dir)) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        QImage f = faces[i];
        if (f.width() > kMaxFace || f.height() > kMaxFace) {
            f = f.scaled(kMaxFace, kMaxFace, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (!f.save(QDir(dir).filePath(QString("panorama_%1.png").arg(i)))) {
            return false;
        }
    }
    return true;
}

bool hasFaces(const QString& dir)
{
    for (int i = 0; i < 6; i++) {
        if (!QFileInfo::exists(QDir(dir).filePath(QString("panorama_%1.png").arg(i)))) {
            return false;
        }
    }
    return true;
}

}  // namespace PanoramaTools
