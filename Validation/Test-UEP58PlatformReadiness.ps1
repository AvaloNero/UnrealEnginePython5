[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [ValidateSet("Linux", "LinuxArm64", "Mac", "Win64")]
    [string]$Platform = "Linux",
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot "..")
$engineRootPath = Get-FullPath $EngineRoot
$engineVersionPath = Join-Path $engineRootPath "Engine\Build\Build.version"
$dotnetRoot = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\DotNet"
$dotnetPath = Get-ChildItem -LiteralPath $dotnetRoot -Directory -ErrorAction Stop |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName "win-x64\dotnet.exe" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
$ubtPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"

foreach ($requiredPath in @($engineVersionPath, $dotnetPath, $ubtPath)) {
    if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file was not found: $requiredPath"
    }
}

$engineVersion = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
if ($engineVersion.MajorVersion -ne 5 -or $engineVersion.MinorVersion -ne 8) {
    throw "Expected Unreal Engine 5.8, found $($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
}

$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$resultRoot = Join-Path $repoRoot ".build\Platform\Results\$runId"
$logPath = Join-Path $resultRoot "ubt-$($Platform.ToLowerInvariant()).log"
$summaryPath = Join-Path $resultRoot "summary.json"
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $dotnetPath
$startInfo.WorkingDirectory = $repoRoot
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
foreach ($argument in @(
    $ubtPath,
    "-Mode=ValidatePlatforms",
    "-Platforms=$Platform",
    "-OutputSDKs",
    "-Verbose",
    "-NoMutex",
    "-Log=$logPath"
)) {
    $startInfo.ArgumentList.Add($argument)
}

$process = [System.Diagnostics.Process]::Start($startInfo)
if (!$process) {
    throw "UnrealBuildTool platform validation failed to start"
}

try {
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $output = ($stdout.GetAwaiter().GetResult(), $stderr.GetAwaiter().GetResult()) -join [Environment]::NewLine
    $exitCode = $process.ExitCode
}
finally {
    $process.Dispose()
}

$validationMatch = [regex]::Match(
    $output,
    "##PlatformValidate:\s+$([regex]::Escape($Platform))\s+(VALID|INVALID)\s+(\S+)",
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (!$validationMatch.Success) {
    throw "UnrealBuildTool did not emit a platform validation marker for $Platform. Log: $logPath"
}

$isValid = $validationMatch.Groups[1].Value -ieq "VALID"
$sdkVersion = $validationMatch.Groups[2].Value
$requiredSdk = if (!$isValid -and $sdkVersion -ne "<UNKNOWN>") { $sdkVersion } else { $null }

$summary = [ordered]@{
    schema_version = 1
    status = if ($isValid) { "ready" } else { "not_ready" }
    run_id = $runId
    host_platform = "Win64"
    target_platform = $Platform
    engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
	reported_sdk = $sdkVersion
    required_sdk = $requiredSdk
    ubt_exit_code = $exitCode
    strict = [bool]$Strict
    log_path = $logPath
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host "UE $Platform readiness: $($summary.status)"
Write-Host "Summary: $summaryPath"

if ($exitCode -ne 0) {
    exit $exitCode
}
if ($Strict -and !$isValid) {
    exit 1
}
