# 扫描 src/resources/icons 下的图标，判断是否在代码/样式中被引用。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\scan-icon-usage.ps1
#
# 判定依据（Icons::resourcePath() 把名字拼成 ":/icons/<name>.svg"）：
#   * .svg —— 文件名去掉扩展名的 stem 在 src/ 的 .cpp/.h/.mm/.qss/.ui 里出现即算被引用
#   * 其它（png/jpg/ico）—— 用带扩展名的完整文件名匹配
#   * qrc 文件不参与匹配（它里面每个图标都有一行 <file>，会把所有图标都算成"被引用"）
#
# 只做扫描输出，不修改任何文件。

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot | Split-Path -Parent
$iconDir = Join-Path $repoRoot 'src\resources\icons'
$qrcPath = Join-Path $repoRoot 'src\resources\resources.qrc'

$icons = Get-ChildItem -LiteralPath $iconDir -File

# qrc 之外的全部源码/样式文件
$files = Get-ChildItem -LiteralPath $repoRoot -Recurse -File |
    Where-Object {
        $_.Extension -in '.cpp', '.h', '.hpp', '.mm', '.qss', '.ui' -and
        $_.FullName -notmatch '\\\.git\\' -and
        $_.FullName -notmatch '\\icons\\' -and
        $_.FullName -notmatch '\\_scan\\' -and
        $_.FullName -notmatch '\\build'
    }

Write-Host ("图标文件数: {0}" -f $icons.Count)
Write-Host ("搜索文件数: {0}" -f $files.Count)

$texts = @{}
foreach ($f in $files) { $texts[$f.FullName] = [System.IO.File]::ReadAllText($f.FullName) }

$used = New-Object 'System.Collections.Generic.List[string]'
$unused = New-Object 'System.Collections.Generic.List[string]'
$whereMap = @{}

foreach ($icon in $icons) {
    $isSvg = $icon.Extension -ieq '.svg'
    $needle = if ($isSvg) { [System.IO.Path]::GetFileNameWithoutExtension($icon.Name) } else { $icon.Name }

    $where = New-Object 'System.Collections.Generic.List[string]'
    foreach ($kv in $texts.GetEnumerator()) {
        if ($kv.Value.Contains($needle)) { $where.Add((Split-Path $kv.Key -Leaf)) }
    }

    if ($where.Count -gt 0) {
        $used.Add($icon.Name)
        $whereMap[$icon.Name] = ($where | Select-Object -Unique) -join ','
    }
    else {
        $unused.Add($icon.Name)
    }
}

Write-Host ""
Write-Host ("被引用: {0}" -f $used.Count)
Write-Host ("未引用: {0}" -f $unused.Count)

$outDir = Join-Path $repoRoot '_scan'
if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$reportPath = Join-Path $outDir 'icons-unused.txt'
$linesOut = New-Object 'System.Collections.Generic.List[string]'
$totalBytes = 0
foreach ($n in ($unused | Sort-Object)) {
    $size = (Get-Item (Join-Path $iconDir $n)).Length
    $totalBytes += $size
    $linesOut.Add(("{0}`t{1}" -f $n, $size))
}
[System.IO.File]::WriteAllLines($reportPath, $linesOut)

Write-Host ""
Write-Host "===== 未被引用的图标 ====="
foreach ($n in ($unused | Sort-Object)) {
    $size = (Get-Item (Join-Path $iconDir $n)).Length
    Write-Host ("  {0,-34} {1,7} bytes" -f $n, $size)
}
Write-Host ""
Write-Host ("合计 {0} 个文件 / {1:N1} KB" -f $unused.Count, ($totalBytes / 1KB))
Write-Host "报告: $reportPath"

# 顺带输出被引用图标的出现位置，便于抽查是否有误判
$usedPath = Join-Path $outDir 'icons-used.txt'
$usedLines = New-Object 'System.Collections.Generic.List[string]'
foreach ($n in ($used | Sort-Object)) { $usedLines.Add(("{0}`t{1}" -f $n, $whereMap[$n])) }
[System.IO.File]::WriteAllLines($usedPath, $usedLines)
Write-Host "被引用清单: $usedPath"
