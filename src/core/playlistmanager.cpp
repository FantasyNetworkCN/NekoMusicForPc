#include "core/playlistmanager.h"
#include "core/playlistdb.h"

#include <QFileInfo>
#include <QSet>

PlaylistManager& PlaylistManager::instance() {
    static PlaylistManager manager;
    return manager;
}

void PlaylistManager::load() {
    // Load from SQLite
    m_playlist = PlaylistDatabase::instance().getQueue();
    m_currentIndex = PlaylistDatabase::instance().getQueueCurrentIndex();
    m_playMode = PlaylistDatabase::instance().getQueuePlayMode();

    // 恢复随机播放进度：重启后继续上一轮，而不是又从开头几首开始
    m_shuffleBag.restore(
        PlaylistDatabase::instance().getQueueStateValue(QStringLiteral("shuffleState")));
    m_shuffleBag.syncPool(poolKeys());
}

void PlaylistManager::save() {
    // Save to SQLite
    PlaylistDatabase::instance().setQueueMusic(m_playlist, m_currentIndex);
    PlaylistDatabase::instance().setQueuePlayMode(m_playMode);
    persistShuffleState();
}

void PlaylistManager::addToPlaylist(const MusicInfo& music) {
    const QString canon = music.isLocalFile()
        ? QFileInfo(music.localPath).canonicalFilePath()
        : QString();
    for (const auto& item : m_playlist) {
        if (!canon.isEmpty()) {
            const QString ic = QFileInfo(item.localPath).canonicalFilePath();
            if (!ic.isEmpty() && ic == canon)
                return;
        } else if (music.id > 0 && item.id == music.id) {
            return;
        }
    }
    m_playlist.append(music);
    if (m_currentIndex == -1) {
        m_currentIndex = 0;
    }
    PlaylistDatabase::instance().addToQueue(music);
    syncShufflePool();
    emit playlistChanged();
}

void PlaylistManager::addAllToPlaylist(const QList<MusicInfo>& musicList) {
    if (musicList.isEmpty())
        return;

    // 去重键每首只算一次再放进 QSet：
    // 旧实现是「每首新歌 × 整条队列」的双重循环，本地文件还会对每个已有条目
    // 调一次 canonicalFilePath()（磁盘 stat），几千首就是几百万次系统调用，
    // 点「播放全部」时直接卡死。
    QSet<QString> seen;
    seen.reserve(m_playlist.size() + musicList.size());
    for (const MusicInfo &item : m_playlist) {
        QString key;
        if (musicDedupeKey(item, &key))
            seen.insert(key);
    }

    QList<MusicInfo> appended;
    appended.reserve(musicList.size());
    for (const MusicInfo &music : musicList) {
        QString key;
        if (musicDedupeKey(music, &key)) {
            if (seen.contains(key))
                continue;
            seen.insert(key);
        }
        appended.append(music);
    }

    if (!appended.isEmpty()) {
        // 队列与数据库各写一次，避免逐首 INSERT（每首一次事务提交）
        m_playlist += appended;
        PlaylistDatabase::instance().addAllToQueue(appended);
    }
    if (m_currentIndex == -1 && !m_playlist.isEmpty())
        m_currentIndex = 0;

    syncShufflePool();
    emit playlistChanged();
}

void PlaylistManager::replacePlaylist(const QList<MusicInfo>& musicList, int currentIndex) {
    // 按曲目稳定标识去重：本地文件用规范化路径参与，不能按 id <= 0 直接丢弃
    QList<MusicInfo> uniqueMusic;
    QSet<QString> seenKeys;
    for (const MusicInfo &music : musicList) {
        const QString key = musicKeyOf(music);
        if (key.isEmpty() || seenKeys.contains(key))
            continue;
        seenKeys.insert(key);
        uniqueMusic.append(music);
    }

    m_playlist = uniqueMusic;
    m_currentIndex = m_playlist.isEmpty()
        ? -1
        : qBound(0, currentIndex, m_playlist.size() - 1);
    m_forcedNextKey.clear();
    PlaylistDatabase::instance().setQueueMusic(m_playlist, m_currentIndex);
    syncShufflePool();
    emit playlistChanged();
    emit currentIndexChanged(m_currentIndex);
}

bool PlaylistManager::playNext(const MusicInfo& music) {
    const QString key = musicKeyOf(music);
    if (key.isEmpty())
        return false;

    const bool hasCurrent = m_currentIndex >= 0 && m_currentIndex < m_playlist.size();

    int index = indexOfKey(key);
    if (index < 0) {
        m_playlist.append(music);
        index = m_playlist.size() - 1;
    }

    if (!hasCurrent) {
        // 队列里没有正在播放的曲目：它直接成为当前曲目，由调用方负责起播
        m_currentIndex = index;
        m_forcedNextKey.clear();
        PlaylistDatabase::instance().setQueueMusic(m_playlist, m_currentIndex);
        syncShufflePool();
        emit playlistChanged();
        emit currentIndexChanged(m_currentIndex);
        return false;
    }

    if (index != m_currentIndex) {
        // 插到当前曲目之后：后插入的「下一首播放」排在先插入的前面
        const MusicInfo item = m_playlist.takeAt(index);
        if (index < m_currentIndex)
            --m_currentIndex;
        m_playlist.insert(m_currentIndex + 1, item);
    }
    m_forcedNextKey = key;

    PlaylistDatabase::instance().setQueueMusic(m_playlist, m_currentIndex);
    syncShufflePool();
    emit playlistChanged();
    return true;
}

void PlaylistManager::removeFromPlaylist(int localId) {
    int index = findIndexByLocalId(localId);
    if (index >= 0) {
        if (musicKeyOf(m_playlist.at(index)) == m_forcedNextKey)
            m_forcedNextKey.clear();
        m_playlist.removeAt(index);
        if (m_currentIndex >= m_playlist.size()) {
            m_currentIndex = m_playlist.isEmpty() ? -1 : m_playlist.size() - 1;
        }
        // Rebuild queue in DB
        save();
        emit playlistChanged();
    }
}

void PlaylistManager::clearPlaylist() {
    m_playlist.clear();
    m_currentIndex = -1;
    m_forcedNextKey.clear();
    m_shuffleBag.reset();
    PlaylistDatabase::instance().clearQueue();
    persistShuffleState();
    emit playlistChanged();
}

void PlaylistManager::setPlayMode(const QString& mode) {
    m_playMode = mode;
    PlaylistDatabase::instance().setQueuePlayMode(mode);
    // 进出随机模式不重置洗牌袋：保留进度，只与当前队列对齐
    syncShufflePool();
    emit playModeChanged(mode);
}

void PlaylistManager::togglePlayMode() {
    if (m_playMode == "list") {
        m_playMode = "single";
    } else if (m_playMode == "single") {
        m_playMode = "random";
    } else {
        m_playMode = "list";
    }
    PlaylistDatabase::instance().setQueuePlayMode(m_playMode);
    syncShufflePool();
    emit playModeChanged(m_playMode);
}

void PlaylistManager::setCurrentIndex(int index) {
    if (m_currentIndex == index)
        return;
    // 随机播放：登记当前曲；用户手动点歌时会从待播队列摘掉，避免本轮重复随到。
    // 正常的"下一首"流程已经消费过游标，这里对同一首是幂等的。
    if (m_playMode == "random" && index >= 0 && index < m_playlist.size()) {
        m_shuffleBag.onUserPicked(musicKeyOf(m_playlist.at(index)), poolKeys());
        persistShuffleState();
    }
    m_currentIndex = index;
    PlaylistDatabase::instance().setQueueCurrentIndex(index);
    emit currentIndexChanged(index);
}

int PlaylistManager::nextIndex() {
    if (m_playlist.isEmpty()) return -1;

    // 「下一首播放」的曲目优先级最高：单曲循环 / 随机播放也必须先播它
    if (!m_forcedNextKey.isEmpty()) {
        const QString forcedKey = m_forcedNextKey;
        m_forcedNextKey.clear();
        const int forcedIndex = indexOfKey(forcedKey);
        if (forcedIndex >= 0) {
            if (m_playMode == "random") {
                // 从洗牌袋待播队列摘掉并记入历史，避免本轮重复随到
                m_shuffleBag.onUserPicked(forcedKey, poolKeys());
                persistShuffleState();
            }
            return forcedIndex;
        }
    }

    if (m_playMode == "single") {
        return m_currentIndex;
    } else if (m_playMode == "random") {
        // 随机播放：由洗牌袋顺序决定，一轮内每首歌只播一次
        const QString key = m_shuffleBag.commitNext(poolKeys());
        persistShuffleState();
        const int index = indexOfKey(key);
        if (index < 0) {
            qWarning() << "[随机播放] 洗牌袋未能给出下一首，队列大小:" << m_playlist.size();
        } else {
            qDebug() << "[随机播放] 下一首:" << m_playlist.at(index).title
                     << "游标:" << m_shuffleBag.cursor() << "/" << m_shuffleBag.totalCount()
                     << "待播:" << m_shuffleBag.pendingCount();
        }
        return index;
    } else {
        // list mode: loop
        return (m_currentIndex + 1) % m_playlist.size();
    }
}

int PlaylistManager::previousIndex() {
    if (m_playlist.isEmpty()) return -1;

    if (m_playMode == "random") {
        // 随机播放：沿洗牌袋历史回退；没有历史时退化为列表顺序上一首
        const QString key = m_shuffleBag.previous(poolKeys());
        persistShuffleState();
        const int index = indexOfKey(key);
        if (index >= 0)
            return index;
    }

    // 列表循环 / 单曲循环：向前一个
    return (m_currentIndex - 1 + m_playlist.size()) % m_playlist.size();
}

QStringList PlaylistManager::poolKeys() const {
    QStringList keys;
    QSet<QString> seen;
    for (const MusicInfo &music : m_playlist) {
        const QString key = musicKeyOf(music);
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        keys.append(key);
    }
    return keys;
}

int PlaylistManager::indexOfKey(const QString &key) const {
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_playlist.size(); ++i) {
        if (musicKeyOf(m_playlist.at(i)) == key)
            return i;
    }
    return -1;
}

void PlaylistManager::syncShufflePool() {
    m_shuffleBag.syncPool(poolKeys());
    persistShuffleState();
}

void PlaylistManager::persistShuffleState() {
    PlaylistDatabase::instance().setQueueStateValue(QStringLiteral("shuffleState"),
                                                     m_shuffleBag.serialize());
}

int PlaylistManager::findIndexByLocalId(int localId) const {
    for (int i = 0; i < m_playlist.size(); ++i) {
        if (m_playlist[i].id == localId) {
            return i;
        }
    }
    return -1;
}

MusicInfo PlaylistManager::lastPlayedMusic() const {
    if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        return m_playlist[m_currentIndex];
    }
    return MusicInfo();
}
