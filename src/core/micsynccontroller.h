#pragma once

#include <QObject>
#include <QString>

/**
 * @file micsynccontroller.h
 * @brief 麦克风同步：把播放中的音乐混入虚拟麦克风设备
 *
 * 开启后会在系统里创建一个名为 NekoMusicMic 的虚拟输入设备，
 * 语音 / 会议 / 直播等软件把它选作麦克风即可听到正在播放的音乐。
 * Linux 走 PulseAudio / PipeWire 的 null-sink + loopback + remap-source。
 */
class PlayerEngine;

class MicSyncController final : public QObject
{
    Q_OBJECT

public:
    static MicSyncController &instance();

    /** 供 Windows 后端切换播放器输出设备使用（Linux 忽略）。 */
    void setPlayerEngine(PlayerEngine *engine);

    /** 当前平台是否支持（Linux 且存在 pactl）。 */
    static bool isSupported();

    bool isEnabled() const { return m_enabled; }

    /** 混音后麦克风在系统中显示的名字，供 UI 提示用户选择。 */
    static QString deviceName();
    /** 各平台使用说明的文案 key。 */
    static QString hintKey();
    static bool installBundledDriver();

public slots:
    void setEnabled(bool enabled);
    void toggle();

signals:
    void enabledChanged(bool enabled);
    void failed(const QString &reason);

private:
    explicit MicSyncController(QObject *parent = nullptr);
    ~MicSyncController() override;

    PlayerEngine *m_playerEngine = nullptr;
    bool m_enabled = false;
};
