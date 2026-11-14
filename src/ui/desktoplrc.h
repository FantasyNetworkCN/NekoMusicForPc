#pragma once

#include <QWidget>
#include <QFont>
#include <QPoint>
#include <QSize>
#include <QTimer>
#include <QVector>

class QContextMenuEvent;
class QEnterEvent;
class QPaintEvent;
class QMouseEvent;
class QShowEvent;
class QScreen;
class QVariantAnimation;
class QWheelEvent;

class DesktopLrc : public QWidget
{
    Q_OBJECT

public:
    explicit DesktopLrc(QWidget *parent = nullptr);
    ~DesktopLrc() override;

    void loadLrcText(const QString &lrcText);
    void updatePosition(qint64 position);
    void setCurrentSong(const QString &title, const QString &artist);
    void setDuration(qint64 durationMs);
    void setPlaybackState(bool playing);
    void setPlaybackAvailable(bool available);

signals:
    void previousRequested();
    void nextRequested();
    void playPauseRequested();
    void seekRequested(qint64 positionMs);
    void closeRequested();

public slots:
    void showWindow();
    void hideWindow();

protected:
    void showEvent(QShowEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    enum class HoverAction {
        None,
        Previous,
        PlayPause,
        Next,
        Lock,
        Close
    };

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
    void refreshCurrentLyric(bool animate);
    void setDisplayedLyric(const QString &text, const QString &translation, bool animate);
    void updateLyricDisplay();
    void restoreGeometry();
    void saveGeometry();
    void applyFallbackText(bool animate = true);
    void refreshStayOnTop();
    QScreen *screenForWidget() const;
    void setWindowScreenTopLeft(const QPoint &topLeft, QScreen *screen = nullptr);
    QPoint snapToScreenEdges(const QPoint &topLeft, QScreen *screen) const;
    void setPanelSize(const QSize &size);
    void resetLayout();
    void adjustFontSize(int delta, const QPoint &anchorGlobal);
    void startHoverAnimation(bool visible);
    void updateHoveredAction(const QPoint &localPos);
    HoverAction buttonAt(const QPoint &localPos, const QRect &panel) const;
    QString hoverActionToolTip(HoverAction action) const;
    void togglePositionLocked();
    void showContextMenu(const QPoint &globalPos);
    void syncCachedScreenPosFromSettings(QScreen *screen);
    QPoint clampToScreen(const QPoint &topLeft, QScreen *screen) const;
    QRect lyricsPanelLocalRect() const;
    void updateInputRegion();
    bool cursorInsidePanel() const;

    QString m_currentLyrics;
    QString m_previousLyrics;
    QString m_currentTranslation;
    QString m_previousTranslation;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_seeking = false;
    bool m_positionLocked = false;
    bool m_hovered = false;
    bool m_isPlaying = false;
    bool m_playbackAvailable = false;
    HoverAction m_hoveredAction = HoverAction::None;
    HoverAction m_pressedAction = HoverAction::None;
    QPoint m_lastDragGlobal;
    QPoint m_cachedScreenPos;
    QPoint m_resizeStartGlobal;
    QSize m_resizeStartSize;
    QScreen *m_dragScreen = nullptr;
    int m_panelW = 380;
    int m_panelH = 80;

    QVector<ParsedLyricLine> m_lyrics;
    qint64 m_currentPosition = 0;
    qint64 m_durationMs = 0;
    qint64 m_seekPreviewMs = -1;
    QString m_currentSongTitle;
    QString m_currentSongArtist;

    QFont m_font;
    bool m_isVisible = false;
    qreal m_hoverProgress = 0.0;
    qreal m_lineProgress = 1.0;
    qreal m_fontWheelAccumulator = 0.0;

    bool m_layerShellActive = false;
    bool m_layerShellConfigured = false;
    bool m_layerShellFullscreen = false;

    QTimer *m_updateTimer = nullptr;
    QTimer *m_stayOnTopTimer = nullptr;
    QVariantAnimation *m_hoverAnimation = nullptr;
    QVariantAnimation *m_lineAnimation = nullptr;
};
