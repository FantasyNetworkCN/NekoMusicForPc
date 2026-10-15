# 从 src/core/i18n.cpp 的三张语言表里删除「全仓无引用」的键定义行。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\prune-i18n-keys.ps1 -KeysFile _scan\i18n-unused.txt
#
# 安全措施：
#   * 只删除形如 `    m["key"] = ...;` 且 key 在名单里的**单行定义**；
#   * 要求该行只包含一个 m["..."] 定义（多定义行会被跳过并报警，交人工处理）；
#   * 按行号倒序删除，避免行号偏移；
#   * 默认 dry-run，加 -Apply 才真正写回。

param(
    [Parameter(Mandatory = $true)][string]$KeysFile,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot | Split-Path -Parent
$i18nPath = Join-Path $repoRoot 'src\core\i18n.cpp'

if (-not (Test-Path -LiteralPath $KeysFile)) { $KeysFile = Join-Path $repoRoot $KeysFile }
$keys = Get-Content -LiteralPath $KeysFile | Where-Object { $_.Trim() -ne '' }
$keySet = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($k in $keys) { [void]$keySet.Add($k.Trim()) }
Write-Host ("待删键数: {0}" -f $keySet.Count)

$lines = [System.IO.File]::ReadAllLines($i18nPath)

# 三张表边界
$zhS = 0; $nyaS = 0; $enS = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'static\s+QMap<QString,\s*QString>\s+zhCN\s*\(') { $zhS = $i }
    if ($lines[$i] -match 'static\s+QMap<QString,\s*QString>\s+nyaCN\s*\(') { $nyaS = $i }
    if ($lines[$i] -match 'static\s+QMap<QString,\s*QString>\s+enUS\s*\(') { $enS = $i }
}
if ($zhS -eq 0 -or $nyaS -eq 0 -or $enS -eq 0) { throw "没能定位三张语言表" }

$toDelete = New-Object 'System.Collections.Generic.List[int]'
$skipped = New-Object 'System.Collections.Generic.List[string]'
$counts = @{ zhCN = 0; nyaCN = 0; enUS = 0 }
$multiLine = 0

for ($i = 0; $i -lt $lines.Count; $i++) {
    $m = [regex]::Match($lines[$i], '^\s*m\["([^"]+)"\]\s*=')
    if (-not $m.Success) { continue }
    $key = $m.Groups[1].Value
    if (-not $keySet.Contains($key)) { continue }

    $defs = ([regex]::Matches($lines[$i], 'm\["')).Count
    if ($defs -ne 1) { $skipped.Add("line $($i+1): 含 $defs 个定义 -> $($lines[$i].Trim())"); continue }

    if ($i -gt $zhS -and $i -lt $nyaS) { $counts.zhCN++ }
    elseif ($i -gt $nyaS -and $i -lt $enS) { $counts.nyaCN++ }
    elseif ($i -gt $enS) { $counts.enUS++ }
    else { $skipped.Add("line $($i+1): 不在任何表体内"); continue }

    # 关键：一条定义可能**跨多行**（QStringLiteral( ... ) 换行写）。
    # 只删首行会留下孤立的续行片段，直接编译报 "expected ';' before ')'"。
    # 所以从定义行一直吃到以 ';' 结尾的那一行为止。
    $toDelete.Add($i)
    if ($lines[$i] -notmatch ';\s*$') {
        $j = $i + 1
        while ($j -lt $lines.Count) {
            $toDelete.Add($j)
            if ($lines[$j] -match ';\s*$') { break }
            $j++
        }
        if ($j -ge $lines.Count) { throw "定义未找到结尾分号，起始行 $($i+1)" }
        $multiLine++
    }
}

Write-Host ("命中定义: zhCN={0} nyaCN={1} enUS={2}" -f $counts.zhCN, $counts.nyaCN, $counts.enUS)
Write-Host ("其中跨多行的定义: {0}" -f $multiLine)
Write-Host ("实际删除行数: {0}" -f $toDelete.Count)
if ($skipped.Count -gt 0) {
    Write-Host "跳过（需人工确认）:"
    $skipped | ForEach-Object { Write-Host "  $_" }
}

# 倒序删除
$sorted = $toDelete | Sort-Object -Descending
$out = New-Object 'System.Collections.Generic.List[string]'
$delSet = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($idx in $sorted) { [void]$delSet.Add($idx) }
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($delSet.Contains($i)) { continue }
    $out.Add($lines[$i])
}

if (-not $Apply) {
    Write-Host ""
    Write-Host "DRY-RUN：未写回。加 -Apply 才真正修改 $i18nPath"
    exit 0
}

$enc = New-Object System.Text.UTF8Encoding($false)
# 关键：原文件是 LF 行尾（仓库约定）。.NET 的 WriteAllLines 会用 Environment.NewLine
# 写成 CRLF，会把整文件行尾都改掉、diff 从 481 行暴涨成全文。这里显式用 "\n" 拼接。
$newText = ($out -join "`n") + "`n"
[System.IO.File]::WriteAllText($i18nPath, $newText, $enc)
Write-Host ""
Write-Host ("已写回 {0}：{1} 行 -> {2} 行（删除 {3} 行，行尾保持 LF）" -f `
        $i18nPath, $lines.Count, $out.Count, ($lines.Count - $out.Count))
