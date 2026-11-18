# AUR 自动同步

`.github/workflows/sync-aur.yml` 会在每次推送到 `qt6` 分支后：

1. 把 `PKGBUILD` 的 `_commit` 更新为本次 GitHub 提交的完整 SHA；
2. 从 `CMakeLists.txt` 更新 `pkgver`；
3. 上游版本未变时自动递增 `pkgrel`，确保包管理器能识别更新；
4. 重新生成 `.SRCINFO`；
5. 提交并推送到 AUR 仓库 `ssh://aur@aur.archlinux.org/neko-cloud-music.git`。

工作流只更新已存在的 AUR 包，不能替代首次提交包。首次发布前请先在
<https://aur.archlinux.org/submit/> 提交 `PKGBUILD` 和 `.SRCINFO`。

## 一次性配置

### 1. 创建专用 SSH 密钥

不要复用个人日常 SSH 密钥：

```bash
ssh-keygen -t ed25519 \
  -C "github-actions@NekoMusicForPc" \
  -f "$HOME/.ssh/aur_nekomusic" \
  -N ""

cat "$HOME/.ssh/aur_nekomusic.pub"
```

登录 AUR，在 **My Account → SSH Public Key** 中添加公钥内容。

### 2. 配置 GitHub Secret

AUR 的 SSH 主机公钥不是机密，已经固定在
`.github/workflows/sync-aur.yml` 中并启用严格主机校验，无需另配 Secret。

在仓库 **Settings → Secrets and variables → Actions** 中添加
`AUR_SSH_PRIVATE_KEY`：`$HOME/.ssh/aur_nekomusic` 的完整内容。

可用以下命令写入 GitHub（需要已安装并登录 `gh`）：

```bash
gh secret set AUR_SSH_PRIVATE_KEY < "$HOME/.ssh/aur_nekomusic"
```

### 3. 验证

推送一次 `qt6` 分支，或在 **Actions → Sync AUR → Run workflow** 手动运行。
成功后 AUR 页面应显示与 `CMakeLists.txt` 相同的 `pkgver`，源码 `_commit`
应等于本次 GitHub 提交。
