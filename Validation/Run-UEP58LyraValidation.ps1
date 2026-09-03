[CmdletBinding()]
param(
    [string]$EngineRoot = "F:\UnrealEngine",
    [string]$LyraProject,
    [string]$OutputRoot,
    [ValidateSet("Readiness", "HUDAudit", "GenerateHUDAssets", "Standalone", "Network", "Package", "All")]
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
    [ValidateRange(0.01, 25.0)]
    [double]$GameplaySliceDamage = 10.0,
    [switch]$SkipGameplaySlice,
    [switch]$SkipHUDSlice,
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

function Assert-LyraHUDAudit {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$UnrealLogPath
    )
    if (!(Test-Path -LiteralPath $ResultPath -PathType Leaf)) {
        throw "Lyra HUD audit result was not created: $ResultPath"
    }
    if (!(Test-Path -LiteralPath $UnrealLogPath -PathType Leaf)) {
        throw "Lyra HUD audit log was not created: $UnrealLogPath"
    }

    $report = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
    if ($report.schema_version -ne 1 -or
        $report.status -ne "passed" -or
        $report.engine_version[0] -ne 5 -or
        $report.engine_version[1] -ne 8) {
        throw "Lyra HUD audit failed its status/engine contract: $ResultPath"
    }

    $expectedBlueprints = @(
        @{ role = "healthbar"; path = "/Game/UI/Hud/W_Healthbar" },
        @{ role = "default_hud_layout"; path = "/Game/UI/Hud/W_DefaultHUDLayout" },
        @{ role = "shooter_hud_layout"; path = "/ShooterCore/UserInterface/W_ShooterHUDLayout" }
    )
    foreach ($expected in $expectedBlueprints) {
        $matches = @($report.widget_blueprints | Where-Object {
            $_.role -eq $expected.role -and $_.path -eq $expected.path
        })
        if ($matches.Count -ne 1 -or
            !([string]$matches[0].generated_class) -or
            @($matches[0].graphs).Count -le 0) {
            throw "Lyra HUD audit lacks '$($expected.role)' Blueprint evidence: $ResultPath"
        }
    }
    $healthbar = @($report.widget_blueprints | Where-Object role -eq "healthbar")[0]
    foreach ($propertyName in @(
        "HealthNumber",
        "NormalizedHealth",
        "HealthOldValue",
        "HealthNewValue",
        "BarFillMID",
        "PythonMixinProfile"
    )) {
        if (@($healthbar.reflected_properties) -notcontains $propertyName) {
            throw "Lyra HUD audit healthbar lacks property '$propertyName': $ResultPath"
        }
    }
    foreach ($functionName in @(
        "Construct",
        "InitializeBarVisuals",
        "SetDynamicMaterials",
        "GetPythonMixinSet",
        "GetPythonMixinProfile"
    )) {
        if (@($healthbar.reflected_functions) -notcontains $functionName) {
            throw "Lyra HUD audit healthbar lacks function '$functionName': $ResultPath"
        }
    }
    $mixin = $healthbar.mixin
    if (@($mixin.implemented_interfaces) -notcontains "UEPPythonMixinInterface" -or
        $mixin.mixin_set_path -ne "/Game/UEPMixins/DA_LyraHealthbarMixin.DA_LyraHealthbarMixin" -or
        $mixin.mixin_set_class -ne "UEPPythonMixinSet" -or
        $mixin.selected_profile -ne "Python" -or
        $mixin.default_profile -ne "Python") {
        throw "Lyra HUD audit healthbar Mixin declaration is inconsistent: $ResultPath"
    }
    $expectedMixinProfiles = @(
        @{
            profile_name = "Python"
            python_module = "uep_lyra_hud.healthbar_mixin"
            python_class = "LyraHealthbarPythonProfile"
        },
        @{
            profile_name = "BlueprintFallback"
            python_module = "uep_lyra_hud.healthbar_mixin"
            python_class = "LyraHealthbarBlueprintFallbackProfile"
        }
    )
    if (@($mixin.profiles).Count -ne $expectedMixinProfiles.Count) {
        throw "Lyra HUD audit healthbar Mixin profile count is inconsistent: $ResultPath"
    }
    foreach ($expectedProfile in $expectedMixinProfiles) {
        $profiles = @($mixin.profiles | Where-Object {
            $_.profile_name -eq $expectedProfile.profile_name -and
            $_.python_module -eq $expectedProfile.python_module -and
            $_.python_class -eq $expectedProfile.python_class
        })
        if ($profiles.Count -ne 1) {
            throw "Lyra HUD audit lacks Mixin profile '$($expectedProfile.profile_name)': $ResultPath"
        }
    }
    $actionSet = @($report.data_assets | Where-Object {
        $_.role -eq "shooter_hud_action_set" -and
        $_.path -eq "/ShooterCore/Experiences/LAS_ShooterGame_StandardHUD"
    })
    if ($actionSet.Count -ne 1 -or
        $actionSet[0].asset_class -ne "LyraExperienceActionSet" -or
        $actionSet[0].action_count -ne 1 -or
        @($actionSet[0].game_features_to_enable) -notcontains "ShooterCore") {
        throw "Lyra HUD audit lacks the Shooter HUD action-set evidence: $ResultPath"
    }
    $addWidgetsActions = @($actionSet[0].actions | Where-Object {
        $_.class -eq "GameFeatureAction_AddWidgets" -and
        $_.layout_entry_count -eq 1 -and
        $_.widget_entry_count -eq 11
    })
    if ($addWidgetsActions.Count -ne 1) {
        throw "Lyra HUD audit Shooter action set lacks the expected Add Widgets layout: $ResultPath"
    }

    $logContents = Get-Content -LiteralPath $UnrealLogPath -Raw
    if (!$logContents.Contains("UEP_LYRA_HUD_AUDIT_PASSED") -or
        $logContents.Contains("UEP_LYRA_HUD_AUDIT_FAILED") -or
        $logContents.Contains("Fatal error:") -or
        $logContents.Contains("Assertion failed:") -or
        $logContents.Contains("Unhandled Exception:")) {
        throw "Lyra HUD audit log failed its contract: $UnrealLogPath"
    }
    return $report
}

function Assert-LyraRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$UnrealLogPath,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][string[]]$ExpectedActiveFeatures,
        [Parameter(Mandatory = $true)][string[]]$ExpectedRegisteredFeatures,
        [string]$ExpectedExperienceContains = "",
        [switch]$ExpectGameplaySlice,
        [switch]$ExpectHUDSlice,
        [double]$ExpectedGameplayDamage = 10.0
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

    if ($ExpectGameplaySlice) {
        if (!$report.PSObject.Properties["gameplay_slice"]) {
            throw "Lyra $ExpectedMode report lacks gameplay-slice evidence: $ResultPath"
        }
        $gameplay = $report.gameplay_slice
        if (!$gameplay.enabled -or !$gameplay.completed -or $gameplay.state -ne "completed") {
            throw "Lyra $ExpectedMode gameplay slice did not complete: $ResultPath"
        }
        if (!$report.snapshot.health_ready -or $report.snapshot.damage_immune -or
            [math]::Abs([double]$report.snapshot.health - [double]$gameplay.baseline_health) -gt 0.05 -or
            [math]::Abs([double]$gameplay.restored_health - [double]$gameplay.baseline_health) -gt 0.05 -or
            [math]::Abs(([double]$gameplay.baseline_health - [double]$gameplay.observed_damaged_health) - $ExpectedGameplayDamage) -gt 0.05) {
            throw "Lyra $ExpectedMode gameplay health evidence is inconsistent: $ResultPath"
        }

        foreach ($commandName in @("damage_command", "heal_command")) {
            $command = $gameplay.$commandName
            if (!$command -or $command.status -ne "Applied" -or !$command.accepted -or !$command.applied -or
                !$command.server_authority -or !([string]$command.target_actor)) {
                throw "Lyra $ExpectedMode gameplay command '$commandName' was not authority-applied: $ResultPath"
            }
        }
        if ([math]::Abs([double]$gameplay.damage_command.requested_health_delta + $ExpectedGameplayDamage) -gt 0.001 -or
            [math]::Abs([double]$gameplay.heal_command.requested_health_delta - $ExpectedGameplayDamage) -gt 0.001) {
            throw "Lyra $ExpectedMode gameplay command magnitudes are inconsistent: $ResultPath"
        }
        if (!$gameplay.duplicate_command -or
            $gameplay.duplicate_command.status -ne "RejectedDuplicateCommand" -or
            $gameplay.duplicate_command.accepted -or $gameplay.duplicate_command.applied) {
            throw "Lyra $ExpectedMode duplicate gameplay command was not rejected: $ResultPath"
        }
        if ($ExpectedMode -in @("server", "client")) {
            $rejection = $gameplay.client_authority_rejection
            if (!$rejection -or $rejection.status -ne "RejectedNotAuthority" -or
                $rejection.accepted -or $rejection.applied -or $rejection.server_authority) {
                throw "Lyra $ExpectedMode lacks client authority-rejection evidence: $ResultPath"
            }
        }
    }

    if ($ExpectHUDSlice) {
        if ($report.schema_version -lt 3 -or
            !$report.PSObject.Properties["hud"] -or
            !$report.PSObject.Properties["hud_lifecycle"]) {
            throw "Lyra $ExpectedMode report lacks 0.8 Python HUD evidence: $ResultPath"
        }
        $hud = $report.hud
        if (!$hud.registered -or !$hud.source_asset_available -or @($hud.errors).Count -ne 0 -or
            !$report.hud_lifecycle.enabled -or !$report.hud_lifecycle.completed) {
            throw "Lyra $ExpectedMode Python HUD registration/lifecycle failed: $ResultPath"
        }
        $hudLifecycle = $report.hud_lifecycle
        if (!$hudLifecycle.respawn.requested -or !$hudLifecycle.respawn.observed -or
            !$hudLifecycle.game_feature.deactivated -or !$hudLifecycle.game_feature.reactivated -or
            [int]$hudLifecycle.game_feature.bridge_generation_after -le [int]$hudLifecycle.game_feature.bridge_generation_before -or
            !$hudLifecycle.travel.requested -or !$hudLifecycle.travel.observed -or
            [int]$hudLifecycle.travel.bridge_generation_after -le [int]$hudLifecycle.travel.bridge_generation_before) {
            throw "Lyra $ExpectedMode lacks respawn/GameFeature/travel HUD evidence: $ResultPath"
        }
        if ($ExpectedMode -in @("server", "server_exit")) {
            if ([int]$hud.active_widget_count -ne 0 -or
                [int]$hud.construct_count -ne 0 -or
                [int]$hud.controller_bind_count -ne 0 -or
                [int]$hud.health_bind_count -ne 0 -or
                @($hud.health_events).Count -ne 0) {
                throw "Lyra $ExpectedMode created dedicated-server HUD state: $ResultPath"
            }
        }
        else {
            if ([int]$hud.active_widget_count -lt 1 -or
                [int]$hud.construct_count -lt 3 -or
                [int]$hud.destruct_count -lt 2 -or
                ([int]$hud.controller_bind_count - [int]$hud.controller_unbind_count) -ne [int]$hud.active_widget_count -or
                ([int]$hud.health_bind_count - [int]$hud.health_unbind_count) -ne [int]$hud.active_widget_count) {
                throw "Lyra $ExpectedMode Python HUD delegate balance failed: $ResultPath"
            }
            $finalHealthEvents = @($hud.health_events | Where-Object {
                [math]::Abs([double]$_.health - [double]$report.snapshot.health) -le 0.05
            })
            if ($finalHealthEvents.Count -lt 1) {
                throw "Lyra $ExpectedMode Python HUD did not render final health: $ResultPath"
            }
            if ($ExpectGameplaySlice) {
                $damagedEvents = @($hud.health_events | Where-Object {
                    $_.event -eq "health_changed" -and
                    [math]::Abs([double]$_.health - [double]$report.gameplay_slice.expected_damaged_health) -le 0.05
                })
                $restoredEvents = @($hud.health_events | Where-Object {
                    $_.event -eq "health_changed" -and
                    [math]::Abs([double]$_.health - [double]$report.gameplay_slice.baseline_health) -le 0.05
                })
                if ($damagedEvents.Count -lt 1 -or $restoredEvents.Count -lt 1) {
                    throw "Lyra $ExpectedMode Python HUD lacks 100-90-100 event evidence: $ResultPath"
                }
            }
        }
    }

    $logContents = Get-Content -LiteralPath $UnrealLogPath -Raw
    $requiredLogMarkers = [System.Collections.Generic.List[string]]::new()
    $requiredLogMarkers.AddRange([string[]]@(
        "Initialized engine CPython at",
        "UEP_LYRA_SCRIPT_LOADED",
        "UEP_LYRA_BRIDGE_ATTACHED",
        "UEP_LYRA_SMOKE_PASSED",
        "Object subsystem successfully closed",
        "Goodbye Python",
        "Log file closed"
    ))
    if ($ExpectGameplaySlice) {
        $requiredLogMarkers.Add("UEP_LYRA_GAMEPLAY_SLICE_PASSED")
    }
    if ($ExpectHUDSlice) {
        $requiredLogMarkers.Add("UEP_LYRA_HUD_INTERFACE_REGISTERED")
        $requiredLogMarkers.Add("UEP_LYRA_PYTHON_HUD_PASSED")
        $requiredLogMarkers.Add("UEP_LYRA_HUD_RESPAWN_PASSED")
        $requiredLogMarkers.Add("UEP_LYRA_HUD_GAME_FEATURE_CYCLE_PASSED")
        $requiredLogMarkers.Add("UEP_LYRA_HUD_TRAVEL_PASSED")
        $requiredLogMarkers.Add("UEP_LYRA_HUD_LIFECYCLE_PROBE_PASSED")
        if ($ExpectedMode -notin @("server", "server_exit")) {
            $requiredLogMarkers.Add("UEP_LYRA_PYTHON_HEALTHBAR_CONSTRUCTED")
            $requiredLogMarkers.Add("UEP_LYRA_HUD_HEALTH")
        }
        elseif ($logContents.Contains("UEP_LYRA_PYTHON_HEALTHBAR_CONSTRUCTED")) {
            throw "Lyra $ExpectedMode log shows a dedicated-server HUD instance: $UnrealLogPath"
        }
    }
    foreach ($marker in $requiredLogMarkers) {
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
        [string]$ReadyReleaseFile = "",
        [switch]$GameplaySlice,
        [switch]$HUDSlice,
        [string]$HUDTravelURL = "",
        [string]$GameplaySyncDir = "",
        [double]$GameplayDamage = 10.0
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
    if ($GameplaySlice) {
        $arguments.Add("-UEPLyraGameplaySlice")
        $arguments.Add("-UEPLyraGameplayDamage=$GameplayDamage")
    }
    if ($GameplaySyncDir) {
        $arguments.Add("-UEPLyraGameplaySyncDir=$GameplaySyncDir")
    }
    if ($HUDSlice) {
        $arguments.Add("-UEPLyraHUDSlice")
        if ($HUDTravelURL) {
            $arguments.Add("-UEPLyraHUDTravelURL=$HUDTravelURL")
        }
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
$gameplaySliceEnabled = !$SkipGameplaySlice.IsPresent -and $Mode -notin @("Readiness", "HUDAudit", "GenerateHUDAssets")
$hudSliceEnabled = !$SkipHUDSlice.IsPresent -and $Mode -notin @("Readiness", "HUDAudit", "GenerateHUDAssets")
$gameplayRuntimeMap = if ($gameplaySliceEnabled) { "${GameplayMap}?NumBots=3" } else { $GameplayMap }
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null

$summary = [ordered]@{
    schema_version = 3
    status = "failed"
    run_id = $runId
    mode = $Mode
    engine = $null
    python = $null
    lyra_project = $lyraProjectPath
    stage_project = $null
    stage_runtime_disabled_plugins = @()
    gameplay_slice_enabled = $gameplaySliceEnabled
    gameplay_slice_damage = if ($gameplaySliceEnabled) { $GameplaySliceDamage } else { $null }
    hud_slice_enabled = $hudSliceEnabled
    package_output_root = if ($Mode -in @("Package", "All")) { $packageRoot } else { $null }
    readiness = $null
    hud_host_tests = $null
    editor_build = $null
    asset_registry_prime = $null
    hud_audit = $null
    hud_asset_generation = $null
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

    $hudHostTestsRoot = Join-Path $repoRoot "Demos\UEPLyraIntegration\Tests"
    $hudHostTestsLog = Join-Path $resultRoot "hud-host-tests.log"
    if (!(Test-Path -LiteralPath $hudHostTestsRoot -PathType Container)) {
        throw "Lyra HUD host tests were not found: $hudHostTestsRoot"
    }
    $summary.hud_host_tests = Invoke-LoggedProcess `
        -FilePath $pythonPath `
        -ArgumentList @("-B", "-m", "unittest", "discover", "-s", $hudHostTestsRoot, "-p", "test_*.py", "-v") `
        -WorkingDirectory $repoRoot `
        -LogPath $hudHostTestsLog `
        -Label "Lyra HUD presenter host tests" `
        -TimeoutSeconds 60

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
        $lyraOverlayContent = Join-Path $repoRoot "Demos\UEPLyraIntegration\Overlay\Content"
        $stagedContent = Join-Path $stageRoot "Content"
        if ($Mode -eq "GenerateHUDAssets") {
            # Asset authoring must always begin from the unchanged Lyra asset,
            # not from a previously generated overlay retained by an
            # incremental stage. Only Scripts are needed while the commandlet
            # creates the two overlay-owned assets below.
            Copy-FilteredTree -Source (Join-Path $lyraOverlayContent "Scripts") -Destination (Join-Path $stagedContent "Scripts") -ExcludedSegments @("__pycache__")
            $referenceHealthbar = Join-Path $lyraRoot "Content\UI\Hud\W_Healthbar.uasset"
            $stagedHealthbar = Join-Path $stagedContent "UI\Hud\W_Healthbar.uasset"
            $stagedMixinAsset = Join-Path $stagedContent "UEPMixins\DA_LyraHealthbarMixin.uasset"
            Assert-PathIsUnder -Path $stagedHealthbar -Parent $stageRoot
            Assert-PathIsUnder -Path $stagedMixinAsset -Parent $stageRoot
            New-Item -ItemType Directory -Path (Split-Path -Parent $stagedHealthbar) -Force | Out-Null
            Copy-Item -LiteralPath $referenceHealthbar -Destination $stagedHealthbar -Force
            if (Test-Path -LiteralPath $stagedMixinAsset -PathType Leaf) {
                Remove-Item -LiteralPath $stagedMixinAsset -Force
            }
        }
        else {
            Copy-FilteredTree -Source $lyraOverlayContent -Destination $stagedContent -ExcludedSegments @("__pycache__")
        }

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

        if ($Mode -in @("Standalone", "Network", "Package", "All")) {
            # A fresh full-content stage has no CachedAssetRegistry. Starting
            # two Editor -game processes while both perform Lyra's uncached
            # initial scan can stall the client before it services the network
            # handshake. Prime one read-only cache in an Editor commandlet;
            # -Multiprocess runtime processes then share it without writing.
            $assetRegistryPrimeScript = Join-Path $repoRoot "Demos\UEPLyraIntegration\Tools\prime_asset_registry.py"
            $assetRegistryPrimeResult = Join-Path $resultRoot "asset-registry-prime.json"
            $assetRegistryPrimeStdout = Join-Path $resultRoot "asset-registry-prime-stdout.log"
            $assetRegistryPrimeLog = Join-Path $resultRoot "asset-registry-prime.log"
            if (!(Test-Path -LiteralPath $assetRegistryPrimeScript -PathType Leaf)) {
                throw "Lyra asset-registry prime script was not found: $assetRegistryPrimeScript"
            }
            $assetRegistryPrimeArguments = @(
                $stageProject,
                "-unattended",
                "-nop4",
                "-NullRHI",
                "-NoSound",
                "-NoSplash",
                "-culture=en",
                "-UTF8Output",
                "-DisablePython",
                "-DisablePlugins=AndroidFileServer,UdpMessaging,TcpMessaging",
                # Ordinary commandlets do not search the complete registry at
                # startup. Force that supported UE path so wait_for_assets
                # observes real work and the editor gatherer can persist both
                # caches before this one process exits.
                "-AssetGatherAll=true",
                "-AssetRegistryDiscoveryCache=AlwaysWrite",
                "-notraceserver",
                "-stdout",
                "-FullStdOutLogOutput",
                "-run=Py",
                $assetRegistryPrimeScript,
                "-UEPLyraAssetRegistryPrimeResult=$assetRegistryPrimeResult",
                "-abslog=$assetRegistryPrimeLog"
            )
            $assetRegistryPrimeProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $assetRegistryPrimeArguments -WorkingDirectory $stageRoot -LogPath $assetRegistryPrimeStdout -Label "Prime staged Lyra asset registry" -TimeoutSeconds 600
            if (!(Test-Path -LiteralPath $assetRegistryPrimeResult -PathType Leaf)) {
                throw "Lyra asset-registry prime result was not created: $assetRegistryPrimeResult"
            }
            $assetRegistryPrimeReport = Get-Content -LiteralPath $assetRegistryPrimeResult -Raw | ConvertFrom-Json
            if ($assetRegistryPrimeReport.schema_version -ne 1 -or
                $assetRegistryPrimeReport.status -ne "passed" -or
                $assetRegistryPrimeReport.engine_version[0] -ne 5 -or
                $assetRegistryPrimeReport.engine_version[1] -ne 8 -or
                $assetRegistryPrimeReport.loading_after_wait) {
                throw "Lyra asset-registry prime report failed its contract: $assetRegistryPrimeResult"
            }
            $assetRegistryPrimeLogContents = Get-Content -LiteralPath $assetRegistryPrimeLog -Raw
            if (!$assetRegistryPrimeLogContents.Contains("UEP_LYRA_ASSET_REGISTRY_PRIME_PASSED") -or
                $assetRegistryPrimeLogContents.Contains("UEP_LYRA_ASSET_REGISTRY_PRIME_FAILED") -or
                $assetRegistryPrimeLogContents.Contains("Fatal error:") -or
                $assetRegistryPrimeLogContents.Contains("Assertion failed:") -or
                $assetRegistryPrimeLogContents.Contains("Unhandled Exception:")) {
                throw "Lyra asset-registry prime log failed its contract: $assetRegistryPrimeLog"
            }
            $assetRegistryDiscoveryCache = Join-Path $stageRoot "Intermediate\CachedAssetRegistryDiscovery.bin"
            $assetRegistryCacheRoot = Join-Path $stageRoot "Intermediate\CachedAssetRegistry"
            $assetRegistryCacheFiles = @(
                Get-ChildItem -LiteralPath $assetRegistryCacheRoot -Filter "CachedAssetRegistry_*.bin" -File -ErrorAction SilentlyContinue
            )
            if (!(Test-Path -LiteralPath $assetRegistryDiscoveryCache -PathType Leaf) -or
                $assetRegistryCacheFiles.Count -lt 1) {
                throw "Lyra asset-registry prime did not persist both discovery and asset caches"
            }
            $summary.asset_registry_prime = [ordered]@{
                process = $assetRegistryPrimeProcess
                report = $assetRegistryPrimeResult
                unreal_log = $assetRegistryPrimeLog
                discovery_cache = $assetRegistryDiscoveryCache
                discovery_cache_bytes = (Get-Item -LiteralPath $assetRegistryDiscoveryCache).Length
                asset_cache_files = @($assetRegistryCacheFiles | ForEach-Object {
                    [ordered]@{ path = $_.FullName; bytes = $_.Length }
                })
            }
        }

        if ($Mode -eq "GenerateHUDAssets") {
            $hudGeneratorScript = Join-Path $repoRoot "Demos\UEPLyraIntegration\Tools\generate_hud_mixin_assets.py"
            $hudGeneratorResult = Join-Path $resultRoot "hud-mixin-assets.json"
            $hudGeneratorStdout = Join-Path $resultRoot "hud-mixin-assets-stdout.log"
            $hudGeneratorLog = Join-Path $resultRoot "hud-mixin-assets.log"
            if (!(Test-Path -LiteralPath $hudGeneratorScript -PathType Leaf)) {
                throw "Lyra HUD Mixin asset generator was not found: $hudGeneratorScript"
            }

            $referenceHealthbar = Join-Path $lyraRoot "Content\UI\Hud\W_Healthbar.uasset"
            if (!(Test-Path -LiteralPath $referenceHealthbar -PathType Leaf)) {
                throw "Reference Lyra healthbar was not found: $referenceHealthbar"
            }
            $referenceHashBefore = (Get-FileHash -LiteralPath $referenceHealthbar -Algorithm SHA256).Hash
            $hudGeneratorArguments = @(
                $stageProject,
                "-unattended",
                "-nop4",
                "-NullRHI",
                "-NoSound",
                "-NoSplash",
                "-culture=en",
                "-UTF8Output",
                "-DisablePython",
                "-DisablePlugins=AndroidFileServer,UdpMessaging,TcpMessaging",
                "-notraceserver",
                "-stdout",
                "-FullStdOutLogOutput",
                "-run=Py",
                $hudGeneratorScript,
                "-UEPGenerateLyraHUDMixinAssetsResult=$hudGeneratorResult",
                "-abslog=$hudGeneratorLog"
            )
            $hudGeneratorProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $hudGeneratorArguments -WorkingDirectory $stageRoot -LogPath $hudGeneratorStdout -Label "Generate staged Lyra HUD Mixin assets" -TimeoutSeconds 300
            if (!(Test-Path -LiteralPath $hudGeneratorResult -PathType Leaf)) {
                throw "Lyra HUD Mixin asset generation result was not created: $hudGeneratorResult"
            }
            $hudGeneratorReport = Get-Content -LiteralPath $hudGeneratorResult -Raw | ConvertFrom-Json
            if ($hudGeneratorReport.schema_version -ne 1 -or
                $hudGeneratorReport.status -ne "passed" -or
                $hudGeneratorReport.healthbar.path -ne "/Game/UI/Hud/W_Healthbar" -or
                $hudGeneratorReport.healthbar.generated_class -ne "W_Healthbar_C" -or
                $hudGeneratorReport.healthbar.mixin_set -ne "DA_LyraHealthbarMixin" -or
                $hudGeneratorReport.healthbar.default_profile -ne "Python" -or
                $hudGeneratorReport.healthbar.profile_count -ne 2 -or
                $hudGeneratorReport.healthbar.profile_variable -ne "PythonMixinProfile" -or
                $hudGeneratorReport.healthbar.profile_value -ne "Python") {
                throw "Lyra HUD Mixin asset generation report failed its contract: $hudGeneratorResult"
            }
            $hudGeneratorLogContents = Get-Content -LiteralPath $hudGeneratorLog -Raw
            if (!$hudGeneratorLogContents.Contains("UEP_LYRA_HUD_MIXIN_ASSET_GENERATION_PASSED") -or
                $hudGeneratorLogContents.Contains("UEP_LYRA_HUD_MIXIN_ASSET_GENERATION_FAILED") -or
                $hudGeneratorLogContents.Contains("Fatal error:") -or
                $hudGeneratorLogContents.Contains("Assertion failed:") -or
                $hudGeneratorLogContents.Contains("Unhandled Exception:")) {
                throw "Lyra HUD Mixin asset generation log failed its contract: $hudGeneratorLog"
            }

            $overlayRoot = Join-Path $repoRoot "Demos\UEPLyraIntegration\Overlay"
            $assetMappings = @(
                @{
                    source = "Content\UI\Hud\W_Healthbar.uasset"
                    destination = "Content\UI\Hud\W_Healthbar.uasset"
                },
                @{
                    source = "Content\UEPMixins\DA_LyraHealthbarMixin.uasset"
                    destination = "Content\UEPMixins\DA_LyraHealthbarMixin.uasset"
                }
            )
            $copiedAssets = @()
            foreach ($assetMapping in $assetMappings) {
                $sourceAsset = Join-Path $stageRoot $assetMapping.source
                $destinationAsset = Join-Path $overlayRoot $assetMapping.destination
                Assert-PathIsUnder -Path $sourceAsset -Parent $stageRoot
                Assert-PathIsUnder -Path $destinationAsset -Parent $overlayRoot
                if (!(Test-Path -LiteralPath $sourceAsset -PathType Leaf)) {
                    throw "Generated Lyra HUD Mixin asset was not found: $sourceAsset"
                }
                New-Item -ItemType Directory -Path (Split-Path -Parent $destinationAsset) -Force | Out-Null
                Copy-Item -LiteralPath $sourceAsset -Destination $destinationAsset -Force
                $copiedAssets += [ordered]@{
                    path = $destinationAsset
                    sha256 = (Get-FileHash -LiteralPath $destinationAsset -Algorithm SHA256).Hash
                }
            }

            $referenceHashAfter = (Get-FileHash -LiteralPath $referenceHealthbar -Algorithm SHA256).Hash
            if ($referenceHashAfter -ne $referenceHashBefore) {
                throw "Reference Lyra healthbar changed during staged generation: $referenceHealthbar"
            }
            $summary.hud_asset_generation = [ordered]@{
                process = $hudGeneratorProcess
                report = $hudGeneratorResult
                unreal_log = $hudGeneratorLog
                copied_assets = $copiedAssets
                reference_healthbar = $referenceHealthbar
                reference_healthbar_sha256 = $referenceHashAfter
            }
        }

        if ($Mode -eq "HUDAudit") {
            $hudAuditScript = Join-Path $repoRoot "Demos\UEPLyraIntegration\Tools\audit_hud.py"
            $hudAuditResult = Join-Path $resultRoot "hud-audit.json"
            $hudAuditStdout = Join-Path $resultRoot "hud-audit-stdout.log"
            $hudAuditLog = Join-Path $resultRoot "hud-audit.log"
            if (!(Test-Path -LiteralPath $hudAuditScript -PathType Leaf)) {
                throw "Lyra HUD audit script was not found: $hudAuditScript"
            }
            $hudAuditArguments = @(
                $stageProject,
                "-unattended",
                "-nop4",
                "-NullRHI",
                "-NoSound",
                "-NoSplash",
                "-culture=en",
                "-UTF8Output",
                "-DisablePython",
                "-DisablePlugins=AndroidFileServer,UdpMessaging,TcpMessaging",
                "-notraceserver",
                "-stdout",
                "-FullStdOutLogOutput",
                "-run=Py",
                $hudAuditScript,
                "-UEPLyraHUDAuditResult=$hudAuditResult",
                "-abslog=$hudAuditLog"
            )
            $hudAuditProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $hudAuditArguments -WorkingDirectory $stageRoot -LogPath $hudAuditStdout -Label "Lyra HUD read-only audit" -TimeoutSeconds 300
            $hudAuditReport = Assert-LyraHUDAudit -ResultPath $hudAuditResult -UnrealLogPath $hudAuditLog
            $summary.hud_audit = [ordered]@{
                process = $hudAuditProcess
                report = $hudAuditResult
                unreal_log = $hudAuditLog
                healthbar = @($hudAuditReport.widget_blueprints | Where-Object role -eq "healthbar")[0]
            }
        }

        if ($Mode -in @("Standalone", "All")) {
            $standaloneResult = Join-Path $resultRoot "standalone.json"
            $standaloneStdout = Join-Path $resultRoot "standalone-stdout.log"
            $standaloneLog = Join-Path $resultRoot "standalone.log"
            $standaloneArguments = @($stageProject, $gameplayRuntimeMap, "-game") +
                (Get-LyraRuntimeArguments -TargetMode "standalone" -ResultPath $standaloneResult -UnrealLogPath $standaloneLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience -GameplaySlice:$gameplaySliceEnabled -HUDSlice:$hudSliceEnabled -HUDTravelURL $gameplayRuntimeMap -GameplayDamage $GameplaySliceDamage)
            $standaloneProcess = Invoke-LoggedProcess -FilePath $editorPath -ArgumentList $standaloneArguments -WorkingDirectory $stageRoot -LogPath $standaloneStdout -Label "Lyra standalone gameplay" -TimeoutSeconds ($RuntimeTimeoutSeconds + 120)
            $standaloneReport = Assert-LyraRuntime -ResultPath $standaloneResult -UnrealLogPath $standaloneLog -ExpectedMode "standalone" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience -ExpectGameplaySlice:$gameplaySliceEnabled -ExpectHUDSlice:$hudSliceEnabled -ExpectedGameplayDamage $GameplaySliceDamage
            $summary.standalone = [ordered]@{ process = $standaloneProcess; report = $standaloneResult; unreal_log = $standaloneLog; snapshot = $standaloneReport.snapshot; gameplay_slice = $standaloneReport.gameplay_slice; hud = $standaloneReport.hud; hud_lifecycle = $standaloneReport.hud_lifecycle }
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
            $gameplaySyncDir = Join-Path $resultRoot "gameplay-sync"
            if ($gameplaySliceEnabled -or $hudSliceEnabled) {
                New-Item -ItemType Directory -Path $gameplaySyncDir -Force | Out-Null
            }
            $serverArguments = @($stageProject, $gameplayRuntimeMap, "-server", "-port=$ServerPort", "-multihome=127.0.0.1", "-Multiprocess") +
                (Get-LyraRuntimeArguments -TargetMode "server" -ResultPath $serverResult -UnrealLogPath $serverLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience -ReadyReleaseFile $networkReleaseFile -GameplaySlice:$gameplaySliceEnabled -HUDSlice:$hudSliceEnabled -HUDTravelURL $gameplayRuntimeMap -GameplaySyncDir $gameplaySyncDir -GameplayDamage $GameplaySliceDamage)
            $clientArguments = @($stageProject, "127.0.0.1:$ServerPort", "-game", "-Multiprocess") +
                (Get-LyraRuntimeArguments -TargetMode "client" -ResultPath $clientResult -UnrealLogPath $clientLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience -ReadyReleaseFile $networkReleaseFile -GameplaySlice:$gameplaySliceEnabled -HUDSlice:$hudSliceEnabled -HUDTravelURL $gameplayRuntimeMap -GameplaySyncDir $gameplaySyncDir -GameplayDamage $GameplaySliceDamage)
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
            $clientReport = Assert-LyraRuntime -ResultPath $clientResult -UnrealLogPath $clientLog -ExpectedMode "client" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience -ExpectGameplaySlice:$gameplaySliceEnabled -ExpectHUDSlice:$hudSliceEnabled -ExpectedGameplayDamage $GameplaySliceDamage
            $serverReport = Assert-LyraRuntime -ResultPath $serverResult -UnrealLogPath $serverLog -ExpectedMode "server" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience -ExpectGameplaySlice:$gameplaySliceEnabled -ExpectHUDSlice:$hudSliceEnabled -ExpectedGameplayDamage $GameplaySliceDamage
            $summary.network = [ordered]@{
                port = $ServerPort
                release_signal = $networkReleaseFile
                runtime_sync = if ($gameplaySliceEnabled -or $hudSliceEnabled) { $gameplaySyncDir } else { $null }
                gameplay_sync = if ($gameplaySliceEnabled) { $gameplaySyncDir } else { $null }
                hud_sync = if ($hudSliceEnabled) { $gameplaySyncDir } else { $null }
                server = [ordered]@{ process = $serverProcess; report = $serverResult; unreal_log = $serverLog; snapshot = $serverReport.snapshot; gameplay_slice = $serverReport.gameplay_slice; hud = $serverReport.hud; hud_lifecycle = $serverReport.hud_lifecycle }
                client = [ordered]@{ process = $clientProcess; report = $clientResult; unreal_log = $clientLog; snapshot = $clientReport.snapshot; gameplay_slice = $clientReport.gameplay_slice; hud = $clientReport.hud; hud_lifecycle = $clientReport.hud_lifecycle }
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
                $gameplayRuntimeMap
            ) +
                (Get-LyraRuntimeArguments -TargetMode "packaged" -ResultPath $packagedResult -UnrealLogPath $packagedLog -ExpectedControllers 1 -ActiveFeatures $RequiredActiveGameFeatures -RegisteredFeatures $RequiredRegisteredGameFeatures -TimeoutSeconds $RuntimeTimeoutSeconds -Experience $ExpectedExperience -GameplaySlice:$gameplaySliceEnabled -HUDSlice:$hudSliceEnabled -HUDTravelURL $gameplayRuntimeMap -GameplayDamage $GameplaySliceDamage)
            $packagedRuntime = Invoke-LoggedProcess -FilePath $packagedNetworkExecutable -ArgumentList $packagedArguments -WorkingDirectory $packagedNetworkDirectory -LogPath $packagedStdout -Label "Packaged Lyra gameplay" -TimeoutSeconds ($RuntimeTimeoutSeconds + 120)
            $packagedReport = Assert-LyraRuntime -ResultPath $packagedResult -UnrealLogPath $packagedLog -ExpectedMode "packaged" -ExpectedActiveFeatures $RequiredActiveGameFeatures -ExpectedRegisteredFeatures $RequiredRegisteredGameFeatures -ExpectedExperienceContains $ExpectedExperience -ExpectGameplaySlice:$gameplaySliceEnabled -ExpectHUDSlice:$hudSliceEnabled -ExpectedGameplayDamage $GameplaySliceDamage
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
                gameplay_slice = $packagedReport.gameplay_slice
                hud = $packagedReport.hud
                hud_lifecycle = $packagedReport.hud_lifecycle
            }
        }

        $summary.status = "passed"
        $summary.full_acceptance = $Mode -eq "All" -and $gameplaySliceEnabled -and $hudSliceEnabled
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
