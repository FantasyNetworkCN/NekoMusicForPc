#!/usr/bin/env python3
"""检查 Windows 部署目录，按 PE 导入表补齐来自同一 Qt 套件的依赖。"""

import argparse
from collections import defaultdict, deque
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


# 由目标 Windows 提供的系统 DLL。运行库和第三方依赖必须来自部署目录。
SYSTEM_DLLS = frozenset("""
    advapi32.dll authz.dll avrt.dll bcrypt.dll combase.dll comctl32.dll
    comdlg32.dll crypt32.dll d2d1.dll d3d11.dll d3d12.dll d3d9.dll dbghelp.dll
    dnsapi.dll dwmapi.dll dwrite.dll dxgi.dll dxva2.dll evr.dll gdi32.dll
    imagehlp.dll imm32.dll iphlpapi.dll kernel32.dll mf.dll mfplat.dll
    mfreadwrite.dll mfuuid.dll mpr.dll msvcrt.dll ncrypt.dll netapi32.dll
    ntdll.dll odbc32.dll ole32.dll oleaut32.dll opengl32.dll propsys.dll psapi.dll
    rpcrt4.dll secur32.dll setupapi.dll shcore.dll shell32.dll shlwapi.dll
    user32.dll userenv.dll uxtheme.dll version.dll winhttp.dll winmm.dll
    winspool.drv ws2_32.dll wtsapi32.dll
""".split())

# Qt 动态发现这些插件，主程序的 PE 导入表里没有它们。
REQUIRED_FILES = (
    "NekoMusic.exe",
    "platforms/qwindows.dll",
    "multimedia/ffmpegmediaplugin.dll",
    "sqldrivers/qsqlite.dll",
    "tls/qschannelbackend.dll",
)


def imported_dlls(objdump, binary):
    result = subprocess.run(
        [objdump, "-p", str(binary)], capture_output=True,
        encoding="latin-1", env={**os.environ, "LC_ALL": "C"},
    )
    if result.returncode != 0:
        raise ValueError(f"无法读取 {binary} 的 PE 导入表: {result.stderr.strip()}")
    return re.findall(r"^\s*DLL Name:\s*(\S+)\s*$", result.stdout, re.MULTILINE)


def check(stage, objdump, qt_bin):
    files = {path.relative_to(stage).as_posix().lower(): path
             for path in stage.rglob("*") if path.is_file()}
    missing_files = [name for name in REQUIRED_FILES if name.lower() not in files]
    for name in missing_files:
        print(f"部署目录缺少必需文件: {name}", file=sys.stderr)

    dlls = {name for name in files if "/" not in name and name.endswith(".dll")}
    available = ({path.name.lower(): path for path in qt_bin.iterdir()
                  if path.is_file() and path.suffix.lower() == ".dll"}
                 if qt_bin else {})
    pending = deque(path for name, path in sorted(files.items())
                    if name.endswith((".exe", ".dll")))
    missing_imports = defaultdict(set)
    while pending:
        binary = pending.popleft()
        for dependency in imported_dlls(objdump, binary):
            name = dependency.lower()
            if (name in dlls or name in SYSTEM_DLLS
                    or name.startswith(("api-ms-win-", "ext-ms-win-"))):
                continue
            if name in available:
                source = available[name]
                deployed = stage / source.name
                shutil.copy2(source, deployed)
                dlls.add(name)
                pending.append(deployed)
                print(f"  从 Qt 套件补齐 {source.name}")
            else:
                missing_imports[name].add(binary.relative_to(stage).as_posix())

    for name, importers in sorted(missing_imports.items()):
        print(f"缺少 DLL: {name} ({', '.join(sorted(importers))})", file=sys.stderr)
    if missing_files or missing_imports:
        return 1
    print("Windows 部署目录的必需插件和 PE 导入依赖检查通过。")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--qt-bin", type=Path)
    args = parser.parse_args()
    try:
        return check(args.stage, args.objdump, args.qt_bin)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
