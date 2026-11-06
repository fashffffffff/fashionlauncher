// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPoint>
#include <QSize>
#include <QVector3D>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQuick3D/QQuick3DTextureData>

/**
 * One box of the player model (head, arm, hat layer…), with the texture mapped the way Minecraft
 * lays out skins: a box of w×h×d pixels whose six faces sit around (u, v) in the texture.
 * Centered on its own origin; units are skin pixels. QML: `import FFPrism.Skins` → `SkinBox`.
 */
class SkinBox : public QQuick3DGeometry {
    Q_OBJECT
    Q_PROPERTY(QVector3D boxSize READ boxSize WRITE setBoxSize NOTIFY changed)
    Q_PROPERTY(QPoint uv READ uv WRITE setUv NOTIFY changed)
    Q_PROPERTY(QSize textureSize READ textureSize WRITE setTextureSize NOTIFY changed)
    /// Grow the box by this many pixels on every side (the outer layer floats above the base)
    Q_PROPERTY(qreal inflate READ inflate WRITE setInflate NOTIFY changed)

   public:
    explicit SkinBox(QQuick3DObject* parent = nullptr);

    QVector3D boxSize() const { return m_size; }
    QPoint uv() const { return m_uv; }
    QSize textureSize() const { return m_textureSize; }
    qreal inflate() const { return m_inflate; }
    void setBoxSize(const QVector3D& v);
    void setUv(const QPoint& v);
    void setTextureSize(const QSize& v);
    void setInflate(qreal v);

   signals:
    void changed();

   private:
    void rebuild();

    QVector3D m_size{ 8, 8, 8 };
    QPoint m_uv;
    QSize m_textureSize{ 64, 64 };
    qreal m_inflate = 0;
};

/**
 * Pixels of a skin or cape PNG for a Quick 3D Texture (nearest filtering keeps them crisp).
 * Old 64×32 skins are converted to the 64×64 layout first, exactly like the game does.
 */
class SkinTexture : public QQuick3DTextureData {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool cape READ cape WRITE setCape NOTIFY sourceChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY loadedChanged)

   public:
    explicit SkinTexture(QQuick3DObject* parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString& path);
    bool cape() const { return m_cape; }
    void setCape(bool c);
    bool loaded() const { return m_loaded; }
    QSize imageSize() const { return m_imageSize; }

   signals:
    void sourceChanged();
    void loadedChanged();

   private:
    void reload();

    QString m_source;
    bool m_cape = false;
    bool m_loaded = false;
    QSize m_imageSize;
};
