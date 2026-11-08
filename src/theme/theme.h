#pragma once

/**
 * @file theme.h
 * @brief NekoMusic 桌面端视觉规范：冰蓝强调、雾面玻璃、统一空间节奏。
 */

#include <QColor>

namespace Theme
{

    // ─── 主色：冰蓝（旧常量名称保留兼容）────────
    constexpr const char *kLavender = "#40C5F1"; // 保留旧标识，避免破坏调用方
    constexpr const char *kLavenderLt = "#8ADFF5";
    constexpr const char *kLavenderDk = "#1677B8";

    // ─── 辅色：浅靛蓝（玻璃描边 / 弱高亮）────────────────────────────
    constexpr const char *kSakura = "#91B9EA";
    constexpr const char *kSakuraLt = "#D1E8FA";
    constexpr const char *kSakuraDk = "#6B8CFF";

    // ─── 点缀：薄荷绿（焦点 / 侧栏选中条）────────────────────────────
    constexpr const char *kMint = "#7D9CA4";
    constexpr const char *kMintLt = "#9FB5BC";
    constexpr const char *kMintDk = "#5F7A81";

    // ─── 背景（墨蓝底色 + 分层容器）────────────────────
    constexpr const char *kBgDeep = "#0B1020";
    constexpr const char *kBgMid = "#111A2C";
    constexpr const char *kBgSurface = "#19243A";

    // ─── 文字 ────────────────────────────────────────────────────────
    constexpr const char *kTextMain = "#F4F6FF";
    constexpr const char *kTextSub = "rgba(244, 246, 255, 168)";
    constexpr const char *kTextMuted = "rgba(244, 246, 255, 105)";

    // ─── 雾面玻璃────────────────────────────────────
    constexpr const char *kGlassBg = "rgba(24, 35, 55, 168)";
    constexpr const char *kGlassSidebar = "rgba(15, 24, 42, 178)";
    constexpr const char *kGlassPlayer = "rgba(24, 35, 55, 210)";
    constexpr const char *kGlassOverlay = "rgba(11, 16, 32, 220)";

    // ─── 边框（冷色高光与清晰焦点环）──────────
    constexpr const char *kBorderGlass = "rgba(180, 210, 255, 32)";
    constexpr const char *kBorderFocus = "rgba(64, 197, 241, 180)";

    // ─── 渐变（QSS）──────────────────────────────────────────────────
    constexpr const char *kGradMain = "qlineargradient(x1:0,y1:0,x2:0.28,y2:1,"
                                      "stop:0 #8ADFF5, stop:1 #40C5F1)";
    constexpr const char *kGradSakura = "qlineargradient(x1:0,y1:0,x2:0.3,y2:1,"
                                        "stop:0 #D1E8FA, stop:1 #91B9EA)";
    constexpr const char *kGradMint = "qlineargradient(x1:0,y1:0,x2:0.3,y2:1,"
                                      "stop:0 #9FB5BC, stop:1 #7D9CA4)";
    constexpr const char *kGradBg = "qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                                    "stop:0 #111A2C, stop:1 #0B1020)";

    // ─── 布局尺寸（侧栏 224 / 顶栏 72 / 播放栏 92）────────
    constexpr int kSidebarW = 224;
    constexpr int kTitleBarH = 72;
    /** 底栏可见高度 */
    constexpr int kPlayerBarBodyH = 92;
    /** 保留旧调用方使用的进度条尺寸常量 */
    constexpr int kPlayerBarSliderOverhang = 8;
    constexpr int kPlayerBarH = kPlayerBarBodyH;
    constexpr int kCoverSmall = 160;
    constexpr int kCoverRadius = 14;

    // ─── 圆角 ──────────────────────────────────────────────────────
    constexpr int kRSm = 10;
    constexpr int kRMd = 14;
    constexpr int kRLg = 18;
    constexpr int kRXl = 22;

    // ─── 动画 (ms) ─────────────────────────────────────────────────
    constexpr int kAnimFast = 150;
    constexpr int kAnimNormal = 250;
    constexpr int kAnimSlow = 400;
    constexpr int kCarouselMs = 5000;

    constexpr const char *kApiBase = "https://music.nekocore.cn";

} // namespace Theme
