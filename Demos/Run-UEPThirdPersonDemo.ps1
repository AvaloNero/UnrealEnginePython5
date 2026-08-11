[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [ValidateSet("Overlay", "PythonFirst")]
    [string]$Variant = "Overlay",
    [ValidateSet("Prepare", "Audit", "Smoke", "Package", "Play", "Editor")]
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
$demoName = if ($Variant -eq "PythonFirst") { "UEPPythonThirdPerson" } else { "UEPThirdPersonDemo" }
$projectFileName = "$demoName.uproject"
$overlayRoot = Join-Path $PSScriptRoot "$demoName\Overlay"
$demoBuildRoot = Join-Path $repoRoot ".build\Demos"
$stageRoot = Join-Path $demoBuildRoot $demoName
$stageMarker = Join-Path $stageRoot ".uep-demo-stage"
$projectPath = Join-Path $stageRoot $projectFileName
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
$automationToolPath = Join-Path $engineRootPath "Engine\Binaries\DotNET\AutomationTool\AutomationTool.dll"
$editorPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor.exe"
$editorCmdPath = Join-Path $engineRootPath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

foreach ($requiredFile in @(
    $engineVersionPath,
    $dotnetPath,
    $ubtPath,
    $automationToolPath,
    $editorPath,
    $editorCmdPath,
    (Join-Path $templateRoot "TP_ThirdPersonBP.uproject"),
    (Join-Path $overlayRoot $projectFileName),
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

if ($Variant -ne "PythonFirst" -and $Mode -in @("Audit", "Package")) {
    throw "Mode $Mode is available only for the PythonFirst variant"
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
    $overlayScripts = Join-Path $overlayRoot "Content\Scripts"
    $stagedScripts = Join-Path $stageRoot "Content\Scripts"
    if (Test-Path -LiteralPath $overlayScripts -PathType Container) {
        Assert-PathIsUnder -Path $stagedScripts -Parent $stageRoot
        if (Test-Path -LiteralPath $stagedScripts -PathType Container) {
            Remove-Item -LiteralPath $stagedScripts -Recurse -Force
        }
        Copy-DirectoryContents $overlayScripts $stagedScripts
    }
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
    $editorTarget = if ($Variant -eq "PythonFirst") {
        "UEPPythonThirdPersonEditor"
    }
    else {
        "UnrealEditor"
    }
    $buildArguments = @(
        $ubtPath,
        $editorTarget,
        "Win64",
        "Development",
        "-Project=$projectPath",
        "-NoHotReload",
        "-NoMutex",
        "-NoUBA",
        "-MaxParallelActions=$MaxParallelActions",
        "-Log=$buildLog"
    )
    if ($Variant -ne "PythonFirst") {
        $buildArguments += "-plugin=$pluginDescriptor"
    }
    Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $buildArguments -Label "Build demo plugin" -WorkingDirectory $repoRoot -TimeoutSeconds 3600
    Assert-BuildLog $buildLog

    if ($Mode -eq "Audit") {
        $auditScript = Join-Path $PSScriptRoot "UEPPythonThirdPerson\Tools\audit_template.py"
        $auditResultPath = Join-Path $resultRoot "template-audit.json"
        $auditLogPath = Join-Path $resultRoot "template-audit.log"
        if (!(Test-Path -LiteralPath $auditScript -PathType Leaf)) {
            throw "Template audit script was not found: $auditScript"
        }
        $auditArguments = @(
            $projectPath,
            "-unattended",
            "-nop4",
            "-NullRHI",
            "-NoSound",
            "-NoSplash",
            "-UTF8Output",
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer",
            "-run=Py",
            $auditScript,
            "-UEPTemplateAuditResult=$auditResultPath",
            "-abslog=$auditLogPath"
        )
        Invoke-CheckedProcess -FilePath $editorCmdPath -ArgumentList $auditArguments -Label "Audit reference Blueprints" -WorkingDirectory $stageRoot -TimeoutSeconds 300
        if (!(Test-Path -LiteralPath $auditResultPath -PathType Leaf)) {
            throw "Template audit result was not created: $auditResultPath"
        }
        $auditReport = Get-Content -LiteralPath $auditResultPath -Raw | ConvertFrom-Json
        if ($auditReport.status -ne "passed" -or
            $auditReport.engine_version[0] -ne 5 -or
            $auditReport.engine_version[1] -ne 8 -or
            $auditReport.summary.blueprint_count -ne 4 -or
            $auditReport.summary.graph_count -le 0 -or
            $auditReport.summary.node_count -le 0) {
            throw "Template audit report failed its contract: $auditResultPath"
        }
        $auditLog = Get-Content -LiteralPath $auditLogPath -Raw
        if (!$auditLog.Contains("UEP_TEMPLATE_AUDIT_PASSED") -or
            $auditLog.Contains("UEP_TEMPLATE_AUDIT_FAILED") -or
            $auditLog.Contains("Fatal error:") -or
            $auditLog.Contains("Assertion failed:")) {
            throw "Template audit log failed its contract: $auditLogPath"
        }
        $summary = [ordered]@{
            schema_version = 1
            status = "passed"
            run_id = $runId
            variant = $Variant
            engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
            project = $projectPath
            build_log = $buildLog
            audit_log = $auditLogPath
            audit = $auditReport
        }
        $summaryPath = Join-Path $resultRoot "summary.json"
        $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8
        Write-Host "UEP Third Person template audit passed."
        Write-Host "Summary: $summaryPath"
    }
    elseif ($Mode -eq "Smoke") {
        $smokeResultPath = Join-Path $resultRoot "smoke.json"
        $smokeLogPath = Join-Path $resultRoot "smoke.log"
        $gameUrl = if ($Variant -eq "PythonFirst") {
            "/Game/ThirdPerson/Lvl_ThirdPerson?game=/Engine/Transient.UEPThirdPersonGameMode"
        }
        else {
            "/Game/ThirdPerson/Lvl_ThirdPerson"
        }
        $smokeResultArgument = if ($Variant -eq "PythonFirst") {
            "-UEPPythonThirdPersonSmokeResult=$smokeResultPath"
        }
        else {
            "-UEPDemoSmokeResult=$smokeResultPath"
        }
        $smokeArguments = @(
            $projectPath,
            $gameUrl,
            "-game",
            "-unattended",
            "-nop4",
            "-NullRHI",
            "-NoSound",
            "-NoSplash",
            "-UTF8Output",
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer",
            $smokeResultArgument,
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
        if ($Variant -eq "PythonFirst") {
            if ($smokeReport.status -ne "passed" -or
                $smokeReport.python_version -notlike "3.11.*" -or
                $smokeReport.game_mode_class -ne "UEPThirdPersonGameMode" -or
                $smokeReport.controller_class -ne "UEPThirdPersonPlayerController" -or
                $smokeReport.character_class -ne "UEPThirdPersonCharacter" -or
                !$smokeReport.mapping_contexts.default -or
                !$smokeReport.mapping_contexts.mouse_look -or
                $smokeReport.input_binding_count -ne 5 -or
                $smokeReport.movement_distance -le 25.0 -or
                $smokeReport.look_delta -le 0.1 -or
                !$smokeReport.jump_observed -or
                $smokeReport.input_events.jump_started -le 0 -or
                $smokeReport.input_events.jump_completed -le 0 -or
                @($smokeReport.animation_states_observed).Count -lt 4 -or
                @($smokeReport.animation_states_observed | Where-Object { $_ -in @("locomotion", "jump", "fall", "land") } | Sort-Object -Unique).Count -ne 4 -or
                !$smokeReport.camera_boom_valid -or
                !$smokeReport.follow_camera_valid -or
                $smokeReport.python_tick_count -le 0 -or
                $smokeReport.map_travel_count -ne 1) {
                throw "Python-first demo smoke report failed its contract: $smokeResultPath"
            }
        }
        elseif ($smokeReport.status -ne "passed" -or
            !$smokeReport.companion_valid -or
            $smokeReport.pickup_count -ne 6 -or
            $smokeReport.python_version -notlike "3.11.*") {
            throw "Overlay demo smoke report failed its contract: $smokeResultPath"
        }
        $smokeLog = Get-Content -LiteralPath $smokeLogPath -Raw
        $requiredMarkers = if ($Variant -eq "PythonFirst") {
            @(
                "Initialized engine CPython at",
                "UEP_PYTHON_THIRD_PERSON_SCRIPT_LOADED",
                "UEP_PYTHON_THIRD_PERSON_CLASSES_READY",
                "UEP_PYTHON_THIRD_PERSON_WORLD_READY",
                "UEP_PYTHON_THIRD_PERSON_SMOKE_PASSED"
            )
        }
        else {
            @(
                "Initialized engine CPython at",
                "UEP_DEMO_SCRIPT_LOADED",
                "UEP_DEMO_WORLD_READY",
                "UEP_DEMO_SMOKE_PASSED"
            )
        }
        foreach ($marker in $requiredMarkers) {
            if (!$smokeLog.Contains($marker)) {
                throw "Demo log does not contain '$marker': $smokeLogPath"
            }
        }
        $variantFailureMarker = if ($Variant -eq "PythonFirst") {
            "UEP_PYTHON_THIRD_PERSON_SMOKE_FAILED"
        }
        else {
            "UEP_DEMO_SMOKE_FAILED"
        }
        foreach ($failureMarker in @(
            "Fatal error:",
            "Assertion failed:",
            "Attaching UnrealEnginePython to the existing CPython interpreter",
            $variantFailureMarker
        )) {
            if ($smokeLog.Contains($failureMarker)) {
                throw "Demo log contains '$failureMarker': $smokeLogPath"
            }
        }

        $summary = [ordered]@{
            schema_version = 1
            status = "passed"
            run_id = $runId
            variant = $Variant
            engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
            project = $projectPath
            build_log = $buildLog
            smoke_log = $smokeLogPath
            smoke = $smokeReport
        }
        $summaryPath = Join-Path $resultRoot "summary.json"
        $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
        Write-Host "UEP Third Person $Variant demo smoke test passed."
        Write-Host "Summary: $summaryPath"
    }
    elseif ($Mode -eq "Package") {
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
            "-clientconfig=Development",
            "-target=UEPPythonThirdPerson",
            "-build",
            "-SkipBuildEditor",
            "-cook",
            "-stage",
            "-pak",
            "-archive",
            "-archivedirectory=$packageRoot",
            "-map=/Game/ThirdPerson/Lvl_ThirdPerson",
            "-AdditionalCookerOptions=-DisablePlugins=AndroidFileServer -SkipZenStore",
            "-nodebuginfo"
        )
        $packageEnvironment = @{
            uebp_LogFolder = $packageLogRoot
            uebp_FinalLogFolder = $packageLogRoot
        }
        Invoke-CheckedProcess -FilePath $dotnetPath -ArgumentList $packageArguments -Label "Cook and package Python-first demo" -WorkingDirectory $engineRootPath -TimeoutSeconds 3600 -EnvironmentVariables $packageEnvironment
        Assert-AutomationLog $packageLog

        $packagedExecutable = Get-ChildItem -LiteralPath $packageRoot -Filter "$demoName.exe" -File -Recurse |
            Select-Object -First 1 -ExpandProperty FullName
        if (!$packagedExecutable) {
            throw "Packaged executable was not found under $packageRoot"
        }

        $packagedResultPath = Join-Path $resultRoot "packaged-smoke.json"
        $packagedLogPath = Join-Path $resultRoot "packaged-smoke.log"
        $packagedGameUrl = "/Game/ThirdPerson/Lvl_ThirdPerson?game=/Engine/Transient.UEPThirdPersonGameMode"
        $packagedArguments = @(
            $packagedGameUrl,
            "-unattended",
            "-nop4",
            "-NullRHI",
            "-NoSound",
            "-NoSplash",
            "-UTF8Output",
            "-DisablePython",
            "-DisablePlugins=AndroidFileServer",
            "-UEPPythonThirdPersonSmokeResult=$packagedResultPath",
            "-abslog=$packagedLogPath"
        )
        Invoke-CheckedProcess -FilePath $packagedExecutable -ArgumentList $packagedArguments -Label "Run packaged Python-first demo" -WorkingDirectory (Split-Path -Parent $packagedExecutable) -TimeoutSeconds 300

        if (!(Test-Path -LiteralPath $packagedResultPath -PathType Leaf) -or
            !(Test-Path -LiteralPath $packagedLogPath -PathType Leaf)) {
            throw "Packaged demo did not create its smoke result and log"
        }
        $packagedReport = Get-Content -LiteralPath $packagedResultPath -Raw | ConvertFrom-Json
        if ($packagedReport.status -ne "passed" -or
            $packagedReport.python_version -notlike "3.11.*" -or
            $packagedReport.game_mode_class -ne "UEPThirdPersonGameMode" -or
            $packagedReport.controller_class -ne "UEPThirdPersonPlayerController" -or
            $packagedReport.character_class -ne "UEPThirdPersonCharacter" -or
            !$packagedReport.mapping_contexts.default -or
            !$packagedReport.mapping_contexts.mouse_look -or
            $packagedReport.input_binding_count -ne 5 -or
            $packagedReport.movement_distance -le 25.0 -or
            $packagedReport.look_delta -le 0.1 -or
            !$packagedReport.jump_observed -or
            $packagedReport.input_events.jump_started -le 0 -or
            $packagedReport.input_events.jump_completed -le 0 -or
            @($packagedReport.animation_states_observed).Count -lt 4 -or
            @($packagedReport.animation_states_observed | Where-Object { $_ -in @("locomotion", "jump", "fall", "land") } | Sort-Object -Unique).Count -ne 4 -or
            !$packagedReport.camera_boom_valid -or
            !$packagedReport.follow_camera_valid -or
            $packagedReport.python_tick_count -le 0 -or
            $packagedReport.map_travel_count -ne 1) {
            throw "Packaged Python-first demo report failed its contract: $packagedResultPath"
        }
        $packagedLogContents = Get-Content -LiteralPath $packagedLogPath -Raw
        foreach ($marker in @(
            "Initialized engine CPython at",
            "UEP_PYTHON_THIRD_PERSON_SCRIPT_LOADED",
            "UEP_PYTHON_THIRD_PERSON_CLASSES_READY",
            "UEP_PYTHON_THIRD_PERSON_WORLD_READY",
            "UEP_PYTHON_THIRD_PERSON_SMOKE_PASSED"
        )) {
            if (!$packagedLogContents.Contains($marker)) {
                throw "Packaged demo log does not contain '$marker': $packagedLogPath"
            }
        }
        foreach ($failureMarker in @(
            "Fatal error:",
            "Assertion failed:",
            "UEP_PYTHON_THIRD_PERSON_SMOKE_FAILED"
        )) {
            if ($packagedLogContents.Contains($failureMarker)) {
                throw "Packaged demo log contains '$failureMarker': $packagedLogPath"
            }
        }

        $summary = [ordered]@{
            schema_version = 1
            status = "passed"
            run_id = $runId
            variant = $Variant
            engine = "$($engineVersion.MajorVersion).$($engineVersion.MinorVersion).$($engineVersion.PatchVersion)"
            project = $projectPath
            build_log = $buildLog
            package_log = $packageLog
            packaged_executable = $packagedExecutable
            packaged_smoke_log = $packagedLogPath
            packaged_smoke = $packagedReport
        }
        $summaryPath = Join-Path $resultRoot "summary.json"
        $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding utf8
        Write-Host "UEP Third Person PythonFirst packaged smoke test passed."
        Write-Host "Summary: $summaryPath"
    }
    elseif ($Mode -eq "Play") {
        $gameUrl = if ($Variant -eq "PythonFirst") {
            "/Game/ThirdPerson/Lvl_ThirdPerson?game=/Engine/Transient.UEPThirdPersonGameMode"
        }
        else {
            "/Game/ThirdPerson/Lvl_ThirdPerson"
        }
        $playArguments = @(
            $projectPath,
            $gameUrl,
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
