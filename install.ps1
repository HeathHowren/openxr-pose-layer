<#
.SYNOPSIS
    Register openxr-pose-layer as an explicit OpenXR API layer for the current
    user, and enable it for this and future sessions.

.DESCRIPTION
    Writes the HKCU explicit-layer value the OpenXR loader reads, pointing at the
    manifest JSON next to this script, and adds the layer to the user's
    XR_ENABLE_API_LAYERS. Explicit layers are off unless named there, so nothing
    changes for other OpenXR apps until you set the mode.

    XR_ENABLE_API_LAYERS is a list of layer names separated by semicolons, for
    example "OtherLayer;XR_APILAYER_GRC_pose_layer". Version 1.0.0 of this script
    used commas by mistake. Running this script again rewrites such a value with
    semicolons.

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

# XR_ENABLE_API_LAYERS helpers. The OpenXR loader on Windows splits the list on
# ';' only. Version 1.0.0 joined it with ',', so read both and write ';'. Layer
# names never contain either character. Keep Split-LayerList and Join-LayerList
# the same in uninstall.ps1; tests/test_install_scripts.ps1 checks both.
function Split-LayerList([string] $Value) {
    $Value -split '[;,]' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
}

function Join-LayerList([string[]] $Layers) {
    @($Layers | Where-Object { $_ }) -join ";"
}

function Add-LayerToList([string] $Value, [string] $Name) {
    $layers = @(Split-LayerList $Value)
    if ($layers -notcontains $Name) {
        $layers += $Name
    }
    Join-LayerList $layers
}

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
$updated = Add-LayerToList $existing $layerName
if ($updated -cne $existing) {
    [Environment]::SetEnvironmentVariable("XR_ENABLE_API_LAYERS", $updated, "User")
    $env:XR_ENABLE_API_LAYERS = $updated
    Write-Host "Enabled via XR_ENABLE_API_LAYERS = $updated"
} else {
    Write-Host "Already enabled in XR_ENABLE_API_LAYERS"
}

Write-Host ""
Write-Host "Set the mode before launching your OpenXR app, for example:"
Write-Host '  $env:XR_POSE_LAYER_MODE = "record"'
Write-Host '  $env:XR_POSE_LAYER_FILE = "run.oxrr"'
Write-Host "Modes: off (default), log, record, replay. See the README."
