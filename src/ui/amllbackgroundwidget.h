#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPixmap>

#include <memory>
#include <vector>

class QOpenGLBuffer;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QShowEvent;
class QTimer;

/**
 * AMLL MeshGradientRenderer 的原生 Qt 移植。
 *
 * 使用封面缩略图作为纹理，在双三次网格上做流动形变，并按播放状态推进
 * 着色器时间。若 OpenGL / 着色器不可用，会发出 rendererUnavailable，
 * 播放页继续使用原有的静态模糊封面降级。
 */
class AmllBackgroundWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit AmllBackgroundWidget(QWidget *parent = nullptr);
    ~AmllBackgroundWidget() override;

    void setAlbum(const QPixmap &album, const QColor &fallbackColor = QColor());
    void clearAlbum(const QColor &fallbackColor = QColor());
    void setPlaying(bool playing);
    void setFlowSpeed(qreal speed);
    void setLowFreqVolume(qreal volume);
    void setBassGain(qreal gain);
    void setScrimStrength(qreal strength);
    bool isRendererAvailable() const { return m_available; }

signals:
    void rendererUnavailable();

protected:
    void showEvent(QShowEvent *event) override;
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    struct Layer {
        std::unique_ptr<QOpenGLTexture> texture;
        qreal alpha = 0;
        qreal targetAlpha = 0;
    };

    static QImage prepareTextureImage(const QPixmap &album, const QColor &fallback);
    static void buildMesh(std::vector<float> &vertices,
                          std::vector<quint16> &indices);
    bool buildPrograms();
    void uploadPendingTexture();
    void releaseGlResources();
    void ensureFrameBuffer();
    void updateAnimation();
    void scheduleFrame();

    std::unique_ptr<QOpenGLShaderProgram> m_meshProgram;
    std::unique_ptr<QOpenGLShaderProgram> m_quadProgram;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_indexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_quadBuffer;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    QTimer *m_frameTimer = nullptr;

    std::vector<Layer> m_layers;
    QImage m_pendingTexture;
    QColor m_pendingFallback = QColor(38, 75, 110);
    bool m_pendingTextureDirty = true;
    bool m_runtimeReady = false;
    bool m_available = true;
    bool m_playing = false;
    qreal m_flowSpeed = 1.0;
    qreal m_lowFreqVolume = 0.0;
    qreal m_bassGain = 1.0;
    qreal m_scrimStrength = 0.68;
    qreal m_smoothedVolume = 0.0;
    qreal m_flowTimeMs = 0;
    QElapsedTimer m_frameClock;
    int m_indexCount = 0;
    int m_pixelWidth = 0;
    int m_pixelHeight = 0;
};
