// SPDX-License-Identifier: GPL-3.0-only
#include "SkinScene.h"

#include <QFileInfo>
#include <QImage>

#include "minecraft/skins/SkinModel.h"

namespace {
struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};
}  // namespace

SkinBox::SkinBox(QQuick3DObject* parent) : QQuick3DGeometry(parent)
{
    rebuild();
}

void SkinBox::setBoxSize(const QVector3D& v)
{
    if (v == m_size)
        return;
    m_size = v;
    rebuild();
    emit changed();
}

void SkinBox::setUv(const QPoint& v)
{
    if (v == m_uv)
        return;
    m_uv = v;
    rebuild();
    emit changed();
}

void SkinBox::setTextureSize(const QSize& v)
{
    if (v == m_textureSize || v.isEmpty())
        return;
    m_textureSize = v;
    rebuild();
    emit changed();
}

void SkinBox::setInflate(qreal v)
{
    if (qFuzzyCompare(v, m_inflate))
        return;
    m_inflate = v;
    rebuild();
    emit changed();
}

void SkinBox::rebuild()
{
    const float w = m_size.x(), h = m_size.y(), d = m_size.z();
    const float hx = w / 2 + m_inflate, hy = h / 2 + m_inflate, hz = d / 2 + m_inflate;
    const float u = m_uv.x(), v = m_uv.y();
    const float tw = m_textureSize.width(), th = m_textureSize.height();
    // A hair inside each pixel rect, so nearest sampling never picks the neighbouring face
    const float e = 0.01f;

    QByteArray vertices;
    QByteArray indices;
    quint16 base = 0;

    // Corners are given as the image's top-left, top-right, bottom-right, bottom-left
    auto face = [&](QVector3D tl, QVector3D tr, QVector3D br, QVector3D bl, QVector3D n, float rx, float ry, float rw, float rh) {
        const float u0 = (rx + e) / tw, u1 = (rx + rw - e) / tw;
        const float v0 = (ry + e) / th, v1 = (ry + rh - e) / th;
        const Vertex quad[4] = {
            { tl.x(), tl.y(), tl.z(), n.x(), n.y(), n.z(), u0, v0 },
            { tr.x(), tr.y(), tr.z(), n.x(), n.y(), n.z(), u1, v0 },
            { br.x(), br.y(), br.z(), n.x(), n.y(), n.z(), u1, v1 },
            { bl.x(), bl.y(), bl.z(), n.x(), n.y(), n.z(), u0, v1 },
        };
        vertices.append(reinterpret_cast<const char*>(quad), sizeof(quad));
        // Counter-clockwise seen from outside
        const quint16 idx[6] = { base, quint16(base + 3), quint16(base + 2), base, quint16(base + 2), quint16(base + 1) };
        indices.append(reinterpret_cast<const char*>(idx), sizeof(idx));
        base += 4;
    };

    // The player faces +Z (towards the camera); its right side is -X.
    face({ -hx, hy, hz }, { hx, hy, hz }, { hx, -hy, hz }, { -hx, -hy, hz }, { 0, 0, 1 }, u + d, v + d, w, h);                   // front
    face({ -hx, hy, -hz }, { -hx, hy, hz }, { -hx, -hy, hz }, { -hx, -hy, -hz }, { -1, 0, 0 }, u, v + d, d, h);                 // right
    face({ hx, hy, hz }, { hx, hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz }, { 1, 0, 0 }, u + d + w, v + d, d, h);              // left
    face({ hx, hy, -hz }, { -hx, hy, -hz }, { -hx, -hy, -hz }, { hx, -hy, -hz }, { 0, 0, -1 }, u + 2 * d + w, v + d, w, h);     // back
    face({ -hx, hy, -hz }, { hx, hy, -hz }, { hx, hy, hz }, { -hx, hy, hz }, { 0, 1, 0 }, u + d, v, w, d);                      // top
    face({ -hx, -hy, hz }, { hx, -hy, hz }, { hx, -hy, -hz }, { -hx, -hy, -hz }, { 0, -1, 0 }, u + d + w, v, w, d);             // bottom

    clear();
    setVertexData(vertices);
    setIndexData(indices);
    setStride(sizeof(Vertex));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 6 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U16Type);
    setBounds({ -hx, -hy, -hz }, { hx, hy, hz });
    update();
}

SkinTexture::SkinTexture(QQuick3DObject* parent) : QQuick3DTextureData(parent)
{
    reload();
}

void SkinTexture::setSource(const QString& path)
{
    if (path == m_source)
        return;
    m_source = path;
    reload();
    emit sourceChanged();
}

void SkinTexture::setCape(bool c)
{
    if (c == m_cape)
        return;
    m_cape = c;
    reload();
    emit sourceChanged();
}

void SkinTexture::reload()
{
    QImage img;
    if (!m_source.isEmpty() && QFileInfo::exists(m_source)) {
        img = m_cape ? QImage(m_source) : SkinModel(m_source).getTexture();
    }
    const bool ok = !img.isNull();
    if (!ok) {
        // A single transparent pixel keeps the material valid while nothing is loaded
        img = QImage(1, 1, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    setSize(img.size());
    setFormat(QQuick3DTextureData::RGBA8);
    setHasTransparency(true);
    setTextureData(QByteArray(reinterpret_cast<const char*>(img.constBits()), img.sizeInBytes()));
    const QSize newSize = ok ? img.size() : QSize();
    if (ok != m_loaded || newSize != m_imageSize) {
        m_loaded = ok;
        m_imageSize = newSize;
    }
    emit loadedChanged();
}
