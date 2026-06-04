#include "shellbackdropsettings.h"

#include <QImageReader>
#include <QSettings>
#include <QtMath>

namespace {

constexpr auto kKeyKind = "shellBackdrop/kind";
constexpr auto kKeyCustomPath = "shellBackdrop/customPath";
constexpr auto kKeySolidColor = "shellBackdrop/solidColor";
constexpr const char *kDefaultResourcePath = ":/background-pages.jpg";
/** 解码时长边上限，避免 4K 壁纸拖慢 UI */
constexpr int kMaxSourceLongSide = 1920;

QPixmap loadPixmapScaled(const QString &path)
{
    if (path.isEmpty())
        return {};

    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return {};

    QSize sz = reader.size();
    if (sz.isValid()) {
        const int longSide = qMax(sz.width(), sz.height());
        if (longSide > kMaxSourceLongSide)
            sz.scale(kMaxSourceLongSide, kMaxSourceLongSide, Qt::KeepAspectRatio);
        reader.setScaledSize(sz);
    }

    const QImage img = reader.read();
    if (img.isNull())
        return {};
    return QPixmap::fromImage(img);
}

} // namespace

ShellBackdropSettings &ShellBackdropSettings::instance()
{
    static ShellBackdropSettings inst;
    return inst;
}

ShellBackdropSettings::ShellBackdropSettings(QObject *parent)
    : QObject(parent)
{
    load();
}

void ShellBackdropSettings::load()
{
    QSettings s;
    m_kind = static_cast<Kind>(s.value(kKeyKind, static_cast<int>(Kind::DefaultImage)).toInt());
    m_customPath = s.value(kKeyCustomPath).toString();
    const QColor c(s.value(kKeySolidColor, QStringLiteral("#1a1a22")).toString());
    m_solidColor = c.isValid() ? c : QColor(26, 26, 34);
    invalidateSourceCache();
}

void ShellBackdropSettings::save() const
{
    QSettings s;
    s.setValue(kKeyKind, static_cast<int>(m_kind));
    s.setValue(kKeyCustomPath, m_customPath);
    s.setValue(kKeySolidColor, m_solidColor.name(QColor::HexRgb));
}

void ShellBackdropSettings::invalidateSourceCache()
{
    m_sourceCache = QPixmap();
    m_sourceCacheKey.clear();
}

QString ShellBackdropSettings::sourceCacheKey() const
{
    switch (m_kind) {
    case Kind::CustomImage:
        return QStringLiteral("custom:") + m_customPath;
    case Kind::DefaultImage:
        return QStringLiteral("default");
    case Kind::SolidColor:
    default:
        return QString();
    }
}

bool ShellBackdropSettings::usesImageBackdrop() const
{
    return m_kind == Kind::DefaultImage || m_kind == Kind::CustomImage;
}

QPixmap ShellBackdropSettings::loadSourcePixmapUncached() const
{
    if (m_kind == Kind::CustomImage && !m_customPath.isEmpty())
        return loadPixmapScaled(m_customPath);
    return loadPixmapScaled(QLatin1String(kDefaultResourcePath));
}

QPixmap ShellBackdropSettings::cachedSourcePixmap()
{
    if (!usesImageBackdrop())
        return {};

    const QString key = sourceCacheKey();
    if (m_sourceCacheKey == key && !m_sourceCache.isNull())
        return m_sourceCache;

    m_sourceCache = loadSourcePixmapUncached();
    m_sourceCacheKey = key;
    return m_sourceCache;
}

void ShellBackdropSettings::setKind(Kind kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    invalidateSourceCache();
    save();
    emit changed();
}

void ShellBackdropSettings::setCustomImagePath(const QString &path)
{
    if (m_customPath == path)
        return;
    m_customPath = path;
    invalidateSourceCache();
    save();
    emit changed();
}

void ShellBackdropSettings::setSolidColor(const QColor &color)
{
    if (!color.isValid() || m_solidColor == color)
        return;
    m_solidColor = color;
    save();
    emit changed();
}

void ShellBackdropSettings::resetToDefaultImage()
{
    m_customPath.clear();
    m_kind = Kind::DefaultImage;
    invalidateSourceCache();
    save();
    emit changed();
}
