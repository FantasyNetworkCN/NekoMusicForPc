# AGENTS.md

本文件约束在 NekoMusic PC 客户端仓库中工作的编码代理与协作者。开始修改前先阅读本文件、
`README.md`，并检查相关模块现有实现；更深层目录的 `AGENTS.md` 优先。

## 通用要求

- 修改前检查 `git status`，保留工作区中已有的用户修改；只改完成当前任务必需的文件。
- 注释、日志与提交信息使用中文。提交信息使用 Conventional Commits，例如
  `fix(auth): 修复桌面客户端登录深链回跳`。
- 不提交密钥、`local.properties`、签名材料、个人配置、构建产物或与任务无关的变更。
- 尊重单实例、播放队列、网络请求头、账号状态等现有架构与行为约定。
- 所有后端请求使用 `NekoNetworkAccessManager`，确保统一携带官方 User-Agent 与认证请求头。

## 构建与测试

- 客户端使用 Qt 6、C++17 与 CMake。系统依赖、平台构建说明及功能约定见 `README.md`。
- Linux 本机构建可使用：

  ```bash
  cmake --preset linux-debug
  cmake --build build/linux-debug --parallel
  ctest --test-dir build/linux-debug --output-on-failure
  ```

- 已有 `build-linux/` 配置可使用：

  ```bash
  cmake --build build-linux --parallel
  ctest --test-dir build-linux --output-on-failure
  ```

- 若改动涉及界面、单实例、登录回跳或平台集成，至少构建相关目标并运行可用的 CTest。
  无法运行的验证须在交付说明中明确写出原因。
- 不要清理或覆盖已有构建目录中的用户文件；构建目录均为生成物，不得纳入提交。

## 账号与深链

- PC 登录通过后端 `/api/user/sso/start` 获取授权地址，再由系统浏览器完成 NekoAuth 登录。
- 授权完成后只允许通过 `nekomusic://sso/complete?handoff=...` 接收一次性交接，并调用
  `/api/user/sso/claim` 兑换登录态；不得把 access token 放进深链。
- 深链必须同时兼容首次启动参数、已运行实例的单实例消息转发，以及平台投递的 Qt
  `QFileOpenEvent`。新增或修改协议处理时，勿将 URL 当作本地文件路径处理。
- NekoAuth 客户端密钥仅保存在服务端，不得加入 PC 源码、资源或安装包。

## 未发布更新日志

`TEMP-LOG.md` 记录已合入但尚未随正式版本发布的用户可见变更。

- 只记录用户能感知的结果，不记录纯内部重构、测试、格式调整或实现细节。
- 按实际变更使用 `新增`、`优化`、`修复`、`移除` 分类；没有对应内容的分类不添加。
- 不在未发布日志中填写版本号、版本代码或正式发布日期。
- 发布正式版本时，将条目整理进 `CHANGELOG.md` 对应版本章节并清空 `TEMP-LOG.md`；仅在明确的发布流程中更新版本信息。

## 提交范围

- 每个提交聚焦一个主题。代码修改与该功能的 `TEMP-LOG.md` 更新应在同一任务中完成。
- PC 是独立 Git 子模块；提交 PC 仓库后，只有在当前任务需要时才更新父仓库子模块指针。
- 不修改 Android 或后端仓库，除非任务明确要求。
