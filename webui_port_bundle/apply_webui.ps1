param(
    [string]$Target = "",
    [string]$ServerUrl = "",
    [switch]$PatchBrain,
    [switch]$NoPatchBrain,
    [switch]$OriginalDemo,
    [switch]$NotOriginalDemo,
    [switch]$NoBackup
)

$ErrorActionPreference = "Stop"

function Normalize-ServerUrl {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return "" }
    if ($Value.StartsWith("http://") -or $Value.StartsWith("https://")) { return $Value }
    return "http://${Value}:8000"
}

function Ask-YesNo {
    param(
        [string]$Prompt,
        [bool]$DefaultYes = $false
    )
    $hint = if ($DefaultYes) { "[Y/n]" } else { "[y/N]" }
    while ($true) {
        $answer = Read-Host "$Prompt $hint"
        if ([string]::IsNullOrWhiteSpace($answer)) {
            return $DefaultYes
        }
        switch ($answer.Trim().ToLowerInvariant()) {
            "y" { return $true }
            "yes" { return $true }
            "n" { return $false }
            "no" { return $false }
            default { Write-Host "Please enter y or n." }
        }
    }
}

Write-Host "WebUI Linux robot patch"
Write-Host "This only patches the Linux robot workspace. Windows frontend/backend are not copied."
Write-Host ""

if ([string]::IsNullOrWhiteSpace($Target)) {
    $Target = Read-Host "Target demo directory [current directory]"
    if ([string]::IsNullOrWhiteSpace($Target)) { $Target = "." }
}

if ([string]::IsNullOrWhiteSpace($ServerUrl)) {
    $serverInput = Read-Host "Windows backend IP or URL, for example 192.168.15.21 or http://192.168.15.21:8000"
    $ServerUrl = Normalize-ServerUrl $serverInput
} else {
    $ServerUrl = Normalize-ServerUrl $ServerUrl
}

$hasBallPredictor = $null
if ($OriginalDemo) { $hasBallPredictor = $false }
if ($NotOriginalDemo) { $hasBallPredictor = $true }
if ($null -eq $hasBallPredictor) {
    Write-Host ""
    Write-Host "Target demo type?"
    Write-Host "  Choose y for an original demo. This assumes there is NO k1_ball_predictor package."
    Write-Host "  Choose n for a newer/custom demo. This assumes k1_ball_predictor exists."
    $hasBallPredictor = -not (Ask-YesNo "Is this an original demo" $true)
}

$patchBrainValue = $null
if ($PatchBrain) { $patchBrainValue = $true }
if ($NoPatchBrain) { $patchBrainValue = $false }
if ($null -eq $patchBrainValue) {
    Write-Host ""
    Write-Host "Patch brain?"
    Write-Host "  Choose y only if the target demo does not already publish /brain/status_json."
    Write-Host "  For older demos, full brain overwrite may introduce version-only dependencies."
    if (-not $hasBallPredictor) {
        Write-Host "  Current selection: original demo / no k1_ball_predictor, so full brain patch will be skipped."
    }
    $patchBrainValue = Ask-YesNo "Patch brain now" $false
}

if ($patchBrainValue -and -not $hasBallPredictor) {
    Write-Host ""
    Write-Host "skip brain patch: target is marked as original demo, so k1_ball_predictor is assumed missing."
    Write-Host "The bundled brain patch depends on k1_ball_predictor and would fail to compile on that demo."
    $patchBrainValue = $false
}

$bundleRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$overlayRoot = Join-Path $bundleRoot "overlay"
$targetRoot = (Resolve-Path $Target).Path
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"

function Backup-Or-Remove {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }

    if ($NoBackup) {
        Remove-Item -Recurse -Force $Path
        return
    }

    $backup = "$Path.backup.$stamp"
    Move-Item -Force $Path $backup
    Write-Host "backup: $Path -> $backup"
    if (Test-Path $backup -PathType Container) {
        $ignorePath = Join-Path $backup "COLCON_IGNORE"
        New-Item -ItemType File -Force -Path $ignorePath | Out-Null
        Write-Host "colcon ignore backup: $ignorePath"
    }
}

function Ignore-Existing-Colcon-Backups {
    $srcPath = Join-Path $targetRoot "src"
    if (-not (Test-Path $srcPath)) { return }

    Get-ChildItem -Path $srcPath -Directory -Filter "*.backup.*" | ForEach-Object {
        $ignorePath = Join-Path $_.FullName "COLCON_IGNORE"
        if (-not (Test-Path $ignorePath)) {
            New-Item -ItemType File -Force -Path $ignorePath | Out-Null
            Write-Host "colcon ignore existing backup: $ignorePath"
        }
    }
}

function Copy-Directory-Fresh {
    param(
        [string]$Source,
        [string]$Destination
    )
    Backup-Or-Remove $Destination
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -Recurse -Force $Source $Destination
    Write-Host "copy: $Source -> $Destination"
}

Write-Host ""
Write-Host "target: $targetRoot"
Write-Host "server_base_url: $(if ($ServerUrl -ne '') { $ServerUrl } else { 'not set' })"
Write-Host "k1_ball_predictor: $(if ($hasBallPredictor) { 'present' } else { 'missing/original-demo' })"
Write-Host "patch brain: $(if ($patchBrainValue) { 'yes' } else { 'no' })"
Write-Host "mode: robot patch only; Windows WebUI frontend/backend are not copied."

Ignore-Existing-Colcon-Backups
New-Item -ItemType Directory -Force -Path (Join-Path $targetRoot "src") | Out-Null
Copy-Directory-Fresh `
    (Join-Path $overlayRoot "src\k1_robot_webui_client") `
    (Join-Path $targetRoot "src\k1_robot_webui_client")

if ($ServerUrl -ne "") {
    $configFile = Join-Path $targetRoot "src\k1_robot_webui_client\config\webui_client.yaml"
    $lines = Get-Content $configFile
    $changed = $false
    $newLines = foreach ($line in $lines) {
        if ($line.TrimStart().StartsWith("server_base_url:")) {
            $indentLength = $line.Length - $line.TrimStart().Length
            $indent = $line.Substring(0, $indentLength)
            $changed = $true
            "${indent}server_base_url: `"$ServerUrl`""
        } else {
            $line
        }
    }
    if (-not $changed) {
        $newLines += "    server_base_url: `"$ServerUrl`""
    }
    Set-Content -Path $configFile -Value $newLines
    Write-Host "set server_base_url: $ServerUrl"
}

if ($patchBrainValue) {
    $brainCpp = Join-Path $targetRoot "src\brain\src\brain.cpp"
    $brainH = Join-Path $targetRoot "src\brain\include\brain.h"

    if (-not (Test-Path (Split-Path -Parent $brainCpp))) {
        throw "Target does not look like a K1 demo: missing src\brain\src"
    }
    if (-not (Test-Path (Split-Path -Parent $brainH))) {
        throw "Target does not look like a K1 demo: missing src\brain\include"
    }

    if (-not (Ask-YesNo "Confirm overwriting target brain.cpp and brain.h from this bundle" $false)) {
        Write-Host "skip brain patch."
    } else {
        Backup-Or-Remove $brainCpp
        Copy-Item -Force (Join-Path $overlayRoot "src\brain\src\brain.cpp") $brainCpp
        Write-Host "copy patched brain.cpp"

        Backup-Or-Remove $brainH
        Copy-Item -Force (Join-Path $overlayRoot "src\brain\include\brain.h") $brainH
        Write-Host "copy patched brain.h"
    }
} else {
    Write-Host "skip brain patch. Use -PatchBrain only if the target demo lacks /brain/status_json."
}

Write-Host ""
Write-Host "done. Next steps:"
Write-Host "  1) Check $targetRoot\src\k1_robot_webui_client\config\webui_client.yaml"
Write-Host "  2) Rebuild/source the ROS workspace on Linux."
Write-Host "  3) Run/launch k1_robot_webui_client on Linux, while WebUI backend/frontend keep running on Windows."
Write-Host ""
Write-Host "If brain was overwritten and build fails with missing k1_ball_predictor, restore the backup brain.cpp/brain.h and rerun this script without brain patch."
