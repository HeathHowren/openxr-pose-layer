<#
Tests the XR_ENABLE_API_LAYERS list helpers in install.ps1 and uninstall.ps1.

The scripts are parsed, not run. Only their helper functions are loaded, so this
test never touches the registry or the user's environment. ctest runs it with
Windows PowerShell 5.1. It exits 1 if any check fails.
#>
param(
    # The folder that holds install.ps1 and uninstall.ps1.
    [string] $SourceDir = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

$layer = "XR_APILAYER_GRC_pose_layer"
$script:checks = 0
$script:failures = 0

function Pass([string] $What) {
    $script:checks++
    Write-Host "ok   $What"
}

function Fail([string] $What) {
    $script:checks++
    $script:failures++
    Write-Host "FAIL $What"
}

# Every helper returns one string. Compare exactly, case included, because the
# loader compares layer names exactly.
function Check([string] $What, $Actual, [string] $Expected) {
    if ($Actual -is [string] -and $Actual -ceq $Expected) {
        Pass $What
    } else {
        Fail "$What (expected '$Expected', got '$Actual')"
    }
}

# Parse a script without running it and return its top-level functions by name.
function Get-ScriptFunctions([string] $Name) {
    $path = Join-Path $SourceDir $Name
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref] $tokens, [ref] $errors)
    if ($errors.Count -gt 0) {
        throw "$Name does not parse. $($errors[0].Message)"
    }
    $functions = @{}
    $isFunction = { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }
    foreach ($fn in $ast.FindAll($isFunction, $false)) {
        $functions[$fn.Name] = $fn
    }
    return @{ Ast = $ast; Functions = $functions }
}

# The source of the named helpers, ready to dot-source into a test scope.
function Get-HelperSource($Parsed, [string] $Script, [string[]] $Names) {
    $source = @()
    foreach ($name in $Names) {
        if (-not $Parsed.Functions.ContainsKey($name)) {
            throw "$Script does not define $name"
        }
        $source += $Parsed.Functions[$name].Extent.Text
    }
    return ($source -join "`n")
}

# True if the script's own code, outside any function, calls the command.
function Test-CallsCommand($Parsed, [string] $Command) {
    $isCall = {
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq $Command
    }.GetNewClosure()
    foreach ($statement in $Parsed.Ast.EndBlock.Statements) {
        if ($statement -is [System.Management.Automation.Language.FunctionDefinitionAst]) {
            continue
        }
        if ($statement.Find($isCall, $true)) {
            return $true
        }
    }
    return $false
}

$install = Get-ScriptFunctions "install.ps1"
$uninstall = Get-ScriptFunctions "uninstall.ps1"
$shared = @("Split-LayerList", "Join-LayerList")

# Both scripts carry their own copy of the shared helpers. Keep them the same.
foreach ($name in $shared) {
    $a = Get-HelperSource $install "install.ps1" @($name)
    $b = Get-HelperSource $uninstall "uninstall.ps1" @($name)
    if ($a -ceq $b) {
        Pass "$name is the same in both scripts"
    } else {
        Fail "$name differs between install.ps1 and uninstall.ps1"
    }
}

if (Test-CallsCommand $install "Add-LayerToList") {
    Pass "install.ps1 uses Add-LayerToList"
} else {
    Fail "install.ps1 does not call Add-LayerToList"
}
if (Test-CallsCommand $uninstall "Remove-LayerFromList") {
    Pass "uninstall.ps1 uses Remove-LayerFromList"
} else {
    Fail "uninstall.ps1 does not call Remove-LayerFromList"
}

# install.ps1. Each block runs in its own scope, so the two scripts' copies of
# the helpers never mix.
& {
    . ([scriptblock]::Create((Get-HelperSource $install "install.ps1" ($shared + "Add-LayerToList"))))

    Check "install: split on ';' and ','" (@(Split-LayerList "A;B,C") -join "|") "A|B|C"
    Check "install: join with ';'" (Join-LayerList @("A", "B")) "A;B"
    Check "install: join nothing" (Join-LayerList @()) ""

    Check "install: add to an empty value" (Add-LayerToList "" $layer) $layer
    Check "install: add to an unset value" (Add-LayerToList $null $layer) $layer
    Check "install: add after another layer" (Add-LayerToList "Other" $layer) "Other;$layer"
    Check "install: already present" (Add-LayerToList $layer $layer) $layer
    Check "install: already present after another layer" (Add-LayerToList "Other;$layer" $layer) "Other;$layer"
    Check "install: repair a 1.0.0 comma value" (Add-LayerToList "Other,$layer" $layer) "Other;$layer"
    Check "install: repair a comma value without the layer" (Add-LayerToList "A,B" $layer) "A;B;$layer"
    Check "install: trim spaces and drop empty entries" (Add-LayerToList " Other ; ;" $layer) "Other;$layer"
}

# uninstall.ps1
& {
    . ([scriptblock]::Create((Get-HelperSource $uninstall "uninstall.ps1" ($shared + "Remove-LayerFromList"))))

    Check "uninstall: split on ';' and ','" (@(Split-LayerList "A;B,C") -join "|") "A|B|C"
    Check "uninstall: join with ';'" (Join-LayerList @("A", "B")) "A;B"
    Check "uninstall: join nothing" (Join-LayerList @()) ""

    Check "uninstall: remove from the middle" (Remove-LayerFromList "A;$layer;B" $layer) "A;B"
    Check "uninstall: remove the only layer" (Remove-LayerFromList $layer $layer) ""
    Check "uninstall: layer not present" (Remove-LayerFromList "Other" $layer) "Other"
    Check "uninstall: clean a 1.0.0 comma value" (Remove-LayerFromList "Other,$layer" $layer) "Other"
    Check "uninstall: clean a comma value with the layer in the middle" (Remove-LayerFromList "A,$layer,B" $layer) "A;B"
    Check "uninstall: trim spaces and drop empty entries" (Remove-LayerFromList "A; $layer ;;B" $layer) "A;B"
}

Write-Host ""
Write-Host "$($script:checks - $script:failures) of $($script:checks) checks passed"
if ($script:failures -gt 0) {
    exit 1
}
exit 0
