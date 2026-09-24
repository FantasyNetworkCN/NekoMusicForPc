/**
 * @file i18n.cpp
 * @brief 简易国际化实现
 */

#include "i18n.h"
#include <QMap>

I18n &I18n::instance()
{
    static I18n i;
    return i;
}

void I18n::setLanguage(Language lang)
{
    m_lang = lang;
}

static QMap<QString, QString> zhCN()
{
    QMap<QString, QString> m;
    // --- 基础通用操作 ---
    m["search"] = QStringLiteral("搜索");
    m["searchPlaceholder"] = QStringLiteral("搜索音乐...");
    m["loading"] = QStringLiteral("加载中...");
    m["noData"] = QStringLiteral("暂无数据");
    m["cancel"] = QStringLiteral("取消");
    m["confirm"] = QStringLiteral("确认");
    m["delete"] = QStringLiteral("删除");
    m["edit"] = QStringLiteral("编辑");
    m["save"] = QStringLiteral("保存");
    m["close"] = QStringLiteral("关闭");
    m["submit"] = QStringLiteral("提交");
    m["back"] = QStringLiteral("返回");
    m["refresh"] = QStringLiteral("刷新");
    m["clear"] = QStringLiteral("清空");
    m["all"] = QStringLiteral("全部");
    m["create"] = QStringLiteral("创建");
    m["rename"] = QStringLiteral("重命名");
    m["actions"] = QStringLiteral("操作");
    m["none"] = QStringLiteral("无");
    m["unknown"] = QStringLiteral("未知");
    m["defaultMusicPlayerTitle"] = QStringLiteral("默认音乐播放器");
    m["defaultMusicPlayerMessage"] = QStringLiteral(
        "当前可能尚未将 Neko 云音乐设为系统默认的音乐打开方式（如 MP3、FLAC、AAC/M4A、WAV、OGG、WMA、"
        "M3U/PLS 等）。\n"
        "是否尝试将其设为默认？");
    m["defaultMusicPlayerYes"] = QStringLiteral("设为默认");
    m["defaultMusicPlayerNo"] = QStringLiteral("暂不");
    m["defaultMusicPlayerDontAskAgain"] = QStringLiteral("不再询问");
    m["defaultMusicPlayerWinFollowUp"] = QStringLiteral(
        "已打开 Windows「默认应用」设置。\n"
        "请将所有支持的媒体文件类型（.mp3、.flac、.wav、.m4a、.aac、.ogg、.wma、.m3u 等）的默认打开方式设置为本程序。");
    m["inputRequired"] = QStringLiteral("请输入");
    m["inputValid"] = QStringLiteral("请输入有效的内容");
    m["justNow"] = QStringLiteral("刚刚");
    m["minutesAgo"] = QStringLiteral("分钟前");
    m["hoursAgo"] = QStringLiteral("小时前");
    m["daysAgo"] = QStringLiteral("天前");
    m["monthsAgo"] = QStringLiteral("个月前");
    m["yearsAgo"] = QStringLiteral("年前");

    // --- 播放器 & 媒体控制 ---
    m["play"] = QStringLiteral("播放");
    m["pause"] = QStringLiteral("暂停");
    m["previous"] = QStringLiteral("上一首");
    m["next"] = QStringLiteral("下一首");
    m["favorite"] = QStringLiteral("收藏");
    m["unfavorite"] = QStringLiteral("取消收藏");
    m["playAll"] = QStringLiteral("播放全部");
    m["playlist"] = QStringLiteral("播放列表");
    m["comments"] = QStringLiteral("评论");
    m["playQueue"] = QStringLiteral("播放队列");
    m["playlistSongCount"] = QStringLiteral("%1 首音乐");
    m["scrollToCurrentPlay"] = QStringLiteral("当前播放");
    m["emptyPlaylist"] = QStringLiteral("播放列表为空");
    m["emptyPlaylistHint"] = QStringLiteral("播放列表暂无歌曲，快去添加吧");
    m["notPlaying"] = QStringLiteral("未播放");
    m["volume"] = QStringLiteral("音量");
    m["muted"] = QStringLiteral("已静音");
    m["unmuted"] = QStringLiteral("取消静音");
    m["toggleMute"] = QStringLiteral("取消静音");
    m["mute"] = QStringLiteral("静音");
    m["desktopLyrics"] = QStringLiteral("词");
    m["desktopLyricsEnabled"] = QStringLiteral("桌面歌词已开启");
    m["desktopLyricsDisabled"] = QStringLiteral("桌面歌词已关闭");
    m["theme"] = QStringLiteral("主题");
    m["themeSystem"] = QStringLiteral("跟随系统");
    m["themeDark"] = QStringLiteral("暗色模式");
    m["themeLight"] = QStringLiteral("亮色模式");
    m["settingsDesc"] = QStringLiteral("集中管理语言、外观、快捷键与版本信息");
    m["settingsTabGeneral"] = QStringLiteral("通用");
    m["settingsTabAppearance"] = QStringLiteral("外观");
    m["settingsTabShortcuts"] = QStringLiteral("快捷键");
    m["settingsTabAbout"] = QStringLiteral("关于");
    m["personalization"] = QStringLiteral("个性化");
    m["shellBackdrop"] = QStringLiteral("窗口背景");
    m["shellBackdropDefaultImage"] = QStringLiteral("默认图片");
    m["shellBackdropCustomImage"] = QStringLiteral("自定义图片");
    m["shellBackdropSolidColor"] = QStringLiteral("纯色");
    m["shellBackdropPickImage"] = QStringLiteral("选择图片");
    m["shellBackdropPickColor"] = QStringLiteral("选择颜色");
    m["shellBackdropResetImage"] = QStringLiteral("恢复默认图片");
    m["shellBackdropUsingDefault"] = QStringLiteral("当前使用内置默认背景图");
    m["shellBackdropNoCustomImage"] = QStringLiteral("尚未选择自定义图片");
    m["shellBackdropCurrentFile"] = QStringLiteral("当前文件");
    m["shellBackdropImageInvalid"] = QStringLiteral("无法加载该图片，请选择其它文件");
    m["shortcuts"] = QStringLiteral("快捷键");
    m["shortcutPlayPause"] = QStringLiteral("播放/暂停");
    m["shortcutPreviousTrack"] = QStringLiteral("上一曲");
    m["shortcutNextTrack"] = QStringLiteral("下一曲");
    m["shortcutPressKeys"] = QStringLiteral("按下快捷键…");
    m["shortcutNotSet"] = QStringLiteral("未设置");
    m["shortcutResetDefault"] = QStringLiteral("恢复默认");
    m["shortcutResetAll"] = QStringLiteral("恢复全部默认");
    m["shortcutConflict"] = QStringLiteral("快捷键冲突：%1");
    m["shortcutWaylandHint"] = QStringLiteral(
        "全局快捷键通过 xdg-desktop-portal 向系统申请权限。首次启动或修改后可能弹出系统授权对话框；"
        "也可点击下方按钮在系统设置中调整。");
    m["shortcutOpenSystemSettings"] = QStringLiteral("在系统设置中配置全局快捷键");
    m["shortcutGlobalPortalActive"] = QStringLiteral("全局快捷键已启用（系统 portal）");
    m["shortcutGlobalFallbackActive"] = QStringLiteral("portal 不可用，已回退为窗口内快捷键");
    m["shortcutGlobalInactive"] = QStringLiteral("全局快捷键未启用");
    m["shortcutGlobalUnavailable"] = QStringLiteral("当前环境不支持全局快捷键（需要 xdg-desktop-portal GlobalShortcuts）");
    m["shortcutGlobalPortalFailed"] = QStringLiteral("全局快捷键注册失败：%1（已尝试窗口内回退）");
    m["shortcutGlobalConfigureFailed"] = QStringLiteral("无法打开系统快捷键授权界面：%1");
    m["shortcutGlobalWinActive"] = QStringLiteral("全局快捷键已启用（Windows）");
    m["shortcutGlobalWinFailed"] = QStringLiteral("全局快捷键注册失败：%1（已回退为窗口内快捷键）");
    m["shortcutGlobalWinPartialFailed"] = QStringLiteral("部分全局快捷键未生效（可能被其他程序占用）：%1");
    m["shortcutMicSync"] = QStringLiteral("麦克风同步（把播放声音送到麦克风）");
    m["shortcutToggleDesktopLyrics"] = QStringLiteral("桌面歌词开关");
    m["micSyncEnabled"] = QStringLiteral("麦克风同步已开启：播放声音已合并进麦克风「%1」，通话 / 会议软件用默认输入即可听到");
    m["micSyncDisabled"] = QStringLiteral("麦克风同步已关闭，默认麦克风已还原");
    m["micSyncFailed"] = QStringLiteral("麦克风同步开启失败：%1");
    m["micSyncUnsupported"] = QStringLiteral("当前平台暂不支持麦克风同步（目前仅支持 Linux）");
    m["micSyncSection"] = QStringLiteral("麦克风同步");
    m["micSyncEnable"] = QStringLiteral("将播放声音合并进麦克风");
    m["micSyncHint"] = QStringLiteral(
        "开启后会把正在播放的音乐合并进默认麦克风（%1），并接管系统默认输入设备；"
        "语音 / 会议 / 直播软件无需手动改设置即可同步听到。关闭后自动还原原来的麦克风。"
        "当前快捷键：%2（可在「快捷键」设置中修改）。");
    m["micSyncUnsupportedHint"] = QStringLiteral("当前平台暂不支持麦克风同步，目前仅 Linux 可用。");
    m["micSyncHintWindows"] = QStringLiteral(
        "开启后会把本应用的播放输出切到虚拟声卡；在语音 / 会议 / 直播软件里把输入设备选为「%1」，"
        "即可同步听到播放的音乐。关闭后自动切回原来的输出设备。需要先安装 VB-Audio Virtual Cable "
        "或 VoiceMeeter。当前快捷键：%2（可在「快捷键」设置中修改）。");
    m["micSyncWindowsNoCable"] = QStringLiteral(
        "未检测到虚拟声卡。Windows 需要先安装 VB-Audio Virtual Cable 或 VoiceMeeter，"
        "才能把播放声音合并进麦克风。");
    m["micSyncInstallPending"] = QStringLiteral("正在安装 NekoMusic 虚拟麦克风。安装完成后请再次开启麦克风同步。");
    m["playModeList"] = QStringLiteral("列表循环");
    m["playModeSingle"] = QStringLiteral("单曲循环");
    m["playModeRandom"] = QStringLiteral("随机播放");
    m["playModeOff"] = QStringLiteral("不循环");
    m["minimize"] = QStringLiteral("最小化");
    m["maximize"] = QStringLiteral("全屏");
    m["closeWindow"] = QStringLiteral("关闭窗口");
    m["duration"] = QStringLiteral("时长");
    m["title"] = QStringLiteral("标题");
    m["cover"] = QStringLiteral("封面");
    m["songs"] = QStringLiteral("歌曲");
    m["playlists"] = QStringLiteral("歌单");
    m["album"] = QStringLiteral("专辑");
    m["artist"] = QStringLiteral("艺术家");
    m["count"] = QStringLiteral("数量");
    m["musicCount"] = QStringLiteral("首歌曲");
    m["songList"] = QStringLiteral("歌曲列表");

    // --- 导航 & 视图名称 ---
    m["home"] = QStringLiteral("首页");
    m["myPlaylists"] = QStringLiteral("我的歌单");
    m["myPlaylistsTitle"] = QStringLiteral("我的歌单");
    m["favorites"] = QStringLiteral("我喜欢的");
    m["myFavorites"] = QStringLiteral("我喜欢的音乐");
    m["favoritePageSongCount"] = QStringLiteral("共 %1 首");
    m["listColTitle"] = QStringLiteral("标题");
    m["listColAlbum"] = QStringLiteral("专辑");
    m["listColRank"] = QStringLiteral("排行");
    m["listColPlayCount"] = QStringLiteral("播放量");
    m["listColUploaded"] = QStringLiteral("上传时间");
    m["playCountFmt"] = QStringLiteral("%1 次播放");
    m["listColActions"] = QStringLiteral("操作");
    m["fuzzySearch"] = QStringLiteral("模糊搜索");
    m["favoritesSearchEmpty"] = QStringLiteral("搜不到关于 %1 的任何歌曲呀");
    m["favoritePlaylists"] = QStringLiteral("收藏的歌单");
    m["favoritePlaylistsTitle"] = QStringLiteral("收藏的歌单");
    m["recentPlay"] = QStringLiteral("最近播放");
    m["emptyRecent"] = QStringLiteral("暂无最近播放记录");
    m["clearRecentList"] = QStringLiteral("清空列表");
    m["clearRecentConfirm"] = QStringLiteral("确认清空最近播放列表？该操作不可撤销！");
    m["clearRecentSuccess"] = QStringLiteral("最近播放列表已清空");
    m["downloadManage"] = QStringLiteral("下载管理");
    m["downloadInProgress"] = QStringLiteral("正在下载");
    m["downloadCompletedTab"] = QStringLiteral("已完成");
    m["emptyDownloading"] = QStringLiteral("暂无正在下载的歌曲");
    m["downloadWaiting"] = QStringLiteral("等待下载");
    m["downloadQueued"] = QStringLiteral("排队中");
    m["downloadingStatus"] = QStringLiteral("下载中");
    m["cancelDownload"] = QStringLiteral("取消下载");
    m["downloadCancelled"] = QStringLiteral("已取消下载");
    m["emptyDownloads"] = QStringLiteral("暂无已完成的下载，可在歌曲列表中下载音乐");
    m["clearDownloads"] = QStringLiteral("清空下载");
    m["clearDownloadsConfirm"] = QStringLiteral("确认清空所有下载记录并删除本地文件？该操作不可撤销！");
    m["clearDownloadsSuccess"] = QStringLiteral("下载记录已清空");
    m["downloadMusic"] = QStringLiteral("下载");
    m["downloadStarted"] = QStringLiteral("下载已开始");
    m["alreadyDownloaded"] = QStringLiteral("该歌曲已下载");
    m["downloadFolderHint"] = QStringLiteral("保存位置：%1");
    m["downloadAll"] = QStringLiteral("下载全部");
    m["batchDownloadStarted"] = QStringLiteral("已开始下载 %1 首歌曲");
    m["allSongsDownloaded"] = QStringLiteral("所有歌曲均已下载");
    m["localMusicBadge"] = QStringLiteral("本地");
    m["hasLyrics"] = QStringLiteral("有歌词");
    m["ranking"] = QStringLiteral("排行榜");
    m["latest"] = QStringLiteral("最新音乐");
    m["library"] = QStringLiteral("音乐库");
    m["topSongs"] = QStringLiteral("热门歌曲");
    m["hotMusic"] = QStringLiteral("热门音乐");
    m["hotMusicDesc"] = QStringLiteral("播放次数最高的热门歌曲");
    m["latestMusic"] = QStringLiteral("最新音乐");
    m["latestMusicDesc"] = QStringLiteral("刚刚上传的最新歌曲");
    m["dailyRecommend"] = QStringLiteral("每日推荐");
    m["dailyRecommendDesc"] = QStringLiteral("根据你的收藏口味生成");
    m["newSongs"] = QStringLiteral("新歌上架");
    m["discoverMusic"] = QStringLiteral("去发现音乐");
    m["recommendPlaylists"] = QStringLiteral("推荐歌单");
    m["recommend_playlists"] = QStringLiteral("推荐歌单");
    m["hot_music"] = QStringLiteral("热门音乐");
    m["latest_music"] = QStringLiteral("最新音乐");
    m["searchMusic"] = QStringLiteral("单曲");
    m["searchPlaylist"] = QStringLiteral("歌单");
    m["searchResults"] = QStringLiteral("搜索结果");
    m["searchRelatedSuffix"] = QStringLiteral("的相关搜索");
    m["searchStartHint"] = QStringLiteral("请先在顶部搜索框输入关键词并搜索");
    m["searchEmptySongs"] = QStringLiteral("很抱歉，未能找到与 %1 相关的任何歌曲");
    m["searchEmptyPlaylists"] = QStringLiteral("很抱歉，未能找到与 %1 相关的任何歌单");
    m["searchEmptyArtists"] = QStringLiteral("很抱歉，未能找到与 %1 相关的任何歌手");
    m["searchBackArtists"] = QStringLiteral("← 歌手");
    m["searchBackToSearch"] = QStringLiteral("返回搜索");
    m["loadMore"] = QStringLiteral("加载更多");
    m["noResults"] = QStringLiteral("暂无搜索结果");

    // --- 账户 & 登录注册 ---
    m["login"] = QStringLiteral("登录");
    m["loginSubtitle"] = QStringLiteral("登录 NekoMusic，继续享受你的音乐空间");
    m["registerSubtitle"] = QStringLiteral("创建账号，开启你的专属音乐空间");
    m["qrLoginSubtitle"] = QStringLiteral("使用手机扫码，快速安全地登录");
    m["qrLogin"] = QStringLiteral("扫码登录");
    m["qrLoginTitle"] = QStringLiteral("扫码登录");
    m["qrLoginBack"] = QStringLiteral("账号密码登录");
    m["qrLoginHint"] = QStringLiteral("打开手机端 NekoMusic，点「我的」页右上角的「扫一扫」，扫描上方二维码");
    m["qrLoginLoading"] = QStringLiteral("正在生成二维码…");
    m["qrLoginPending"] = QStringLiteral("正在等待扫码…");
    m["qrLoginScanned"] = QStringLiteral("已扫码，请在手机上确认");
    m["qrLoginCanceled"] = QStringLiteral("已在手机上取消登录");
    m["qrLoginConnectionLost"] = QStringLiteral("连接已断开，请点击刷新");
    m["logout"] = QStringLiteral("退出登录");
    m["register"] = QStringLiteral("注册");
    m["email"] = QStringLiteral("邮箱");
    m["password"] = QStringLiteral("密码");
    m["nickname"] = QStringLiteral("昵称");
    m["nickname"] = QStringLiteral("昵称");
    m["nicknamePlaceholder"] = QStringLiteral("请输入新昵称（1-20字）");
    m["nicknameUpdated"] = QStringLiteral("昵称修改成功");
    m["nicknameUpdateFailed"] = QStringLiteral("昵称修改失败，请稍后重试");
    m["emailOrName"] = QStringLiteral("邮箱/名称");
    m["verificationCode"] = QStringLiteral("验证码");
    m["verifyCode"] = QStringLiteral("验证码");
    m["getCode"] = QStringLiteral("获取验证码");
    m["sendCode"] = QStringLiteral("发送验证码");
    m["resendCode"] = QStringLiteral("重新发送");
    m["registerNeedNicknameForCode"] = QStringLiteral("发送验证码前请先填写昵称");
    m["captchaSecurityTitle"] = QStringLiteral("安全验证");
    m["captchaSecurityDesc"] =
        QStringLiteral("拖动下方滑块对齐拼图，松开后自动校验；验证通过后将向你的邮箱发送验证码。");
    m["captchaLoading"] = QStringLiteral("正在加载拼图…");
    m["captchaVerifying"] = QStringLiteral("校验中…");
    m["captchaRefresh"] = QStringLiteral("换一张");
    m["captchaImageError"] = QStringLiteral("拼图图片解析失败");
    m["captchaLoadFailed"] = QStringLiteral("加载安全验证失败");
    m["captchaVerifyFail"] = QStringLiteral("位置不正确或已失效，请换一张重试");
    m["registerTime"] = QStringLiteral("注册时间");
    m["accountInfo"] = QStringLiteral("账号信息");
    m["account"] = QStringLiteral("账号");
    m["accountSettings"] = QStringLiteral("账号设置");
    m["notLoggedIn"] = QStringLiteral("未登录");
    m["goToLogin"] = QStringLiteral("去登录");
    m["forgotPassword"] = QStringLiteral("忘记密码?");
    m["forgotPasswordTitle"] = QStringLiteral("找回密码");
    m["forgotPasswordEmailHint"] = QStringLiteral("请输入注册时的邮箱地址");
    m["forgotPasswordCodeHint"] = QStringLiteral("请输入邮箱中的验证码");
    m["newPassword"] = QStringLiteral("新密码(6-30位)");
    m["confirmPassword"] = QStringLiteral("确认新密码");
    m["resetPassword"] = QStringLiteral("重置密码");
    m["backToLogin"] = QStringLiteral("返回登录");
    m["passwordLengthError"] = QStringLiteral("密码长度必须在6-30位之间");
    m["passwordMismatch"] = QStringLiteral("两次输入的密码不一致");
    m["passwordResetSuccess"] = QStringLiteral("密码重置成功,请登录");
    m["passwordTooLong"] = QStringLiteral("密码长度不能超过128位");
    m["inputTooLong"] = QStringLiteral("输入内容超出长度限制");
    m["loadingShort"] = QStringLiteral("处理中…");
    m["countdownSeconds"] = QStringLiteral("%1秒");
    m["welcome"] = QStringLiteral("欢迎回来");
    m["welcomeSubtitle"] = QStringLiteral("发现好音乐，享受每一刻");
    m["goodMorning"] = QStringLiteral("早上好");
    m["goodAfternoon"] = QStringLiteral("下午好");
    m["goodEvening"] = QStringLiteral("晚上好");
    m["lateNight"] = QStringLiteral("夜深了");

    // --- 歌单特定操作 ---
    m["createPlaylist"] = QStringLiteral("创建歌单");
    m["createFirstPlaylist"] = QStringLiteral("创建第一个歌单");
    m["noPlaylists"] = QStringLiteral("暂无歌单，请先创建歌单");
    m["playlistName"] = QStringLiteral("歌单名称");
    m["playlistDescription"] = QStringLiteral("歌单描述");
    m["playlistNamePlaceholder"] = QStringLiteral("输入歌单名称");
    m["playlistDescPlaceholder"] = QStringLiteral("输入歌单描述");
    m["creator"] = QStringLiteral("创建者");
    m["editPlaylist"] = QStringLiteral("编辑歌单");
    m["deletePlaylist"] = QStringLiteral("删除歌单");
    m["renamePlaylist"] = QStringLiteral("重命名歌单");
    m["modifyPlaylistDesc"] = QStringLiteral("修改歌单描述");
    m["inputNewPlaylistName"] = QStringLiteral("输入新歌单名称");
    m["inputPlaylistDesc"] = QStringLiteral("输入歌单描述");
    m["inputPlaylistName"] = QStringLiteral("请输入歌单名称");
    m["collectPlaylist"] = QStringLiteral("收藏歌单");
    // 版本更新
    m["newVersionAvailable"] = QStringLiteral("发现新版本");
    m["currentVersion"] = QStringLiteral("当前版本");
    m["newVersion"] = QStringLiteral("新版本");
    m["remindLater"] = QStringLiteral("稍后提醒");
    m["updateNow"] = QStringLiteral("立即更新");
    m["downloading"] = QStringLiteral("下载中...");
    m["downloadComplete"] = QStringLiteral("下载完成");
    m["installNow"] = QStringLiteral("立即安装");
    m["installLater"] = QStringLiteral("稍后安装");
    m["updateViaAur"] = QStringLiteral("通过 AUR 更新");
    m["updateViaAurHint"] = QStringLiteral("点击「立即更新」后将在终端显示命令，请自行执行：%1");
    m["updateViaAurNoHelper"] = QStringLiteral("未检测到 yay 或 paru，将打开 AUR 包页面");
    m["openAurPage"] = QStringLiteral("打开 AUR 页面");
    m["updateOpenWebHint"] = QStringLiteral("请从网页下载并安装新版本");
    m["downloadFailed"] = QStringLiteral("下载失败");
    m["retry"] = QStringLiteral("重试");
    m["checkForUpdates"] = QStringLiteral("检查更新");
    m["alreadyLatest"] = QStringLiteral("已是最新版本");
    m["version"] = QStringLiteral("版本");
    m["collectedPlaylist"] = QStringLiteral("已收藏歌单");
    m["uncollectPlaylist"] = QStringLiteral("取消收藏");
    m["removeFromPlaylist"] = QStringLiteral("从歌单中移除");
    m["addToPlaylist"] = QStringLiteral("添加到歌单");
    m["addToLocalPlaylist"] = QStringLiteral("添加到本地歌单");
    m["addToQueue"] = QStringLiteral("添加到播放队列");
    m["playNext"] = QStringLiteral("下一首播放");
    m["playNextAdded"] = QStringLiteral("已添加到下一首播放");
    m["addToPlaylistTitle"] = QStringLiteral("添加到歌单");
    m["createNewPlaylistRow"] = QStringLiteral("创建新歌单");
    m["addingToPlaylist"] = QStringLiteral("正在添加歌曲至歌单");
    m["selectPlaylist"] = QStringLiteral("选择歌单");
    m["orCreateNewPlaylist"] = QStringLiteral("或创建新歌单");
    m["addMusicToPlaylist"] = QStringLiteral("添加音乐到歌单");
    m["newPlaylist"] = QStringLiteral("新歌单");
    m["ok"] = QStringLiteral("确定");
    m["musicAdded"] = QStringLiteral("音乐已添加到歌单");
    m["musicAddFailed"] = QStringLiteral("添加音乐失败");
    m["noMusicInPlaylist"] = QStringLiteral("该歌单暂无音乐");

    // --- 系统设置 & 缓存 & 更新 ---
    m["settings"] = QStringLiteral("设置");
    m["playSettings"] = QStringLiteral("播放设置");
    m["language"] = QStringLiteral("语言");
    m["languageSettings"] = QStringLiteral("语言设置");
    m["languageNya"] = QStringLiteral("喵语中文");
    m["languageChinese"] = QStringLiteral("简体中文");
    m["languageEnglish"] = QStringLiteral("English");
    m["musicCache"] = QStringLiteral("音乐缓存");
    m["musicCacheDesc"] = QStringLiteral("自动缓存听过的音乐到本地下载目录");
    m["cachePath"] = QStringLiteral("缓存路径");
    m["cacheEnabled"] = QStringLiteral("音乐缓存已开启");
    m["cacheDisabled"] = QStringLiteral("音乐缓存已关闭");
    m["loadFromCache"] = QStringLiteral("从缓存加载");
    m["about"] = QStringLiteral("关于");
    m["appVersion"] = QStringLiteral("版本号");
    m["githubRepo"] = QStringLiteral("GitHub 仓库");
    m["apiDocs"] = QStringLiteral("API 文档");
    m["createdAt"] = QStringLiteral("创建时间");
    m["updatedAt"] = QStringLiteral("更新时间");
    m["description"] = QStringLiteral("描述");
    m["checkUpdate"] = QStringLiteral("检查更新");
    m["checking"] = QStringLiteral("检查中...");
    m["updateAvailable"] = QStringLiteral("发现新版本");
    m["downloadUpdate"] = QStringLiteral("立即下载");
    m["downloadCancelled"] = QStringLiteral("下载已取消");
    m["isLatest"] = QStringLiteral("当前已是最新版本");
    m["versionDownloaded"] = QStringLiteral("版本 {version} 已下载完成");
    m["updateDownloadComplete"] = QStringLiteral("更新下载完成");

    // --- 交互反馈 (成功/失败/校验) ---
    m["operationFailed"] = QStringLiteral("操作失败");
    m["networkError"] = QStringLiteral("网络错误，请检查服务器连接");
    m["networkErrorRetry"] = QStringLiteral("网络错误，请重试");
    m["lanDeviceConnected"] = QStringLiteral("已连接到%1");
    m["loginSuccess"] = QStringLiteral("登录成功，欢迎回来！");
    m["loginFailed"] = QStringLiteral("登录失败");
    m["logoutSuccess"] = QStringLiteral("已退出登录");
    m["registerSuccess"] = QStringLiteral("注册成功，请登录");
    m["registerFailed"] = QStringLiteral("注册失败");
    m["createSuccess"] = QStringLiteral("创建成功");
    m["createFailed"] = QStringLiteral("创建失败");
    m["createPlaylistSuccess"] = QStringLiteral("歌单创建成功");
    m["createPlaylistFailed"] = QStringLiteral("创建失败");
    m["deleteSuccess"] = QStringLiteral("删除成功");
    m["deleteFailed"] = QStringLiteral("删除失败");
    m["modifySuccess"] = QStringLiteral("修改成功");
    m["renameSuccess"] = QStringLiteral("重命名成功");
    m["updateSuccess"] = QStringLiteral("更新成功");
    m["favoriteSuccess"] = QStringLiteral("收藏成功");
    m["favoriteFailed"] = QStringLiteral("收藏失败");
    m["cancelFavoriteSuccess"] = QStringLiteral("已取消收藏");
    m["cancelFavoriteFailed"] = QStringLiteral("取消收藏失败");
    m["cancelFavoritePlaylistSuccess"] = QStringLiteral("已取消收藏歌单");
    m["cancelFavoritePlaylistFailed"] = QStringLiteral("取消收藏失败");
    m["collectPlaylistSuccess"] = QStringLiteral("已收藏歌单");
    m["collectPlaylistFailed"] = QStringLiteral("收藏失败");
    m["unfavoriteSuccess"] = QStringLiteral("取消收藏成功");
    m["addToPlaylistSuccess"] = QStringLiteral("添加到歌单成功");
    m["addToPlaylistFailed"] = QStringLiteral("添加失败");
    m["addSuccess"] = QStringLiteral("添加成功");
    m["removeSuccess"] = QStringLiteral("移除成功");
    m["removeFailed"] = QStringLiteral("移除失败");
    m["musicRemovedSuccess"] = QStringLiteral("音乐已从歌单中移除");
    m["switchLanguageSuccess"] = QStringLiteral("语言设置已保存");
    m["switchLanguageFailed"] = QStringLiteral("切换语言失败");
    m["checkUpdateFailed"] = QStringLiteral("检查更新失败，请稍后重试");
    m["showWindow"] = QStringLiteral("显示窗口");
    m["exitApp"] = QStringLiteral("退出");
    m["sendVerifyCodeFailed"] = QStringLiteral("发送验证码失败");
    m["verifyCodeSent"] = QStringLiteral("验证码已发送至您的邮箱");
    m["codeSent"] = QStringLiteral("验证码已发送");
    m["inputEmail"] = QStringLiteral("请先输入邮箱地址");
    m["inputValidEmail"] = QStringLiteral("请输入有效的邮箱地址");
    m["emailRequired"] = QStringLiteral("请输入邮箱");
    m["passwordRequired"] = QStringLiteral("请输入密码");
    m["codeRequired"] = QStringLiteral("请输入验证码");
    m["nameRequired"] = QStringLiteral("请输入昵称");
    m["inputNameRequired"] = QStringLiteral("请输入歌单名称");
    m["fillEmailAndPassword"] = QStringLiteral("请填写邮箱和密码");
    m["fillEmail"] = QStringLiteral("请填写邮箱");
    m["fillVerificationCode"] = QStringLiteral("请填写验证码");
    m["fillAllFields"] = QStringLiteral("请填写所有字段");
    m["pleaseEnterEmail"] = QStringLiteral("请先输入邮箱地址");
    m["pleaseLoginFirst"] = QStringLiteral("请先登录");
    m["deleteConfirm"] = QStringLiteral("确定要删除吗？");
    m["confirmRemove"] = QStringLiteral("确认移除");
    m["confirmRemoveMessage"] = QStringLiteral("确定要从歌单中移除这首音乐吗？");
    m["emptyFavorites"] = QStringLiteral("暂无收藏音乐");
    m["addToFavorites"] = QStringLiteral("添加到收藏");
    m["removeFromFavorites"] = QStringLiteral("取消收藏");
    m["shareTrack"] = QStringLiteral("分享");
    m["shareOnlineCopied"] = QStringLiteral("分享文案已复制到剪贴板");
    m["shareLocalCopied"] = QStringLiteral("分享文案已复制到剪贴板");
    m["shareNothingPlaying"] = QStringLiteral("当前没有可分享的曲目");
    m["shareClipboardHeaderOnline"] = QStringLiteral(
        "【Neko歌姬计划】线上曲库 · 流媒体与歌单发现 · 免费高品质播放");
    m["shareClipboardHeaderLocal"] = QStringLiteral("【Neko歌姬计划客户端】本地播放中");
    m["shareClipboardTrack"] = QStringLiteral("曲目：%1");
    m["shareClipboardArtist"] = QStringLiteral("歌手：%1");
    m["shareClipboardAlbum"] = QStringLiteral("专辑：%1");
    m["shareClipboardPromo"] = QStringLiteral(
        "海量曲库、歌单与排行榜，支持桌面歌词与云歌单。完全免费；下载 Windows / macOS / Linux / "
        "移动端客户端，体验更完整的播放与个性化功能。欢迎把 Neko歌姬计划 推荐给朋友一起使用。");
    m["shareClipboardPromoLocal"] = QStringLiteral(
        "本首为本地音频文件。Neko歌姬计划 同样提供丰富的在线曲库与歌单发现，完全免费；可继续使用客户端管理本地与在线音乐。");
    m["shareClipboardLinkOnline"] = QStringLiteral("歌曲详情页链接（在浏览器中打开）：");
    m["shareClipboardLinkLocal"] = QStringLiteral("本地文件（可粘贴到支持 file:// 的环境）：");
    m["deleteConfirmWithPlaylistName"] = QStringLiteral("确定要删除歌单\"{name}\"吗？此操作不可恢复。");
    m["invalidEmail"] = QStringLiteral("邮箱格式不正确");
    m["emailPlaceholder"] = QStringLiteral("请输入邮箱");
    m["passwordPlaceholder"] = QStringLiteral("请输入密码");
    m["namePlaceholder"] = QStringLiteral("请输入名称（昵称）");
    m["inputKeywordToSearch"] = QStringLiteral("输入关键词开始搜索");

    // --- 上传音乐 ---
    m["uploadMusic"] = QStringLiteral("上传音乐");
    m["uploadMusicTitle"] = QStringLiteral("上传音乐");
    m["uploadMusicDesc"] = QStringLiteral("上传你的音乐作品，等待管理员审核后发布");
    m["selectMusicFile"] = QStringLiteral("选择音乐文件");
    m["selectCoverFile"] = QStringLiteral("选择封面图片");
    m["selectLyricsFile"] = QStringLiteral("选择歌词文件");
    m["musicTitle"] = QStringLiteral("歌曲标题");
    m["artistName"] = QStringLiteral("歌手名称");
    m["uploadLanguageLabel"] = QStringLiteral("语言");
    m["albumName"] = QStringLiteral("专辑名称（可选）");
    m["tagsLabel"] = QStringLiteral("标签（可选）");
    m["durationLabel"] = QStringLiteral("时长（秒）");
    m["uploadBtn"] = QStringLiteral("上传");
    m["uploading"] = QStringLiteral("上传中...");
    m["uploadSuccess"] = QStringLiteral("上传成功，等待审核");
    m["uploadFailed"] = QStringLiteral("上传失败");
    m["uploadDuplicate"] = QStringLiteral("已有重复音乐，请检查后重新上传");
    m["uploadUnauthorized"] = QStringLiteral("请先登录");
    m["musicFileRequired"] = QStringLiteral("请选择音乐文件");
    m["titleRequired"] = QStringLiteral("请输入歌曲标题");
    m["artistRequired"] = QStringLiteral("请输入歌手名称");
    m["languageRequired"] = QStringLiteral("请选择语言");
    m["durationRequired"] = QStringLiteral("请输入时长");
    m["durationInvalid"] = QStringLiteral("时长必须是正整数");
    m["noMusicFileSelected"] = QStringLiteral("未选择音乐文件");
    m["noCoverSelected"] = QStringLiteral("未选择封面");
    m["noLyricsSelected"] = QStringLiteral("未选择歌词");
    m["musicFileFormat"] = QStringLiteral("支持 MP3、FLAC、WAV 格式");
    m["coverFileFormat"] = QStringLiteral("支持 JPG、PNG、GIF 等格式");
    m["lyricsFileFormat"] = QStringLiteral("支持 LRC 格式");
    m["fillRequiredFields"] = QStringLiteral("请填写必填项");
    m["langChinese"] = QStringLiteral("中文");
    m["langCantonese"] = QStringLiteral("粤语");
    m["langShanghainese"] = QStringLiteral("上海语");
    m["langEnglish"] = QStringLiteral("英文");
    m["langJapanese"] = QStringLiteral("日语");
    m["langKorean"] = QStringLiteral("韩语");
    m["langFrench"] = QStringLiteral("法语");
    m["langGerman"] = QStringLiteral("德语");
    m["langRussian"] = QStringLiteral("俄语");
    m["langInstrumental"] = QStringLiteral("纯音乐");
    m["required"] = QStringLiteral("*");
    m["optional"] = QStringLiteral("可选");
    m["musicFileInfo"] = QStringLiteral("音乐文件");
    m["coverInfo"] = QStringLiteral("封面图片");
    m["lyricsInfo"] = QStringLiteral("歌词文件");
    m["lyrics"] = QStringLiteral("歌词");
    m["unknownArtist"] = QStringLiteral("未知艺术家");
    m["noLyrics"] = QStringLiteral("暂无歌词");
    m["videoRenderButton"] = QStringLiteral("生成分享视频");
    m["videoRenderButtonBusy"] = QStringLiteral("生成中…");
    m["videoRenderDialogTitle"] = QStringLiteral("生成分享视频");
    m["videoRenderWatermark"] = QStringLiteral("添加平台水印");
    m["videoRenderStartLabel"] = QStringLiteral("成片起始");
    m["videoRenderRangeValue"] = QStringLiteral("%1 → %2");
    m["videoRenderVipHint"] = QStringLiteral("会员：从所选位置渲染至歌曲结束，可选水印");
    m["videoRenderFreeHint"] = QStringLiteral("免费：30 秒横屏成片（含水印），每日 10 次");
    m["videoRenderEmailNotice"] = QStringLiteral("提交后后台渲染，完成后将邮件通知并附下载链接");
    m["videoRenderSubmit"] = QStringLiteral("开始生成");
    m["videoRenderSubmitted"] = QStringLiteral("已提交渲染，完成后将邮件通知");
    m["videoRenderRemainingToday"] = QStringLiteral("今日剩余免费次数：%1");
    m["videoRenderStatusProcessing"] = QStringLiteral("视频渲染中…");
    m["videoRenderStatusDone"] = QStringLiteral("分享视频已生成");
    m["videoRenderDownload"] = QStringLiteral("下载 MP4");
    m["videoRenderDownloadDone"] = QStringLiteral("已保存至：%1");
    m["videoRenderFailed"] = QStringLiteral("视频渲染失败：%1");
    m["videoRenderCreateFailed"] = QStringLiteral("创建任务失败");
    m["videoRenderLocalUnsupported"] = QStringLiteral("本地音乐暂不支持生成分享视频");
    m["vipNav"] = QStringLiteral("会员中心");
    m["vipCenterTitle"] = QStringLiteral("会员中心");
    m["vipStatusActive"] = QStringLiteral("尊贵 VIP");
    m["vipStatusExpires"] = QStringLiteral("到期：%1");
    m["vipStatusLabel"] = QStringLiteral("会员状态");
    m["vipStatusInactive"] = QStringLiteral("非会员");
    m["nicknameEmpty"] = QStringLiteral("昵称不能为空");
    m["vipOpenMembership"] = QStringLiteral("开通会员");
    m["vipPricingLoadFailed"] = QStringLiteral("价目表加载失败");
    m["vipPayNoUrl"] = QStringLiteral("没有拿到支付链接");
    m["vipPayCreateFailed"] = QStringLiteral("下单失败");
    m["vipPayOpenFailed"] = QStringLiteral("无法打开支付链接");
    m["vipPayBrowserHint"] = QStringLiteral("已在浏览器打开收银台，支付完成后请返回并点击刷新");
    m["vipRefresh"] = QStringLiteral("刷新状态");
    m["vipMonthsSuffix"] = QStringLiteral("个月");
    m["vipDaysSuffix"] = QStringLiteral("天");
    m["vipPlanDefault"] = QStringLiteral("会员套餐");
    m["vipPriceYuan"] = QStringLiteral("¥ %1");
    m["vipPayAlipay"] = QStringLiteral("支付宝");
    m["vipPayWechat"] = QStringLiteral("微信");
    m["vipChoosePlan"] = QStringLiteral("选择套餐");
    m["vipMemberBenefits"] = QStringLiteral("尊享更长视频、无水印等权益");
    m["vipNonMemberBenefits"] = QStringLiteral("开通后解锁更长分享视频、去水印等");
    m["vipLoginRequired"] = QStringLiteral("请先登录后再购买会员");
    m["vipPillTooltipActive"] = QStringLiteral("会员已开通 · 点击查看会员中心");
    m["vipPillTooltipInactive"] = QStringLiteral("非会员 · 点击查看会员中心");
    m["vipTagline"] = QStringLiteral("开通会员，畅享高品质音乐与更多权益");
    m["vipTerms"] = QStringLiteral("支付成功后会员时长将按套餐叠加；请在常用网络环境下完成支付。");
    m["vipCheckoutCurrentPlan"] = QStringLiteral("当前套餐");
    m["vipCheckoutSelectPlan"] = QStringLiteral("请先在左侧选择套餐");
    m["vipCheckoutPayHint"] = QStringLiteral("选择支付方式");
    m["vipQrScanTip"] = QStringLiteral("请使用相机或对应 App 扫描完成支付");
    m["vipQrPayTitle"] = QStringLiteral("%1扫码支付");
    m["vipOpenInBrowser"] = QStringLiteral("浏览器打开支付");
    m["vipPaidDone"] = QStringLiteral("我已完成支付");
    m["vipPaidDoneHint"] = QStringLiteral("已刷新会员状态");
    m["vipChangePayMethod"] = QStringLiteral("更换支付方式");
    m["vipQrGenerateFailed"] = QStringLiteral("二维码生成失败，请稍后再试");
    m["vipQrImageLoadFailed"] = QStringLiteral("二维码图片加载失败");
    m["basicInfo"] = QStringLiteral("基本信息");
    m["uploadNote"] = QStringLiteral("上传的音乐会进入待审核状态，审核通过后发布");

    // --- 网易云歌单导入 ---
    m["importNeteasePlaylist"] = QStringLiteral("导入网易云歌单");
    m["importNeteaseDesc"] = QStringLiteral("输入网易云歌单链接或ID，将歌曲导入到本地歌单");
    m["inputNeteaseLink"] = QStringLiteral("输入网易云歌单链接或ID...");
    m["createNewPlaylist"] = QStringLiteral("新建歌单");
    m["creatingPlaylist"] = QStringLiteral("正在创建歌单...");
    m["fetchPlaylist"] = QStringLiteral("获取歌单");
    m["selectTargetPlaylist"] = QStringLiteral("选择目标歌单：");
    m["startImport"] = QStringLiteral("开始导入");
    m["invalidNeteaseLink"] = QStringLiteral("无效的歌单链接或ID");
    m["fetchPlaylistFailed"] = QStringLiteral("获取歌单失败");
    m["emptyNeteasePlaylist"] = QStringLiteral("歌单为空");
    m["neteasePlaylistInfo"] = QStringLiteral("歌单：%1（共 %2 首歌曲）");
    m["loadPlaylistsFailed"] = QStringLiteral("加载歌单列表失败");
    m["noPlaylistsToImport"] = QStringLiteral("没有可用的歌单，请先创建歌单");
    m["noTracksToImport"] = QStringLiteral("没有歌曲可导入");
    m["searchingTracks"] = QStringLiteral("正在搜索匹配歌曲...");
    m["searchFailed"] = QStringLiteral("搜索失败");
    m["searchServiceBusy"] = QStringLiteral("服务繁忙，请稍后再试");
    m["noMatchedTracks"] = QStringLiteral("未找到匹配的歌曲");
    m["addingToPlaylist"] = QStringLiteral("正在添加 %1 首歌曲到歌单...");
    m["addToPlaylistFailed"] = QStringLiteral("添加到歌单失败");
    m["importSuccess"] = QStringLiteral("导入成功！成功添加 %1 首，共 %2 首，%3 首未匹配");

    // --- QQ 音乐歌单导入 ---
    m["importQqPlaylist"] = QStringLiteral("导入QQ音乐歌单");
    m["importQqDesc"] = QStringLiteral("输入 QQ 音乐歌单分享链接或 ID，将歌曲导入到本地歌单");
    m["inputQqLink"] = QStringLiteral("输入 QQ 音乐歌单链接或 ID...");
    m["invalidQqLink"] = QStringLiteral("无效的歌单链接或 ID");
    m["emptyQqPlaylist"] = QStringLiteral("歌单为空");
    m["qqPlaylistInfo"] = QStringLiteral("歌单：%1（共 %2 首歌曲）");

    // --- 酷狗音乐歌单导入 ---
    m["importKugouPlaylist"] = QStringLiteral("导入酷狗音乐歌单");
    m["importKugouDesc"] = QStringLiteral("输入酷狗音乐歌单分享链接或 ID，将歌曲导入到本地歌单");
    m["importQishuiPlaylist"] = QStringLiteral("导入汽水音乐歌单");
    m["importQishuiDesc"] = QStringLiteral("输入汽水音乐歌单链接、短链或 ID，将歌曲导入到本地歌单");
    m["inputQishuiLink"] = QStringLiteral("输入汽水音乐歌单链接、短链或 ID...");
    m["invalidQishuiLink"] = QStringLiteral("无效的汽水歌单链接或 ID");
    m["emptyQishuiPlaylist"] = QStringLiteral("汽水歌单为空");
    m["qishuiPlaylistInfo"] = QStringLiteral("汽水歌单：%1\n歌曲数：%2");
    m["inputKugouLink"] = QStringLiteral("输入酷狗音乐歌单链接或 ID...");
    m["invalidKugouLink"] = QStringLiteral("无效的歌单链接或 ID");
    m["emptyKugouPlaylist"] = QStringLiteral("歌单为空");
    m["kugouPlaylistInfo"] = QStringLiteral("歌单：%1（共 %2 首歌曲）");

    // --- 异常与调试 ---
    m["emptyMusic"] = QStringLiteral("音乐数据为空");
    m["musicInPlaylist"] = QStringLiteral("音乐已在播放列表中");
    m["invalidMusicList"] = QStringLiteral("无效的音乐列表");
    m["musicExistsSkip"] = QStringLiteral("音乐已存在，跳过");
    m["downloadMusicFailedSkipCache"] = QStringLiteral("下载音乐失败，跳过缓存");
    m["getFilePathFailedSkipCache"] = QStringLiteral("获取文件路径失败，跳过缓存");
    m["writeToFileFailedSkipCache"] = QStringLiteral("写入文件失败，跳过缓存");
    m["parseRecentPlayListFailed"] = QStringLiteral("解析最近播放列表失败");
    m["parseMusicFailed"] = QStringLiteral("解析音乐失败");

    return m;
}

static QMap<QString, QString> nyaCN()
{
    QMap<QString, QString> m = zhCN();
    // 喵语中文：在 zhCN 基础上替换语气，不含图形符号
    m["search"] = QStringLiteral("搜寻宝藏喵~");
    m["searchPlaceholder"] = QStringLiteral("想听什么歌呢？跟人家说嘛喵~...");
    m["loading"] = QStringLiteral("正在努力搬运中喵~ 加油加油的说！");
    m["noData"] = QStringLiteral("空荡荡的，什么都没有喵... 呜喵...");
    m["cancel"] = QStringLiteral("才、才不要了喵~");
    m["confirm"] = QStringLiteral("决定就是你啦喵！");
    m["delete"] = QStringLiteral("悄悄丢掉喵~");
    m["edit"] = QStringLiteral("变身改名术喵！");
    m["save"] = QStringLiteral("好好存起来喵~");
    m["close"] = QStringLiteral("消失掉喵~");
    m["submit"] = QStringLiteral("交给指挥官喵~");
    m["back"] = QStringLiteral("退回刚才的地方喵~");
    m["refresh"] = QStringLiteral("让世界焕然一新喵~");
    m["clear"] = QStringLiteral("全部洗干净喵~");
    m["all"] = QStringLiteral("全都要喵！");
    m["create"] = QStringLiteral("变出一个新的喵~");
    m["rename"] = QStringLiteral("换个称呼喵~");
    m["play"] = QStringLiteral("让节奏响起来喵~");
    m["pause"] = QStringLiteral("稍微休息一下喵~");
    m["previous"] = QStringLiteral("前一首的说~");
    m["next"] = QStringLiteral("下一首的说~");
    m["shareTrack"] = QStringLiteral("安利给全世界喵~");
    m["shareOnlineCopied"] = QStringLiteral("嘿嘿，分享咒语已经抓到剪贴板里了喵！");
    m["shareLocalCopied"] = QStringLiteral("本地好歌也想让大家听到喵~ 复制好啦！");
    m["shareNothingPlaying"] = QStringLiteral("现在没有在唱歌，没法分享的说喵...");
    m["shareClipboardHeaderOnline"] = QStringLiteral("【Neko歌姬计划】线上曲库 · 把喜欢的歌留在身边喵");
    m["shareClipboardHeaderLocal"] = QStringLiteral("【Neko歌姬计划客户端】本地曲库播放中喵");
    m["shareClipboardTrack"] = QStringLiteral("幻之曲目：%1");
    m["shareClipboardArtist"] = QStringLiteral("灵动歌姬：%1");
    m["shareClipboardAlbum"] = QStringLiteral("梦境专辑：%1");
    m["shareClipboardPromo"] = QStringLiteral(
        "曲库超级大，还有好多厉害的排行榜喵~ 桌面歌词什么的也全都有，完全免费的说！如果不嫌弃的话，推荐给朋友一起用也可以喵~");
    m["shareClipboardPromoLocal"] = QStringLiteral(
        "这首是人家的私藏文件喵~ 在线库也完全免费，一直用人家听歌最棒了喵~");
    m["shareClipboardLinkOnline"] = QStringLiteral("歌曲详情传送门喵（用浏览器打开即可）：");
    m["shareClipboardLinkLocal"] = QStringLiteral("本地文件路径：");
    m["favorite"] = QStringLiteral("心动收藏喵！");
    m["unfavorite"] = QStringLiteral("不喜欢了喵...");
    m["playAll"] = QStringLiteral("全员集合，开始合唱喵！");
    m["playlist"] = QStringLiteral("咱的私藏歌单喵~");
    m["comments"] = QStringLiteral("评论喵~");
    m["playQueue"] = QStringLiteral("播放队列喵~");
    m["playlistSongCount"] = QStringLiteral("共 %1 首歌喵");
    m["scrollToCurrentPlay"] = QStringLiteral("正在播的");
    m["emptyPlaylist"] = QStringLiteral("播放列表空空的，快去加歌喵~");
    m["emptyPlaylistHint"] = QStringLiteral("队列空空的，快去加歌喵~");
    m["notPlaying"] = QStringLiteral("耳朵好寂寞喵，还没在唱歌呢...");
    m["volume"] = QStringLiteral("音量调节喵~");
    m["muted"] = QStringLiteral("嘘——人家静音了喵~");
    m["songs"] = QStringLiteral("音符碎片喵~");
    m["playlists"] = QStringLiteral("音乐星系喵~");
    m["album"] = QStringLiteral("珍藏集喵~");
    m["artist"] = QStringLiteral("大歌星喵~");
    m["home"] = QStringLiteral("温暖的家喵~");
    m["myPlaylists"] = QStringLiteral("咱的私人领性喵~");
    m["myPlaylistsTitle"] = QStringLiteral("指挥官的秘密歌单喵~");
    m["favorites"] = QStringLiteral("最最最喜欢的喵！");
    m["myFavorites"] = QStringLiteral("我最喜欢的音乐喵~");
    m["nickname"] = QStringLiteral("昵称");
    m["nickname"] = QStringLiteral("昵称");
    m["nicknamePlaceholder"] = QStringLiteral("输入新昵称喵（1-20字）");
    m["nicknameUpdated"] = QStringLiteral("昵称换好啦喵~");
    m["nicknameUpdateFailed"] = QStringLiteral("昵称没换成喵，待会儿再试试喵~");
    m["favoritePageSongCount"] = QStringLiteral("共 %1 首歌喵");
    m["listColTitle"] = QStringLiteral("标题");
    m["listColAlbum"] = QStringLiteral("专辑");
    m["listColRank"] = QStringLiteral("排行");
    m["listColPlayCount"] = QStringLiteral("播放量");
    m["listColUploaded"] = QStringLiteral("上传时间");
    m["playCountFmt"] = QStringLiteral("%1 次播放");
    m["listColActions"] = QStringLiteral("操作");
    m["fuzzySearch"] = QStringLiteral("模糊搜索");
    m["favoritesSearchEmpty"] = QStringLiteral("搜不到关于 %1 的任何歌曲呀");
    m["favoritePlaylists"] = QStringLiteral("珍藏的歌单喵~");
    m["favoritePlaylistsTitle"] = QStringLiteral("这些歌单超赞的喵~");
    m["recentPlay"] = QStringLiteral("刚才路过的声音喵~");
    m["emptyRecent"] = QStringLiteral("这里还没有留下过声音喵~");
    m["clearRecentList"] = QStringLiteral("清空列表喵");
    m["clearRecentConfirm"] = QStringLiteral("真的要清空最近播放吗？清空可就找不回来啦喵！");
    m["clearRecentSuccess"] = QStringLiteral("最近播放列表已清空喵~");
    m["downloadManage"] = QStringLiteral("下载小仓库喵~");
    m["downloadInProgress"] = QStringLiteral("正在搬歌喵~");
    m["downloadCompletedTab"] = QStringLiteral("搬完了喵~");
    m["emptyDownloading"] = QStringLiteral("现在没有在搬的歌喵~");
    m["downloadWaiting"] = QStringLiteral("排队等着喵~");
    m["downloadQueued"] = QStringLiteral("排队中喵");
    m["downloadingStatus"] = QStringLiteral("正在搬喵~");
    m["cancelDownload"] = QStringLiteral("取消下载喵");
    m["downloadCancelled"] = QStringLiteral("下载取消啦喵~");
    m["emptyDownloads"] = QStringLiteral("还没有搬完的歌喵~ 去歌曲列表里点下载吧喵~");
    m["clearDownloads"] = QStringLiteral("清空下载喵");
    m["clearDownloadsConfirm"] = QStringLiteral("真的要清空所有下载并删掉本地文件吗？找不回来啦喵！");
    m["clearDownloadsSuccess"] = QStringLiteral("下载记录已清空喵~");
    m["downloadMusic"] = QStringLiteral("下载喵");
    m["downloadStarted"] = QStringLiteral("开始帮你搬歌啦喵~");
    m["alreadyDownloaded"] = QStringLiteral("这首歌已经下载过啦喵~");
    m["downloadFolderHint"] = QStringLiteral("文件藏在：%1 喵");
    m["downloadAll"] = QStringLiteral("全部抱回家喵");
    m["batchDownloadStarted"] = QStringLiteral("已经开始帮你搬 %1 首歌啦喵~");
    m["allSongsDownloaded"] = QStringLiteral("这些歌都已经下载过啦喵~");
    m["localMusicBadge"] = QStringLiteral("家里的音乐喵~");
    m["hasLyrics"] = QStringLiteral("有歌词喵~");
    m["ranking"] = QStringLiteral("最厉害的排行喵~");
    m["latest"] = QStringLiteral("新降临的旋律喵~");
    m["library"] = QStringLiteral("大大的宝库喵~");
    m["topSongs"] = QStringLiteral("超级热门曲喵~");
    m["hotMusic"] = QStringLiteral("火热音乐喵~");
    m["hotMusicDesc"] = QStringLiteral("大家都在听的火热音乐喵~");
    m["latestMusic"] = QStringLiteral("新鲜出炉喵~");
    m["latestMusicDesc"] = QStringLiteral("刚刚诞生到这个世界的音乐喵~");
    m["dailyRecommend"] = QStringLiteral("今日推荐喵~");
    m["dailyRecommendDesc"] = QStringLiteral("按你的收藏口味挑出来的喵~");
    m["newSongs"] = QStringLiteral("新歌上架了喵~");
    m["discoverMusic"] = QStringLiteral("一起去冒险寻歌喵！");
    m["recommendPlaylists"] = QStringLiteral("为您挑选的礼物喵~");
    m["recommend_playlists"] = QStringLiteral("为您挑选的礼物喵~");
    m["hot_music"] = QStringLiteral("火热音乐喵~");
    m["latest_music"] = QStringLiteral("新鲜出炉喵~");
    m["login"] = QStringLiteral("指挥官，欢迎回来喵！");
    m["loginSubtitle"] = QStringLiteral("登录 NekoMusic，继续享受你的音乐空间喵~");
    m["registerSubtitle"] = QStringLiteral("创建账号，开启你的专属音乐空间喵~");
    m["qrLoginSubtitle"] = QStringLiteral("使用手机扫码，快速安全地登录喵~");
    m["qrLogin"] = QStringLiteral("喵码登录喵");
    m["qrLoginTitle"] = QStringLiteral("扫喵码登录喵");
    m["qrLoginBack"] = QStringLiteral("用暗号登录喵");
    m["qrLoginHint"] = QStringLiteral("打开手机上的 NekoMusic，点「我的」右上角的「扫一扫喵」，对准上面的喵码喵");
    m["qrLoginLoading"] = QStringLiteral("正在画喵码…");
    m["qrLoginPending"] = QStringLiteral("等指挥官扫喵码喵…");
    m["qrLoginScanned"] = QStringLiteral("扫到啦！在手机上点确认喵～");
    m["qrLoginCanceled"] = QStringLiteral("指挥官在手机上取消了喵…");
    m["qrLoginFailed"] = QStringLiteral("喵码画不出来喵，点一下刷新吧");
    m["qrLoginConnectionLost"] = QStringLiteral("连接断掉了喵，点一下刷新吧");
    m["qrLoginRefresh"] = QStringLiteral("换一张喵码");
    m["logout"] = QStringLiteral("要离开了喵？人家会想你的喵...");
    m["accountInfo"] = QStringLiteral("账号信息喵");
    m["account"] = QStringLiteral("账号喵");
    m["registerTime"] = QStringLiteral("注册时间喵");
    m["vipStatusLabel"] = QStringLiteral("会员状态喵");
    m["vipStatusActive"] = QStringLiteral("尊贵 VIP 喵~");
    m["vipStatusExpires"] = QStringLiteral("到期喵：%1");
    m["vipStatusInactive"] = QStringLiteral("普通小猫咪");
    m["nicknameEmpty"] = QStringLiteral("昵称不能为空喵");
    m["register"] = QStringLiteral("加入 Neko 家族喵~");
    m["email"] = QStringLiteral("联络暗号(邮箱)喵~");
    m["password"] = QStringLiteral("秘密护盾(密码)喵~");
    m["settings"] = QStringLiteral("调试模式喵~");
    m["settingsDesc"] = QStringLiteral("语言、外观、快捷键和版本消息都在这里喵~");
    m["settingsTabGeneral"] = QStringLiteral("通用喵");
    m["settingsTabAppearance"] = QStringLiteral("外观喵");
    m["settingsTabShortcuts"] = QStringLiteral("快捷键喵");
    m["settingsTabAbout"] = QStringLiteral("关于喵");
    m["playSettings"] = QStringLiteral("播放习惯设置喵~");
    m["language"] = QStringLiteral("交流语言喵~");
    m["languageSettings"] = QStringLiteral("语言调校喵~");
    m["languageNya"] = QStringLiteral("喵语中文喵");
    m["languageChinese"] = QStringLiteral("简体中文");
    m["languageEnglish"] = QStringLiteral("English");
    m["musicCache"] = QStringLiteral("音乐零食缓存喵~");
    m["musicCacheDesc"] = QStringLiteral("把听过的音乐偷偷藏进下载目录，下次就不费力了喵~");
    m["cachePath"] = QStringLiteral("零食仓库路径喵~");
    m["about"] = QStringLiteral("关于咱的事喵~");
    m["appVersion"] = QStringLiteral("版本代号喵~");
    m["currentVersion"] = QStringLiteral("现在的等级喵~");
    m["githubRepo"] = QStringLiteral("我们的秘密基地(GitHub)喵~");
    m["apiDocs"] = QStringLiteral("魔法说明书(API)喵~");
    m["checkUpdate"] = QStringLiteral("看看有没有变聪明喵？");
    m["checking"] = QStringLiteral("正在努力询问总部喵~...");
    m["isLatest"] = QStringLiteral("人家现在已经是超进化形态了喵！");
    m["createPlaylist"] = QStringLiteral("变出一个新歌单喵~");
    m["playlistName"] = QStringLiteral("歌单的名字喵~");
    m["playlistDescription"] = QStringLiteral("给歌单写点什么喵~");
    m["musicCount"] = QStringLiteral("首音符喵~");
    m["goToLogin"] = QStringLiteral("快去登录嘛喵~");
    m["forgotPassword"] = QStringLiteral("弄丢暗号了吗喵？");
    m["forgotPasswordTitle"] = QStringLiteral("找回暗号喵~");
    m["forgotPasswordEmailHint"] = QStringLiteral("请输入注册时的邮箱地址喵~");
    m["forgotPasswordCodeHint"] = QStringLiteral("请输入邮箱中的验证码喵~");
    m["newPassword"] = QStringLiteral("新暗号(6-30位)喵~");
    m["confirmPassword"] = QStringLiteral("确认新暗号喵~");
    m["resetPassword"] = QStringLiteral("重置暗号喵~");
    m["backToLogin"] = QStringLiteral("返回登录喵~");
    m["passwordLengthError"] = QStringLiteral("暗号长度必须在6-30位之间喵~");
    m["passwordMismatch"] = QStringLiteral("两次输入的暗号不一致喵~");
    m["passwordResetSuccess"] = QStringLiteral("暗号重置成功，请登录喵~");
    m["passwordTooLong"] = QStringLiteral("暗号长度不能超过128位喵~");
    m["inputTooLong"] = QStringLiteral("输入内容太长了喵~");
    m["loadingShort"] = QStringLiteral("处理中喵…");
    m["countdownSeconds"] = QStringLiteral("%1秒喵");
    m["fillAllFields"] = QStringLiteral("指挥官笨蛋！要把空位填满才行喵~");
    m["pleaseEnterEmail"] = QStringLiteral("不告诉人家邮箱的话，没法继续了喵~");
    m["registerNeedNicknameForCode"] = QStringLiteral("发验证码前先把昵称填好喵，不然邮件里不知道喊你什么喵~");
    m["captchaSecurityTitle"] = QStringLiteral("安全小考验喵~");
    m["captchaSecurityDesc"] =
        QStringLiteral("把滑块拖到拼图刚好对上，松手就会自动检查喵~ 过了就给邮箱发验证码喵~");
    m["captchaLoading"] = QStringLiteral("正在变出拼图喵…");
    m["captchaVerifying"] = QStringLiteral("正在偷偷核对喵…");
    m["captchaRefresh"] = QStringLiteral("换一张图喵~");
    m["captchaImageError"] = QStringLiteral("拼图数据坏掉了喵，换一张试试喵~");
    m["captchaLoadFailed"] = QStringLiteral("加载安全验证失败了喵…");
    m["captchaVerifyFail"] = QStringLiteral("没对准或者过期了喵，点换一张再来喵~");
    m["unknown"] = QStringLiteral("迷之物喵...");
    m["defaultMusicPlayerTitle"] = QStringLiteral("默认音乐助理喵~");
    m["defaultMusicPlayerMessage"] = QStringLiteral(
        "人家还不是你的默认音乐助理喵... 真的不考虑让人家全权负责你的耳朵吗喵？\n"
        "把各种格式都交给人家处理吧喵~");
    m["defaultMusicPlayerYes"] = QStringLiteral("就选你啦喵！");
    m["defaultMusicPlayerNo"] = QStringLiteral("还没准备好喵...");
    m["defaultMusicPlayerDontAskAgain"] = QStringLiteral("别再问了喵，真讨厌！");
    m["defaultMusicPlayerWinFollowUp"] = QStringLiteral(
        "已经带你去 Windows 的设置页面了喵~\n"
        "把所有支持的音乐文件格式都交给 Neko 云音乐打开就好啦喵！");
    m["playModeList"] = QStringLiteral("乖乖排队循环喵~");
    m["playModeSingle"] = QStringLiteral("就喜欢这一首，死不放手喵！");
    m["playModeRandom"] = QStringLiteral("随心所欲乱点兵喵~");
    m["playModeOff"] = QStringLiteral("播完就下班喵~");
    m["shortcuts"] = QStringLiteral("快捷键喵~");
    m["shortcutPlayPause"] = QStringLiteral("播放/暂停喵~");
    m["shortcutPreviousTrack"] = QStringLiteral("上一曲喵~");
    m["shortcutNextTrack"] = QStringLiteral("下一曲喵~");
    m["shortcutPressKeys"] = QStringLiteral("按下快捷键喵…");
    m["shortcutNotSet"] = QStringLiteral("还没设置喵");
    m["shortcutResetDefault"] = QStringLiteral("恢复默认喵");
    m["shortcutResetAll"] = QStringLiteral("全部恢复默认喵");
    m["shortcutConflict"] = QStringLiteral("快捷键撞车啦：%1 喵");
    m["shortcutWaylandHint"] = QStringLiteral(
        "全局快捷键要走 xdg-desktop-portal 向系统申请权限喵~ 第一次或改键后可能会弹授权框；"
        "也可以点下面按钮去系统设置里调喵~");
    m["shortcutOpenSystemSettings"] = QStringLiteral("去系统设置里配全局快捷键喵");
    m["shortcutGlobalPortalActive"] = QStringLiteral("全局快捷键已启用（系统 portal）喵~");
    m["shortcutGlobalFallbackActive"] = QStringLiteral("portal 不可用，暂时只能窗口内用喵");
    m["shortcutGlobalInactive"] = QStringLiteral("全局快捷键还没启用喵");
    m["shortcutGlobalUnavailable"] = QStringLiteral("这环境不支持全局快捷键喵（要 xdg-desktop-portal GlobalShortcuts）");
    m["shortcutGlobalPortalFailed"] = QStringLiteral("全局快捷键注册失败：%1（已尝试窗口内回退）喵");
    m["shortcutGlobalConfigureFailed"] = QStringLiteral("系统快捷键授权界面打不开喵：%1");
    m["shortcutGlobalWinActive"] = QStringLiteral("全局快捷键已启用喵（Windows）");
    m["shortcutGlobalWinFailed"] = QStringLiteral("全局快捷键注册失败啦：%1（先退回窗口内用喵）");
    m["shortcutGlobalWinPartialFailed"] = QStringLiteral("有几个全局快捷键没生效喵（可能被别的程序抢了）：%1");
    m["shortcutMicSync"] = QStringLiteral("麦克风同步喵（把歌送到麦克风里）");
    m["shortcutToggleDesktopLyrics"] = QStringLiteral("桌面歌词开关喵");
    m["micSyncEnabled"] = QStringLiteral("麦克风同步开好啦：歌已经合并进麦克风「%1」喵，通话软件用默认输入就能听到喵~");
    m["micSyncDisabled"] = QStringLiteral("麦克风同步关掉啦喵，默认麦克风还原了喵");
    m["micSyncFailed"] = QStringLiteral("麦克风同步开不起来喵：%1");
    m["micSyncUnsupported"] = QStringLiteral("这个平台还不支持麦克风同步喵（现在只有 Linux 能用）");
    m["micSyncSection"] = QStringLiteral("麦克风同步喵");
    m["micSyncEnable"] = QStringLiteral("把播放声音合并进麦克风");
    m["micSyncHint"] = QStringLiteral(
        "开起来后会把正在放的歌合并进默认麦克风（%1）喵，还会接管系统默认输入；"
        "语音 / 会议 / 直播软件不用改设置就能听到喵。关掉后会还原原来的麦克风喵。"
        "现在的快捷键是 %2，可以在「快捷键」设置里改喵。");
    m["micSyncUnsupportedHint"] = QStringLiteral("这个平台还不支持麦克风同步喵，现在只有 Linux 能用喵。");
    m["micSyncHintWindows"] = QStringLiteral(
        "开起来后会把本应用的播放输出切到虚拟声卡喵；在语音 / 会议 / 直播软件里把输入设备选成「%1」，"
        "就能同步听到歌喵。关掉后会自动切回原来的输出设备喵。要先装 VB-Audio Virtual Cable 或 "
        "VoiceMeeter 喵。现在的快捷键是 %2，可以在「快捷键」设置里改喵。");
    m["micSyncWindowsNoCable"] = QStringLiteral(
        "没找到虚拟声卡喵。Windows 上要先装 VB-Audio Virtual Cable 或 VoiceMeeter，"
        "才能把播放声音合并进麦克风喵。");
    m["micSyncInstallPending"] = QStringLiteral("正在安装 NekoMusic 虚拟麦克风喵，安装完成后请再开一次麦克风同步喵。");
    m["showWindow"] = QStringLiteral("快现身喵~");
    m["exitApp"] = QStringLiteral("残忍离去喵...");

    // --- 上传音乐喵~ ---
    m["uploadMusic"] = QStringLiteral("把好歌喂给服务器喵~");
    m["uploadMusicTitle"] = QStringLiteral("奉献音乐喵~");
    m["uploadMusicDesc"] = QStringLiteral("上传你的大作喵~ 等管理员大大点头就能发布的说");
    m["selectMusicFile"] = QStringLiteral("挑选音乐文件喵~");
    m["selectCoverFile"] = QStringLiteral("挑选漂亮封面喵~");
    m["selectLyricsFile"] = QStringLiteral("挑选歌词脚本喵~");
    m["musicTitle"] = QStringLiteral("歌曲的大名喵~");
    m["artistName"] = QStringLiteral("歌星的名字喵~");
    m["uploadLanguageLabel"] = QStringLiteral("语言种类喵~");
    m["albumName"] = QStringLiteral("属于哪个专辑喵？（不写也行喵）");
    m["tagsLabel"] = QStringLiteral("贴上标签喵~（选填喵）");
    m["durationLabel"] = QStringLiteral("要唱多久喵~（秒）");
    m["uploadBtn"] = QStringLiteral("投喂开始喵！");
    m["uploading"] = QStringLiteral("正在拼命上传中喵~...");
    m["uploadSuccess"] = QStringLiteral("投喂成功喵！乖乖等审核的说~");
    m["uploadFailed"] = QStringLiteral("呜喵... 失败了，是不是网络欺负你喵？");
    m["uploadDuplicate"] = QStringLiteral("这首歌已经住在这里了喵，换一首嘛~");
    m["uploadUnauthorized"] = QStringLiteral("还没告诉人家你是谁喵，快登录！");
    m["musicFileRequired"] = QStringLiteral("不给人家音乐文件，人家吃什么喵！");
    m["titleRequired"] = QStringLiteral("歌曲叫什么名字喵？");
    m["artistRequired"] = QStringLiteral("是谁唱的喵？");
    m["languageRequired"] = QStringLiteral("一定要选一种语言喵~");
    m["durationRequired"] = QStringLiteral("告诉人家要唱多久喵~");
    m["durationInvalid"] = QStringLiteral("时长得是正整数才行喵~");
    m["musicFileFormat"] = QStringLiteral("人家只吃 MP3、FLAC 和 WAV 喵~");
    m["coverFileFormat"] = QStringLiteral("封面要漂亮的图片格式喵~");
    m["lyricsFileFormat"] = QStringLiteral("歌词要 LRC 格式的喵~");
    m["fillRequiredFields"] = QStringLiteral("红星号标记的都要填喵！");
    m["langChinese"] = QStringLiteral("中土方言喵~");
    m["langCantonese"] = QStringLiteral("粤味十足喵~");
    m["langShanghainese"] = QStringLiteral("阿拉上海语喵~");
    m["langEnglish"] = QStringLiteral("异域英文喵~");
    m["langJapanese"] = QStringLiteral("霓虹语喵~");
    m["langKorean"] = QStringLiteral("思密达语喵~");
    m["langFrench"] = QStringLiteral("浪漫法语喵~");
    m["langGerman"] = QStringLiteral("严谨德语喵~");
    m["langRussian"] = QStringLiteral("战斗俄语喵~");
    m["langInstrumental"] = QStringLiteral("纯纯的器乐喵~");
    m["required"] = QStringLiteral("*必须填喵！");
    m["optional"] = QStringLiteral("随你喜欢喵~");
    m["musicFileInfo"] = QStringLiteral("档案信息喵~");
    m["coverInfo"] = QStringLiteral("外貌信息喵~");
    m["lyricsInfo"] = QStringLiteral("歌词信息喵~");
    m["lyrics"] = QStringLiteral("歌词全文喵~");
    m["unknownArtist"] = QStringLiteral("无名歌星喵~");
    m["noLyrics"] = QStringLiteral("歌词逃走了喵~");
    m["videoRenderButton"] = QStringLiteral("生成分享视频喵");
    m["videoRenderButtonBusy"] = QStringLiteral("生成中…喵");
    m["videoRenderDialogTitle"] = QStringLiteral("生成分享视频喵");
    m["videoRenderWatermark"] = QStringLiteral("添加平台水印喵");
    m["videoRenderStartLabel"] = QStringLiteral("成片起始喵");
    m["videoRenderRangeValue"] = QStringLiteral("%1 → %2");
    m["videoRenderVipHint"] = QStringLiteral("会员：从所选位置渲染至歌曲结束，可选水印喵");
    m["videoRenderFreeHint"] = QStringLiteral("免费：30 秒横屏成片（含水印），每日 10 次喵");
    m["videoRenderEmailNotice"] = QStringLiteral("提交后后台渲染，完成后将邮件通知并附下载链接喵");
    m["videoRenderSubmit"] = QStringLiteral("开始生成喵");
    m["videoRenderSubmitted"] = QStringLiteral("已提交渲染，完成后将邮件通知喵");
    m["videoRenderRemainingToday"] = QStringLiteral("今日剩余免费次数：%1 喵");
    m["videoRenderStatusProcessing"] = QStringLiteral("视频渲染中…喵");
    m["videoRenderStatusDone"] = QStringLiteral("分享视频已生成喵");
    m["videoRenderDownload"] = QStringLiteral("下载 MP4 喵");
    m["videoRenderDownloadDone"] = QStringLiteral("已保存至：%1 喵");
    m["videoRenderFailed"] = QStringLiteral("视频渲染失败：%1 喵");
    m["videoRenderCreateFailed"] = QStringLiteral("创建任务失败喵");
    m["videoRenderLocalUnsupported"] = QStringLiteral("本地音乐暂不支持生成分享视频喵");
    m["basicInfo"] = QStringLiteral("基本档案喵~");
    m["uploadNote"] = QStringLiteral("上传之后会进入幻境审核喵，审核通过才会正式降临的说~");

    // 版本更新喵~
    m["newVersionAvailable"] = QStringLiteral("锵锵！发现新版本喵！");
    m["newVersion"] = QStringLiteral("更高等级的喵~");
    m["remindLater"] = QStringLiteral("等会儿再说喵~");
    m["updateNow"] = QStringLiteral("马上进化喵！");
    m["downloading"] = QStringLiteral("正在加速跑，要把新版本带给你喵~...");
    m["downloadComplete"] = QStringLiteral("呼，搬回来了！快安装喵~");
    m["installNow"] = QStringLiteral("现在就合体喵！");
    m["installLater"] = QStringLiteral("晚点再合体喵~");
    m["updateViaAur"] = QStringLiteral("走 AUR 进化喵！");
    m["updateViaAurHint"] = QStringLiteral("点「马上进化」会在终端显示命令，自己敲喵：%1");
    m["updateViaAurNoHelper"] = QStringLiteral("没找到 yay/paru，只好带你去 AUR 页面喵...");
    m["openAurPage"] = QStringLiteral("打开 AUR 页面喵");
    m["updateOpenWebHint"] = QStringLiteral("去网页下载新版本再安装喵~");
    m["downloadFailed"] = QStringLiteral("半路摔倒了喵，下载失败...");
    m["retry"] = QStringLiteral("再试一次嘛，拜托了喵~");
    m["checkForUpdates"] = QStringLiteral("看看有没有变聪明喵？");
    m["alreadyLatest"] = QStringLiteral("已经是顶级形态了喵~");
    m["version"] = QStringLiteral("版本代号喵~");
    
    // --- 网易云歌单导入喵~ ---
    m["importNeteasePlaylist"] = QStringLiteral("导入网易云歌单喵~");
    m["importNeteaseDesc"] = QStringLiteral("把网易云的歌单链接或ID告诉人家，人家帮你搬过来喵~");
    m["inputNeteaseLink"] = QStringLiteral("输入网易云歌单链接或ID喵...");
    m["createNewPlaylist"] = QStringLiteral("变出一个新歌单喵~");
    m["creatingPlaylist"] = QStringLiteral("正在变出歌单中喵...");
    m["fetchPlaylist"] = QStringLiteral("获取歌单喵~");
    m["selectTargetPlaylist"] = QStringLiteral("选择目标歌单喵：");
    m["startImport"] = QStringLiteral("开始导入喵！");
    m["invalidNeteaseLink"] = QStringLiteral("这个链接看不懂喵...给个正确的嘛~");
    m["fetchPlaylistFailed"] = QStringLiteral("获取歌单失败了喵...");
    m["emptyNeteasePlaylist"] = QStringLiteral("歌单空空的没有歌喵...");
    m["neteasePlaylistInfo"] = QStringLiteral("歌单：%1（共 %2 首歌曲喵）");
    m["loadPlaylistsFailed"] = QStringLiteral("加载歌单列表失败了喵...");
    m["noPlaylistsToImport"] = QStringLiteral("没有歌单可以放歌喵，先创建一个嘛~");
    m["noTracksToImport"] = QStringLiteral("没有歌曲可导入喵~");
    m["searchingTracks"] = QStringLiteral("正在努力搜索匹配歌曲喵...");
    m["searchFailed"] = QStringLiteral("搜索失败了喵...");
    m["searchServiceBusy"] = QStringLiteral("服务繁忙，请稍后再试喵");
    m["noMatchedTracks"] = QStringLiteral("找不到匹配的歌曲喵...");
    m["addingToPlaylist"] = QStringLiteral("正在添加 %1 首歌曲到歌单喵...");
    m["addToPlaylistFailed"] = QStringLiteral("添加到歌单失败了喵...");
    m["importSuccess"] = QStringLiteral("导入成功喵！添加了 %1 首，共 %2 首，%3 首没找到喵~");

    // --- QQ 音乐歌单导入喵~ ---
    m["importQqPlaylist"] = QStringLiteral("导入QQ音乐歌单喵~");
    m["importQqDesc"] = QStringLiteral("把 QQ 音乐歌单链接或 ID 告诉人家，人家帮你搬过来喵~");
    m["inputQqLink"] = QStringLiteral("输入 QQ 音乐歌单链接或 ID 喵...");
    m["invalidQqLink"] = QStringLiteral("这个链接看不懂喵...给个正确的嘛~");
    m["emptyQqPlaylist"] = QStringLiteral("歌单空空的没有歌喵...");
    m["qqPlaylistInfo"] = QStringLiteral("歌单：%1（共 %2 首歌曲喵）");

    // --- 酷狗音乐歌单导入喵~ ---
    m["importKugouPlaylist"] = QStringLiteral("导入酷狗音乐歌单喵~");
    m["importKugouDesc"] = QStringLiteral("把酷狗音乐歌单链接或 ID 告诉人家，人家帮你搬过来喵~");
    m["importQishuiPlaylist"] = QStringLiteral("导入汽水音乐歌单喵~");
    m["importQishuiDesc"] = QStringLiteral("把汽水音乐歌单链接、短链或 ID 告诉人家，人家帮你搬过来喵~");
    m["inputQishuiLink"] = QStringLiteral("输入汽水音乐歌单链接、短链或 ID 喵...");
    m["invalidQishuiLink"] = QStringLiteral("这个汽水歌单链接看不懂喵...");
    m["emptyQishuiPlaylist"] = QStringLiteral("汽水歌单空空的没有歌喵...");
    m["qishuiPlaylistInfo"] = QStringLiteral("汽水歌单：%1\n歌曲数：%2");
    m["inputKugouLink"] = QStringLiteral("输入酷狗音乐歌单链接或 ID 喵...");
    m["invalidKugouLink"] = QStringLiteral("这个链接看不懂喵...给个正确的嘛~");
    m["emptyKugouPlaylist"] = QStringLiteral("歌单空空的没有歌喵...");
    m["kugouPlaylistInfo"] = QStringLiteral("歌单：%1（共 %2 首歌曲喵）");
    
    return m;
}

static QMap<QString, QString> enUS()
{
    QMap<QString, QString> m;
    // --- General Operations ---
    m["search"] = "Search";
    m["searchPlaceholder"] = "Search music...";
    m["loading"] = "Loading...";
    m["noData"] = "No Data";
    m["cancel"] = "Cancel";
    m["confirm"] = "Confirm";
    m["delete"] = "Delete";
    m["edit"] = "Edit";
    m["save"] = "Save";
    m["close"] = "Close";
    m["submit"] = "Submit";
    m["back"] = "Back";
    m["refresh"] = "Refresh";
    m["clear"] = "Clear";
    m["all"] = "All";
    m["create"] = "Create";
    m["rename"] = "Rename";
    m["actions"] = "Actions";
    m["none"] = "None";
    m["unknown"] = "Unknown";
    m["defaultMusicPlayerTitle"] = "Default music player";
    m["defaultMusicPlayerMessage"] =
        "Neko Music may not be the system default for common audio (MP3, FLAC, AAC/M4A, WAV, OGG, WMA, "
        "M3U/PLS, etc.).\n"
        "Set it as the default now?";
    m["defaultMusicPlayerYes"] = "Set as default";
    m["defaultMusicPlayerNo"] = "Not now";
    m["defaultMusicPlayerDontAskAgain"] = "Don't ask again";
    m["defaultMusicPlayerWinFollowUp"] =
        "The Windows Default apps page has been opened.\n"
        "Set Neko Music as the default opener for all supported media types such as .mp3, .flac, .wav, .m4a, .aac, .ogg, .wma, and .m3u.";
    m["inputRequired"] = "Please input";
    m["inputValid"] = "Please enter valid content";
    m["justNow"] = "Just now";
    m["minutesAgo"] = "m ago";
    m["hoursAgo"] = "h ago";
    m["daysAgo"] = "d ago";
    m["monthsAgo"] = "mo ago";
    m["yearsAgo"] = "y ago";

    // --- Player & Media Controls ---
    m["play"] = "Play";
    m["pause"] = "Pause";
    m["previous"] = "Previous";
    m["next"] = "Next";
    m["favorite"] = "Favorite";
    m["unfavorite"] = "Unfavorite";
    m["playAll"] = "Play All";
    m["playlist"] = "Playlist";
    m["comments"] = "Comments";
    m["playQueue"] = "Play Queue";
    m["playlistSongCount"] = "%1 songs";
    m["scrollToCurrentPlay"] = "Now Playing";
    m["emptyPlaylist"] = "Playlist is empty";
    m["emptyPlaylistHint"] = "Queue is empty. Add some songs!";
    m["notPlaying"] = "Not Playing";
    m["volume"] = "Volume";
    m["muted"] = "Muted";
    m["unmuted"] = "Unmuted";
    m["toggleMute"] = "Toggle Mute";
    m["mute"] = "Mute";
    m["desktopLyrics"] = "Lyrics";
    m["desktopLyricsEnabled"] = "Desktop lyrics enabled";
    m["desktopLyricsDisabled"] = "Desktop lyrics disabled";
    m["theme"] = "Theme";
    m["themeSystem"] = "Follow System";
    m["themeDark"] = "Dark Mode";
    m["themeLight"] = "Light Mode";
    m["settingsDesc"] = "Manage language, appearance, shortcuts, and app information";
    m["settingsTabGeneral"] = "General";
    m["settingsTabAppearance"] = "Appearance";
    m["settingsTabShortcuts"] = "Shortcuts";
    m["settingsTabAbout"] = "About";
    m["personalization"] = "Personalization";
    m["shellBackdrop"] = "Window Background";
    m["shellBackdropDefaultImage"] = "Default Image";
    m["shellBackdropCustomImage"] = "Custom Image";
    m["shellBackdropSolidColor"] = "Solid Color";
    m["shellBackdropPickImage"] = "Choose Image";
    m["shellBackdropPickColor"] = "Choose Color";
    m["shellBackdropResetImage"] = "Reset to Default Image";
    m["shellBackdropUsingDefault"] = "Using built-in default background";
    m["shellBackdropNoCustomImage"] = "No custom image selected";
    m["shellBackdropCurrentFile"] = "Current file";
    m["shellBackdropImageInvalid"] = "Could not load image. Please choose another file.";
    m["shortcuts"] = "Keyboard Shortcuts";
    m["shortcutPlayPause"] = "Play / Pause";
    m["shortcutPreviousTrack"] = "Previous Track";
    m["shortcutNextTrack"] = "Next Track";
    m["shortcutPressKeys"] = "Press shortcut…";
    m["shortcutNotSet"] = "Not set";
    m["shortcutResetDefault"] = "Reset";
    m["shortcutResetAll"] = "Reset All to Defaults";
    m["shortcutConflict"] = "Shortcut conflict: %1";
    m["shortcutWaylandHint"] =
        "Global shortcuts use xdg-desktop-portal to request system permission. "
        "You may see an authorization dialog on first launch or after changes; "
        "use the button below to configure in system settings.";
    m["shortcutOpenSystemSettings"] = "Configure in System Settings";
    m["shortcutGlobalPortalActive"] = "Global shortcuts active (system portal)";
    m["shortcutGlobalFallbackActive"] = "Portal unavailable; using in-window shortcuts";
    m["shortcutGlobalInactive"] = "Global shortcuts inactive";
    m["shortcutGlobalUnavailable"] =
        "Global shortcuts unavailable (xdg-desktop-portal GlobalShortcuts required)";
    m["shortcutGlobalPortalFailed"] = "Global shortcut registration failed: %1 (in-window fallback tried)";
    m["shortcutGlobalConfigureFailed"] = "Could not open system shortcut authorization dialog: %1";
    m["shortcutGlobalWinActive"] = "Global shortcuts active (Windows)";
    m["shortcutGlobalWinFailed"] = "Global shortcut registration failed: %1 (in-window fallback enabled)";
    m["shortcutGlobalWinPartialFailed"] =
        "Some global shortcuts are inactive (possibly taken by another app): %1";
    m["shortcutMicSync"] = "Microphone Sync (route playback into the mic)";
    m["shortcutToggleDesktopLyrics"] = "Toggle Desktop Lyrics";
    m["micSyncEnabled"] =
        "Microphone sync on — playback is now merged into mic \"%1\"; voice/meeting apps just use the default input";
    m["micSyncDisabled"] = "Microphone sync off — the original microphone was restored";
    m["micSyncFailed"] = "Failed to enable microphone sync: %1";
    m["micSyncUnsupported"] =
        "Microphone sync is not supported on this platform yet (Linux only for now)";
    m["micSyncSection"] = "Microphone Sync";
    m["micSyncEnable"] = "Merge playback into the microphone";
    m["micSyncHint"] =
        "When enabled, the playing music is merged into your default microphone (%1) and the system default "
        "input is switched to it, so voice / meeting / streaming apps pick it up without manual setup. "
        "Disabling restores the original microphone. Current shortcut: %2 (editable in Shortcuts settings).";
    m["micSyncUnsupportedHint"] =
        "Microphone sync is not supported on this platform yet; currently Linux only.";
    m["micSyncHintWindows"] =
        "When enabled, this app's playback is routed to a virtual audio cable. Select \"%1\" as the input "
        "device in your voice / meeting / streaming app to hear the music; disabling restores the previous "
        "output device. Requires VB-Audio Virtual Cable or VoiceMeeter. Current shortcut: %2 (editable in "
        "Shortcuts settings).";
    m["micSyncWindowsNoCable"] =
        "No virtual audio cable detected. Windows needs VB-Audio Virtual Cable or VoiceMeeter to merge "
        "playback into the microphone.";
    m["micSyncInstallPending"] =
        "Installing the bundled NekoMusic virtual microphone. Enable microphone sync again after installation.";
    m["playModeList"] = "List Loop";
    m["playModeSingle"] = "Single Loop";
    m["playModeRandom"] = "Random";
    m["playModeOff"] = "No Loop";
    m["minimize"] = "Minimize";
    m["maximize"] = "Maximize";
    m["closeWindow"] = "Close Window";
    m["duration"] = "Duration";
    m["title"] = "Title";
    m["cover"] = "Cover";
    m["songs"] = "Songs";
    m["playlists"] = "Playlists";
    m["album"] = "Album";
    m["artist"] = "Artist";
    m["count"] = "Count";
    m["musicCount"] = "Songs";
    m["songList"] = "Song List";

    // --- Navigation & Views ---
    m["home"] = "Home";
    m["myPlaylists"] = "My Playlists";
    m["myPlaylistsTitle"] = "My Playlists";
    m["favorites"] = "My Favorites";
    m["myFavorites"] = "My Favorite Music";
    m["favoritePageSongCount"] = "%1 songs";
    m["listColTitle"] = "Title";
    m["listColAlbum"] = "Album";
    m["listColRank"] = "Rank";
    m["listColPlayCount"] = "Plays";
    m["listColUploaded"] = "Uploaded";
    m["playCountFmt"] = "%1 plays";
    m["listColActions"] = "Actions";
    m["fuzzySearch"] = "Search";
    m["favoritesSearchEmpty"] = "No songs found for \"%1\"";
    m["favoritePlaylists"] = "Favorite Playlists";
    m["favoritePlaylistsTitle"] = "Favorite Playlists";
    m["recentPlay"] = "Recently Played";
    m["emptyRecent"] = "No recently played records";
    m["clearRecentList"] = "Clear List";
    m["clearRecentConfirm"] = "Clear all recently played songs? This cannot be undone.";
    m["clearRecentSuccess"] = "Recently played list cleared";
    m["downloadManage"] = "Downloads";
    m["downloadInProgress"] = "Downloading";
    m["downloadCompletedTab"] = "Completed";
    m["emptyDownloading"] = "No downloads in progress";
    m["downloadWaiting"] = "Waiting";
    m["downloadQueued"] = "Queued";
    m["downloadingStatus"] = "Downloading";
    m["cancelDownload"] = "Cancel download";
    m["downloadCancelled"] = "Download cancelled";
    m["emptyDownloads"] = "No completed downloads yet. Download music from song lists.";
    m["clearDownloads"] = "Clear Downloads";
    m["clearDownloadsConfirm"] = "Clear all download records and delete local files? This cannot be undone.";
    m["clearDownloadsSuccess"] = "Downloads cleared";
    m["downloadMusic"] = "Download";
    m["downloadStarted"] = "Download started";
    m["alreadyDownloaded"] = "This song is already downloaded";
    m["downloadFolderHint"] = "Saved to: %1";
    m["downloadAll"] = "Download All";
    m["batchDownloadStarted"] = "Started downloading %1 songs";
    m["allSongsDownloaded"] = "All songs are already downloaded";
    m["localMusicBadge"] = "Local";
    m["hasLyrics"] = "Has lyrics";
    m["ranking"] = "Ranking";
    m["latest"] = "Latest";
    m["library"] = "Library";
    m["topSongs"] = "Top Songs";
    m["hotMusic"] = "Hot Music";
    m["hotMusicDesc"] = "Most played popular songs";
    m["latestMusic"] = "Latest Music";
    m["latestMusicDesc"] = "Recently uploaded songs";
    m["dailyRecommend"] = "Daily Picks";
    m["dailyRecommendDesc"] = "Generated from your favorites taste";
    m["newSongs"] = "New Songs";
    m["discoverMusic"] = "Discover Music";
    m["recommendPlaylists"] = "Recommended Playlists";
    m["recommend_playlists"] = "Recommended Playlists";
    m["hot_music"] = "Hot Music";
    m["latest_music"] = "Latest Music";
    m["searchMusic"] = "Songs";
    m["searchPlaylist"] = "Playlists";
    m["searchResults"] = "Search Results";
    m["searchRelatedSuffix"] = " search results";
    m["searchStartHint"] = "Enter a keyword in the top search box first";
    m["searchEmptySongs"] = "Sorry, no songs found for \"%1\"";
    m["searchEmptyPlaylists"] = "Sorry, no playlists found for \"%1\"";
    m["searchEmptyArtists"] = "Sorry, no artists found for \"%1\"";
    m["searchBackArtists"] = "← Artists";
    m["searchBackToSearch"] = "Back to search";
    m["loadMore"] = "Load more";
    m["noResults"] = "No results found";

    // --- Account & Auth ---
    m["login"] = "Login";
    m["loginSubtitle"] = "Log in to NekoMusic to continue listening";
    m["registerSubtitle"] = "Create an account for your personal music space";
    m["qrLoginSubtitle"] = "Scan with your phone to log in securely";
    m["qrLogin"] = "Scan QR Code";
    m["qrLoginTitle"] = "Scan QR to Log In";
    m["qrLoginBack"] = "Use Password";
    m["qrLoginHint"] = "Open NekoMusic on your phone, tap Scan at the top-right of the Me tab, then scan the code above";
    m["qrLoginLoading"] = "Generating QR code…";
    m["qrLoginPending"] = "Waiting for scan…";
    m["qrLoginScanned"] = "Scanned. Please confirm on your phone.";
    m["qrLoginCanceled"] = "Canceled on your phone";
    m["qrLoginExpired"] = "QR code expired. Click refresh.";
    m["qrLoginFailed"] = "Failed to generate QR code. Click refresh.";
    m["qrLoginConnectionLost"] = "Connection lost. Click refresh.";
    m["qrLoginRefresh"] = "Refresh QR Code";
    m["logout"] = "Logout";
    m["register"] = "Register";
    m["email"] = "Email";
    m["password"] = "Password";
    m["nickname"] = "Nickname";
    m["nickname"] = "Nickname";
    m["nicknamePlaceholder"] = "Enter a new nickname (1-20 chars)";
    m["nicknameUpdated"] = "Nickname updated";
    m["nicknameUpdateFailed"] = "Failed to update nickname, please try again later";
    m["emailOrName"] = "Email/Name";
    m["verificationCode"] = "Verification Code";
    m["verifyCode"] = "Verify Code";
    m["getCode"] = "Get Code";
    m["sendCode"] = "Send Code";
    m["resendCode"] = "Resend";
    m["registerNeedNicknameForCode"] = "Please enter a nickname before requesting the verification code";
    m["captchaSecurityTitle"] = "Security check";
    m["captchaSecurityDesc"] =
        "Drag the slider to align the puzzle piece, then release to verify. After success, a code will be sent to "
        "your email.";
    m["captchaLoading"] = "Loading puzzle…";
    m["captchaVerifying"] = "Verifying…";
    m["captchaRefresh"] = "New image";
    m["captchaImageError"] = "Failed to decode puzzle images";
    m["captchaLoadFailed"] = "Failed to load security check";
    m["captchaVerifyFail"] = "Incorrect position or expired challenge; try a new image";
    m["registerTime"] = "Registration Time";
    m["accountInfo"] = "Account Info";
    m["account"] = "Account";
    m["accountSettings"] = "Account Settings";
    m["notLoggedIn"] = "Not Logged In";
    m["goToLogin"] = "Go to Login";
    m["forgotPassword"] = "Forgot Password?";
    m["forgotPasswordTitle"] = "Reset your password";
    m["forgotPasswordEmailHint"] = "Enter the email address used to register";
    m["forgotPasswordCodeHint"] = "Enter the verification code from your email";
    m["newPassword"] = "New password (6-30 characters)";
    m["confirmPassword"] = "Confirm new password";
    m["resetPassword"] = "Reset password";
    m["backToLogin"] = "Back to login";
    m["passwordLengthError"] = "Password must be 6-30 characters";
    m["passwordMismatch"] = "The passwords do not match";
    m["passwordResetSuccess"] = "Password reset. Please log in.";
    m["passwordTooLong"] = "Password cannot exceed 128 characters";
    m["inputTooLong"] = "Some input is too long";
    m["loadingShort"] = "Working...";
    m["countdownSeconds"] = "%1s";
    m["welcome"] = "Welcome Back";
    m["welcomeSubtitle"] = "Discover great music, enjoy every moment";
    m["goodMorning"] = "Good Morning";
    m["goodAfternoon"] = "Good Afternoon";
    m["goodEvening"] = "Good Evening";
    m["lateNight"] = "Late Night";

    // --- Playlist Operations ---
    m["createPlaylist"] = "Create Playlist";
    m["createFirstPlaylist"] = "Create your first playlist";
    m["noPlaylists"] = "No playlists yet, please create one";
    m["playlistName"] = "Playlist Name";
    m["playlistDescription"] = "Description";
    m["playlistNamePlaceholder"] = "Enter playlist name";
    m["playlistDescPlaceholder"] = "Enter playlist description";
    m["creator"] = "Creator";
    m["editPlaylist"] = "Edit Playlist";
    m["deletePlaylist"] = "Delete Playlist";
    m["renamePlaylist"] = "Rename Playlist";
    m["modifyPlaylistDesc"] = "Edit Description";
    m["inputNewPlaylistName"] = "Enter new playlist name";
    m["inputPlaylistDesc"] = "Enter description";
    m["inputPlaylistName"] = "Please enter playlist name";
    m["collectPlaylist"] = "Collect Playlist";
    // Update
    m["newVersionAvailable"] = "New Version Available";
    m["currentVersion"] = "Current Version";
    m["newVersion"] = "New Version";
    m["remindLater"] = "Remind Later";
    m["updateNow"] = "Update Now";
    m["downloading"] = "Downloading...";
    m["downloadComplete"] = "Download Complete";
    m["installNow"] = "Install Now";
    m["installLater"] = "Install Later";
    m["updateViaAur"] = "Update via AUR";
    m["updateViaAurHint"] = "Terminal will show this command after you click Update: %1";
    m["updateViaAurNoHelper"] = "yay or paru not found; opening AUR package page";
    m["openAurPage"] = "Open AUR Page";
    m["updateOpenWebHint"] = "Download and install the new version from the web";
    m["downloadFailed"] = "Download Failed";
    m["retry"] = "Retry";
    m["checkForUpdates"] = "Check for Updates";
    m["alreadyLatest"] = "Already Latest";
    m["version"] = "Version";
    m["collectedPlaylist"] = "Collected";
    m["uncollectPlaylist"] = "Uncollect";
    m["removeFromPlaylist"] = "Remove from Playlist";
    m["addToPlaylist"] = "Add to Playlist";
    m["addToLocalPlaylist"] = "Add to Local Playlist";
    m["addToQueue"] = "Add to Queue";
    m["playNext"] = "Play Next";
    m["playNextAdded"] = "Added to play next";
    m["addToPlaylistTitle"] = "Add to Playlist";
    m["createNewPlaylistRow"] = "Create New Playlist";
    m["addingToPlaylist"] = "Adding to playlist…";
    m["selectPlaylist"] = "Select Playlist";
    m["orCreateNewPlaylist"] = "Or create new playlist";
    m["addMusicToPlaylist"] = "Add music to playlist";
    m["newPlaylist"] = "New Playlist";
    m["ok"] = "OK";
    m["musicAdded"] = "Music added to playlist";
    m["musicAddFailed"] = "Failed to add music to playlist";
    m["noMusicInPlaylist"] = "No music in this playlist";

    // --- Settings & Cache & Update ---
    m["settings"] = "Settings";
    m["playSettings"] = "Playback Settings";
    m["language"] = "Language";
    m["languageSettings"] = "Language Settings";
    m["languageNya"] = "喵语中文";
    m["languageChinese"] = "简体中文";
    m["languageEnglish"] = "English";
    m["musicCache"] = "Music Cache";
    m["musicCacheDesc"] = "Automatically cache played music to local directory";
    m["cachePath"] = "Cache Path";
    m["cacheEnabled"] = "Cache Enabled";
    m["cacheDisabled"] = "Cache Disabled";
    m["loadFromCache"] = "Load from Cache";
    m["about"] = "About";
    m["appVersion"] = "Version";
    m["currentVersion"] = "Current Version";
    m["githubRepo"] = "GitHub Repository";
    m["apiDocs"] = "API Docs";
    m["createdAt"] = "Created At";
    m["updatedAt"] = "Updated At";
    m["description"] = "Description";
    m["checkUpdate"] = "Check for Updates";
    m["checking"] = "Checking...";
    m["updateAvailable"] = "New version available";
    m["downloadUpdate"] = "Download Now";
    m["downloadCancelled"] = "Download Cancelled";
    m["isLatest"] = "Already the latest version";
    m["versionDownloaded"] = "Version {version} downloaded";
    m["updateDownloadComplete"] = "Update download complete";

    // --- Feedback ---
    m["operationFailed"] = "Operation Failed";
    m["networkError"] = "Network Error, please check server connection";
    m["networkErrorRetry"] = "Network error, please try again";
    m["lanDeviceConnected"] = "Connected to %1";
    m["loginSuccess"] = "Login Successful, welcome back!";
    m["loginFailed"] = "Login Failed";
    m["logoutSuccess"] = "Logged out successfully";
    m["registerSuccess"] = "Registered successfully, please login";
    m["registerFailed"] = "Registration Failed";
    m["createSuccess"] = "Created Successfully";
    m["createFailed"] = "Creation Failed";
    m["createPlaylistSuccess"] = "Playlist created successfully";
    m["createPlaylistFailed"] = "Failed to create playlist";
    m["deleteSuccess"] = "Deleted successfully";
    m["deleteFailed"] = "Deletion failed";
    m["modifySuccess"] = "Modified successfully";
    m["renameSuccess"] = "Renamed successfully";
    m["updateSuccess"] = "Updated successfully";
    m["favoriteSuccess"] = "Added to favorites";
    m["favoriteFailed"] = "Failed to favorite";
    m["cancelFavoriteSuccess"] = "Removed from favorites";
    m["cancelFavoriteFailed"] = "Failed to remove favorite";
    m["cancelFavoritePlaylistSuccess"] = "Uncollected playlist";
    m["cancelFavoritePlaylistFailed"] = "Failed to uncollect";
    m["collectPlaylistSuccess"] = "Collected playlist";
    m["collectPlaylistFailed"] = "Failed to collect";
    m["unfavoriteSuccess"] = "Unfavorited successfully";
    m["addToPlaylistSuccess"] = "Added to playlist successfully";
    m["addToPlaylistFailed"] = "Failed to add";
    m["addSuccess"] = "Added successfully";
    m["removeSuccess"] = "Removed successfully";
    m["removeFailed"] = "Removal failed";
    m["musicRemovedSuccess"] = "Music removed from playlist";
    m["switchLanguageSuccess"] = "Language settings saved";
    m["switchLanguageFailed"] = "Failed to switch language";
    m["checkUpdateFailed"] = "Failed to check for updates";
    m["sendVerifyCodeFailed"] = "Failed to send code";
    m["verifyCodeSent"] = "Verification code sent to your email";
    m["codeSent"] = "Code sent";
    m["inputEmail"] = "Please enter email address";
    m["inputValidEmail"] = "Please enter a valid email";
    m["emailRequired"] = "Email is required";
    m["passwordRequired"] = "Password is required";
    m["codeRequired"] = "Verification code is required";
    m["nameRequired"] = "Nickname is required";
    m["inputNameRequired"] = "Playlist name is required";
    m["fillEmailAndPassword"] = "Please fill email and password";
    m["fillEmail"] = "Please fill email";
    m["fillVerificationCode"] = "Please fill verification code";
    m["fillAllFields"] = "Please fill all fields";
    m["pleaseEnterEmail"] = "Please enter email address first";
    m["pleaseLoginFirst"] = "Please login first";
    m["deleteConfirm"] = "Are you sure you want to delete?";
    m["confirmRemove"] = "Confirm Removal";
    m["confirmRemoveMessage"] = "Are you sure you want to remove this music from the playlist?";
    m["emptyFavorites"] = "No favorite music yet";
    m["addToFavorites"] = "Add to favorites";
    m["removeFromFavorites"] = "Remove from favorites";
    m["shareTrack"] = "Share";
    m["shareOnlineCopied"] = "Share text copied to clipboard";
    m["shareLocalCopied"] = "Share text copied to clipboard";
    m["shareNothingPlaying"] = "Nothing to share right now";
    m["shareClipboardHeaderOnline"] =
        "[Neko Music] Fantasy Network — free high-quality streaming & lossless music";
    m["shareClipboardHeaderLocal"] = "[Neko Music app] Playing a local audio file";
    m["shareClipboardTrack"] = "Track: %1";
    m["shareClipboardArtist"] = "Artist: %1";
    m["shareClipboardAlbum"] = "Album: %1";
    m["shareClipboardPromo"] =
        "Large catalog, playlists & charts, desktop lyrics, and cloud playlists. Completely free — grab the "
        "Windows / macOS / Linux / mobile clients for the full experience. Tell a friend about Neko Music!";
    m["shareClipboardPromoLocal"] =
        "This track is a local file. Neko Music also offers a rich online library for free — keep using the app "
        "for local + online playback.";
    m["shareClipboardLinkOnline"] = "Song page (open in your browser):";
    m["shareClipboardLinkLocal"] = "Local file (file URL):";
    m["deleteConfirmWithPlaylistName"] = "Are you sure you want to delete playlist \"{name}\"? This cannot be undone.";
    m["invalidEmail"] = "Invalid email format";
    m["emailPlaceholder"] = "Enter email";
    m["passwordPlaceholder"] = "Enter password";
    m["namePlaceholder"] = "Enter nickname";
    m["inputKeywordToSearch"] = "Enter keyword to search";

    // --- Errors & Debug ---
    m["emptyMusic"] = "Music data is empty";
    m["musicInPlaylist"] = "Music is already in playlist";
    m["invalidMusicList"] = "Invalid music list";
    m["musicExistsSkip"] = "Music already exists, skipping";
    m["downloadMusicFailedSkipCache"] = "Failed to download, skipping cache";
    m["getFilePathFailedSkipCache"] = "Failed to get path, skipping cache";
    m["writeToFileFailedSkipCache"] = "Failed to write file, skipping cache";
    m["parseRecentPlayListFailed"] = "Failed to parse recent plays";
    m["parseMusicFailed"] = "Failed to parse music";

    m["showWindow"] = "Show Window";
    m["exitApp"] = "Exit";

    // --- Upload Music ---
    m["uploadMusic"] = "Upload Music";
    m["uploadMusicTitle"] = "Upload Music";
    m["uploadMusicDesc"] = "Upload your music, waiting for admin review and publish";
    m["selectMusicFile"] = "Select Music File";
    m["selectCoverFile"] = "Select Cover Image";
    m["selectLyricsFile"] = "Select Lyrics File";
    m["musicTitle"] = "Song Title";
    m["artistName"] = "Artist Name";
    m["uploadLanguageLabel"] = "Language";
    m["albumName"] = "Album Name (Optional)";
    m["tagsLabel"] = "Tags (Optional)";
    m["durationLabel"] = "Duration (seconds)";
    m["uploadBtn"] = "Upload";
    m["uploading"] = "Uploading...";
    m["uploadSuccess"] = "Uploaded successfully, pending review";
    m["uploadFailed"] = "Upload failed";
    m["uploadDuplicate"] = "Duplicate music exists, please check and re-upload";
    m["uploadUnauthorized"] = "Please login first";
    m["musicFileRequired"] = "Please select a music file";
    m["titleRequired"] = "Please enter song title";
    m["artistRequired"] = "Please enter artist name";
    m["languageRequired"] = "Please select language";
    m["durationRequired"] = "Please enter duration";
    m["durationInvalid"] = "Duration must be a positive integer";
    m["noMusicFileSelected"] = "No music file selected";
    m["noCoverSelected"] = "No cover selected";
    m["noLyricsSelected"] = "No lyrics selected";
    m["musicFileFormat"] = "Supports MP3, FLAC, WAV formats";
    m["coverFileFormat"] = "Supports JPG, PNG, GIF formats";
    m["lyricsFileFormat"] = "Supports LRC format";
    m["fillRequiredFields"] = "Please fill required fields";
    m["langChinese"] = "Chinese";
    m["langCantonese"] = "Cantonese";
    m["langShanghainese"] = "Shanghainese";
    m["langEnglish"] = "English";
    m["langJapanese"] = "Japanese";
    m["langKorean"] = "Korean";
    m["langFrench"] = "French";
    m["langGerman"] = "German";
    m["langRussian"] = "Russian";
    m["langInstrumental"] = "Instrumental";
    m["required"] = "*";
    m["optional"] = "Optional";
    m["musicFileInfo"] = "Music File";
    m["coverInfo"] = "Cover Image";
    m["lyricsInfo"] = "Lyrics File";
    m["lyrics"] = "Lyrics";
    m["unknownArtist"] = "Unknown Artist";
    m["noLyrics"] = "No Lyrics";
    m["videoRenderButton"] = "Create share video";
    m["videoRenderButtonBusy"] = "Rendering…";
    m["videoRenderDialogTitle"] = "Create share video";
    m["videoRenderWatermark"] = "Add platform watermark";
    m["videoRenderStartLabel"] = "Clip start";
    m["videoRenderRangeValue"] = "%1 → %2";
    m["videoRenderVipHint"] = "VIP: render from selection to end of track; optional watermark";
    m["videoRenderFreeHint"] = "Free: 30s landscape clip with watermark, 10 per day";
    m["videoRenderEmailNotice"] = "Renders in background; email with download link when done";
    m["videoRenderSubmit"] = "Start";
    m["videoRenderSubmitted"] = "Job submitted; you will be emailed when ready";
    m["videoRenderRemainingToday"] = "Free renders left today: %1";
    m["videoRenderStatusProcessing"] = "Video rendering…";
    m["videoRenderStatusDone"] = "Share video is ready";
    m["videoRenderDownload"] = "Download MP4";
    m["videoRenderDownloadDone"] = "Saved to: %1";
    m["videoRenderFailed"] = "Render failed: %1";
    m["videoRenderCreateFailed"] = "Failed to create job";
    m["videoRenderLocalUnsupported"] = "Local tracks cannot create share videos";
    m["vipNav"] = "VIP Center";
    m["vipCenterTitle"] = "VIP Center";
    m["vipStatusActive"] = "VIP Member";
    m["vipStatusExpires"] = "Expires: %1";
    m["vipStatusLabel"] = "Membership";
    m["vipStatusInactive"] = "Non-member";
    m["nicknameEmpty"] = "Nickname cannot be empty";
    m["vipOpenMembership"] = "Get VIP";
    m["vipPricingLoadFailed"] = "Failed to load pricing";
    m["vipPayNoUrl"] = "No payment URL received";
    m["vipPayCreateFailed"] = "Failed to create order";
    m["vipPayOpenFailed"] = "Could not open payment URL";
    m["vipPayBrowserHint"] = "Checkout opened in browser. Return here and tap Refresh after payment.";
    m["vipRefresh"] = "Refresh";
    m["vipMonthsSuffix"] = " mo";
    m["vipDaysSuffix"] = " days";
    m["vipPlanDefault"] = "VIP plan";
    m["vipPriceYuan"] = "¥ %1";
    m["vipPayAlipay"] = "Alipay";
    m["vipPayWechat"] = "WeChat Pay";
    m["vipChoosePlan"] = "Choose a plan";
    m["vipMemberBenefits"] = "Longer share videos, optional watermark, and more";
    m["vipNonMemberBenefits"] = "Unlock longer share videos and watermark options";
    m["vipLoginRequired"] = "Please sign in before purchasing VIP";
    m["vipPillTooltipActive"] = "VIP active · Open VIP center";
    m["vipPillTooltipInactive"] = "Not VIP · Open VIP center";
    m["vipTagline"] = "Unlock high-quality music and member benefits";
    m["vipTerms"] = "Membership time stacks after payment. Use a stable network when paying.";
    m["vipCheckoutCurrentPlan"] = "Selected plan";
    m["vipCheckoutSelectPlan"] = "Choose a plan on the left";
    m["vipCheckoutPayHint"] = "Choose payment method";
    m["vipQrScanTip"] = "Scan with your camera or payment app";
    m["vipQrPayTitle"] = "%1 scan to pay";
    m["vipOpenInBrowser"] = "Open in browser";
    m["vipPaidDone"] = "I have paid";
    m["vipPaidDoneHint"] = "Membership status refreshed";
    m["vipChangePayMethod"] = "Change method";
    m["vipQrGenerateFailed"] = "Failed to generate QR code";
    m["vipQrImageLoadFailed"] = "Failed to load QR image";
    m["basicInfo"] = "Basic Info";
    m["uploadNote"] = "Uploaded music will be pending review, published after approval";

    // --- Netease Playlist Import ---
    m["importNeteasePlaylist"] = "Import Netease Playlist";
    m["importNeteaseDesc"] = "Enter Netease playlist link or ID to import songs to local playlist";
    m["inputNeteaseLink"] = "Enter Netease playlist link or ID...";
    m["createNewPlaylist"] = "Create New Playlist";
    m["creatingPlaylist"] = "Creating playlist...";
    m["fetchPlaylist"] = "Fetch Playlist";
    m["selectTargetPlaylist"] = "Select target playlist:";
    m["startImport"] = "Start Import";
    m["invalidNeteaseLink"] = "Invalid playlist link or ID";
    m["fetchPlaylistFailed"] = "Failed to fetch playlist";
    m["emptyNeteasePlaylist"] = "Playlist is empty";
    m["neteasePlaylistInfo"] = "Playlist: %1 (%2 songs)";
    m["loadPlaylistsFailed"] = "Failed to load playlists";
    m["noPlaylistsToImport"] = "No playlists available, please create one first";
    m["noTracksToImport"] = "No tracks to import";
    m["searchingTracks"] = "Searching for matching tracks...";
    m["searchFailed"] = "Search failed";
    m["searchServiceBusy"] = "Server is busy. Please try again later.";
    m["noMatchedTracks"] = "No matching tracks found";
    m["addingToPlaylist"] = "Adding %1 tracks to playlist...";
    m["addToPlaylistFailed"] = "Failed to add to playlist";
    m["importSuccess"] = "Import successful! Added %1 of %2 tracks, %3 not matched";

    // --- QQ Music Playlist Import ---
    m["importQqPlaylist"] = "Import QQ Music Playlist";
    m["importQqDesc"] = "Enter QQ Music playlist link or ID to import songs to local playlist";
    m["inputQqLink"] = "Enter QQ Music playlist link or ID...";
    m["invalidQqLink"] = "Invalid playlist link or ID";
    m["emptyQqPlaylist"] = "Playlist is empty";
    m["qqPlaylistInfo"] = "Playlist: %1 (%2 songs)";

    // --- Kugou Music Playlist Import ---
    m["importKugouPlaylist"] = "Import Kugou Music Playlist";
    m["importKugouDesc"] = "Enter Kugou Music playlist link or ID to import songs to local playlist";
    m["importQishuiPlaylist"] = "Import Qishui Music Playlist";
    m["importQishuiDesc"] = "Enter Qishui playlist link, short link, or ID to import songs to local playlist";
    m["inputQishuiLink"] = "Enter Qishui playlist link, short link, or ID...";
    m["invalidQishuiLink"] = "Invalid Qishui playlist link or ID";
    m["emptyQishuiPlaylist"] = "Qishui playlist is empty";
    m["qishuiPlaylistInfo"] = "Qishui playlist: %1\nTracks: %2";
    m["inputKugouLink"] = "Enter Kugou Music playlist link or ID...";
    m["invalidKugouLink"] = "Invalid playlist link or ID";
    m["emptyKugouPlaylist"] = "Playlist is empty";
    m["kugouPlaylistInfo"] = "Playlist: %1 (%2 songs)";

    return m;
}

QString I18n::tr(const QString &key) const
{
    const auto &map = (m_lang == EnUS) ? enUS() : (m_lang == NyaCN) ? nyaCN()
                                                                    : zhCN();
    auto it = map.constFind(key);
    if (it != map.constEnd())
        return it.value();
    return key;
}

QString I18n::settingsTitle() const { return tr("settings"); }
QString I18n::version() const { return tr("appVersion"); }
QString I18n::system() const { return tr("system"); }
QString I18n::languageLabel() const { return tr("language"); }
QString I18n::languageNya() const { return tr("languageNya"); }
QString I18n::languageChinese() const { return tr("languageChinese"); }
QString I18n::languageEnglish() const { return tr("languageEnglish"); }
QString I18n::languageZhCN() const { return tr("languageChinese"); }
QString I18n::languageEnUS() const { return tr("languageEnglish"); }
QString I18n::showWindow() const { return tr("showWindow"); }
QString I18n::exitApp() const { return tr("exitApp"); }
