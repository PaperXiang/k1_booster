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
    Write-Host "Backup saved: $dir"
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
            $pattern = '^      ' + [regex]::Escape($Key) + ':\s*([^#\r\n]*?)(?:\s+#.*)?$'
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
            $lines[$i] = $Matches[1] + $Value + $Matches[3]
            $changed = $true
            break
        }
    }

    if (-not $changed) {
        throw "Could not find $Section.$Key in $ConfigFile"
    }

    Write-TextFileNoBom $ConfigFile ($lines -join "`n")
    Write-Host "Updated $Section.$Key = $Value"
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
        throw "Expected one active <Chase ...> in $Path, found none"
    }

    Write-TextFileNoBom $Path ($lines -join "`n")
    Write-Host "Updated $(Split-Path -Leaf $Path): arc_walk=$Value"
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
        default { throw "Unknown arc_walk target: $Target" }
    }
}

function Show-Status {
    Write-Host ""
    Write-Host "Current test switches"
    Write-Host "----------------------------------------------------------------"
    $rows = @(
        @("striker arc_walk", (Get-ArcWalk $StrikerXml)),
        @("goal_keeper arc_walk", (Get-ArcWalk $GoalieXml)),
        @("strategy.adjust_timeout_secs", (Get-YamlValue $ConfigFile "strategy" "adjust_timeout_secs")),
        @("ball_prediction.enable", (Get-YamlValue $ConfigFile "ball_prediction" "enable")),
        @("ball_prediction.use_for_chase", (Get-YamlValue $ConfigFile "ball_prediction" "use_for_chase")),
        @("ball_prediction.predict_time", (Get-YamlValue $ConfigFile "ball_prediction" "predict_time")),
        @("ball_prediction.lost_prediction_timeout", (Get-YamlValue $ConfigFile "ball_prediction" "lost_prediction_timeout")),
        @("ball_prediction.disable_for_set_play_chase", (Get-YamlValue $ConfigFile "ball_prediction" "disable_for_set_play_chase"))
    )
    foreach ($row in $rows) {
        Write-Host ("{0,-42} {1}" -f $row[0], $row[1])
    }
    Write-Host "----------------------------------------------------------------"
}

function Ask-Bool {
    param([string]$Prompt)
    while ($true) {
        $value = Read-Host "$Prompt [true/false]"
        switch -Regex ($value) {
            '^(true|t|yes|y|1)$' { return "true" }
            '^(false|f|no|n|0)$' { return "false" }
            default { Write-Host "Please input true or false." }
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
        Write-Host "Please input a number, for example 0, 0.25, 3.5."
    }
}

function Restore-Backup {
    if (-not (Test-Path $BackupDir)) {
        Write-Host "No backup directory: $BackupDir"
        return
    }
    $dirs = Get-ChildItem -Path $BackupDir -Directory | Sort-Object Name -Descending
    if ($dirs.Count -eq 0) {
        Write-Host "No backups found."
        return
    }
    Write-Host "Available backups:"
    for ($i = 0; $i -lt $dirs.Count; $i++) {
        Write-Host ("  {0}) {1}" -f ($i + 1), $dirs[$i].Name)
    }
    $choice = Read-Host "Choose backup number to restore, or blank to cancel"
    if ([string]::IsNullOrWhiteSpace($choice)) { return }
    if (-not ($choice -match '^[0-9]+$') -or [int]$choice -lt 1 -or [int]$choice -gt $dirs.Count) {
        Write-Host "Invalid choice."
        return
    }
    $selected = $dirs[[int]$choice - 1].FullName
    Copy-Item (Join-Path $selected "config.yaml") $ConfigFile -Force
    Copy-Item (Join-Path $selected "subtree_striker_play.xml") $StrikerXml -Force
    Copy-Item (Join-Path $selected "subtree_goal_keeper_play.xml") $GoalieXml -Force
    Write-Host "Restored backup: $selected"
}

function Show-Menu {
    while ($true) {
        Show-Status
        Write-Host ""
        Write-Host "Test switch TUI"
        Write-Host "1) Set striker arc_walk"
        Write-Host "2) Set goalie arc_walk"
        Write-Host "3) Set both arc_walk"
        Write-Host "4) Set strategy.adjust_timeout_secs (0 disables)"
        Write-Host "5) Set ball_prediction.enable"
        Write-Host "6) Set ball_prediction.use_for_chase"
        Write-Host "7) Set ball_prediction.predict_time"
        Write-Host "8) Set ball_prediction.lost_prediction_timeout"
        Write-Host "9) Set ball_prediction.disable_for_set_play_chase"
        Write-Host "b) Restore backup"
        Write-Host "q) Quit"

        $choice = Read-Host "Choose"
        switch ($choice) {
            "1" { Set-ArcWalk "striker" (Ask-Bool "striker arc_walk") }
            "2" { Set-ArcWalk "goalie" (Ask-Bool "goalie arc_walk") }
            "3" { Set-ArcWalk "all" (Ask-Bool "both arc_walk") }
            "4" { Set-YamlValue "strategy" "adjust_timeout_secs" (Ask-Number "adjust_timeout_secs seconds, 0 disables") }
            "5" { Set-YamlValue "ball_prediction" "enable" (Ask-Bool "ball_prediction.enable") }
            "6" { Set-YamlValue "ball_prediction" "use_for_chase" (Ask-Bool "ball_prediction.use_for_chase") }
            "7" { Set-YamlValue "ball_prediction" "predict_time" (Ask-Number "ball_prediction.predict_time seconds") }
            "8" { Set-YamlValue "ball_prediction" "lost_prediction_timeout" (Ask-Number "ball_prediction.lost_prediction_timeout seconds") }
            "9" { Set-YamlValue "ball_prediction" "disable_for_set_play_chase" (Ask-Bool "ball_prediction.disable_for_set_play_chase") }
            "b" { Restore-Backup }
            "B" { Restore-Backup }
            "q" { return }
            "Q" { return }
            default { Write-Host "Unknown choice: $choice" }
        }
    }
}

switch ($args[0]) {
    "--status" { Show-Status; break }
    "--backup" { Backup-All; break }
    "--restore" { Restore-Backup; break }
    $null { Show-Menu; break }
    default {
        Write-Host "Usage: powershell -ExecutionPolicy Bypass -File tools/test_switch_tui.ps1 [--status|--backup|--restore]"
        exit 2
    }
}
