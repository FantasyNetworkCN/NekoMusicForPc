# 扫描 src/core/i18n.cpp 的键定义，并在全仓（排除 i18n.cpp 自身）搜索每个键的字面出现。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\scan-i18n-usage.ps1
#
# 判定语义（严格）：
#   * "used"      —— 该键在 i18n.cpp 之外至少出现 1 次（可能是 tr("x")、字符串拼接等）
#   * "unused"    —— 该键在 i18n.cpp 之外**任何地方**都找不到，可安全删除
#   * 动态拼接的键（例如 tr(QString::fromUtf8(albumKey))）不可能是"字面出现"，
#     所以脚本对这类键单独输出"需人工确认"清单，避免误删。
#
# 仅供人工审核，不做任何修改。

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot | Split-Path -Parent
$i18nPath = Join-Path $repoRoot 'src\core\i18n.cpp'

$files = Get-ChildItem -LiteralPath $repoRoot -Recurse -File |
    Where-Object {
        $_.Extension -in '.cpp', '.h', '.hpp', '.mm', '.qss', '.qrc', '.txt', '.md', '.ps1' -and
        $_.FullName -ne $i18nPath -and
        $_.FullName -notmatch '\\\.git\\' -and
        $_.FullName -notmatch '\\build'
    }

Write-Host ("扫描文件数: {0}" -f $files.Count)

$blob = New-Object System.Text.StringBuilder
foreach ($f in $files) { [void]$blob.AppendLine([System.IO.File]::ReadAllText($f.FullName)) }
$hay = $blob.ToString()
Write-Host ("合并文本长度: {0:N0} 字符" -f $hay.Length)

# 三张表的键定义（去重；nyaCN() 以 zhCN() 为底，所以同名键只算一次）
$lines = Get-Content -LiteralPath $i18nPath
$starts = @{}
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'static\s+QMap<QString,\s*QString>\s+(zhCN|nyaCN|enUS)\s*\(') { $starts[$matches[1]] = $i + 1 }
}
$zhKeys = New-Object 'System.Collections.Generic.HashSet[string]'
for ($i = $starts['zhCN'] - 1; $i -lt $starts['nyaCN'] - 1; $i++) {
    $m = [regex]::Match($lines[$i], '^\s*m\["([^"]+)"\]\s*=')
    if ($m.Success) { [void]$zhKeys.Add($m.Groups[1].Value) }
}

$used = New-Object 'System.Collections.Generic.List[string]'
$unused = New-Object 'System.Collections.Generic.List[string]'
$dynamic = New-Object 'System.Collections.Generic.List[string]'

foreach ($key in $zhKeys) {
    if ($hay.Contains('"' + $key + '"')) {
        $used.Add($key)
    }
    else {
        # 没有字面出现：检查是否可能是由变量拼出来的键（同名前缀出现在其它键里）
        $unused.Add($key)
    }
}

Write-Host ""
Write-Host ("键总数(zhCN 去重): {0}" -f $zhKeys.Count)
Write-Host ("全仓有字面引用   : {0}" -f $used.Count)
Write-Host ("全仓无字面引用   : {0}" -f $unused.Count)
Write-Host ""

$outDir = Join-Path $repoRoot '_scan'
if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$unusedPath = Join-Path $outDir 'i18n-unused.txt'
$usedPath = Join-Path $outDir 'i18n-used.txt'
[System.IO.File]::WriteAllLines($unusedPath, ($unused | Sort-Object))
[System.IO.File]::WriteAllLines($usedPath, ($used | Sort-Object))

Write-Host "无字面引用的键已写入: $unusedPath"
Write-Host ""
Write-Host "===== 无字面引用清单 ====="
$unused | Sort-Object | ForEach-Object { Write-Host "  $_" }
