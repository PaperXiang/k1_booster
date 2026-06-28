Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir

$ConfigFile = Join-Path $RootDir "src/brain/config/config.yaml"
$StrikerXml = Join-Path $RootDir "src/brain/behavior_trees/subtrees/subtree_striker_play.xml"
$GoalieXml = Join-Path $RootDir "src/brain/behavior_trees/subtrees/subtree_goal_keeper_play.xml"
$BackupDir = Join-Path $RootDir "tools/config-backups"

function Read-TextFileNoBom {
    param([string]$Path)
    return [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
}

function Write-TextFileNoBom {
    param(
        [string]$Path,
        [string]$Text
    )
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
}

function Backup-All {
    New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $dir = Join-Path $BackupDir $stamp
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item $ConfigFile (Join-Path $dir "config.yaml") -Force
    Copy-Item $StrikerXml (Join-Path $dir "subtree_striker_play.xml") -Force
    Copy-Item $GoalieXml (Join-Path $dir "subtree_goal_keeper_play.xml") -Force
    Write-Host "已备份当前配置到: $dir"
}

function Get-YamlValue {
    param(
        [string]$Path,
        [string]$Section,
        [string]$Key
    )
    $current = $null
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line -match '^    ([A-Za-z0-9_]+):\s*(?:#.*)?$') {
            $current = $Matches[1]
            continue
        }
        if ($current -eq $Section) {
            $pattern = '^      ' + [regex]::Escape($Key) + ':\s*([^#\r\n]*?)(?:\s*#.*)?$'
            if ($line -match $pattern) {
                return $Matches[1].Trim()
            }
        }
    }
    return "<not found>"
}

function Set-YamlValue {
    param(
        [string]$Section,
        [string]$Key,
        [string]$Value
    )
    Backup-All
    $text = Read-TextFileNoBom $ConfigFile
    $lines = $text -split "`n", -1
    $current = $null
    $changed = $false
    $keyPattern = '^(      ' + [regex]::Escape($Key) + ':\s*)([^#\r\n]*?)(\s*(?:#.*)?)?$'

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i].TrimEnd("`r")
        if ($line -match '^    ([A-Za-z0-9_]+):\s*(?:#.*)?$') {
            $current = $Matches[1]
            continue
        }
        if ($current -eq $Section -and $line -match $keyPattern) {
            $suffix = $Matches[3]
            if ($null -eq $suffix) { $suffix = "" }
            if ($suffix.StartsWith("#")) { $suffix = " " + $suffix }
            $lines[$i] = $Matches[1] + $Value + $suffix
            $changed = $true
            break
        }
    }

    if (-not $changed) {
        throw "找不到配置项 $Section.$Key，文件: $ConfigFile"
    }

    Write-TextFileNoBom $ConfigFile ($lines -join "`n")
    Write-Host "已修改 $Section.$Key = $Value"
}

function Get-ArcWalk {
    param([string]$Path)
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line.Contains('<Chase') -and -not $line.Contains('<!--') -and -not $line.Contains('-->')) {
            if ($line -match 'arc_walk="(true|false)"') {
                return $Matches[1]
            }
            return "<not found>"
        }
    }
    return "<not found>"
}

function Set-ArcWalkInFile {
    param(
        [string]$Path,
        [string]$Value
    )
    $text = Read-TextFileNoBom $Path
    $lines = $text -split "`n", -1
    $changed = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i].TrimEnd("`r")
        if ($line.Contains('<Chase') -and -not $line.Contains('<!--') -and -not $line.Contains('-->')) {
            if ($line -match 'arc_walk="(?:true|false)"') {
                $lines[$i] = [regex]::Replace($line, 'arc_walk="(?:true|false)"', "arc_walk=`"$Value`"", 1)
            } else {
                $lines[$i] = $line.TrimEnd(' ', '/', '>') + " arc_walk=`"$Value`" />"
            }
            $changed = $true
            break
        }
    }

    if (-not $changed) {
        throw "没有找到可修改的 <Chase ...>，文件: $Path"
    }

    Write-TextFileNoBom $Path ($lines -join "`n")
    Write-Host "已修改 $(Split-Path -Leaf $Path): arc_walk=$Value"
}

function Set-ArcWalk {
    param(
        [string]$Target,
        [string]$Value
    )
    Backup-All
    switch ($Target) {
        "striker" { Set-ArcWalkInFile $StrikerXml $Value }
        "goalie" { Set-ArcWalkInFile $GoalieXml $Value }
        "all" {
            Set-ArcWalkInFile $StrikerXml $Value
            Set-ArcWalkInFile $GoalieXml $Value
        }
        default { throw "未知 arc_walk 目标: $Target" }
    }
}

function Show-Status {
    Write-Host ""
    Write-Host "当前测试开关状态"
    Write-Host "----------------------------------------------------------------"
    Write-Host ("{0,-42} {1}" -f "前锋 Chase 弧线追球 arc_walk", (Get-ArcWalk $StrikerXml))
    Write-Host ("{0,-42} {1}" -f "守门员 Chase 弧线追球 arc_walk", (Get-ArcWalk $GoalieXml))
    Write-Host ("{0,-42} {1}" -f "Adjust 绕球超时秒数，0=关闭", (Get-YamlValue $ConfigFile "strategy" "adjust_timeout_secs"))
    Write-Host ("{0,-42} {1}" -f "球路预测总开关 enable", (Get-YamlValue $ConfigFile "ball_prediction" "enable"))
    Write-Host ("{0,-42} {1}" -f "球路预测是否接管追球 use_for_chase", (Get-YamlValue $ConfigFile "ball_prediction" "use_for_chase"))
    Write-Host ("{0,-42} {1}" -f "球路预测提前量 predict_time", (Get-YamlValue $ConfigFile "ball_prediction" "predict_time"))
    Write-Host ("{0,-42} {1}" -f "丢球预测保持时间 lost_timeout", (Get-YamlValue $ConfigFile "ball_prediction" "lost_prediction_timeout"))
    Write-Host ("{0,-42} {1}" -f "定位球阶段禁用预测追球", (Get-YamlValue $ConfigFile "ball_prediction" "disable_for_set_play_chase"))
    Write-Host "----------------------------------------------------------------"
}

function Ask-Bool {
    param([string]$Prompt)
    while ($true) {
        $value = Read-Host "$Prompt [true/false]"
        switch -Regex ($value) {
            '^(true|t|yes|y|1|开|开启)$' { return "true" }
            '^(false|f|no|n|0|关|关闭)$' { return "false" }
            default { Write-Host "请输入 true/false，或输入 开/关。" }
        }
    }
}

function Ask-Number {
    param([string]$Prompt)
    while ($true) {
        $value = Read-Host $Prompt
        if ($value -match '^-?[0-9]+([.][0-9]+)?$') {
            return $value
        }
        Write-Host "请输入数字，例如 0、0.25、3.5。"
    }
}

function Restore-Backup {
    if (-not (Test-Path $BackupDir)) {
        Write-Host "没有备份目录: $BackupDir"
        return
    }
    $dirs = Get-ChildItem -Path $BackupDir -Directory | Sort-Object Name -Descending
    if ($dirs.Count -eq 0) {
        Write-Host "没有找到备份。"
        return
    }
    Write-Host "可恢复的备份:"
    for ($i = 0; $i -lt $dirs.Count; $i++) {
        Write-Host ("  {0}) {1}" -f ($i + 1), $dirs[$i].Name)
    }
    $choice = Read-Host "输入要恢复的备份编号，直接回车取消"
    if ([string]::IsNullOrWhiteSpace($choice)) { return }
    if (-not ($choice -match '^[0-9]+$') -or [int]$choice -lt 1 -or [int]$choice -gt $dirs.Count) {
        Write-Host "选择无效。"
        return
    }
    $selected = $dirs[[int]$choice - 1].FullName
    Copy-Item (Join-Path $selected "config.yaml") $ConfigFile -Force
    Copy-Item (Join-Path $selected "subtree_striker_play.xml") $StrikerXml -Force
    Copy-Item (Join-Path $selected "subtree_goal_keeper_play.xml") $GoalieXml -Force
    Write-Host "已恢复备份: $selected"
}

function Show-Menu {
    while ($true) {
        Show-Status
        Write-Host ""
        Write-Host "测试开关菜单"
        Write-Host "1) 设置前锋 Chase 弧线追球 arc_walk"
        Write-Host "2) 设置守门员 Chase 弧线追球 arc_walk"
        Write-Host "3) 同时设置前锋和守门员 arc_walk"
        Write-Host "4) 设置 Adjust 绕球超时秒数，0 表示关闭"
        Write-Host "5) 设置球路预测总开关 enable"
        Write-Host "6) 设置球路预测是否接管追球 use_for_chase"
        Write-Host "7) 设置球路预测提前量 predict_time"
        Write-Host "8) 设置丢球预测保持时间 lost_prediction_timeout"
        Write-Host "9) 设置定位球/开球阶段是否禁用预测追球"
        Write-Host "b) 恢复备份"
        Write-Host "q) 退出"

        $choice = Read-Host "请选择"
        switch ($choice) {
            "1" { Set-ArcWalk "striker" (Ask-Bool "前锋 arc_walk，开=true，关=false") }
            "2" { Set-ArcWalk "goalie" (Ask-Bool "守门员 arc_walk，开=true，关=false") }
            "3" { Set-ArcWalk "all" (Ask-Bool "前锋和守门员 arc_walk，开=true，关=false") }
            "4" { Set-YamlValue "strategy" "adjust_timeout_secs" (Ask-Number "Adjust 绕球超时秒数，0 表示关闭") }
            "5" { Set-YamlValue "ball_prediction" "enable" (Ask-Bool "球路预测总开关 enable") }
            "6" { Set-YamlValue "ball_prediction" "use_for_chase" (Ask-Bool "球路预测是否接管追球 use_for_chase") }
            "7" { Set-YamlValue "ball_prediction" "predict_time" (Ask-Number "球路预测提前量 predict_time，单位秒") }
            "8" { Set-YamlValue "ball_prediction" "lost_prediction_timeout" (Ask-Number "丢球预测保持时间 lost_prediction_timeout，单位秒") }
            "9" { Set-YamlValue "ball_prediction" "disable_for_set_play_chase" (Ask-Bool "定位球/开球阶段禁用预测追球") }
            "b" { Restore-Backup }
            "B" { Restore-Backup }
            "q" { return }
            "Q" { return }
            default { Write-Host "未知选项: $choice" }
        }
    }
}

switch ($args[0]) {
    "--status" { Show-Status; break }
    "--backup" { Backup-All; break }
    "--restore" { Restore-Backup; break }
    $null { Show-Menu; break }
    default {
        Write-Host "用法: powershell -ExecutionPolicy Bypass -File tools/test_switch_tui.ps1 [--status|--backup|--restore]"
        exit 2
    }
}
