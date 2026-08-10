[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [ValidateSet("Prepare", "Smoke", "Play", "Editor")]
    [string]$Mode = "Prepare",
    [ValidateRange(1, 64)]
    [int]$MaxParallelActions = 4,
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
        [int]$TimeoutSeconds = 0
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

function Assert-BuildLog {
    param([Parameter(Mandatory = $true)][string]$LogPath)

    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        throw "Build log was not created: $LogPath"
    }
    $contents = Get-Content -LiteralPath $LogPath -Raw
    if ($contents -notmatch "Result:\s+Succeeded") {
        throw "Build log does not contain a successful result: $LogPath"
    }
    $diagnostics = [regex]::Matches(
        $contents,
        "(?im)\b(?:warning C\d+|error C\d+|fatal error C\d+|error LNK\d+)\b"
    )
    if ($diagnostics.Count -gt 0) {
        throw "Build produced $($diagnostics.Count) compiler/linker diagnostic(s): $LogPath"
    }
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot "..")
$engineRootPath = Get-FullPath $EngineRoot
$engineVersionPath = Join-Path $engineRootPath "Engine\Build\Build.version"
$templateRoot = Join-Path $engineRootPath "Templates\TP_ThirdPersonBP"
$templateResourcesRoot = Join-Path $engineRootPath "Templates\TemplateResources\High"
$overlayRoot = Join-Path $PSScriptRoot "UEPThirdPersonDemo\Overlay"
$demoBuildRoot = Join-Path $repoRoot ".build\Demos"
$stageRoot = Join-Path $demoBuildRoot "UEPThirdPersonDemo"
$stageMarker = Join-Path $stageRoot ".uep-demo-stage"
$projectPath = Join-Path $stageRoot "UEPThirdPersonDemo.uproject"
$pluginStage = Join-Path $stageRoot "Plugins\UnrealEnginePython"
$pluginDescriptor = Join-Path $pluginStage "UnrealEnginePython.uplugin"
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$resultRoot = Join-Path $demoBuildRoot "Results\$runId"

$dotnetRoot = Join-Path $engineRootPath "Engine\Binaries\ThirdParty\DotNet"
$dotnetPath = Get-ChildItem -LiteralPath $dotnetRoot -Directory -ErrorAction Stop |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName "win-x64\dotnet.exe" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
$ubtPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
$editorPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor.exe"

foreach ($requiredFile in @(
    $engineVersionPath,
    $dotnetPath,
    $ubtPath,
    $editorPath,
    (Join-Path $templateRoot "TP_ThirdPersonBP.uproject"),
    (Join-Path $overlayRoot "UEPThirdPersonDemo.uproject"),
    (Join-Path $repoRoot "UnrealEnginePython.uplugin")
)) {
    if (!$requiredFile -or !(Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file was not found: $requiredFile"
    }
}

$engineVersion = Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
if ($engineVersion.MajorVersion -ne 5 -or $engineVersion.MinorVersion -ne 8) {
    throw "Expected Unreal Engine 5.8, found $($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
}

New-Item -ItemType Directory -Path $demoBuildRoot -Force | Out-Null
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null

try {
    if ((Test-Path -LiteralPath $stageRoot) -and !(Test-Path -LiteralPath $stageMarker -PathType Leaf)) {
        throw "Refusing to use an unmarked staging directory: $stageRoot"
    }

    if (!$Incremental -and (Test-Path -LiteralPath $stageRoot)) {
        Assert-PathIsUnder -Path $stageRoot -Parent $demoBuildRoot
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }

    $templateMap = Join-Path $stageRoot "Content\ThirdPerson\Lvl_ThirdPerson.umap"
    $sharedMesh = Join-Path $stageRoot "Content\LevelPrototyping\Meshes\SM_ChamferCube.uasset"
    $needsTemplateCopy = !(Test-Path -LiteralPath $templateMap -PathType Leaf) -or
        !(Test-Path -LiteralPath $sharedMesh -PathType Leaf)

    if ($needsTemplateCopy) {
        New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
        Copy-DirectoryContents (Join-Path $templateRoot "Config") (Join-Path $stageRoot "Config")
        Copy-DirectoryContents (Join-Path $templateRoot "Content") (Join-Path $stageRoot "Content")
        foreach ($resourceName in @("Characters", "Input", "LevelPrototyping")) {
            $resourceContent = Join-Path $templateResourcesRoot "$resourceName\Content"
            Copy-DirectoryContents $resourceContent (Join-Path $stageRoot "Content")
        }
    }

    Copy-DirectoryContents $overlayRoot $stageRoot
    Set-Content -LiteralPath $stageMarker -Value "Managed by Demos/Run-UEPThirdPersonDemo.ps1" -Encoding utf8

    New-Item -ItemType Directory -Path $pluginStage -Force | Out-Null
    foreach ($directoryName in @("Source", "Config", "Resources")) {
        $sourceDirectory = Join-Path $repoRoot $directoryName
        if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
            Copy-DirectoryContents $sourceDirectory (Join-Path $pluginStage $directoryName)
        }
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot "UnrealEnginePython.uplugin") -Destination $pluginDescriptor -Force

    $buildLog = Join-Path $resultRoot "build-editor.log"
    $buildArguments = @(
        $ubtPath,
        "UnrealEditor",
        "Win64",
        "Development",
        "-Project=$projectPath",
        "-plugin=$pluginDescriptor",
        "-NoHotReload",
        "-NoMutex",
        "-NoUBA",
        "-MaxParallelActions=$MaxParallelActions",
        "-Log=$buildLog"
    )
    Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $buildArguments -Label "Build demo plugin" -WorkingDirectory $repoRoot -TimeoutSeconds 3600
    Assert-BuildLog $buildLog

    if ($Mode -eq "Smoke") {
        $smokeResultPath = Join-Path $resultRoot "smoke.json"
        $smokeLogPath = Join-Path $resultRoot "smoke.log"
        $smokeArguments = @(
            $projectPath,
            "/Game/ThirdPerson/Lvl_ThirdPerson",
            "-game",
            "-unattended",
            "-nop4",
            "-NullRHI",
            "-NoSound",
            "-NoSplash",
            "-UTF8Output",
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer",
            "-UEPDemoSmokeResult=$smokeResultPath",
            "-abslog=$smokeLogPath"
        )
        Invoke-CheckedProcess -FilePath $editorPath -ArgumentList $smokeArguments -Label "Run demo smoke test" -WorkingDirectory $stageRoot -TimeoutSeconds 300

        if (!(Test-Path -LiteralPath $smokeResultPath -PathType Leaf)) {
            throw "Demo smoke result was not created: $smokeResultPath"
        }
        if (!(Test-Path -LiteralPath $smokeLogPath -PathType Leaf)) {
            throw "Demo smoke log was not created: $smokeLogPath"
        }
        $smokeReport = Get-Content -LiteralPath $smokeResultPath -Raw | ConvertFrom-Json
        if ($smokeReport.status -ne "passed" -or
            !$smokeReport.companion_valid -or
            $smokeReport.pickup_count -ne 6 -or
            $smokeReport.python_version -notlike "3.11.*") {
            throw "Demo smoke report failed its contract: $smokeResultPath"
        }
        $smokeLog = Get-Content -LiteralPath $smokeLogPath -Raw
        foreach ($marker in @(
            "Initialized engine CPython at",
            "UEP_DEMO_SCRIPT_LOADED",
            "UEP_DEMO_WORLD_READY",
            "UEP_DEMO_SMOKE_PASSED"
        )) {
            if (!$smokeLog.Contains($marker)) {
                throw "Demo log does not contain '$marker': $smokeLogPath"
            }
        }
        foreach ($failureMarker in @(
            "Fatal error:",
            "Assertion failed:",
            "Attaching UnrealEnginePython to the existing CPython interpreter",
            "UEP_DEMO_SMOKE_FAILED"
        )) {
            if ($smokeLog.Contains($failureMarker)) {
                throw "Demo log contains '$failureMarker': $smokeLogPath"
            }
        }

        $summary = [ordered]@{
            schema_version = 1
            status = "passed"
            run_id = $runId
            engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
            project = $projectPath
            build_log = $buildLog
            smoke_log = $smokeLogPath
            smoke = $smokeReport
        }
        $summaryPath = Join-Path $resultRoot "summary.json"
        $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
        Write-Host "UEP Third Person demo smoke test passed."
        Write-Host "Summary: $summaryPath"
    }
    elseif ($Mode -eq "Play") {
        $playArguments = @(
            $projectPath,
            "/Game/ThirdPerson/Lvl_ThirdPerson",
            "-game",
            "-windowed",
            "-ResX=1280",
            "-ResY=720",
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer",
            "-log"
        )
        $process = Start-Process -FilePath $editorPath -ArgumentList $playArguments -WorkingDirectory $stageRoot -PassThru
        Write-Host "Demo game launched (PID $($process.Id))."
    }
    elseif ($Mode -eq "Editor") {
        $process = Start-Process -FilePath $editorPath -ArgumentList @(
            $projectPath,
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer"
        ) -WorkingDirectory $stageRoot -PassThru
        Write-Host "Demo editor launched (PID $($process.Id)). Press Play to start the Python gameplay."
    }
    else {
        Write-Host "UEP Third Person demo is ready."
    }

    Write-Host "Project: $projectPath"
}
catch {
    $failurePath = Join-Path $resultRoot "failure.txt"
    $_ | Out-String | Set-Content -LiteralPath $failurePath -Encoding utf8
    Write-Error "UEP Third Person demo failed. Details: $failurePath`n$($_.Exception.Message)"
    exit 1
}
