# 崩溃记录（Windows）

本文件记录 NekoMusic 在 Windows 上的崩溃事件与归因结论。数据来源是 Windows 事件日志
（Application，事件 ID 1000/1001/1002），因为应用自身此前没有文件日志。

## 怎么新增一条

```powershell
# 1. 取最近的崩溃事件
Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000} -MaxEvents 50 |
  Where-Object { $_.Message -match 'NekoMusic' } |
  Select-Object TimeCreated, @{n='Msg'; e={$_.Message}} | Format-List

# 2. 把「模块 + 错误偏移」反查成函数（无需 PDB，靠 PE 导出表）
python tools/win-symbolize-offset.py "D:\NEKO\Neko歌姬计划\Qt6Widgets.dll" 0x45516
```

> 注意：**偏移与 Qt 版本强绑定**。本项目历史上先后随包发过 Qt 6.10.3 与 6.10.2，
> 同一个偏移在两个版本里可能落在不同函数上，反查时务必用**当时运行的那个 DLL**。

---

## 2026-10-28 18:33:57（v2026.1027.57 / Qt 6.10.2）

| 字段 | 值 |
|---|---|
| 进程 ID | 0x4808 (18440) |
| 出错模块 | `Qt6Widgets.dll` 6.10.2.0 |
| 异常代码 | `0xc0000005` |
| 错误偏移 | `0x0000000000045523` |
| 反查结果 | **`QWidget::setGraphicsEffect(QGraphicsEffect*)` +0x1a3** |
| 报告 ID | `6bcef315-a0fc-4dfc-98b2-b752ea9f6aef` |

**反汇编判读**（关键，和 10/27 那次不是同一个失效点）：

```
0x45516  48 8b 43 78    mov  rax, [rbx+0x78]   ; rax = d->graphicsEffect
0x4551a  48 85 c0       test rax, rax
0x4551d  74 3e          je   ...
0x4551f  48 8b 48 08    mov  rcx, [rax+8]      ; rcx = effect->d_ptr
0x45523  48 8b 01       mov  rax, [rcx]        ; ← 本次崩在这里
```

- 10/27（`0x45516`）：第一条指令就崩 → **控件本身已被销毁**（`rbx` 无效）。
- 10/28（`0x45523`）：`d->graphicsEffect` 读出来了、`test` 也过了，崩在解引用
  `effect->d_ptr` 之后 → **控件还活着，但挂在它上面的 QGraphicsEffect 已被销毁**，
  即 `d->graphicsEffect` 是悬垂指针。

**意义**：这是同一族的第二种形态（悬垂 effect，而不是悬垂控件）。它说明存在
「effect 被提前销毁、控件却仍持有它」的所有权问题——例如把 effect 挂在 A 名下却
`setGraphicsEffect` 给 B，或 effect 随父对象先于目标控件析构。这类问题靠 QPointer
（只保护动画成员）修不到，必须知道**具体调用点**。

---

## 2026-10-27 21:30:32（v2026.1027.57 / Qt 6.10.2）

| 字段 | 值 |
|---|---|
| 应用程序 | `NekoMusic.exe`，文件版本字段 `2026.417.0-beta`（发布打包未更新版本资源） |
| 安装版本 | 注册表 `DisplayVersion = 2026.1027.57` |
| 路径 | `D:\NEKO\Neko歌姬计划\NekoMusic.exe` |
| 进程 ID | 0x265C (9820) |
| 出错模块 | `Qt6Widgets.dll` 6.10.2.0 |
| 异常代码 | `0xc0000005`（访问违例） |
| 错误偏移 | `0x0000000000045516` |
| 反查结果 | **`QWidget::setGraphicsEffect(QGraphicsEffect*)` +0x196**（函数体 0x45480–0x45620） |
| 报告 ID | `83b4e13f-787c-409a-808e-6fb769ce32ea` |

**归因**：与 10/23 15:00（Qt6Widgets 0x45216）、10/26 00:42（0x45216）属于同一类——
对**已销毁的 QWidget** 调用 `setGraphicsEffect()`，函数内部第一条 `mov rax,[rbx+0x78]`
就解引用了一个无效指针。即 UI 对象生命周期问题（野指针/use-after-free），不是音频链路。

**意义**：换成官方新版本（2026.1027.57 + Qt 6.10.2）后**仍然复现**，说明问题在应用层的
特效/动画使用方式，而不是某个 Qt 版本的 bug；此前在 `D:\git\nekoforpc` 里做的
`HttpMediaDevice` 生命周期修复与文件日志**尚未编入本包**，因此本次依旧没有应用侧日志。

---

## 历史汇总（2026-09-15 ~ 2026-10-28，共 51 次）

按模块：`Qt6Core` 25 次、`Qt6Widgets` 11 次、`unknown` 10 次、`NekoMusic.exe` 4 次、`Qt6Gui` 1 次。

| 偏移 | 次数 | Qt 6.10.3 下反查 | Qt 6.10.2 下反查 | 备注 |
|---|---:|---|---|---|
| `0x2161c8` | 16 | `QAbstractAnimation::stop()` +0x8 | 同 | **最多**：对已销毁的动画调 stop |
| `0x00000000` | 10 | （unknown 模块） | — | 调用空函数指针，无模块信息 |
| `0x215a51` | 6 | `QAbstractAnimationPrivate::setState` +0x51 | 同 | 动画状态机踩野指针 |
| `0x45216` | 3 | `QWidget::setGraphicsEffect` +0x196 | `QWidget::focusOutEvent` | 版本相关，需按当时 DLL 解读 |
| `0x45265` | 2 | `QWidget::setGraphicsEffect` | `QWidget::setAutoFillBackground` | 同上 |
| `0x45516` | 2 | `QWidget::setMask` +0x196 | **`QWidget::setGraphicsEffect` +0x196** | 控件已销毁 |
| `0x4521f` | 1 | `QWidget::setGraphicsEffect` +0x19f | `QWidget::focusOutEvent` | |
| `0x4551f` | 1 | `QWidget::setMask` +0x19f | `QWidget::setGraphicsEffect` +0x19f | |
| `0x45523` | 1 | — | **`QWidget::setGraphicsEffect` +0x1a3** | 控件存活、effect 悬垂（10/28） |
| `0x1634d8` | 2 | `QAnyStringView::compare` | `QString::operator=` | 疑似导出表对齐误差，待复核 |
| 其它单次 | 6 | `0x159cf5` `0x68a30` `0x512d7` `0xa04a5` `0xa5e45` `0x9ba0b0` `0x54ad7` | — | 零散 |

**聚类结论**：约 43%（22/51）是「动画对象被 stop/setState 时已销毁」，约 20%（10/51）是
「图形特效踩野指针（控件或 effect 已销毁）」，两者合计约六成，全部集中在 UI 特效/动画层。

---

## 修复记录

### 第二批（2026-10-28，定位到具体调用点）

| 改动 | 位置 | 解决的问题 |
|---|---|---|
| **不再用 `setGraphicsEffect(nullptr)` 关阴影后复用同一个 effect** | `playlistpanel.cpp::setFullPlayerMode` | **10/28 崩溃的直接原因**：`setGraphicsEffect(nullptr)` 会把 `m_drawerShadow` 交给 `deleteLater()`，随后又把同一个（已排队销毁的）effect 挂回去，销毁后控件持有悬垂 effect，再调用即崩在 `QWidget::setGraphicsEffect` +0x1a3 |

> 触发条件：进出播放页全屏模式（`PlaylistPanel::setFullPlayerMode`），与「播放时操作导致崩溃」的现象一致。
> 现在改成 `m_drawerShadow->setEnabled(!on)`，只开关效果、从不摘除，effect 生命周期与控件保持一致。

### 第一批（2026-10-27，针对聚类）

| 改动 | 位置 | 解决的问题 |
|---|---|---|
| 新增崩溃抓栈（`SetUnhandledExceptionFilter` + DbgHelp） | `src/core/crashhandler.{h,cpp}`，在 `main.cpp` 安装，输出 `logs\crash-*.txt` | 崩溃只有「模块+偏移」，看不到调用点 |
| 动画成员改 `QPointer`，去掉 `stop()` 后的二次 `deleteLater()` | `commentpanel`、`notificationpanel`、`playlistpanel` 的 `m_slideAnim`；`mainwindow` 抽屉遮罩的 `m_fadeAnim` | `QAbstractAnimation::stop()` 在已销毁动画上被调用（14 次） |
| 歌词模糊不再反复增删效果 | `playerpage.cpp::applyLyricViewportFocusBlur` | 16ms 定时器里高频 `setGraphicsEffect(nullptr)`/`new QGraphicsBlurEffect`，与销毁时序重叠时踩野指针（`QWidget::setGraphicsEffect` 崩溃） |
| 页面切换加 `QPointer` 守卫 + 防卡死兜底 | `mainwindow.cpp::switchPage` | 对已销毁页面调 `setGraphicsEffect`；动画被中断时 `m_switching` 永久为 true，界面「点不动」 |

> 仍待验证：以上改动需要重新出包（本机无编译器），崩溃抓栈也只能在下次崩溃时才产出证据。

## 待办

1. ~~装应用内崩溃抓栈~~ ✅ 已实现，待出包验证
2. ~~统一动画/特效生命周期~~ ✅ 已改 4 处动画成员 + 歌词模糊
3. 出包后复现一次，用 `logs\crash-*.txt` 里的调用栈确认剩余崩溃的具体调用点
