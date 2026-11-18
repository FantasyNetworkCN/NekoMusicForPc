#!/usr/bin/env bash
# 从 CMakeLists.txt 同步 PKGBUILD / .SRCINFO 版本号，并把 AUR 源固定到指定提交。
#
# 用法：
#   ./packaging/aur/sync-aur.sh [commit] [pkgrel]
#
# 不传 commit 时使用当前 HEAD，不传 pkgrel 时保留 PKGBUILD 中的值。
# GitHub Actions 会传入触发工作流的 github.sha，并在上游版本未变时递增 pkgrel。
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

commit="${1:-$(git rev-parse HEAD)}"
pkgrel="${2:-}"
if [[ ! "$commit" =~ ^[0-9a-f]{40}$ ]]; then
  echo "ERROR: commit 必须是完整的 40 位小写 SHA，收到：${commit}" >&2
  exit 1
fi
if [[ -n "$pkgrel" && ! "$pkgrel" =~ ^[1-9][0-9]*$ ]]; then
  echo "ERROR: pkgrel 必须是正整数，收到：${pkgrel}" >&2
  exit 1
fi

pkgver="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if [[ -z "$pkgver" ]]; then
  echo "ERROR: 无法从 CMakeLists.txt 读取 VERSION" >&2
  exit 1
fi

sed -i "s/^pkgver=.*/pkgver=${pkgver}/" PKGBUILD
sed -i "s/^_commit=.*/_commit=${commit}/" PKGBUILD
if [[ -n "$pkgrel" ]]; then
  sed -i "s/^pkgrel=.*/pkgrel=${pkgrel}/" PKGBUILD
fi

if ! grep -q "^pkgver=${pkgver}$" PKGBUILD || ! grep -q "^_commit=${commit}$" PKGBUILD; then
  echo "ERROR: 更新 PKGBUILD 失败，请检查 pkgver / _commit 字段" >&2
  exit 1
fi
if [[ -n "$pkgrel" ]] && ! grep -q "^pkgrel=${pkgrel}$" PKGBUILD; then
  echo "ERROR: 更新 PKGBUILD 失败，请检查 pkgrel 字段" >&2
  exit 1
fi

# makepkg 拒绝以 root 运行；CI 容器默认是 root，因此仅在生成元数据时
# 切换到 nobody，输出重定向仍由当前用户执行。
if [[ "$(id -u)" -eq 0 ]]; then
  runuser -u nobody -- env \
    HOME=/tmp \
    BUILDDIR=/tmp \
    PKGDEST=/tmp \
    SRCDEST=/tmp \
    SRCPKGDEST=/tmp \
    LOGDEST=/tmp \
    makepkg --printsrcinfo > .SRCINFO
else
  makepkg --printsrcinfo > .SRCINFO
fi

echo "Synced AUR pkgver=${pkgver}, commit=${commit}"
