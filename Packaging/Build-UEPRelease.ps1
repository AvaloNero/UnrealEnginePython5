[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [string]$OutputRoot,
    [string]$Commit = "HEAD",
    [switch]$IncludeBinary,
    [string[]]$TargetPlatforms = @("Win64"),
    [switch]$AllowDirty
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$Label,
        [int]$TimeoutSeconds = 0,
        [hashtable]$EnvironmentVariables = @{}
    )

    Write-Host "[$Label] $FilePath $($ArgumentList -join ' ')"
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in $ArgumentList) {
        $startInfo.ArgumentList.Add($argument)
    }
    foreach ($entry in $EnvironmentVariables.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = [string]$entry.Value
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (!$process) {
        throw "$Label failed to start"
    }
    try {
        if ($TimeoutSeconds -gt 0) {
            if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
                $process.Kill($true)
                $process.WaitForExit()
                throw "$Label timed out after $TimeoutSeconds seconds"
            }
        }
        else {
            $process.WaitForExit()
        }
        if ($process.ExitCode -ne 0) {
            throw "$Label exited with code $($process.ExitCode)"
        }
    }
    finally {
        $process.Dispose()
    }
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot "..")
$commitHash = (& git -C $repoRoot rev-parse "$Commit^{commit}").Trim()
if ($LASTEXITCODE -ne 0 -or $commitHash -notmatch "^[0-9a-f]{40}$") {
    throw "Could not resolve Git commit '$Commit'"
}
$shortCommit = $commitHash.Substring(0, 12)

$descriptorSpec = "${commitHash}:UnrealEnginePython.uplugin"
$pluginDescriptorJson = @(& git -C $repoRoot show $descriptorSpec) -join [Environment]::NewLine
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($pluginDescriptorJson)) {
    throw "Could not read UnrealEnginePython.uplugin from commit $commitHash"
}
$pluginDescriptor = $pluginDescriptorJson | ConvertFrom-Json
$version = [string]$pluginDescriptor.VersionName
if ($version -notmatch "^\d+\.\d+\.\d+$") {
    throw "Plugin VersionName at commit $commitHash must be semantic x.y.z, found '$version'"
}

if (!$AllowDirty) {
    $dirtyPaths = @(& git -C $repoRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw "git status failed"
    }
    if ($dirtyPaths.Count -gt 0) {
        throw "Release packaging requires a clean worktree. Use -AllowDirty only for script development."
    }
}

if (!$OutputRoot) {
    $OutputRoot = Join-Path $repoRoot ".build\Releases"
}
$outputRootPath = Get-FullPath $OutputRoot
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$releaseRoot = Join-Path $outputRootPath "$version\$shortCommit-$runId"
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null

$archiveName = "UnrealEnginePython-$version-source.zip"
$archivePath = Join-Path $releaseRoot $archiveName
$archivePrefix = "UnrealEnginePython-$version/"
$sourcePaths = @(
    ".github/workflows/ue58-linux-validation.yml",
    "AUTHORS",
    "CHANGELOG.md",
    "CONTRIBUTORS",
    "Config",
    "Demos",
    "LICENSE",
    "Packaging",
    "README.md",
    "ROADMAP.md",
    "Resources",
    "Source",
    "UnrealEnginePython.uplugin",
    "Validation",
    "docs",
    "examples",
    "run_tests.py"
)

$archiveArguments = @(
    "-C", $repoRoot,
    "archive",
    "--format=zip",
    "--prefix=$archivePrefix",
    "-o", $archivePath,
    $commitHash,
    "--"
) + $sourcePaths
Invoke-CheckedProcess -FilePath "git" -ArgumentList $archiveArguments -WorkingDirectory $repoRoot -Label "Source archive" -TimeoutSeconds 300

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $forbiddenEntries = @($zip.Entries | Where-Object {
        $_.FullName -match "(?:^|/)(?:\.nepy|\.build|\.git|\.codegraph|Binaries|Intermediate|__pycache__)(?:/|$)" -or
        $_.FullName -match "\.py[co]$"
    })
    if ($forbiddenEntries.Count -gt 0) {
        throw "Source archive contains forbidden entries: $(($forbiddenEntries.FullName) -join ', ')"
    }
    $sourceEntryCount = $zip.Entries.Count
}
finally {
    $zip.Dispose()
}

$artifacts = [System.Collections.Generic.List[object]]::new()
$sourceHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$artifacts.Add([ordered]@{
    kind = "source"
    file = $archiveName
    sha256 = $sourceHash
    bytes = (Get-Item -LiteralPath $archivePath).Length
    entries = $sourceEntryCount
})

$engineVersion = $null
if ($IncludeBinary) {
    $engineRootPath = Get-FullPath $EngineRoot
    $engineVersionPath = Join-Path $engineRootPath "Engine\Build\Build.version"
    $dotnetRoot = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\DotNet"
    $dotnetPath = Get-ChildItem -LiteralPath $dotnetRoot -Directory -ErrorAction Stop |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "win-x64\dotnet.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    $automationToolPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\AutomationTool\AutomationTool.dll"
    foreach ($requiredPath in @($engineVersionPath, $dotnetPath, $automationToolPath)) {
        if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Required binary-packaging file was not found: $requiredPath"
        }
    }

    $engineVersionObject = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
    if ($engineVersionObject.MajorVersion -ne 5 -or $engineVersionObject.MinorVersion -ne 8) {
        throw "Binary packaging requires Unreal Engine 5.8"
    }
    $engineVersion = "$($engineVersionObject.MajorVersion).$($engineVersionObject.MinorVersion).$($engineVersionObject.PatchVersion)"

    # BuildPlugin copies the directory containing the descriptor before its own
    # filtering runs. Never point it at the repository: extract the already
    # audited public source archive and compile only that isolated tree.
    $binarySourceRoot = Join-Path $releaseRoot "BinarySource"
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $binarySourceRoot)
    $stagedPluginRoot = Join-Path $binarySourceRoot "UnrealEnginePython-$version"
    $stagedPluginPath = Join-Path $stagedPluginRoot "UnrealEnginePython.uplugin"
    if (!(Test-Path -LiteralPath $stagedPluginPath -PathType Leaf)) {
        throw "Isolated binary source descriptor was not found: $stagedPluginPath"
    }

    $binaryPackageRoot = Join-Path $releaseRoot "BinaryPackage"
    $uatLogRoot = Join-Path $releaseRoot "BuildPluginLogs"
    $uatLog = Join-Path $uatLogRoot "Log.txt"
    New-Item -ItemType Directory -Path $uatLogRoot -Force | Out-Null
    $uatArguments = @(
        $automationToolPath,
        "BuildPlugin",
        "-Plugin=$stagedPluginPath",
        "-Package=$binaryPackageRoot",
        "-TargetPlatforms=$($TargetPlatforms -join '+')",
        "-WaitForUATMutex",
        "-NoP4",
        "-UTF8Output"
    )
    $uatEnvironment = @{
        uebp_LogFolder = $uatLogRoot
        uebp_FinalLogFolder = $uatLogRoot
    }
    Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $uatArguments -WorkingDirectory $engineRootPath -Label "Binary BuildPlugin" -TimeoutSeconds 3600 -EnvironmentVariables $uatEnvironment

    if (!(Test-Path -LiteralPath $uatLog -PathType Leaf)) {
        throw "BuildPlugin log was not created: $uatLog"
    }
    $uatContents = Get-Content -LiteralPath $uatLog -Raw
    foreach ($marker in @("BUILD SUCCESSFUL", "AutomationTool exiting with ExitCode=0")) {
        if (!$uatContents.Contains($marker)) {
            throw "BuildPlugin log does not contain '$marker': $uatLog"
        }
    }

    $binaryPackageEntries = @(Get-ChildItem -LiteralPath $binaryPackageRoot -Recurse -Force)
    $forbiddenBinaryPackageEntries = @($binaryPackageEntries | Where-Object {
        $relativePath = [System.IO.Path]::GetRelativePath($binaryPackageRoot, $_.FullName).Replace("\", "/")
        $relativePath -match "(?:^|/)(?:\.nepy|\.build|\.git|\.codegraph|HostProject|__pycache__)(?:/|$)" -or
        $relativePath -match "\.py[co]$"
    })
    if ($forbiddenBinaryPackageEntries.Count -gt 0) {
        $relativeForbiddenPaths = $forbiddenBinaryPackageEntries | ForEach-Object {
            [System.IO.Path]::GetRelativePath($binaryPackageRoot, $_.FullName)
        }
        throw "Binary package staging contains forbidden entries: $($relativeForbiddenPaths -join ', ')"
    }

    $binaryArchiveName = "UnrealEnginePython-$version-UE$engineVersion-$($TargetPlatforms -join '+').zip"
    $binaryArchivePath = Join-Path $releaseRoot $binaryArchiveName
    Compress-Archive -Path (Join-Path $binaryPackageRoot "*") -DestinationPath $binaryArchivePath -CompressionLevel Optimal
    $binaryZip = [System.IO.Compression.ZipFile]::OpenRead($binaryArchivePath)
    try {
        $forbiddenBinaryArchiveEntries = @($binaryZip.Entries | Where-Object {
            $_.FullName -match "(?:^|/)(?:\.nepy|\.build|\.git|\.codegraph|HostProject|__pycache__)(?:/|$)" -or
            $_.FullName -match "\.py[co]$"
        })
        if ($forbiddenBinaryArchiveEntries.Count -gt 0) {
            throw "Binary archive contains forbidden entries: $(($forbiddenBinaryArchiveEntries.FullName) -join ', ')"
        }
        $binaryEntryCount = $binaryZip.Entries.Count
    }
    finally {
        $binaryZip.Dispose()
    }
    $binaryHash = (Get-FileHash -LiteralPath $binaryArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $artifacts.Add([ordered]@{
        kind = "binary"
        file = $binaryArchiveName
        sha256 = $binaryHash
        bytes = (Get-Item -LiteralPath $binaryArchivePath).Length
        entries = $binaryEntryCount
        engine = $engineVersion
        target_platforms = $TargetPlatforms
        build_log = $uatLog
    })
}

$manifest = [ordered]@{
    schema_version = 1
    plugin_version = $version
    git_commit = $commitHash
    source_prefix = $archivePrefix
    engine = $engineVersion
    artifacts = $artifacts
}
$manifestPath = Join-Path $releaseRoot "manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$checksumLines = $artifacts | ForEach-Object { "$($_.sha256)  $($_.file)" }
$checksumPath = Join-Path $releaseRoot "SHA256SUMS.txt"
$checksumLines | Set-Content -LiteralPath $checksumPath -Encoding ascii

Write-Host "UEP release artifacts created."
Write-Host "Manifest: $manifestPath"
