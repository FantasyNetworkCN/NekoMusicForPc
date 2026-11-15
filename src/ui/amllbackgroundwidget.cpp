#include "amllbackgroundwidget.h"

#include <QDateTime>
#include <QLinearGradient>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QShowEvent>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

constexpr int kSubdivisions = 50;
constexpr int kControlSize = 5;
constexpr int kVertexWidth = (kControlSize - 1) * kSubdivisions;
constexpr int kVertexHeight = (kControlSize - 1) * kSubdivisions;
constexpr qreal kLayerFadeSecs = 0.5;

// AMLL 5x5 控制点预设之一。只取位置；切线/缩放由 Catmull-Rom 采样近似，
// 视觉效果与上游 MeshGradientRenderer 的有机流动保持一致。
constexpr qreal kControlPoints[kControlSize][kControlSize][2] = {
    {{-1.0, -1.0}, {-0.5, -1.0}, {0.0, -1.0}, {0.5, -1.0}, {1.0, -1.0}},
    {{-1.0, -0.5}, {-0.5, -0.5}, {-0.005203, -0.613142}, {0.588423, -0.399081}, {1.0, -0.5}},
    {{-1.0, 0.0}, {-0.421002, -0.118951}, {-0.101961, -0.023812}, {0.402751, -0.063453}, {1.0, 0.0}},
    {{-1.0, 0.5}, {0.068020, 0.520591}, {0.214465, 0.293316}, {0.5, 0.5}, {1.0, 0.5}},
    {{-1.0, 1.0}, {-0.313784, 1.0}, {0.261536, 1.0}, {0.5, 1.0}, {1.0, 1.0}},
};

qreal catmullRom(qreal p0, qreal p1, qreal p2, qreal p3, qreal t)
{
    return 0.5
        * ((2.0 * p1)
           + (-p0 + p2) * t
           + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t
           + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t);
}

qreal controlX(int x, int y)
{
    x = std::clamp(x, 0, kControlSize - 1);
    y = std::clamp(y, 0, kControlSize - 1);
    return kControlPoints[y][x][0];
}

qreal controlY(int x, int y)
{
    x = std::clamp(x, 0, kControlSize - 1);
    y = std::clamp(y, 0, kControlSize - 1);
    return kControlPoints[y][x][1];
}

qreal sampleRow(qreal gx, int row, bool xAxis)
{
    const int x = std::clamp(int(std::floor(gx)), 0, kControlSize - 2);
    const qreal t = std::clamp<qreal>(gx - x, 0.0, 1.0);
    auto loc = [row, xAxis](int col) {
        return xAxis ? controlX(col, row) : controlY(col, row);
    };
    return catmullRom(loc(x - 1), loc(x), loc(x + 1), loc(x + 2), t);
}

qreal sampleGrid(qreal gx, qreal gy, bool xAxis)
{
    const int y = std::clamp(int(std::floor(gy)), 0, kControlSize - 2);
    const qreal t = std::clamp<qreal>(gy - y, 0.0, 1.0);
    return catmullRom(sampleRow(gx, y - 1, xAxis),
                      sampleRow(gx, y, xAxis),
                      sampleRow(gx, y + 1, xAxis),
                      sampleRow(gx, y + 2, xAxis),
                      t);
}

void boxBlurPass(QImage &image, int radius, bool horizontal)
{
    if (radius <= 0 || image.isNull())
        return;
    const QImage source = image;
    const int w = source.width();
    const int h = source.height();
    const int diameter = radius * 2 + 1;
    for (int y = 0; y < h; ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0, count = 0;
            for (int d = -radius; d <= radius; ++d) {
                const int sx = horizontal ? std::clamp(x + d, 0, w - 1) : x;
                const int sy = horizontal ? y : std::clamp(y + d, 0, h - 1);
                const QRgb px = reinterpret_cast<const QRgb *>(
                    source.constScanLine(sy))[sx];
                r += qRed(px);
                g += qGreen(px);
                b += qBlue(px);
                a += qAlpha(px);
                ++count;
            }
            Q_UNUSED(diameter)
            out[x] = qRgba(r / count, g / count, b / count, a / count);
        }
    }
}

void blurTextureImage(QImage &image)
{
    for (int iteration = 0; iteration < 4; ++iteration) {
        boxBlurPass(image, 2, true);
        boxBlurPass(image, 2, false);
    }
}

QString shaderHeader(bool fragment)
{
    const bool gles = QOpenGLContext::currentContext()
        && QOpenGLContext::currentContext()->isOpenGLES();
    if (gles) {
        return fragment
            ? QStringLiteral("#version 100\nprecision mediump float;\n")
            : QStringLiteral("#version 100\nprecision highp float;\n");
    }
    return QStringLiteral("#version 120\n");
}

} // namespace

AmllBackgroundWidget::AmllBackgroundWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setFocusPolicy(Qt::NoFocus);

    m_frameTimer = new QTimer(this);
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    m_frameTimer->setInterval(16);
    connect(m_frameTimer, &QTimer::timeout, this, [this]() {
        const bool fading = std::any_of(
            m_layers.begin(), m_layers.end(), [](const Layer &layer) {
                return qAbs(layer.alpha - layer.targetAlpha) > 0.002;
            });
        if (m_playing || m_pendingTextureDirty || fading) {
            update();
        } else {
            m_frameTimer->stop();
        }
    });
}

AmllBackgroundWidget::~AmllBackgroundWidget()
{
    if (m_runtimeReady) {
        makeCurrent();
        releaseGlResources();
        doneCurrent();
    }
}

void AmllBackgroundWidget::setAlbum(const QPixmap &album, const QColor &fallbackColor)
{
    m_pendingFallback = fallbackColor.isValid() ? fallbackColor
                                                : QColor(38, 75, 110);
    m_pendingTexture = prepareTextureImage(album, m_pendingFallback);
    m_pendingTextureDirty = true;
    scheduleFrame();
}

void AmllBackgroundWidget::clearAlbum(const QColor &fallbackColor)
{
    m_pendingFallback = fallbackColor.isValid() ? fallbackColor
                                                : QColor(38, 75, 110);
    m_pendingTexture = prepareTextureImage(QPixmap(), m_pendingFallback);
    m_pendingTextureDirty = true;
    scheduleFrame();
}

void AmllBackgroundWidget::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    scheduleFrame();
}

void AmllBackgroundWidget::setFlowSpeed(qreal speed)
{
    m_flowSpeed = qMax<qreal>(0.0, speed);
    scheduleFrame();
}

void AmllBackgroundWidget::setLowFreqVolume(qreal volume)
{
    m_lowFreqVolume = std::clamp<qreal>(volume, 0.0, 1.0);
    scheduleFrame();
}

void AmllBackgroundWidget::scheduleFrame()
{
    if (!m_frameTimer)
        return;
    if (!m_frameTimer->isActive())
        m_frameTimer->start();
    update();
}

void AmllBackgroundWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    QTimer::singleShot(400, this, [this]() {
        if (m_available && !isValid()) {
            m_available = false;
            hide();
            emit rendererUnavailable();
        }
    });
}

QImage AmllBackgroundWidget::prepareTextureImage(const QPixmap &album,
                                                 const QColor &fallback)
{
    QImage image(32, 32, QImage::Format_RGB32);
    if (album.isNull()) {
        QPainter painter(&image);
        QLinearGradient gradient(0, 0, 32, 32);
        gradient.setColorAt(0.0, fallback.darker(175));
        gradient.setColorAt(0.52, fallback.darker(120));
        gradient.setColorAt(1.0, fallback.lighter(125));
        painter.fillRect(image.rect(), gradient);
        painter.end();
    } else {
        image = album.toImage().scaled(32, 32, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGB32);
    }

    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x];
            qreal r = qRed(px);
            qreal g = qGreen(px);
            qreal b = qBlue(px);
            r = (r - 128.0) * 0.4 + 128.0;
            g = (g - 128.0) * 0.4 + 128.0;
            b = (b - 128.0) * 0.4 + 128.0;
            const qreal gray = r * 0.3 + g * 0.59 + b * 0.11;
            r = gray * -2.0 + r * 3.0;
            g = gray * -2.0 + g * 3.0;
            b = gray * -2.0 + b * 3.0;
            r = ((r - 128.0) * 1.7 + 128.0) * 0.75;
            g = ((g - 128.0) * 1.7 + 128.0) * 0.75;
            b = ((b - 128.0) * 1.7 + 128.0) * 0.75;
            line[x] = qRgb(
                qBound(0, qRound(r), 255),
                qBound(0, qRound(g), 255),
                qBound(0, qRound(b), 255));
        }
    }
    blurTextureImage(image);
    return image.convertToFormat(QImage::Format_RGBA8888);
}

void AmllBackgroundWidget::buildMesh(std::vector<float> &vertices,
                                     std::vector<quint16> &indices)
{
    vertices.clear();
    indices.clear();
    vertices.reserve(kVertexWidth * kVertexHeight * 7);
    indices.reserve((kVertexWidth - 1) * (kVertexHeight - 1) * 6);

    for (int y = 0; y < kVertexHeight; ++y) {
        const qreal gy = qreal(y) / (kVertexHeight - 1) * (kControlSize - 1);
        for (int x = 0; x < kVertexWidth; ++x) {
            const qreal gx = qreal(x) / (kVertexWidth - 1) * (kControlSize - 1);
            const qreal px = sampleGrid(gx, gy, true);
            const qreal py = sampleGrid(gx, gy, false);
            const qreal u = qreal(x) / (kVertexWidth - 1);
            const qreal v = 1.0 - qreal(y) / (kVertexHeight - 1);
            vertices.insert(vertices.end(),
                            {float(px), float(py), 1.0f, 1.0f, 1.0f,
                             float(u), float(v)});
        }
    }

    for (int y = 0; y < kVertexHeight - 1; ++y) {
        for (int x = 0; x < kVertexWidth - 1; ++x) {
            const quint16 topLeft = quint16(y * kVertexWidth + x);
            const quint16 topRight = quint16(topLeft + 1);
            const quint16 bottomLeft = quint16((y + 1) * kVertexWidth + x);
            const quint16 bottomRight = quint16(bottomLeft + 1);
            indices.insert(indices.end(),
                           {topLeft, topRight, bottomLeft,
                            topRight, bottomRight, bottomLeft});
        }
    }
}

bool AmllBackgroundWidget::buildPrograms()
{
    const QString meshVertex = shaderHeader(false) + QStringLiteral(R"(
attribute vec2 a_pos;
attribute vec3 a_color;
attribute vec2 a_uv;
varying vec3 v_color;
varying vec2 v_uv;
uniform float u_aspect;
void main() {
    v_color = a_color;
    v_uv = a_uv;
    vec2 pos = a_pos;
    if (u_aspect > 1.0) {
        pos.y *= u_aspect;
    } else {
        pos.x /= u_aspect;
    }
    gl_Position = vec4(pos, 0.0, 1.0);
}
)");
    const QString meshFragment = shaderHeader(true) + QStringLiteral(R"(
varying vec3 v_color;
varying vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_volume;
uniform float u_alpha;
uniform float u_sinAngle;
uniform float u_cosAngle;
float gradientNoise(in vec2 uv) {
    return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
}
void main() {
    float volumeEffect = u_volume * 2.0;
    float dither = (1.0 / 255.0) * gradientNoise(gl_FragCoord.xy) - (0.5 / 255.0);
    vec2 centeredUV = v_uv - vec2(0.2);
    vec2 rotatedUV = vec2(
        u_cosAngle * centeredUV.x - u_sinAngle * centeredUV.y,
        u_sinAngle * centeredUV.x + u_cosAngle * centeredUV.y
    );
    vec2 finalUV = rotatedUV * max(0.001, 1.0 - volumeEffect) + vec2(0.5);
    vec4 result = texture2D(u_texture, finalUV);
    float alphaVolumeFactor = u_alpha * max(0.5, 1.0 - u_volume * 0.5);
    result.rgb *= v_color * alphaVolumeFactor;
    result.a *= alphaVolumeFactor;
    result.rgb += vec3(dither);
    float dist = distance(v_uv, vec2(0.5));
    float vignette = smoothstep(0.8, 0.3, dist);
    result.rgb *= 0.6 + vignette * 0.4;
    gl_FragColor = result;
}
)");
    const QString quadVertex = shaderHeader(false) + QStringLiteral(R"(
attribute vec2 a_pos;
varying vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_pos * 0.5 + 0.5;
}
)");
    const QString quadFragment = shaderHeader(true) + QStringLiteral(R"(
varying vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_alpha;
void main() {
    vec4 color = texture2D(u_texture, v_uv);
    float edgeY = smoothstep(0.20, 0.0, v_uv.y)
        + smoothstep(0.20, 0.0, 1.0 - v_uv.y);
    vec3 scrim = vec3(4.0, 9.0, 11.0) / 255.0;
    color.rgb = mix(color.rgb, scrim, clamp(0.68 + edgeY * 0.16, 0.0, 1.0));
    gl_FragColor = vec4(color.rgb, color.a * u_alpha);
}
)");

    m_meshProgram = std::make_unique<QOpenGLShaderProgram>();
    if (!m_meshProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, meshVertex)
        || !m_meshProgram->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                   meshFragment)
        || !m_meshProgram->link()) {
        return false;
    }

    m_quadProgram = std::make_unique<QOpenGLShaderProgram>();
    if (!m_quadProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, quadVertex)
        || !m_quadProgram->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                   quadFragment)
        || !m_quadProgram->link()) {
        return false;
    }
    return true;
}

void AmllBackgroundWidget::initializeGL()
{
    initializeOpenGLFunctions();
    m_frameClock.start();
    if (!buildPrograms()) {
        m_available = false;
        emit rendererUnavailable();
        return;
    }

    std::vector<float> vertices;
    std::vector<quint16> indices;
    buildMesh(vertices, indices);

    m_vertexBuffer = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_vertexBuffer->create();
    m_vertexBuffer->bind();
    m_vertexBuffer->allocate(vertices.data(),
                             int(vertices.size() * sizeof(float)));
    m_vertexBuffer->release();

    m_indexBuffer = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
    m_indexBuffer->create();
    m_indexBuffer->bind();
    m_indexBuffer->allocate(indices.data(),
                            int(indices.size() * sizeof(quint16)));
    m_indexBuffer->release();
    m_indexCount = int(indices.size());

    static const float quadVertices[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
    };
    m_quadBuffer = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_quadBuffer->create();
    m_quadBuffer->bind();
    m_quadBuffer->allocate(quadVertices, sizeof(quadVertices));
    m_quadBuffer->release();

    m_runtimeReady = true;
    if (m_pendingTextureDirty && m_pendingTexture.isNull())
        m_pendingTexture = prepareTextureImage(QPixmap(), m_pendingFallback);
}

void AmllBackgroundWidget::resizeGL(int width, int height)
{
    m_pixelWidth = qMax(1, width);
    m_pixelHeight = qMax(1, height);
    ensureFrameBuffer();
}

void AmllBackgroundWidget::ensureFrameBuffer()
{
    if (m_pixelWidth <= 0 || m_pixelHeight <= 0)
        return;
    if (m_fbo && m_fbo->width() == m_pixelWidth && m_fbo->height() == m_pixelHeight)
        return;
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    format.setSamples(0);
    m_fbo = std::make_unique<QOpenGLFramebufferObject>(
        m_pixelWidth, m_pixelHeight, format);
}

void AmllBackgroundWidget::uploadPendingTexture()
{
    if (!m_runtimeReady || !m_pendingTextureDirty || m_pendingTexture.isNull())
        return;

    for (Layer &layer : m_layers)
        layer.targetAlpha = 0;

    Layer layer;
    layer.texture = std::make_unique<QOpenGLTexture>(
        m_pendingTexture, QOpenGLTexture::DontGenerateMipMaps);
    layer.texture->setMinificationFilter(QOpenGLTexture::Linear);
    layer.texture->setMagnificationFilter(QOpenGLTexture::Linear);
    layer.texture->setWrapMode(QOpenGLTexture::MirroredRepeat);
    layer.alpha = 0;
    layer.targetAlpha = 1;
    m_layers.push_back(std::move(layer));
    m_pendingTextureDirty = false;

    while (m_layers.size() > 3)
        m_layers.erase(m_layers.begin());
}

void AmllBackgroundWidget::updateAnimation()
{
    if (!m_runtimeReady)
        return;
    const qreal deltaSecs = std::clamp<qreal>(
        m_frameClock.restart() / 1000.0, 0.001, 0.10);
    if (m_playing)
        m_flowTimeMs += deltaSecs * 1000.0 * m_flowSpeed;

    const qreal volumeTarget = m_playing ? m_lowFreqVolume / 10.0 : 0.0;
    const qreal lerp = std::min<qreal>(1.0, deltaSecs / 0.10);
    m_smoothedVolume += (volumeTarget - m_smoothedVolume) * lerp;

    for (Layer &layer : m_layers) {
        const qreal maxStep = deltaSecs / kLayerFadeSecs;
        if (layer.alpha < layer.targetAlpha)
            layer.alpha = qMin(layer.targetAlpha, layer.alpha + maxStep);
        else if (layer.alpha > layer.targetAlpha)
            layer.alpha = qMax(layer.targetAlpha, layer.alpha - maxStep);
    }
    for (auto it = m_layers.begin(); it != m_layers.end();) {
        if (it->targetAlpha <= 0.001 && it->alpha <= 0.001)
            it = m_layers.erase(it);
        else
            ++it;
    }
}

void AmllBackgroundWidget::paintGL()
{
    if (!m_runtimeReady) {
        glClearColor(0.016f, 0.035f, 0.043f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    if (!m_fbo)
        ensureFrameBuffer();
    if (!m_fbo) {
        glClearColor(0.016f, 0.035f, 0.043f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    uploadPendingTexture();
    updateAnimation();

    glClearColor(0.016f, 0.035f, 0.043f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const qreal aspect = m_pixelHeight > 0
        ? qreal(m_pixelWidth) / qreal(m_pixelHeight)
        : 1.0;
    const qreal angle = (m_flowTimeMs / 10000.0 + m_smoothedVolume) * 2.0;

    for (const Layer &layer : m_layers) {
        if (!layer.texture || layer.alpha <= 0.001)
            continue;

        m_fbo->bind();
        glViewport(0, 0, m_fbo->width(), m_fbo->height());
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_meshProgram->bind();
        m_meshProgram->setUniformValue("u_aspect", float(aspect));
        m_meshProgram->setUniformValue("u_texture", 0);
        m_meshProgram->setUniformValue("u_volume", float(m_smoothedVolume));
        m_meshProgram->setUniformValue("u_alpha", 1.0f);
        m_meshProgram->setUniformValue("u_sinAngle", float(std::sin(angle)));
        m_meshProgram->setUniformValue("u_cosAngle", float(std::cos(angle)));

        layer.texture->bind(0);
        m_vertexBuffer->bind();
        const int pos = m_meshProgram->attributeLocation("a_pos");
        const int color = m_meshProgram->attributeLocation("a_color");
        const int uv = m_meshProgram->attributeLocation("a_uv");
        const int stride = 7 * int(sizeof(float));
        m_meshProgram->enableAttributeArray(pos);
        m_meshProgram->setAttributeBuffer(pos, GL_FLOAT, 0, 2, stride);
        m_meshProgram->enableAttributeArray(color);
        m_meshProgram->setAttributeBuffer(color, GL_FLOAT, 2 * sizeof(float), 3,
                                          stride);
        m_meshProgram->enableAttributeArray(uv);
        m_meshProgram->setAttributeBuffer(uv, GL_FLOAT, 5 * sizeof(float), 2,
                                          stride);
        m_indexBuffer->bind();
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_SHORT, nullptr);
        m_indexBuffer->release();
        m_vertexBuffer->release();
        m_meshProgram->release();
        m_fbo->release();

        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, m_pixelWidth, m_pixelHeight);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        const qreal eased = -(std::cos(
                                  std::clamp<qreal>(layer.alpha, 0.0, 1.0) * M_PI)
                              - 1.0)
            / 2.0;
        m_quadProgram->bind();
        m_quadProgram->setUniformValue("u_texture", 0);
        m_quadProgram->setUniformValue("u_alpha", float(eased));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_fbo->texture());
        m_quadBuffer->bind();
        const int quadPos = m_quadProgram->attributeLocation("a_pos");
        m_quadProgram->enableAttributeArray(quadPos);
        m_quadProgram->setAttributeBuffer(quadPos, GL_FLOAT, 0, 2, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_quadBuffer->release();
        m_quadProgram->release();
    }
    glDisable(GL_BLEND);
}

void AmllBackgroundWidget::releaseGlResources()
{
    m_layers.clear();
    m_meshProgram.reset();
    m_quadProgram.reset();
    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_quadBuffer.reset();
    m_fbo.reset();
    m_runtimeReady = false;
}
