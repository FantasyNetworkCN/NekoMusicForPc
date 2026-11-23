#ifndef PLAYLISTMANAGER_H
#define PLAYLISTMANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include "core/musicinfo.h"
#include "core/shufflebag.h"

class PlaylistManager : public QObject {
    Q_OBJECT

public:
    static PlaylistManager& instance();

    void load();
    void save();

    void addToPlaylist(const MusicInfo& music);
    void addAllToPlaylist(const QList<MusicInfo>& musicList);
    /**
     * 「下一首播放」：把曲目插到当前曲目之后，并强制下一首为它。
     *
     * 单曲循环 / 随机播放同样生效——随机模式不消耗洗牌袋游标，而是把该曲
     * 从待播队列摘掉，保证本轮不会再随到。队列里还没有正在播放的曲目时，
     * 该曲直接成为当前曲目并返回 false（调用方需要自行起播）。
     */
    bool playNext(const MusicInfo& music);
    void replacePlaylist(const QList<MusicInfo>& musicList, int currentIndex = 0);
    void removeFromPlaylist(int localId);
    void clearPlaylist();
    /** 刷新队列中的当前可播放状态；只改内存，不删除用户队列/歌单数据。 */
    void setPlayable(int musicId, bool playable);

    /** 返回内部队列的 const 引用，避免按值返回时与 playlist()[i] 组合产生悬空引用。 */
    const QList<MusicInfo> &playlist() const { return m_playlist; }
    int count() const { return m_playlist.size(); }

    // 恢复上次播放
    bool hasLastPlayed() const { return m_currentIndex >= 0 && !m_playlist.isEmpty(); }
    MusicInfo lastPlayedMusic() const;

    // Play mode: "list" (loop), "single", "random"
    void setPlayMode(const QString& mode);
    QString playMode() const { return m_playMode; }
    void togglePlayMode();

    // Navigation
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    // 随机模式下会消费洗牌袋游标，故不能是 const
    int nextIndex();
    int previousIndex();

signals:
    void playlistChanged();
    void currentIndexChanged(int index);
    void playRequested(int localId);
    void playModeChanged(const QString& mode);

private:
    PlaylistManager() = default;
    ~PlaylistManager() = default;
    PlaylistManager(const PlaylistManager&) = delete;
    PlaylistManager& operator=(const PlaylistManager&) = delete;

    int findIndexByLocalId(int localId) const;

    /** 当前队列的曲目稳定标识（去重、保序）。 */
    QStringList poolKeys() const;
    int indexOfKey(const QString &key) const;
    /** 队列增删后同步洗牌袋。 */
    void syncShufflePool();
    /** 持久化洗牌袋游标与历史。 */
    void persistShuffleState();

    QList<MusicInfo> m_playlist;
    int m_currentIndex = -1;
    QString m_playMode = "list"; // list, single, random
    /** 待播放的「下一首播放」曲目 key；非空时 nextIndex() 优先返回它。 */
    QString m_forcedNextKey;
    ShuffleBag m_shuffleBag;
};

#endif // PLAYLISTMANAGER_H
