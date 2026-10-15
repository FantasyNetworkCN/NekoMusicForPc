# 删除未引用的图标文件，并同步移除 src/resources/resources.qrc 里对应的 <file> 行。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\prune-icons.ps1 -ListFile _scan\icons-unused.txt [-Apply]
#
# 名单格式：每行 "文件名<TAB>字节数"（由 scan-icon-usage.ps1 生成）。
# 默认 dry-run，加 -Apply 才真正删除。
# 行尾保持 LF（仓库约定）。

param(
    [Parameter(Mandatory = $true)][string]$ListFile,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot | Split-Path -Parent
$iconDir = Join-Path $repoRoot 'src\resources\icons'
$qrcPath = Join-Path $repoRoot 'src\resources\resources.qrc'

if (-not (Test-Path -LiteralPath $ListFile)) { $ListFile = Join-Path $repoRoot $ListFile }
$names = @()
foreach ($line in (Get-Content -LiteralPath $ListFile)) {
    if ($line.Trim() -eq '') { continue }
    $names += ($line -split "`t")[0].Trim()
}
Write-Host ("名单文件: {0}" -f $ListFile)
Write-Host ("待删图标: {0}" -f $names.Count)

$nameSet = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($n in $names) { [void]$nameSet.Add($n) }

# 安全检查：文件必须真实存在，且必须出现在 qrc 里
$missing = @()
$notInQrc = @()
$qrcLines = [System.IO.File]::ReadAllLines($qrcPath)
foreach ($n in $names) {
    if (-not (Test-Path -LiteralPath (Join-Path $iconDir $n))) { $missing += $n }
    $found = $false
    foreach ($l in $qrcLines) { if ($l -match ('icons/' + [regex]::Escape($n) + '<')) { $found = $true; break } }
    if (-not $found) { $notInQrc += $n }
}
if ($missing.Count -gt 0) { Write-Host "!! 文件不存在: $($missing -join ', ')"; exit 1 }
if ($notInQrc.Count -gt 0) { Write-Host "!! 名单里有 qrc 未登记的项: $($notInQrc -join ', ')"; exit 1 }

# 计算 qrc 要删的行
$qrcDel = New-Object 'System.Collections.Generic.List[int]'
for ($i = 0; $i -lt $qrcLines.Count; $i++) {
    $m = [regex]::Match($qrcLines[$i], '<file>icons/(.+?)</file>')
    if ($m.Success -and $nameSet.Contains($m.Groups[1].Value)) { $qrcDel.Add($i) }
}
Write-Host ("qrc 命中行: {0}" -f $qrcDel.Count)
if ($qrcDel.Count -ne $names.Count) {
    Write-Host "!! qrc 命中数($($qrcDel.Count)) 与名单数($($names.Count)) 不一致，停止"
    exit 1
}

$totalBytes = 0
foreach ($n in $names) { $totalBytes += (Get-Item (Join-Path $iconDir $n)).Length }
Write-Host ("待删体积: {0:N1} KB" -f ($totalBytes / 1KB))

if (-not $Apply) {
    Write-Host ""
    Write-Host "DRY-RUN：未修改任何文件。加 -Apply 执行。"
    exit 0
}

# 1) 删文件
foreach ($n in $names) { Remove-Item -LiteralPath (Join-Path $iconDir $n) -Force }

# 2) 重写 qrc（去掉命中行，保持原有行尾；resources.qrc 本来是 CRLF，不要改成 LF）
$delSet = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($i in $qrcDel) { [void]$delSet.Add($i) }
$out = New-Object 'System.Collections.Generic.List[string]'
for ($i = 0; $i -lt $qrcLines.Count; $i++) {
    if ($delSet.Contains($i)) { continue }
    $out.Add($qrcLines[$i])
}
$rawQrc = [System.IO.File]::ReadAllText($qrcPath)
$crlfCount = ([regex]::Matches($rawQrc, "`r`n")).Count
$nlCount = ([regex]::Matches($rawQrc, "`n")).Count
$eol = if ($crlfCount -gt 0 -and $crlfCount -eq $nlCount) { "`r`n" } else { "`n" }
Write-Host ("qrc 行尾: CRLF={0} LF={1} -> 写回用 {2}" -f $crlfCount, $nlCount, ($(if ($eol -eq "`r`n") { 'CRLF' } else { 'LF' })))
$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($qrcPath, (($out -join $eol) + $eol), $enc)

Write-Host ""
Write-Host ("已删除 {0} 个图标（{1:N1} KB），qrc {2} 行 -> {3} 行" -f `
        $names.Count, ($totalBytes / 1KB), $qrcLines.Count, $out.Count)
