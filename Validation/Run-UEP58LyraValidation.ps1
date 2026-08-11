[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [string]$LyraProject,
    [string]$OutputRoot,
    [ValidateSet("Readiness", "Standalone", "Network", "Package", "All")]
    [string]$Mode = "All",
    [string]$GameplayMap = "/ShooterMaps/Maps/L_Expanse",
    [string]$ExpectedExperience = "B_ShooterGame_Elimination",
    [string[]]$RequiredActiveGameFeatures = @("ShooterCore"),
    [string[]]$RequiredRegisteredGameFeatures = @("ShooterMaps"),
    [ValidateRange(1024, 65535)]
    [int]$ServerPort = 7788,
    [ValidateRange(1, 64)]
    [int]$MaxParallelActions = 2,
    [ValidateRange(30, 600)]
    [int]$RuntimeTimeoutSeconds = 180,
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
        [string[]]$ExcludedSegments = @(),
        [switch]$PruneDestination
    )
    $sourcePath = Get-FullPath $Source
    $destinationPath = Get-FullPath $Destination
    if (!(Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Copy source directory was not found: $sourcePath"
    }
    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
    $enumerationOptions = [System.IO.EnumerationOptions]::new()
    $enumerationOptions.RecurseSubdirectories = $true
    $enumerationOptions.IgnoreInaccessible = $false
    $enumerationOptions.AttributesToSkip = [System.IO.FileAttributes]::ReparsePoint
    $expectedPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($file in [System.IO.Directory]::EnumerateFiles($sourcePath, "*", $enumerationOptions)) {
        $relativePath = [System.IO.Path]::GetRelativePath($sourcePath, $file)
        $segments = @($relativePath -split "[\\/]")
        if (@($segments | Where-Object { $ExcludedSegments -contains $_ }).Count -gt 0) {
            continue
        }
        $expectedPaths.Add($relativePath) | Out-Null
        $targetPath = Join-Path $destinationPath $relativePath
        if ([System.IO.File]::Exists($targetPath)) {
            $sourceInfo = [System.IO.FileInfo]::new($file)
            $targetInfo = [System.IO.FileInfo]::new($targetPath)
            if ($sourceInfo.Length -eq $targetInfo.Length -and
                $sourceInfo.LastWriteTimeUtc -eq $targetInfo.LastWriteTimeUtc) {
                continue
            }
        }
        $targetDirectory = Split-Path -Parent $targetPath
        New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
        Copy-Item -LiteralPath $file -Destination $targetPath -Force
    }
    if ($PruneDestination) {
        foreach ($file in @([System.IO.Directory]::EnumerateFiles($destinationPath, "*", $enumerationOptions))) {
            $relativePath = [System.IO.Path]::GetRelativePath($destinationPath, $file)
            $segments = @($relativePath -split "[\\/]")
            if (@($segments | Where-Object { $ExcludedSegments -contains $_ }).Count -gt 0) {
                continue
            }
            if (!$expectedPaths.Contains($relativePath)) {
                Remove-Item -LiteralPath $file -Force
            }
        }
    }
}

function Format-ProcessCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList
    )
    return "$FilePath $($ArgumentList -join ' ')"
}

function Invoke-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string]$Label,
        [int]$TimeoutSeconds = 0,
        [int[]]$AllowedExitCodes = @(0),
        [hashtable]$EnvironmentVariables = @{}
    )
    Write-Host "[$Label] $(Format-ProcessCommand $FilePath $ArgumentList)"
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
    foreach ($entry in $EnvironmentVariables.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = [string]$entry.Value
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
        if ($AllowedExitCodes -notcontains $process.ExitCode) {
            throw "$Label exited with code $($process.ExitCode); log: $LogPath"
        }
        return $result
    }
    finally {
        $process.Dispose()
    }
}

function Start-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Write-Host "[$Label] $(Format-ProcessCommand $FilePath $ArgumentList)"
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
    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (!$process) {
        throw "$Label failed to start"
    }
    return [pscustomobject]@{
        Process = $process
        StdoutTask = $process.StandardOutput.ReadToEndAsync()
        StderrTask = $process.StandardError.ReadToEndAsync()
        StartedAt = Get-Date
        LogPath = $LogPath
        Label = $Label
        Completed = $false
    }
}

function Complete-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)]$Handle,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )
    if ($Handle.Completed) {
        throw "$($Handle.Label) was already completed"
    }
    $process = $Handle.Process
    try {
        $timedOut = $false
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            $timedOut = $true
        }
        $stdout = $Handle.StdoutTask.GetAwaiter().GetResult()
        $stderr = $Handle.StderrTask.GetAwaiter().GetResult()
        @($stdout, $stderr) -join [Environment]::NewLine | Set-Content -LiteralPath $Handle.LogPath -Encoding utf8
        if ($timedOut) {
            throw "$($Handle.Label) timed out after $TimeoutSeconds seconds; log: $($Handle.LogPath)"
        }
        if ($process.ExitCode -ne 0) {
            throw "$($Handle.Label) exited with code $($process.ExitCode); log: $($Handle.LogPath)"
        }
        return [ordered]@{
            exit_code = $process.ExitCode
            duration_seconds = [math]::Round(((Get-Date) - $Handle.StartedAt).TotalSeconds, 3)
            log = $Handle.LogPath
        }
    }
    finally {
        $Handle.Completed = $true
        $process.Dispose()
    }
}

function Stop-LoggedProcess {
    param($Handle)
    if (!$Handle -or $Handle.Completed) {
        return
    }
    $process = $Handle.Process
    try {
        if (!$process.HasExited) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        $stdout = $Handle.StdoutTask.GetAwaiter().GetResult()
        $stderr = $Handle.StderrTask.GetAwaiter().GetResult()
        @($stdout, $stderr) -join [Environment]::NewLine | Set-Content -LiteralPath $Handle.LogPath -Encoding utf8
    }
    finally {
        $Handle.Completed = $true
        $process.Dispose()
    }
}

function Wait-ForLogMarker {
    param(
        [Parameter(Mandatory = $true)]$Handle,
        [Parameter(Mandatory = $true)][string]$UnrealLogPath,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Handle.Process.HasExited) {
            throw "$($Handle.Label) exited before '$Marker' appeared: $UnrealLogPath"
        }
        if (Test-Path -LiteralPath $UnrealLogPath -PathType Leaf) {
            $contents = Get-Content -LiteralPath $UnrealLogPath -Raw -ErrorAction SilentlyContinue
            if ($contents -and $contents.Contains($Marker)) {
                return
            }
        }
        Start-Sleep -Milliseconds 500
    }
    throw "$($Handle.Label) did not emit '$Marker' within $TimeoutSeconds seconds: $UnrealLogPath"
}

function Assert-BuildLog {
    param([Parameter(Mandatory = $true)][string]$LogPath)
    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        throw "Build log was not created: $LogPath"
    }
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

function Assert-AutomationLog {
    param([Parameter(Mandatory = $true)][string]$LogPath)
    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        throw "AutomationTool log was not created: $LogPath"
    }
    $contents = Get-Content -LiteralPath $LogPath -Raw
    foreach ($marker in @("BUILD SUCCESSFUL", "AutomationTool exiting with ExitCode=0")) {
        if (!$contents.Contains($marker)) {
            throw "AutomationTool log does not contain '$marker': $LogPath"
        }
    }
}

function Assert-LyraRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$UnrealLogPath,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][string[]]$ExpectedActiveFeatures,
        [Parameter(Mandatory = $true)][string[]]$ExpectedRegisteredFeatures,
        [string]$ExpectedExperienceContains = ""
    )
    if (!(Test-Path -LiteralPath $ResultPath -PathType Leaf)) {
        throw "Lyra runtime result was not created: $ResultPath"
    }
    if (!(Test-Path -LiteralPath $UnrealLogPath -PathType Leaf)) {
        throw "Lyra runtime log was not created: $UnrealLogPath"
    }
    $report = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
    if ($report.status -ne "passed" -or $report.mode -ne $ExpectedMode -or $report.python_version -notlike "3.11.*") {
        throw "Lyra $ExpectedMode report failed its status/mode/Python contract: $ResultPath"
    }
    if (!$report.snapshot.game_thread -or !$report.snapshot.game_world -or !$report.snapshot.experience_loaded) {
        throw "Lyra $ExpectedMode report lacks game-thread/world/Experience readiness: $ResultPath"
    }
    if ($ExpectedExperienceContains -and
        ([string]$report.snapshot.experience).IndexOf($ExpectedExperienceContains, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "Lyra $ExpectedMode report used unexpected Experience '$($report.snapshot.experience)': $ResultPath"
    }
    foreach ($feature in $ExpectedActiveFeatures) {
        $active = @($report.snapshot.game_features | Where-Object {
            $_.name -eq $feature -and $_.active
        })
        if ($active.Count -ne 1) {
            throw "Lyra $ExpectedMode report lacks active Game Feature '$feature': $ResultPath"
        }
    }
    foreach ($feature in $ExpectedRegisteredFeatures) {
        $registered = @($report.snapshot.game_features | Where-Object {
            $_.name -eq $feature -and @("Registered", "Loaded", "Active") -contains ([string]$_.state)
        })
        if ($registered.Count -ne 1) {
            throw "Lyra $ExpectedMode report lacks registered Game Feature '$feature': $ResultPath"
        }
    }

    $logContents = Get-Content -LiteralPath $UnrealLogPath -Raw
    foreach ($marker in @(
        "Initialized engine CPython at",
        "UEP_LYRA_SCRIPT_LOADED",
        "UEP_LYRA_BRIDGE_ATTACHED",
        "UEP_LYRA_SMOKE_PASSED",
        "Object subsystem successfully closed",
        "Goodbye Python",
        "Log file closed"
    )) {
        if (!$logContents.Contains($marker)) {
            throw "Lyra $ExpectedMode log is missing '$marker': $UnrealLogPath"
        }
    }
    $fatalMatches = @([regex]::Matches($logContents, "(?im)Fatal error:|Assertion failed:|Unhandled Exception:|LogWindows:\s+Error:"))
    $errorMatches = @([regex]::Matches($logContents, "(?im)^.*Log[A-Za-z0-9_]+:\s+Error:.*$"))
    if ($fatalMatches.Count -gt 0 -or $errorMatches.Count -gt 0) {
        throw "Lyra $ExpectedMode log contains fatal/assert/error diagnostics: $UnrealLogPath"
    }
    return $report
}

function Get-LyraRuntimeArguments {
    param(
        [Parameter(Mandatory = $true)][string]$TargetMode,
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$UnrealLogPath,
        [Parameter(Mandatory = $true)][int]$ExpectedControllers,
        [Parameter(Mandatory = $true)][string[]]$ActiveFeatures,
        [Parameter(Mandatory = $true)][string[]]$RegisteredFeatures,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [string]$Experience = "",
        [int]$HoldReadySeconds = 0,
        [string]$ReadyReleaseFile = ""
    )
    $arguments = [System.Collections.Generic.List[string]]::new()
    foreach ($argument in @(
        "-unattended",
        "-nop4",
        "-NullRHI",
        "-NoSound",
        "-NoSplash",
        "-NoLoadingScreen",
        # UE 5.8 runs Core smoke tests during editor startup. Pinning the
        # validation culture keeps their source-language string assertions
        # deterministic on localized Windows installations.
        "-culture=en",
        "-UTF8Output",
        "-DisablePython",
        "-DisablePlugins=AndroidFileServer",
        "-stdout",
        "-FullStdOutLogOutput",
        "-abslog=$UnrealLogPath",
        "-UEPLyraSmokeResult=$ResultPath",
        "-UEPLyraMode=$TargetMode",
        "-UEPLyraRequireExperience",
        "-UEPLyraTimeoutSeconds=$TimeoutSeconds",
        "-UEPLyraExpectedPlayerControllers=$ExpectedControllers",
        "-UEPLyraRequiredFeatures=$($ActiveFeatures -join '+')",
        "-UEPLyraRequiredRegisteredFeatures=$($RegisteredFeatures -join '+')",
        "-UEPLyraHoldReadySeconds=$HoldReadySeconds"
    )) {
        $arguments.Add($argument)
    }
    if ($Experience) {
        $arguments.Add("-UEPLyraExpectedExperienceContains=$Experience")
    }
    if ($ReadyReleaseFile) {
        $arguments.Add("-UEPLyraReadyReleaseFile=$ReadyReleaseFile")
    }
    return $arguments.ToArray()
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot "..")
$engineRootPath = Get-FullPath $EngineRoot
if (!$LyraProject) {
    throw "-LyraProject must identify a complete external UE5.8 Lyra project"
}
$lyraProjectPath = Get-FullPath $LyraProject
$lyraRoot = Split-Path -Parent $lyraProjectPath
if (!$OutputRoot) {
    $OutputRoot = Join-Path $repoRoot ".build\LyraValidation\Full"
}
$outputRootPath = Get-FullPath $OutputRoot
if ((Test-PathIsUnder -Path $outputRootPath -Parent $engineRootPath) -or
    (Test-PathIsUnder -Path $engineRootPath -Parent $outputRootPath) -or
    (Test-PathIsUnder -Path $outputRootPath -Parent $lyraRoot) -or
    (Test-PathIsUnder -Path $lyraRoot -Parent $outputRootPath)) {
    throw "OutputRoot, Unreal Engine and Lyra reference trees must not overlap: $outputRootPath"
}
if (Test-PathIsUnder -Path $lyraRoot -Parent $repoRoot) {
    throw "Full Lyra validation requires a reference project outside this repository: $lyraRoot"
}

$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$resultRoot = Join-Path $outputRootPath "Results\$runId"
$stageRoot = Join-Path $outputRootPath "Stage\Lyra"
$stageMarkerPath = Join-Path $stageRoot ".uep-lyra-full-stage.json"
$packageRoot = Join-Path $outputRootPath "Package"
$packageMarkerPath = Join-Path $packageRoot ".uep-lyra-full-package.json"
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null

$summary = [ordered]@{
    schema_version = 1
    status = "failed"
    run_id = $runId
    mode = $Mode
    engine = $null
    python = $null
    lyra_project = $lyraProjectPath
    stage_project = $null
    stage_runtime_disabled_plugins = @()
    package_output_root = if ($Mode -in @("Package", "All")) { $packageRoot } else { $null }
    readiness = $null
    editor_build = $null
    standalone = $null
    network = $null
    package = $null
    full_acceptance = $false
    error = $null
}

try {
    $engineVersionPath = Join-Path $engineRootPath "Engine\Build\Build.version"
    $pythonPath = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
    $editorPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    $ubtPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
    $automationToolPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\AutomationTool\AutomationTool.dll"
    $dotnetRoot = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\DotNet"
    $dotnetPath = Get-ChildItem -LiteralPath $dotnetRoot -Directory |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "win-x64\dotnet.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    foreach ($requiredPath in @($engineVersionPath, $pythonPath, $editorPath, $ubtPath, $automationToolPath, $dotnetPath, $lyraProjectPath)) {
        if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Required file was not found: $requiredPath"
        }
    }
    $engineVersion = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
    $summary.engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
    if ($engineVersion.MajorVersion -ne 5 -or $engineVersion.MinorVersion -ne 8) {
        throw "Full Lyra validation requires Unreal Engine 5.8"
    }
    $summary.python = (& $pythonPath -c "import platform; print(platform.python_version())").Trim()
    if ($summary.python -notlike "3.11.*") {
        throw "Full Lyra validation requires engine-bundled CPython 3.11"
    }

    $readinessRoot = Join-Path $resultRoot "Readiness"
    $readinessLog = Join-Path $resultRoot "readiness.log"
    $powerShellPath = (Get-Process -Id $PID).Path
    if (!$powerShellPath) {
        $powerShellPath = "powershell.exe"
    }
    $readinessArguments = @(
        "-NoProfile",
        "-NonInteractive",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "Test-UEP58LyraReadiness.ps1"),
        "-EngineRoot", $engineRootPath,
        "-LyraProject", $lyraProjectPath,
        "-OutputRoot", $readinessRoot,
        "-Strict"
    )
    $readinessProcess = Invoke-LoggedProcess -FilePath $powerShellPath -ArgumentList $readinessArguments -WorkingDirectory $repoRoot -LogPath $readinessLog -Label "Lyra strict readiness" -TimeoutSeconds 300 -AllowedExitCodes @(0, 1)
    $readinessSummaryPath = Get-ChildItem -LiteralPath $readinessRoot -Filter summary.json -File -Recurse |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (!$readinessSummaryPath) {
        throw "Lyra readiness did not create a summary under $readinessRoot"
    }
    $readinessReport = Get-Content -LiteralPath $readinessSummaryPath -Raw | ConvertFrom-Json
    $summary.readiness = [ordered]@{
        process = $readinessProcess
        report = $readinessSummaryPath
        source_ready = $readinessReport.source_ready
        content_ready = $readinessReport.content_ready
        blocking_reasons = @($readinessReport.blocking_reasons)
    }
    if (!$readinessReport.source_ready -or !$readinessReport.content_ready -or $readinessProcess.exit_code -ne 0) {
        $summary.status = "blocked"
        throw "Complete Lyra source/content readiness is required: $readinessSummaryPath"
    }
    if ($Mode -eq "Readiness") {
        $summary.status = "passed"
        $summary.full_acceptance = $false
    }
    else {
        if (Test-PathIsUnder -Path $lyraRoot -Parent $engineRootPath) {
            throw "Runtime/package validation requires a Lyra project outside the Unreal Engine source tree: $lyraRoot"
        }

        Assert-PathIsUnder -Path $stageRoot -Parent $outputRootPath
        if (Test-Path -LiteralPath $stageRoot -PathType Container) {
            if (!(Test-Path -LiteralPath $stageMarkerPath -PathType Leaf)) {
                throw "Refusing to use unmarked Lyra staging directory: $stageRoot"
            }
            $existingMarker = Get-Content -LiteralPath $stageMarkerPath -Raw | ConvertFrom-Json
            if ($Incremental -and $existingMarker.source -ne $lyraProjectPath) {
                throw "Incremental stage belongs to '$($existingMarker.source)', not '$lyraProjectPath'"
            }
            if (!$Incremental) {
                Remove-Item -LiteralPath $stageRoot -Recurse -Force
            }
        }
        New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
        [ordered]@{
            schema_version = 1
            purpose = "UEP Lyra full-content disposable stage"
            source = $lyraProjectPath
        } | ConvertTo-Json | Set-Content -LiteralPath $stageMarkerPath -Encoding utf8

        Write-Host "[Stage complete Lyra project] $lyraRoot -> $stageRoot"
        Copy-FilteredTree -Source $lyraRoot -Destination $stageRoot -ExcludedSegments @(
            "Binaries", "DerivedDataCache", "Intermediate", "Saved", ".git", ".codegraph", ".vs"
        )

        $stagedUEP = Join-Path $stageRoot "Plugins\UnrealEnginePython"
        $stagedBridge = Join-Path $stageRoot "Plugins\UEPLyraBridge"
        foreach ($ownedPluginPath in @($stagedUEP, $stagedBridge)) {
            Assert-PathIsUnder -Path $ownedPluginPath -Parent $stageRoot
            # Incremental runs retain only owned Binaries/Intermediate outputs;
            # source/config files are synchronized and pruned below.
            if (!$Incremental -and (Test-Path -LiteralPath $ownedPluginPath -PathType Container)) {
                Remove-Item -LiteralPath $ownedPluginPath -Recurse -Force
            }
        }
        foreach ($directory in @("Config", "Resources", "Source")) {
            Copy-FilteredTree -Source (Join-Path $repoRoot $directory) -Destination (Join-Path $stagedUEP $directory) -ExcludedSegments @(
                "Binaries", "Intermediate", "__pycache__", ".build", ".git", ".codegraph"
            ) -PruneDestination
        }
        Copy-Item -LiteralPath (Join-Path $repoRoot "UnrealEnginePython.uplugin") -Destination (Join-Path $stagedUEP "UnrealEnginePython.uplugin") -Force
        Copy-FilteredTree -Source (Join-Path $repoRoot "Demos\UEPLyraIntegration\Plugins\UEPLyraBridge") -Destination $stagedBridge -ExcludedSegments @(
            "Binaries", "Intermediate", "__pycache__"
        ) -PruneDestination
        Copy-FilteredTree -Source (Join-Path $repoRoot "Demos\UEPLyraIntegration\Overlay\Content\Scripts") -Destination (Join-Path $stageRoot "Content\Scripts") -ExcludedSegments @("__pycache__")

        $stageProject = Join-Path $stageRoot ([System.IO.Path]::GetFileName($lyraProjectPath))
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
        # AndroidFileServer is unrelated on this desktop host and can trigger a
        # separate Windows firewall prompt. This change is staged only.
        $androidFileServer = $pluginEntries | Where-Object Name -eq "AndroidFileServer" | Select-Object -First 1
        if ($androidFileServer) {
            $androidFileServer.Enabled = $false
        }
        else {
            $pluginEntries += [pscustomobject]@{ Name = "AndroidFileServer"; Enabled = $false }
        }
        $projectDescriptor.Plugins = $pluginEntries
        $projectDescriptor | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $stageProject -Encoding utf8

        $packagingOverrides = @"

; UEP full Lyra validation: staged file, never written to the reference project.
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="Scripts")
"@
        [System.IO.File]::AppendAllText((Join-Path $stageRoot "Config\DefaultGame.ini"), $packagingOverrides, [System.Text.UTF8Encoding]::new($false))
        $summary.stage_project = $stageProject

        $buildLog = Join-Path $resultRoot "build-editor.log"
        $buildArguments = @(
            $ubtPath,
            "LyraEditor",
            "Win64",
            "Development",
            "-Project=$stageProject",
            "-WaitMutex",
            "-NoHotReload",
            "-NoUBA",
            "-MaxParallelActions=$MaxParallelActions",
            "-UTF8Output"
        )
        $summary.editor_build = Invoke-LoggedProcess -FilePath $dotnetPath -ArgumentList $buildArguments -WorkingDirectory $engineRootPath -LogPath $buildLog -Label "LyraEditor full-content build" -TimeoutSeconds 3600
        Assert-BuildLog $buildLog

        # Preserve compile coverage for the complete reference-project target,
        # then disable only test runners for gameplay/package execution. The
        # ShooterTests -> AsyncMessageSystemTests dependency re-enables
        # RuntimeTests and its intentional Log*:Error cases unless both roots
        # are disabled. Reference project files remain untouched.
        $runtimeDisabledPlugins = @("RuntimeTests", "ShooterTests")
        foreach ($pluginName in $runtimeDisabledPlugins) {
            $entry = $pluginEntries | Where-Object Name -eq $pluginName | Select-Object -First 1
            if ($entry) {
                $entry.Enabled = $false
            }
            else {
                $pluginEntries += [pscustomobject]@{ Name = $pluginName; Enabled = $false }
            }
        }
        $projectDescriptor.Plugins = $pluginEntries
        $projectDescriptor | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $stageProject -Encoding utf8
        $summary.stage_runtime_disabled_plugins = @("AndroidFileServer") + $runtimeDisabledPlugins

        if ($Mode -in @("Standalone", "All")) {
            $standaloneResult = Join-Path $resultRoot "standalone.json"
            $standaloneStdout = Join-Path $resultRoot "standalone-stdout.log"
            $standaloneLog = Join-Path $resultRoot "standalone.log"
            $standaloneArguments = @($stageProject, $GameplayMap, "-game") +
                (Get-LyraRuntimeArguments -TargetMode "standalone" -ResultPath $standaloneResult -UnrealLogPath $standaloneLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience)
            $standaloneProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $standaloneArguments -WorkingDirectory $stageRoot -LogPath $standaloneStdout -Label "Lyra standalone gameplay" -TimeoutSeconds ($RuntimeTimeoutSeconds + 120)
            $standaloneReport = Assert-LyraRuntime -ResultPath $standaloneResult -UnrealLogPath $standaloneLog -ExpectedMode "standalone" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience
            $summary.standalone = [ordered]@{ process = $standaloneProcess; report = $standaloneResult; unreal_log = $standaloneLog; snapshot = $standaloneReport.snapshot }
        }

        if ($Mode -in @("Network", "All")) {
            $networkProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
            $activePorts = @($networkProperties.GetActiveTcpListeners().Port) + @($networkProperties.GetActiveUdpListeners().Port)
            if ($activePorts -contains $ServerPort) {
                throw "ServerPort $ServerPort is already in use"
            }
            $serverResult = Join-Path $resultRoot "server.json"
            $serverStdout = Join-Path $resultRoot "server-stdout.log"
            $serverLog = Join-Path $resultRoot "server.log"
            $clientResult = Join-Path $resultRoot "client.json"
            $clientStdout = Join-Path $resultRoot "client-stdout.log"
            $clientLog = Join-Path $resultRoot "client.log"
            $networkReleaseFile = Join-Path $resultRoot "network-ready.release"
            $serverArguments = @($stageProject, $GameplayMap, "-server", "-port=$ServerPort", "-Multiprocess") +
                (Get-LyraRuntimeArguments -TargetMode "server" -ResultPath $serverResult -UnrealLogPath $serverLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience -ReadyReleaseFile $networkReleaseFile)
            $clientArguments = @($stageProject, "127.0.0.1:$ServerPort", "-game", "-Multiprocess") +
                (Get-LyraRuntimeArguments -TargetMode "client" -ResultPath $clientResult -UnrealLogPath $clientLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience -ReadyReleaseFile $networkReleaseFile)
            $serverHandle = $null
            $clientHandle = $null
            try {
                $serverHandle = Start-LoggedProcess -FilePath $editorPath -ArgumentList $serverArguments -WorkingDirectory $stageRoot -LogPath $serverStdout -Label "Lyra dedicated server"
                Wait-ForLogMarker -Handle $serverHandle -UnrealLogPath $serverLog -Marker "listening on port $ServerPort" -TimeoutSeconds 120
                Wait-ForLogMarker -Handle $serverHandle -UnrealLogPath $serverLog -Marker "UEP_LYRA_BRIDGE_ATTACHED" -TimeoutSeconds 120
                $clientHandle = Start-LoggedProcess -FilePath $editorPath -ArgumentList $clientArguments -WorkingDirectory $stageRoot -LogPath $clientStdout -Label "Lyra multiplayer client"
                Wait-ForLogMarker -Handle $serverHandle -UnrealLogPath $serverLog -Marker "UEP_LYRA_REQUIREMENTS_READY server" -TimeoutSeconds $RuntimeTimeoutSeconds
                Wait-ForLogMarker -Handle $clientHandle -UnrealLogPath $clientLog -Marker "UEP_LYRA_REQUIREMENTS_READY client" -TimeoutSeconds $RuntimeTimeoutSeconds
                "release" | Set-Content -LiteralPath $networkReleaseFile -Encoding ascii
                $clientProcess = Complete-LoggedProcess -Handle $clientHandle -TimeoutSeconds ($RuntimeTimeoutSeconds + 120)
                $serverProcess = Complete-LoggedProcess -Handle $serverHandle -TimeoutSeconds 120
            }
            finally {
                Stop-LoggedProcess -Handle $clientHandle
                Stop-LoggedProcess -Handle $serverHandle
            }
            $clientReport = Assert-LyraRuntime -ResultPath $clientResult -UnrealLogPath $clientLog -ExpectedMode "client" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience
            $serverReport = Assert-LyraRuntime -ResultPath $serverResult -UnrealLogPath $serverLog -ExpectedMode "server" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience
            $summary.network = [ordered]@{
                port = $ServerPort
                release_signal = $networkReleaseFile
                server = [ordered]@{ process = $serverProcess; report = $serverResult; unreal_log = $serverLog; snapshot = $serverReport.snapshot }
                client = [ordered]@{ process = $clientProcess; report = $clientResult; unreal_log = $clientLog; snapshot = $clientReport.snapshot }
            }
        }

        if ($Mode -in @("Package", "All")) {
            # Windows Firewall identifies unpackaged programs by their complete
            # executable path. Archive directly to a guarded fixed directory so
            # every validation run uses the same application identity without an
            # additional package-sized mirror copy.
            Assert-PathIsUnder -Path $packageRoot -Parent $outputRootPath
            if (Test-Path -LiteralPath $packageRoot -PathType Container) {
                if (!(Test-Path -LiteralPath $packageMarkerPath -PathType Leaf)) {
                    throw "Refusing to clean an unmarked Lyra package directory: $packageRoot"
                }
                Remove-Item -LiteralPath $packageRoot -Recurse -Force
            }
            New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
            [ordered]@{
                schema_version = 1
                purpose = "UEP Lyra full-content disposable package"
                source = $lyraProjectPath
            } | ConvertTo-Json | Set-Content -LiteralPath $packageMarkerPath -Encoding utf8

            $packageLogRoot = Join-Path $resultRoot "package-uat"
            $packageLog = Join-Path $packageLogRoot "Log.txt"
            New-Item -ItemType Directory -Path $packageLogRoot -Force | Out-Null
            $packageArguments = @(
                $automationToolPath,
                "BuildCookRun",
                "-project=$stageProject",
                "-noP4",
                "-utf8output",
                "-WaitForUATMutex",
                "-unattended",
                "-platform=Win64",
                "-clientconfig=Development",
                "-target=LyraGame",
                "-ubtargs=-NoUBA -MaxParallelActions=$MaxParallelActions",
                "-build",
                "-SkipBuildEditor",
                "-cook",
                "-stage",
                "-pak",
                "-archive",
                "-archivedirectory=$packageRoot",
                "-map=$GameplayMap",
                "-AdditionalCookerOptions=-DisablePlugins=AndroidFileServer -SkipZenStore",
                "-nodebuginfo"
            )
            $packageEnvironment = @{ uebp_LogFolder = $packageLogRoot; uebp_FinalLogFolder = $packageLogRoot }
            $packageProcess = Invoke-LoggedProcess -FilePath $dotnetPath -ArgumentList $packageArguments -WorkingDirectory $engineRootPath -LogPath (Join-Path $resultRoot "package-stdout.log") -Label "Lyra cook and package" -TimeoutSeconds 7200 -EnvironmentVariables $packageEnvironment
            Assert-AutomationLog $packageLog

            # Recreate the marker in case AutomationTool replaced the archive root.
            [ordered]@{
                schema_version = 1
                purpose = "UEP Lyra full-content disposable package"
                source = $lyraProjectPath
            } | ConvertTo-Json | Set-Content -LiteralPath $packageMarkerPath -Encoding utf8
            $packagedExecutable = Join-Path $packageRoot "Windows\LyraGame.exe"
            $packagedInternalExecutable = Join-Path $packageRoot "Windows\LyraStarterGame\Binaries\Win64\LyraGame.exe"
            foreach ($requiredExecutable in @($packagedExecutable, $packagedInternalExecutable)) {
                if (!(Test-Path -LiteralPath $requiredExecutable -PathType Leaf)) {
                    throw "Packaged LyraGame executable was not found: $requiredExecutable"
                }
            }

            # Lyra archives its internal target under the project directory,
            # while Windows Firewall keys unpackaged applications by the exact
            # executable path. Copy the complete project-binary tree to a stable
            # validation-only alias so one preconfigured Block rule covers
            # every run without mirroring Content/Paks or changing the package
            # payload being tested. Preserve nested D3D12/DML runtime files.
            $packagedNetworkDirectory = Join-Path $packageRoot "Windows\LyraGame\Binaries\Win64"
            Copy-FilteredTree -Source (Split-Path -Parent $packagedInternalExecutable) -Destination $packagedNetworkDirectory -PruneDestination
            $packagedNetworkExecutable = Join-Path $packagedNetworkDirectory "LyraGame.exe"
            if (!(Test-Path -LiteralPath $packagedNetworkExecutable -PathType Leaf)) {
                throw "Stable packaged LyraGame validation executable was not created: $packagedNetworkExecutable"
            }
            $packagedInternalExecutableSha256 = (Get-FileHash -LiteralPath $packagedInternalExecutable -Algorithm SHA256).Hash
            $packagedNetworkExecutableSha256 = (Get-FileHash -LiteralPath $packagedNetworkExecutable -Algorithm SHA256).Hash
            if ($packagedNetworkExecutableSha256 -ne $packagedInternalExecutableSha256) {
                throw "Stable packaged LyraGame validation executable does not match the archived internal executable"
            }
            $packagedResult = Join-Path $resultRoot "packaged.json"
            $packagedStdout = Join-Path $resultRoot "packaged-stdout.log"
            $packagedLog = Join-Path $resultRoot "packaged.log"
            $packagedArguments = @(
                "-basedir=$(Split-Path -Parent $packagedInternalExecutable)",
                $GameplayMap
            ) +
                (Get-LyraRuntimeArguments -TargetMode "packaged" -ResultPath $packagedResult -UnrealLogPath $packagedLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience)
            $packagedRuntime = Invoke-LoggedProcess -FilePath $packagedNetworkExecutable -ArgumentList $packagedArguments -WorkingDirectory $packagedNetworkDirectory -LogPath $packagedStdout -Label "Packaged Lyra gameplay" -TimeoutSeconds ($RuntimeTimeoutSeconds + 120)
            $packagedReport = Assert-LyraRuntime -ResultPath $packagedResult -UnrealLogPath $packagedLog -ExpectedMode "packaged" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience
            $summary.package = [ordered]@{
                process = $packageProcess
                uat_log = $packageLog
                output_root = $packageRoot
                executable = $packagedExecutable
                internal_executable = $packagedInternalExecutable
                internal_executable_sha256 = $packagedInternalExecutableSha256
                network_executable = $packagedNetworkExecutable
                network_executable_sha256 = $packagedNetworkExecutableSha256
                runtime = $packagedRuntime
                report = $packagedResult
                unreal_log = $packagedLog
                snapshot = $packagedReport.snapshot
            }
        }

        $summary.status = "passed"
        $summary.full_acceptance = $Mode -eq "All"
    }
}
catch {
    if ($summary.status -ne "blocked") {
        $summary.status = "failed"
    }
    $summary.error = $_.Exception.Message
}

$summaryPath = Join-Path $resultRoot "summary.json"
$summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host "Lyra $Mode validation: $($summary.status)"
Write-Host "Summary: $summaryPath"
if ($summary.status -ne "passed") {
    Write-Error $summary.error
    exit 1
}
