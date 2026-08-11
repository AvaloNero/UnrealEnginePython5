[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [string]$LyraProject,
    [string]$OutputRoot,
    [ValidateRange(1024, 65535)]
    [int]$ServerPort = 7789,
    [switch]$Incremental
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

function Assert-PathIsUnder {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Parent
    )
    if (!(Test-PathIsUnder -Path $Path -Parent $Parent)) {
        throw "Refusing operation outside '$(Get-FullPath $Parent)': $(Get-FullPath $Path)"
    }
}

function Copy-FilteredTree {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [string[]]$ExcludedSegments = @()
    )
    $sourcePath = Get-FullPath $Source
    $destinationPath = Get-FullPath $Destination
    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
    $enumerationOptions = [System.IO.EnumerationOptions]::new()
    $enumerationOptions.RecurseSubdirectories = $true
    $enumerationOptions.IgnoreInaccessible = $false
    $enumerationOptions.AttributesToSkip = [System.IO.FileAttributes]::ReparsePoint
    foreach ($file in [System.IO.Directory]::EnumerateFiles($sourcePath, "*", $enumerationOptions)) {
        $relativePath = [System.IO.Path]::GetRelativePath($sourcePath, $file)
        $segments = @($relativePath -split "[\\/]")
        if (@($segments | Where-Object { $ExcludedSegments -contains $_ }).Count -gt 0) {
            continue
        }
        $targetPath = Join-Path $destinationPath $relativePath
        $targetDirectory = Split-Path -Parent $targetPath
        New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
        Copy-Item -LiteralPath $file -Destination $targetPath -Force
    }
}

function Invoke-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string]$Label,
        [int]$TimeoutSeconds = 0
    )
    Write-Host "[$Label] $FilePath $($ArgumentList -join ' ')"
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $ArgumentList) {
        $startInfo.ArgumentList.Add($argument)
    }

    $startedAt = Get-Date
    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (!$process) {
        throw "$Label failed to start"
    }
    try {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $timedOut = $false
        if ($TimeoutSeconds -gt 0 -and !$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            $timedOut = $true
        }
        if ($TimeoutSeconds -le 0) {
            $process.WaitForExit()
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        @($stdout, $stderr) -join [Environment]::NewLine | Set-Content -LiteralPath $LogPath -Encoding utf8
        if ($timedOut) {
            throw "$Label timed out after $TimeoutSeconds seconds; log: $LogPath"
        }
        $result = [ordered]@{
            exit_code = $process.ExitCode
            duration_seconds = [math]::Round(((Get-Date) - $startedAt).TotalSeconds, 3)
            log = $LogPath
        }
        if ($process.ExitCode -ne 0) {
            throw "$Label exited with code $($process.ExitCode); log: $LogPath"
        }
        return $result
    }
    finally {
        $process.Dispose()
    }
}

function Assert-BuildLog {
    param([Parameter(Mandatory = $true)][string]$LogPath)
    $contents = Get-Content -LiteralPath $LogPath -Raw
    if ($contents -notmatch "Result:\s+Succeeded") {
        throw "Build log does not contain a successful result: $LogPath"
    }
    $diagnostics = [regex]::Matches($contents, "(?im)\b(?:warning C\d+|error C\d+|fatal error C\d+|error LNK\d+)\b")
    $toolDiagnostics = [regex]::Matches($contents, "(?im)^\s*(?:warning|error):")
    if ($diagnostics.Count -gt 0 -or $toolDiagnostics.Count -gt 0) {
        throw "Build produced compiler/linker/UHT diagnostic(s): $LogPath"
    }
}

function Assert-LyraRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$UnrealLogPath,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][string[]]$ExpectedNetModes
    )
    if (!(Test-Path -LiteralPath $ResultPath -PathType Leaf)) {
        throw "Lyra runtime did not create $ResultPath"
    }
    $report = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
    if ($report.status -ne "passed" -or $report.mode -ne $ExpectedMode) {
        throw "Lyra $ExpectedMode runtime failed: $($report.error)"
    }
    if ($report.python_version -notlike "3.11.*") {
        throw "Lyra $ExpectedMode runtime used unexpected Python $($report.python_version)"
    }
    if ($ExpectedNetModes -notcontains $report.snapshot.net_mode) {
        throw "Lyra $ExpectedMode runtime used unexpected net mode '$($report.snapshot.net_mode)'"
    }

    $contents = Get-Content -LiteralPath $UnrealLogPath -Raw
    foreach ($marker in @(
        "UEP_LYRA_SCRIPT_LOADED",
        "UEP_LYRA_BRIDGE_ATTACHED",
        "UEP_LYRA_SMOKE_PASSED",
        "FPlatformMisc::RequestExit",
        "Object subsystem successfully closed",
        "Goodbye Python",
        "Log file closed"
    )) {
        if (!$contents.Contains($marker)) {
            throw "Lyra $ExpectedMode runtime log is missing '$marker': $UnrealLogPath"
        }
    }
    $fatalMatches = @([regex]::Matches($contents, "(?im)Fatal error:|Assertion failed:|Unhandled Exception:|LogWindows:\s+Error:"))
    $errorMatches = @([regex]::Matches($contents, "(?im)^.*Log[A-Za-z0-9_]+:\s+Error:.*$"))
    if ($fatalMatches.Count -gt 0 -or $errorMatches.Count -gt 0) {
        throw "Lyra $ExpectedMode runtime log contains fatal/assert/error diagnostics: $UnrealLogPath"
    }
    return [pscustomobject]@{
        Report = $report
        FatalDiagnostics = $fatalMatches.Count
        ErrorDiagnostics = $errorMatches.Count
    }
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot "..")
$engineRootPath = Get-FullPath $EngineRoot
if (!$LyraProject) {
    $LyraProject = Join-Path $engineRootPath "Samples\Games\Lyra\Lyra.uproject"
}
$lyraProjectPath = Get-FullPath $LyraProject
$lyraRoot = Split-Path -Parent $lyraProjectPath

if (!$OutputRoot) {
    $OutputRoot = Join-Path $repoRoot ".build\LyraValidation"
}
$outputRootPath = Get-FullPath $OutputRoot
if ((Test-PathIsUnder -Path $outputRootPath -Parent $engineRootPath) -or
    (Test-PathIsUnder -Path $engineRootPath -Parent $outputRootPath) -or
    (Test-PathIsUnder -Path $outputRootPath -Parent $lyraRoot) -or
    (Test-PathIsUnder -Path $lyraRoot -Parent $outputRootPath)) {
    throw "OutputRoot must not overlap the Unreal Engine or Lyra reference trees: $outputRootPath"
}
$stageRoot = Join-Path $outputRootPath "Stage\Lyra"
$markerPath = Join-Path $stageRoot ".uep-lyra-source-stage.json"
$runRoot = Join-Path $outputRootPath ("Results\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$summary = [ordered]@{
    schema_version = 1
    status = "failed"
    mode = "source_only"
    engine = $null
    python = $null
    lyra_project = $lyraProjectPath
    stage_project = $null
    source_content_gate = "not_claimed"
    build = $null
    runtime = $null
    server_exit = $null
    error = $null
}

try {
    $engineVersionPath = Join-Path $engineRootPath "Engine\Build\Build.version"
    $pythonPath = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
    $editorPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    $ubtPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
    $dotnetRoot = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\DotNet"
    $dotnetPath = Get-ChildItem -LiteralPath $dotnetRoot -Directory |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "win-x64\dotnet.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    foreach ($requiredPath in @($engineVersionPath, $pythonPath, $editorPath, $ubtPath, $dotnetPath, $lyraProjectPath)) {
        if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Required file was not found: $requiredPath"
        }
    }

    $engineVersionObject = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
    $summary.engine = "$($engineVersionObject.MajorVersion).$($engineVersionObject.MinorVersion).$($engineVersionObject.PatchVersion)"
    if ($engineVersionObject.MajorVersion -ne 5 -or $engineVersionObject.MinorVersion -ne 8) {
        throw "Lyra source validation requires Unreal Engine 5.8"
    }
    $summary.python = (& $pythonPath -c "import platform; print(platform.python_version())").Trim()
    if ($summary.python -notlike "3.11.*") {
        throw "Lyra source validation requires engine-bundled CPython 3.11"
    }

    Assert-PathIsUnder -Path $stageRoot -Parent $outputRootPath
    if (Test-Path -LiteralPath $stageRoot -PathType Container) {
        if (!(Test-Path -LiteralPath $markerPath -PathType Leaf)) {
            throw "Refusing to use unmarked staging directory: $stageRoot"
        }
        if (!$Incremental) {
            Remove-Item -LiteralPath $stageRoot -Recurse -Force
        }
    }

    $excludedSegments = @("Binaries", "Content", "DerivedDataCache", "Intermediate", "Saved", ".git", ".codegraph", ".vs")
    Copy-FilteredTree -Source $lyraRoot -Destination $stageRoot -ExcludedSegments $excludedSegments
    [ordered]@{
        schema_version = 1
        purpose = "UEP Lyra source-only disposable stage"
        source = $lyraProjectPath
    } | ConvertTo-Json | Set-Content -LiteralPath $markerPath -Encoding utf8

    $stagedUEP = Join-Path $stageRoot "Plugins\UnrealEnginePython"
    foreach ($directory in @("Config", "Resources", "Source")) {
        Copy-FilteredTree -Source (Join-Path $repoRoot $directory) -Destination (Join-Path $stagedUEP $directory) -ExcludedSegments @("Binaries", "Intermediate", "__pycache__", ".build", ".git", ".codegraph")
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot "UnrealEnginePython.uplugin") -Destination (Join-Path $stagedUEP "UnrealEnginePython.uplugin") -Force

    $bridgeSource = Join-Path $repoRoot "Demos\UEPLyraIntegration\Plugins\UEPLyraBridge"
    $bridgeDestination = Join-Path $stageRoot "Plugins\UEPLyraBridge"
    Copy-FilteredTree -Source $bridgeSource -Destination $bridgeDestination -ExcludedSegments @("Binaries", "Intermediate", "__pycache__")
    Copy-FilteredTree -Source (Join-Path $repoRoot "Demos\UEPLyraIntegration\Overlay\Content\Scripts") -Destination (Join-Path $stageRoot "Content\Scripts") -ExcludedSegments @("__pycache__")

    $stageProject = Join-Path $stageRoot "Lyra.uproject"
    $projectDescriptor = Get-Content -LiteralPath $stageProject -Raw | ConvertFrom-Json
    $pluginEntries = @($projectDescriptor.Plugins)
    foreach ($pluginName in @("UnrealEnginePython", "UEPLyraBridge")) {
        $entry = $pluginEntries | Where-Object Name -eq $pluginName | Select-Object -First 1
        if ($entry) {
            $entry.Enabled = $true
        }
        else {
            $pluginEntries += [pscustomobject]@{ Name = $pluginName; Enabled = $true }
        }
    }
    foreach ($pluginName in @("ShooterCore", "ShooterExplorer", "ShooterMaps", "ShooterTests", "TopDownArena")) {
        $entry = $pluginEntries | Where-Object Name -eq $pluginName | Select-Object -First 1
        if ($entry) {
            $entry.Enabled = $false
        }
    }
    $projectDescriptor.Plugins = $pluginEntries
    $projectDescriptor | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $stageProject -Encoding utf8

    $engineOverrides = @"

; UEP source-only validation: staged file, never written to the Lyra reference.
[/Script/Engine.Engine]
AssetManagerClassName=/Script/Engine.AssetManager

[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Engine/Maps/Entry
ServerDefaultMap=/Engine/Maps/Entry
EditorStartupMap=/Engine/Maps/Entry
GlobalDefaultGameMode=/Script/Engine.GameModeBase
GameInstanceClass=/Script/Engine.GameInstance

[/Script/RemoteSession.RemoteSessionSettings]
bAutoHostWithPIE=False
bAutoHostWithGame=False
"@
    [System.IO.File]::AppendAllText((Join-Path $stageRoot "Config\DefaultEngine.ini"), $engineOverrides, [System.Text.UTF8Encoding]::new($false))
    $gameOverrides = @"

; UEP source-only validation: no content-backed Lyra policy or cook claim.
[/Script/GameFeatures.GameFeaturesSubsystemSettings]
GameFeaturesManagerClassName=/Script/GameFeatures.DefaultGameFeaturesProjectPolicies

[/Script/LyraGame.LyraUIManagerSubsystem]
DefaultUIPolicyClass=

[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="Scripts")
"@
    [System.IO.File]::AppendAllText((Join-Path $stageRoot "Config\DefaultGame.ini"), $gameOverrides, [System.Text.UTF8Encoding]::new($false))
    $summary.stage_project = $stageProject

    $buildLog = Join-Path $runRoot "build.log"
    $buildArguments = @(
        $ubtPath,
        "LyraEditor",
        "Win64",
        "Development",
        "-Project=$stageProject",
        "-WaitMutex",
        "-NoHotReload",
        "-NoUBA",
        "-UTF8Output"
    )
    $summary.build = Invoke-LoggedProcess -FilePath $dotnetPath -ArgumentList $buildArguments -WorkingDirectory $engineRootPath -LogPath $buildLog -Label "LyraEditor source build" -TimeoutSeconds 3600
    Assert-BuildLog $buildLog

    $smokeResult = Join-Path $runRoot "runtime.json"
    $runtimeStdout = Join-Path $runRoot "runtime-stdout.log"
    $runtimeLog = Join-Path $runRoot "runtime.log"
    $runtimeArguments = @(
        $stageProject,
        "/Engine/Maps/Entry",
        "-game",
        "-DisablePython",
        "-NullRHI",
        "-NoSound",
        "-NoSplash",
        "-NoP4",
        "-Unattended",
        "-NoAssetRegistryCache",
        "-NoLoadingScreen",
        "-stdout",
        "-FullStdOutLogOutput",
        "-abslog=$runtimeLog",
        "-UEPLyraSmokeResult=$smokeResult",
        "-UEPLyraMode=source"
    )
    $runtimeProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $runtimeArguments -WorkingDirectory $stageRoot -LogPath $runtimeStdout -Label "Lyra source runtime smoke" -TimeoutSeconds 300
    $runtimeEvidence = Assert-LyraRuntime -ResultPath $smokeResult -UnrealLogPath $runtimeLog -ExpectedMode "source" -ExpectedNetModes @("Standalone")
    $runtimeReport = $runtimeEvidence.Report
    $summary.runtime = [ordered]@{
        process = $runtimeProcess
        report = $smokeResult
        unreal_log = $runtimeLog
        world_ticks = $runtimeReport.world_ticks
        python = $runtimeReport.python_version
        net_mode = $runtimeReport.snapshot.net_mode
        fatal_diagnostics = $runtimeEvidence.FatalDiagnostics
        error_diagnostics = $runtimeEvidence.ErrorDiagnostics
    }

    $networkProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
    $activePorts = @($networkProperties.GetActiveTcpListeners().Port) + @($networkProperties.GetActiveUdpListeners().Port)
    if ($activePorts -contains $ServerPort) {
        throw "ServerPort $ServerPort is already in use"
    }
    $serverResult = Join-Path $runRoot "server-exit.json"
    $serverStdout = Join-Path $runRoot "server-exit-stdout.log"
    $serverLog = Join-Path $runRoot "server-exit.log"
    $serverArguments = @(
        $stageProject,
        "/Engine/Maps/Entry",
        "-server",
        "-port=$ServerPort",
        "-DisablePython",
        "-NullRHI",
        "-NoSound",
        "-NoSplash",
        "-NoP4",
        "-Unattended",
        "-NoAssetRegistryCache",
        "-NoLoadingScreen",
        "-stdout",
        "-FullStdOutLogOutput",
        "-abslog=$serverLog",
        "-UEPLyraSmokeResult=$serverResult",
        "-UEPLyraMode=server_exit",
        "-UEPLyraTimeoutSeconds=60"
    )
    $serverProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $serverArguments -WorkingDirectory $stageRoot -LogPath $serverStdout -Label "Lyra dedicated-server exit smoke" -TimeoutSeconds 300
    $serverEvidence = Assert-LyraRuntime -ResultPath $serverResult -UnrealLogPath $serverLog -ExpectedMode "server_exit" -ExpectedNetModes @("DedicatedServer")
    $serverReport = $serverEvidence.Report
    $serverContents = Get-Content -LiteralPath $serverLog -Raw
    if (!$serverContents.Contains("listening on port $ServerPort")) {
        throw "Lyra dedicated server did not listen on port $ServerPort"
    }
    if (!$serverReport.snapshot.server_authority) {
        throw "Lyra dedicated-server exit smoke did not retain authority"
    }
    $summary.server_exit = [ordered]@{
        process = $serverProcess
        report = $serverResult
        unreal_log = $serverLog
        port = $ServerPort
        world_ticks = $serverReport.world_ticks
        python = $serverReport.python_version
        net_mode = $serverReport.snapshot.net_mode
        server_authority = $serverReport.snapshot.server_authority
        fatal_diagnostics = $serverEvidence.FatalDiagnostics
        error_diagnostics = $serverEvidence.ErrorDiagnostics
    }
    $summary.status = "passed"
}
catch {
    $summary.error = $_.Exception.Message
}

$summaryPath = Join-Path $runRoot "summary.json"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host "Lyra source validation: $($summary.status)"
Write-Host "Summary: $summaryPath"
if ($summary.status -ne "passed") {
    Write-Error $summary.error
    exit 1
}
