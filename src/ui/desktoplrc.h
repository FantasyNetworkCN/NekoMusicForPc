#pragma once

#include <QWidget>
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QTimer>
#include <QVector>

class QPaintEvent;
class QMouseEvent;
class QShowEvent;
class QScreen;

class DesktopLrc : public QWidget
{
    Q_OBJECT

public:
    explicit DesktopLrc(QWidget *parent = nullptr);
    ~DesktopLrc() override;

    void loadLrcText(const QString &lrcText);
    void updatePosition(qint64 position);
    void setCurrentSong(const QString &title, const QString &artist);

public slots:
    void showWindow();
    void hideWindow();

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct ParsedLyricLine {
        qint64 timeMs = 0;
        QString text;
        QString translation;
    };

    bool useLayerShellPath() const;
    void ensureLayerShellConfigured();
    void applyLayerShellGeometry();
    void applyLayerShellFullscreen(QScreen *screen);
    void saveLayerShellGeometry();

    void parseLyrics(const QString &lyricsText);
    ParsedLyricLine getLyricEntryAtTime(qint64 timeMs) const;
    QString getLyricAtTime(qint64 timeMs) const;
    void updateLyricDisplay();
    void restoreGeometry();
    void saveGeometry();
    void applyFallbackText();
    void refreshStayOnTop();
    QScreen *screenForWidget() const;
    void setWindowScreenTopLeft(const QPoint &topLeft, QScreen *screen = nullptr);
    void syncCachedScreenPosFromSettings(QScreen *screen);
    QPoint clampToScreen(const QPoint &topLeft, QScreen *screen) const;
    QRect lyricsPanelLocalRect() const;
    void updateInputRegion();

    QString m_currentLyrics;
    bool m_dragging = false;
    QPoint m_lastDragGlobal;
    QPoint m_cachedScreenPos;
    QScreen *m_dragScreen = nullptr;
    int m_panelW = 380;
    int m_panelH = 80;

    QVector<ParsedLyricLine> m_lyrics;
    qint64 m_currentPosition = 0;
    QString m_currentSongTitle;
    QString m_currentSongArtist;

    QFont m_font;
    QColor m_textColor;
    QColor m_backgroundColor;
    bool m_isVisible = false;

    bool m_layerShellActive = false;
    bool m_layerShellConfigured = false;
    bool m_layerShellFullscreen = false;

    QTimer *m_updateTimer = nullptr;
    QTimer *m_stayOnTopTimer = nullptr;
};
