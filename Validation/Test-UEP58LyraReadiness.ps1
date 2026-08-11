[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [string]$LyraProject,
    [string]$OutputRoot,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-PathIsUnder {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Parent
    )
    $pathValue = (Get-FullPath $Path).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $parentValue = (Get-FullPath $Parent).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    return $pathValue.StartsWith($parentValue, [System.StringComparison]::OrdinalIgnoreCase)
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot "..")
$engineRootPath = Get-FullPath $EngineRoot
if (!$LyraProject) {
    $LyraProject = Join-Path $engineRootPath "Samples\Games\Lyra\Lyra.uproject"
}
$lyraProjectPath = Get-FullPath $LyraProject
$lyraRoot = Split-Path -Parent $lyraProjectPath

if (!$OutputRoot) {
    $OutputRoot = Join-Path $repoRoot ".build\LyraValidation\Readiness"
}
$outputRootPath = Get-FullPath $OutputRoot
if ((Test-PathIsUnder -Path $outputRootPath -Parent $engineRootPath) -or
    (Test-PathIsUnder -Path $outputRootPath -Parent $lyraRoot)) {
    throw "OutputRoot must be outside the Unreal Engine and Lyra reference trees: $outputRootPath"
}
$runRoot = Join-Path $outputRootPath (Get-Date -Format "yyyyMMdd-HHmmss")
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$checks = [System.Collections.Generic.List[object]]::new()
function Add-Check {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)][ValidateSet("source", "content")][string]$Gate,
        [Parameter(Mandatory = $true)][string]$Evidence
    )
    $script:checks.Add([ordered]@{
        name = $Name
        passed = $Passed
        gate = $Gate
        evidence = $Evidence
    })
}

$engineVersionPath = Join-Path $engineRootPath "Engine\Build\Build.version"
$engineVersion = $null
if (Test-Path -LiteralPath $engineVersionPath -PathType Leaf) {
    $versionObject = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
    $engineVersion = "$($versionObject.MajorVersion).$($versionObject.MinorVersion).$($versionObject.PatchVersion)"
}
Add-Check -Name "engine_5_8" -Passed ($engineVersion -like "5.8.*") -Gate source -Evidence ([string]$engineVersion)

$pythonPath = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
$pythonVersion = $null
if (Test-Path -LiteralPath $pythonPath -PathType Leaf) {
    $pythonVersion = (& $pythonPath -c "import platform; print(platform.python_version())").Trim()
}
Add-Check -Name "engine_cpython_3_11" -Passed ($pythonVersion -like "3.11.*") -Gate source -Evidence ([string]$pythonVersion)

Add-Check -Name "lyra_project_descriptor" -Passed (Test-Path -LiteralPath $lyraProjectPath -PathType Leaf) -Gate source -Evidence $lyraProjectPath
foreach ($relativePath in @(
    "Source\LyraGame\LyraGame.Build.cs",
    "Source\LyraGame.Target.cs",
    "Source\LyraEditor.Target.cs",
    "Config\DefaultEngine.ini",
    "Config\DefaultGame.ini"
)) {
    $path = Join-Path $lyraRoot $relativePath
    Add-Check -Name ("source_" + ($relativePath -replace "[^A-Za-z0-9]", "_")) -Passed (Test-Path -LiteralPath $path -PathType Leaf) -Gate source -Evidence $path
}

$featureRoot = Join-Path $lyraRoot "Plugins\GameFeatures"
$featureReports = [System.Collections.Generic.List[object]]::new()
if (Test-Path -LiteralPath $featureRoot -PathType Container) {
    foreach ($descriptorPath in Get-ChildItem -LiteralPath $featureRoot -Recurse -Filter *.uplugin -File | Sort-Object FullName) {
        $descriptor = Get-Content -LiteralPath $descriptorPath.FullName -Raw | ConvertFrom-Json
        $featureName = [System.IO.Path]::GetFileNameWithoutExtension($descriptorPath.Name)
        $contentRoot = Join-Path $descriptorPath.Directory.FullName "Content"
        $gameFeatureDataPath = Join-Path $contentRoot "$featureName.uasset"
        $assetCount = 0
        $mapCount = 0
        if (Test-Path -LiteralPath $contentRoot -PathType Container) {
            $assetCount = @([System.IO.Directory]::EnumerateFiles($contentRoot, "*.uasset", [System.IO.SearchOption]::AllDirectories)).Count
            $mapCount = @([System.IO.Directory]::EnumerateFiles($contentRoot, "*.umap", [System.IO.SearchOption]::AllDirectories)).Count
        }
        $descriptorValid = $descriptor.CanContainContent -eq $true -and
            $descriptor.ExplicitlyLoaded -eq $true -and
            $descriptor.BuiltInInitialFeatureState -eq "Registered"
        Add-Check -Name "feature_descriptor_$featureName" -Passed $descriptorValid -Gate source -Evidence $descriptorPath.FullName
        Add-Check -Name "feature_data_$featureName" -Passed (Test-Path -LiteralPath $gameFeatureDataPath -PathType Leaf) -Gate content -Evidence $gameFeatureDataPath
        $featureReports.Add([ordered]@{
            name = $featureName
            descriptor = $descriptorPath.FullName
            explicitly_loaded = $descriptor.ExplicitlyLoaded
            initial_state = $descriptor.BuiltInInitialFeatureState
            content_assets = $assetCount
            content_maps = $mapCount
            game_feature_data = $gameFeatureDataPath
            game_feature_data_present = Test-Path -LiteralPath $gameFeatureDataPath -PathType Leaf
        })
    }
}
Add-Check -Name "game_feature_descriptors" -Passed ($featureReports.Count -ge 5) -Gate source -Evidence "$($featureReports.Count) descriptors"

$projectAssets = 0
$projectMaps = 0
$projectContentRoot = Join-Path $lyraRoot "Content"
if (Test-Path -LiteralPath $projectContentRoot -PathType Container) {
    $projectAssets = @([System.IO.Directory]::EnumerateFiles($projectContentRoot, "*.uasset", [System.IO.SearchOption]::AllDirectories)).Count
    $projectMaps = @([System.IO.Directory]::EnumerateFiles($projectContentRoot, "*.umap", [System.IO.SearchOption]::AllDirectories)).Count
}

$criticalContent = @(
    "Content\DefaultGameData.uasset",
    "Content\System\FrontEnd\Maps\L_LyraFrontEnd.umap",
    "Content\System\DefaultEditorMap\L_DefaultEditorOverview.umap",
    "Plugins\GameFeatures\ShooterMaps\Content\Maps\L_Expanse.umap"
)
foreach ($relativePath in $criticalContent) {
    $path = Join-Path $lyraRoot $relativePath
    Add-Check -Name ("critical_" + ($relativePath -replace "[^A-Za-z0-9]", "_")) -Passed (Test-Path -LiteralPath $path -PathType Leaf) -Gate content -Evidence $path
}
Add-Check -Name "project_content_assets" -Passed ($projectAssets -gt 0) -Gate content -Evidence "$projectAssets .uasset files"
Add-Check -Name "project_content_maps" -Passed ($projectMaps -ge 2) -Gate content -Evidence "$projectMaps .umap files"

$sourceFailures = @($checks | Where-Object { $_.gate -eq "source" -and !$_.passed })
$contentFailures = @($checks | Where-Object { $_.gate -eq "content" -and !$_.passed })
$sourceReady = $sourceFailures.Count -eq 0
$contentReady = $contentFailures.Count -eq 0
$status = if ($sourceReady -and $contentReady) { "ready" } else { "blocked" }

$summary = [ordered]@{
    schema_version = 1
    status = $status
    engine = $engineVersion
    python = $pythonVersion
    lyra_project = $lyraProjectPath
    source_ready = $sourceReady
    content_ready = $contentReady
    project_content = [ordered]@{
        assets = $projectAssets
        maps = $projectMaps
    }
    game_features = $featureReports
    checks = $checks
    blocking_reasons = @($checks | Where-Object { !$_.passed } | ForEach-Object { "$($_.name): $($_.evidence)" })
}
$summaryPath = Join-Path $runRoot "summary.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host "Lyra readiness: $status"
Write-Host "Source ready: $sourceReady"
Write-Host "Content ready: $contentReady"
Write-Host "Summary: $summaryPath"

if ($Strict -and $status -ne "ready") {
    exit 1
}
