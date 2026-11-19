#pragma once

#include <QObject>

class QVariantAnimation;

class AppearanceSettings final : public QObject
{
    Q_OBJECT

public:
    static AppearanceSettings &instance();

    bool liquidGlassEnabled() const { return m_liquidGlassEnabled; }
    qreal liquidGlassProgress() const { return m_liquidGlassProgress; }
    void setLiquidGlassEnabled(bool enabled);

signals:
    void liquidGlassChanged(bool enabled);
    void liquidGlassProgressChanged(qreal progress);

private:
    AppearanceSettings();
    bool m_liquidGlassEnabled = false;
    qreal m_liquidGlassProgress = 0;
    QVariantAnimation *m_liquidGlassTransition = nullptr;
};
