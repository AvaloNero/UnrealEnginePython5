[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [ValidateSet("Development", "DebugGame")]
    [string]$Configuration = "Development",
    [ValidateRange(1, 64)]
    [int]$MaxParallelActions = 4,
    [switch]$SkipPackage,
    [switch]$SkipRuntimeCompile,
    [switch]$Incremental
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-PathIsUnder {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Parent
    )

    $fullPath = Get-FullPath $Path
    $fullParent = (Get-FullPath $Parent).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar

    if (!$fullPath.StartsWith($fullParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside $fullParent (resolved path: $fullPath)"
    }
}

function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (!(Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Source directory does not exist: $Source"
    }

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Format-ProcessCommand {
    param([string]$FilePath, [string[]]$ArgumentList)
    $displayArguments = $ArgumentList | ForEach-Object {
        if ($_ -match "\s") { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
    }
    return "$FilePath $($displayArguments -join ' ')"
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [int]$TimeoutSeconds = 0,
        [hashtable]$EnvironmentVariables = @{}
    )

    Write-Host "[$Label] $(Format-ProcessCommand $FilePath $ArgumentList)"

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

function Assert-BuildLog {
    param([Parameter(Mandatory = $true)][string]$LogPath)

    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        throw "Build log was not created: $LogPath"
    }

    $contents = Get-Content -LiteralPath $LogPath -Raw
    if ($contents -notmatch "Result:\s+Succeeded") {
        throw "Build log does not contain a successful result: $LogPath"
    }

    $compilerDiagnostics = [regex]::Matches(
        $contents,
        "(?im)\b(?:warning C\d+|error C\d+|fatal error C\d+|error LNK\d+)\b"
    )
    if ($compilerDiagnostics.Count -gt 0) {
        throw "Build produced $($compilerDiagnostics.Count) compiler/linker diagnostic(s): $LogPath"
    }
}

function Assert-ValidationResult {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [string]$RequiredLogMarker = "UEP_VALIDATION_PASSED"
    )

    if (!(Test-Path -LiteralPath $ResultPath -PathType Leaf)) {
        throw "Validation result was not created: $ResultPath"
    }
    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        throw "Validation log was not created: $LogPath"
    }

    $report = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
    if ($report.status -ne "passed") {
        $failures = @($report.tests | Where-Object { $_.status -eq "failed" })
        $failureText = ($failures | ForEach-Object { "$($_.name): $($_.error)" }) -join "; "
        throw "Validation failed: $failureText (report: $ResultPath)"
    }

    $logContents = Get-Content -LiteralPath $LogPath -Raw
    if (!$logContents.Contains($RequiredLogMarker)) {
        throw "Validation marker '$RequiredLogMarker' is missing from $LogPath"
    }

    $fatalPatterns = @(
        "Fatal error:",
        "Assertion failed:",
        "Unhandled Exception:",
        "UEP_VALIDATION_FAILED",
        "UEP_PACKAGED_VALIDATION_FAILED"
    )
    foreach ($pattern in $fatalPatterns) {
        if ($logContents.Contains($pattern)) {
            throw "Validation log contains '$pattern': $LogPath"
        }
    }

    return $report
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
$automationToolPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\AutomationTool\AutomationTool.dll"
$editorCmdPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$editorPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor.exe"
$validationSource = Join-Path $PSScriptRoot "UEP58Host"
$pluginHostSource = Join-Path $PSScriptRoot "UEP58PluginHost\UEP58PluginHost.uproject"
$validationRoot = Join-Path $repoRoot ".build\Validation"
$stageRoot = Join-Path $validationRoot "UEP58Host"
$stageMarker = Join-Path $stageRoot ".uep-validation-stage"
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$resultRoot = Join-Path $validationRoot "Results\$runId"

if (!$dotnetPath) {
    throw "No engine-bundled Win64 dotnet runtime was found under $dotnetRoot"
}

foreach ($requiredPath in @(
    $engineVersionPath,
    $dotnetPath,
    $ubtPath,
    $automationToolPath,
    $editorCmdPath,
    $editorPath,
    $pluginHostSource,
    (Join-Path $repoRoot "UnrealEnginePython.uplugin")
)) {
    if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file was not found: $requiredPath"
    }
}

$engineVersion = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
if ($engineVersion.MajorVersion -ne 5 -or $engineVersion.MinorVersion -ne 8) {
    throw "Expected Unreal Engine 5.8, found $($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
}

New-Item -ItemType Directory -Path $validationRoot -Force | Out-Null
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null

try {
    if (!$Incremental -and (Test-Path -LiteralPath $stageRoot)) {
        Assert-PathIsUnder $stageRoot $validationRoot
        if (!(Test-Path -LiteralPath $stageMarker -PathType Leaf)) {
            throw "Refusing to clean an unmarked staging directory: $stageRoot"
        }
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }

    Copy-DirectoryContents $validationSource $stageRoot
    Set-Content -LiteralPath $stageMarker -Value "Managed by Validation/Run-UEP58Validation.ps1" -Encoding utf8

    $pluginStage = Join-Path $stageRoot "Plugins\UnrealEnginePython"
    New-Item -ItemType Directory -Path $pluginStage -Force | Out-Null
    foreach ($directoryName in @("Source", "Config", "Resources")) {
        $sourceDirectory = Join-Path $repoRoot $directoryName
        if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
            Copy-DirectoryContents $sourceDirectory (Join-Path $pluginStage $directoryName)
        }
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot "UnrealEnginePython.uplugin") -Destination $pluginStage -Force

    $pluginHostStage = Join-Path $stageRoot "ForeignHost"
    New-Item -ItemType Directory -Path $pluginHostStage -Force | Out-Null
    $pluginHostProject = Join-Path $pluginHostStage "UEP58PluginHost.uproject"
    Copy-Item -LiteralPath $pluginHostSource -Destination $pluginHostProject -Force
    $pluginHostPlugin = Join-Path $pluginHostStage "Plugins\UnrealEnginePython"
    New-Item -ItemType Directory -Path $pluginHostPlugin -Force | Out-Null
    foreach ($directoryName in @("Source", "Config", "Resources")) {
        $sourceDirectory = Join-Path $repoRoot $directoryName
        if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
            Copy-DirectoryContents $sourceDirectory (Join-Path $pluginHostPlugin $directoryName)
        }
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot "UnrealEnginePython.uplugin") -Destination $pluginHostPlugin -Force

    $projectPath = Join-Path $stageRoot "UEP58Host.uproject"
    $coreScript = Join-Path $stageRoot "Tests\core_validation.py"
    $editorScript = Join-Path $stageRoot "Tests\editor_validation.py"
    $commonBuildArguments = @(
        "Win64",
        $Configuration,
        "-Project=$projectPath",
        "-NoHotReload",
        "-NoMutex",
        "-NoUBA",
        "-MaxParallelActions=$MaxParallelActions"
    )

    $editorBuildLog = Join-Path $resultRoot "build-editor.log"
    $editorBuildArguments = @($ubtPath, "UEP58HostEditor") + $commonBuildArguments + @("-Log=$editorBuildLog")
    Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $editorBuildArguments -Label "Build Editor" -WorkingDirectory $repoRoot -TimeoutSeconds 3600
    Assert-BuildLog $editorBuildLog
    $buildLogs = [System.Collections.Generic.List[string]]::new()
    $buildLogs.Add($editorBuildLog)

    # A source-engine project Game target is monolithic and recompiles the
    # engine. In quick mode, compile the staged plugin against UnrealGame in
    # UBT's foreign-plugin mode without loading the validation project module
    # (the module intentionally depends on UEP for packaged test startup).
    # The full path lets BuildCookRun perform the real project link once.
    if ($SkipPackage -and !$SkipRuntimeCompile) {
        $runtimeBuildLog = Join-Path $resultRoot "build-runtime-foreign.log"
        $runtimeBuildArguments = @(
            $ubtPath,
            "UnrealGame",
            "Win64",
            $Configuration,
            "-Project=$pluginHostProject",
            "-plugin=$(Join-Path $pluginHostPlugin 'UnrealEnginePython.uplugin')",
            "-NoHotReload",
            "-NoMutex",
            "-NoUBA",
            "-MaxParallelActions=$MaxParallelActions",
            "-Log=$runtimeBuildLog"
        )
        Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $runtimeBuildArguments -Label "Compile Runtime Plugin" -WorkingDirectory $repoRoot -TimeoutSeconds 3600
        Assert-BuildLog $runtimeBuildLog
        $buildLogs.Add($runtimeBuildLog)
    }

    $reports = [System.Collections.Generic.List[object]]::new()
    foreach ($mode in @("shared", "standalone")) {
        $resultPath = Join-Path $resultRoot "core-$mode.json"
        $logPath = Join-Path $resultRoot "core-$mode.log"
        $arguments = @(
            $projectPath,
            "-unattended",
            "-nop4",
            "-NullRHI",
            "-NoSound",
            "-NoSplash",
            "-UTF8Output",
            "-DisablePlugins=AndroidFileServer",
            "-run=Py",
            $coreScript,
            "-UEPValidationMode=$mode",
            "-UEPValidationResult=$resultPath",
            "-abslog=$logPath"
        )
        if ($mode -eq "standalone") {
            $arguments += "-DisablePython"
        }

        Invoke-CheckedProcess $editorCmdPath $arguments "Core $mode" $stageRoot 300
        $reports.Add((Assert-ValidationResult $resultPath $logPath))
    }

    $editorResultPath = Join-Path $resultRoot "editor.json"
    $editorLogPath = Join-Path $resultRoot "editor.log"
    $editorArguments = @(
        $projectPath,
        "-unattended",
        "-nop4",
        "-NullRHI",
        "-NoSound",
        "-NoSplash",
        "-UTF8Output",
        "-DisablePlugins=AndroidFileServer",
        "-ExecutePythonScript=$editorScript",
        "-ScriptErrorsAreFatal",
        "-UEPValidationResult=$editorResultPath",
        "-abslog=$editorLogPath"
    )
    $validationAssetPath = Join-Path $stageRoot "Content\UEPValidation\M_UEP58Validation.uasset"
    Assert-PathIsUnder -Path $validationAssetPath -Parent $stageRoot
    try {
        Invoke-CheckedProcess -FilePath $editorPath -ArgumentList $editorArguments -Label "Editor and Slate" -WorkingDirectory $stageRoot -TimeoutSeconds 600
        $reports.Add((Assert-ValidationResult $editorResultPath $editorLogPath))
    }
    finally {
        # ObjectTools::DeleteObjects performs an unbounded reference scan in
        # this UE5.8 unattended host. The asset lives only in the guarded
        # staging project, so clean up its exact file after the editor exits.
        if (Test-Path -LiteralPath $validationAssetPath -PathType Leaf) {
            Remove-Item -LiteralPath $validationAssetPath -Force
        }
    }

    $packagedExecutable = $null
    if (!$SkipPackage) {
        $packageRoot = Join-Path $resultRoot "Package"
        $packageLogRoot = Join-Path $resultRoot "package-uat"
        $packageLog = Join-Path $packageLogRoot "Log.txt"
        $packageArguments = @(
            $automationToolPath,
            "BuildCookRun",
            "-project=$projectPath",
            "-noP4",
            "-utf8output",
            "-WaitForUATMutex",
            "-unattended",
            "-platform=Win64",
            "-clientconfig=$Configuration",
            "-target=UEP58Host",
            "-build",
            "-cook",
            "-stage",
            "-pak",
            "-archive",
            "-archivedirectory=$packageRoot",
            "-map=/Engine/Maps/Entry",
            "-AdditionalCookerOptions=-DisablePlugins=AndroidFileServer -SkipZenStore",
            "-nodebuginfo"
        )
        $packageEnvironment = @{
            uebp_LogFolder = $packageLogRoot
            uebp_FinalLogFolder = $packageLogRoot
        }
        Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $packageArguments -Label "Cook and package" -WorkingDirectory $engineRootPath -TimeoutSeconds 3600 -EnvironmentVariables $packageEnvironment
        Assert-AutomationLog $packageLog
        $buildLogs.Add($packageLog)

        $packagedExecutable = Get-ChildItem -LiteralPath $packageRoot -Filter "UEP58Host.exe" -File -Recurse |
            Select-Object -First 1 -ExpandProperty FullName
        if (!$packagedExecutable) {
            throw "Packaged executable was not found under $packageRoot"
        }

        $packagedResultPath = Join-Path $resultRoot "packaged.json"
        $packagedLogPath = Join-Path $resultRoot "packaged.log"
        $packagedArguments = @(
            "-unattended",
            "-nop4",
            "-NullRHI",
            "-NoSound",
            "-NoSplash",
            "-UTF8Output",
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer",
            "-UEPValidationScript=$coreScript",
            "-UEPValidationMode=packaged",
            "-UEPValidationResult=$packagedResultPath",
            "-abslog=$packagedLogPath"
        )
        $packagedWorkingDirectory = Split-Path -Parent $packagedExecutable
        Invoke-CheckedProcess -FilePath $packagedExecutable -ArgumentList $packagedArguments -Label "Packaged runtime" -WorkingDirectory $packagedWorkingDirectory -TimeoutSeconds 300
        $reports.Add((Assert-ValidationResult $packagedResultPath $packagedLogPath "UEP_PACKAGED_VALIDATION_PASSED"))
    }

    $summary = [ordered]@{
        schema_version = 1
        status = "passed"
        run_id = $runId
        engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
        configuration = $Configuration
        package_tested = !$SkipPackage
        packaged_executable = $packagedExecutable
        stage_root = $stageRoot
        result_root = $resultRoot
        build_logs = $buildLogs
        suites = $reports
    }
    $summaryPath = Join-Path $resultRoot "summary.json"
    $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8

    Write-Host "UEP 5.8 validation passed."
    Write-Host "Summary: $summaryPath"
}
catch {
    $failurePath = Join-Path $resultRoot "failure.txt"
    $_ | Out-String | Set-Content -LiteralPath $failurePath -Encoding utf8
    Write-Error "UEP 5.8 validation failed. Details: $failurePath`n$($_.Exception.Message)"
    exit 1
}
