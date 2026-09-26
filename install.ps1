<#
.SYNOPSIS
    Register openxr-pose-layer as an explicit OpenXR API layer for the current
    user, and enable it for this and future sessions.

.DESCRIPTION
    Writes the HKCU explicit-layer value the OpenXR loader reads, pointing at the
    manifest JSON next to this script, and adds the layer to the user's
    XR_ENABLE_API_LAYERS. Explicit layers are off unless named there, so nothing
    changes for other OpenXR apps until you set the mode.

    All writes are under HKEY_CURRENT_USER. No administrator rights are needed and
    nothing machine-wide is touched. Run uninstall.ps1 to undo it.

.EXAMPLE
    .\install.ps1
    Register and enable the layer.

.EXAMPLE
    $env:XR_POSE_LAYER_MODE = "record"; $env:XR_POSE_LAYER_FILE = "run.oxrr"
    Then start your OpenXR app; it records to run.oxrr.
#>
[CmdletBinding()]
param(
    [string] $ManifestPath
)

$ErrorActionPreference = "Stop"

$layerName = "XR_APILAYER_GRC_pose_layer"
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot "$layerName.json"
}
$ManifestPath = (Resolve-Path $ManifestPath).Path

if (-not (Test-Path $ManifestPath)) {
    throw "Manifest not found: $ManifestPath"
}

# The loader reads this key; the value name is the manifest path, the DWORD data
# is a disable flag (0 = enabled).
$apiLayersKey = "HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit"
New-Item -Path $apiLayersKey -Force | Out-Null
New-ItemProperty -Path $apiLayersKey -Name $ManifestPath -Value 0 -PropertyType DWord -Force | Out-Null
Write-Host "Registered $layerName -> $ManifestPath"

# Explicit layers must be named in XR_ENABLE_API_LAYERS to take effect.
$existing = [Environment]::GetEnvironmentVariable("XR_ENABLE_API_LAYERS", "User")
$layers = @()
if ($existing) {
    $layers = $existing.Split(",") | Where-Object { $_ -ne "" }
}
if ($layers -notcontains $layerName) {
    $layers += $layerName
    $joined = ($layers -join ",")
    [Environment]::SetEnvironmentVariable("XR_ENABLE_API_LAYERS", $joined, "User")
    $env:XR_ENABLE_API_LAYERS = $joined
    Write-Host "Enabled via XR_ENABLE_API_LAYERS = $joined"
} else {
    Write-Host "Already enabled in XR_ENABLE_API_LAYERS"
}

Write-Host ""
Write-Host "Set the mode before launching your OpenXR app, for example:"
Write-Host '  $env:XR_POSE_LAYER_MODE = "record"'
Write-Host '  $env:XR_POSE_LAYER_FILE = "run.oxrr"'
Write-Host "Modes: off (default), log, record, replay. See the README."
