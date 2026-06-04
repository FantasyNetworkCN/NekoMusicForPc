#pragma once

#include <QColor>
#include <QObject>
#include <QPixmap>
#include <QString>

/** 整窗壳层背景（设置页「个性化」） */
class ShellBackdropSettings : public QObject
{
    Q_OBJECT

public:
    enum class Kind {
        DefaultImage = 0,
        CustomImage = 1,
        SolidColor = 2,
    };
    Q_ENUM(Kind)

    static ShellBackdropSettings &instance();

    Kind kind() const { return m_kind; }
    QString customImagePath() const { return m_customPath; }
    QColor solidColor() const { return m_solidColor; }

    bool usesImageBackdrop() const;
    /** 解码后的源图（带内存缓存，避免每帧从磁盘加载）。 */
    QPixmap cachedSourcePixmap();

    void setKind(Kind kind);
    void setCustomImagePath(const QString &path);
    void setSolidColor(const QColor &color);
    void resetToDefaultImage();

    void load();
    void save() const;
    void invalidateSourceCache();

signals:
    void changed();

private:
    explicit ShellBackdropSettings(QObject *parent = nullptr);
    QString sourceCacheKey() const;
    QPixmap loadSourcePixmapUncached() const;

    Kind m_kind = Kind::DefaultImage;
    QString m_customPath;
    QColor m_solidColor{26, 26, 34};
    QString m_sourceCacheKey;
    QPixmap m_sourceCache;
};
