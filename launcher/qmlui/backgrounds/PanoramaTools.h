// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * Helpers for title-screen panoramas: six cube faces named like Minecraft's
 * assets/minecraft/textures/gui/title/background/panorama_0..5.png
 * (0 front, 1 right, 2 back, 3 left, 4 up, 5 down).
 *
 * Everything here is blocking file work and runs on a worker thread.
 */
namespace PanoramaTools {

/// Faces are stored at most this big: plenty for a window, 4 MB of GPU memory each.
constexpr int kMaxFace = 1024;

/// Path of panorama face i inside a resource pack or the client jar.
QString packFacePath(int i);

/// The six faces from a resource pack / client jar (.zip/.jar) or an unpacked pack folder; empty list if missing.
QList<QImage> facesFromPack(const QString& path);
/// The six faces from Minecraft's downloaded assets (assets/indexes/<id>.json + assets/objects), where the real
/// title-screen panoramas live; the client jar only carries tiny placeholders since 1.13.
QList<QImage> facesFromAssetIndex(const QString& indexJson, const QString& objectsDir);
/// The six faces from a folder holding panorama_0..5.png directly; empty list if missing.
QList<QImage> facesFromDir(const QString& dir);
/// Six face images picked by the user, in any order: "…_0" … "…_5" names are honored, otherwise sorted by name.
QList<QImage> facesFromFiles(const QStringList& files);
/// Convert one 360° equirectangular picture (2:1) into the six faces.
QList<QImage> facesFromEquirect(const QImage& img);

/// Newest folder (searched two levels deep) that holds panorama_0..5.png, or "".
QString newestPanoramaDir(const QStringList& roots);

/// Scale faces to at most kMaxFace and save them as panorama_0..5.png in dir. Returns false on any failure.
bool saveFaces(const QList<QImage>& faces, const QString& dir);
/// True if dir contains all six faces.
bool hasFaces(const QString& dir);

}  // namespace PanoramaTools
