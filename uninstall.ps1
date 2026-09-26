<#
.SYNOPSIS
    Remove openxr-pose-layer's registration for the current user.

.DESCRIPTION
    Deletes the HKCU explicit-layer value that points at this layer's manifest and
    removes the layer from the user's XR_ENABLE_API_LAYERS. Undoes install.ps1.
    All changes are under HKEY_CURRENT_USER; no administrator rights are needed.

    XR_ENABLE_API_LAYERS is a list of layer names separated by semicolons. The
    names left in it are written back with semicolons, even if version 1.0.0 of
    install.ps1 joined them with commas.
#>
[CmdletBinding()]
param(
    [string] $ManifestPath
)

$ErrorActionPreference = "Stop"

# XR_ENABLE_API_LAYERS helpers. The OpenXR loader on Windows splits the list on
# ';' only. Version 1.0.0 joined it with ',', so read both and write ';'. Layer
# names never contain either character. Keep Split-LayerList and Join-LayerList
# the same in install.ps1; tests/test_install_scripts.ps1 checks both.
function Split-LayerList([string] $Value) {
    $Value -split '[;,]' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
}

function Join-LayerList([string[]] $Layers) {
    @($Layers | Where-Object { $_ }) -join ";"
}

function Remove-LayerFromList([string] $Value, [string] $Name) {
    Join-LayerList @(Split-LayerList $Value | Where-Object { $_ -ne $Name })
}

$layerName = "XR_APILAYER_GRC_pose_layer"
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot "$layerName.json"
}
if (Test-Path $ManifestPath) {
    $ManifestPath = (Resolve-Path $ManifestPath).Path
}

$apiLayersKey = "HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit"
if (Test-Path $apiLayersKey) {
    $props = Get-ItemProperty -Path $apiLayersKey
    foreach ($name in ($props.PSObject.Properties.Name)) {
        if ($name -like "*$layerName*.json") {
            Remove-ItemProperty -Path $apiLayersKey -Name $name -ErrorAction SilentlyContinue
            Write-Host "Removed registry value $name"
        }
    }
}

$existing = [Environment]::GetEnvironmentVariable("XR_ENABLE_API_LAYERS", "User")
if ($existing) {
    $updated = Remove-LayerFromList $existing $layerName
    if ($updated -cne $existing) {
        [Environment]::SetEnvironmentVariable("XR_ENABLE_API_LAYERS", $updated, "User")
        $env:XR_ENABLE_API_LAYERS = $updated
        Write-Host "Updated XR_ENABLE_API_LAYERS = '$updated'"
    }
}

Write-Host "Uninstalled $layerName for the current user."
