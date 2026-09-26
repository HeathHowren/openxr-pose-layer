<#
.SYNOPSIS
    Remove openxr-pose-layer's registration for the current user.

.DESCRIPTION
    Deletes the HKCU explicit-layer value that points at this layer's manifest and
    removes the layer from the user's XR_ENABLE_API_LAYERS. Undoes install.ps1.
    All changes are under HKEY_CURRENT_USER; no administrator rights are needed.
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
    $layers = $existing.Split(",") | Where-Object { $_ -ne "" -and $_ -ne $layerName }
    $joined = ($layers -join ",")
    [Environment]::SetEnvironmentVariable("XR_ENABLE_API_LAYERS", $joined, "User")
    $env:XR_ENABLE_API_LAYERS = $joined
    Write-Host "Updated XR_ENABLE_API_LAYERS = '$joined'"
}

Write-Host "Uninstalled $layerName for the current user."
