<#
.SYNOPSIS
  Local test suite for the Windows installer, run ONLY against isolated portable OBS copies in
  test-env\installer\obs-a and obs-b (copies of test-env\obs without the plugin). The real OBS
  folder is never targeted; its plugin files are hashed (read-only) before and after.

  Outside the workspace the installer itself writes (and the final uninstall removes):
    HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{9BF09CF6-FFE4-4C45-8478-282689049548}_is1
    %APPDATA%\Multi-Stream Stats\uninstall\
  plus Inno Setup's short-lived temp folders in %TEMP%. This script reports any leftovers.

  Results: test-env\results\installer\summary.txt (+ setup logs, scenario results).
#>
param(
    [string]$Installer = '',
    [string]$RealObsRoot = "$env:USERPROFILE\Documents\OBS Studio"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -Raw (Join-Path $root 'buildspec.json') | ConvertFrom-Json).version
if (-not $Installer) { $Installer = Join-Path $root "artifacts\$version\windows\obs-multistream-stats-$version-windows-x64-installer.exe" }
$Installer = (Resolve-Path $Installer).Path
$obsA = Join-Path $root 'test-env\installer\obs-a'
$obsB = Join-Path $root 'test-env\installer\obs-b'
$out = Join-Path $root 'test-env\results\installer'
$name = 'obs-multistream-stats'
$appKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{9BF09CF6-FFE4-4C45-8478-282689049548}_is1'
$uninsDir = Join-Path $env:APPDATA 'Multi-Stream Stats\uninstall'
foreach ($c in $obsA, $obsB) { if (-not (Test-Path "$c\bin\64bit\obs64.exe")) { throw "Missing isolated OBS copy $c" } }
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
$pins = (Get-Content -Raw (Join-Path $root 'packaging\windows\known-good-payloads.json') | ConvertFrom-Json).$version.files
$results = New-Object System.Collections.Generic.List[object]
function Check([string]$test, [bool]$ok, [string]$detail = '') {
    $results.Add([pscustomobject]@{ test = $test; pass = $ok; detail = $detail })
    "{0}  {1}{2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $test, $(if ($detail) { "  ($detail)" } else { '' })
}
function Snapshot([string]$dir) {
    $h = @{}
    if (Test-Path $dir) { Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object { $h[$_.FullName.Substring($dir.Length + 1)] = (Get-FileHash -LiteralPath $_.FullName).Hash } }
    return $h
}
function Compare-Snapshot($a, $b, [string[]]$ignorePrefixes) {
    $keys = @($a.Keys) + @($b.Keys) | Sort-Object -Unique
    @($keys | Where-Object { $k = $_; -not ($ignorePrefixes | Where-Object { $k.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) -and $a[$k] -ne $b[$k] })
}
function Run-Setup([string]$label, [string[]]$setupArgs) {
    $log = Join-Path $out "setup-$label.log"
    $p = Start-Process -Wait -PassThru -FilePath $Installer -ArgumentList (@('/VERYSILENT', '/SUPPRESSMSGBOXES', "/LOG=`"$log`"") + $setupArgs)
    return $p.ExitCode
}
function Plugin-Matches([string]$obs) {
    $dir = Join-Path $obs "plugins\$name"
    $files = Snapshot $dir
    $want = @{}; foreach ($p in $pins.PSObject.Properties) { $want[$p.Name.Replace('/', '\')] = $p.Value.ToUpper() }
    $bad = @($want.Keys | Where-Object { $files[$_] -ne $want[$_] }) + @($files.Keys | Where-Object { -not $want.ContainsKey($_) })
    return $bad
}
function Wait-Gone([string]$path, [int]$seconds = 60) {
    for ($i = 0; $i -lt $seconds -and (Test-Path $path); $i++) { Start-Sleep -Seconds 1 }
    -not (Test-Path $path)
}
function Run-Smoke([string]$obs, [string]$label) {
    $r = & (Join-Path $PSScriptRoot 'run-scenario.ps1') -Scenario (Join-Path $root 'tests\scenarios\smoke.json') -Name "installer-$label" -ObsDir $obs
    $summary = Join-Path $root "test-env\results\installer-$label\summary.txt"
    $pass = (Test-Path $summary) -and ((Get-Content $summary | Select-Object -Last 1) -eq 'RESULT: PASS')
    Copy-Item $summary (Join-Path $out "smoke-$label.txt") -ErrorAction SilentlyContinue
    return $pass
}

# --- Real OBS: read-only snapshot ---------------------------------------------------
$realPlugin = Join-Path $RealObsRoot "plugins\$name"
$realBefore = Snapshot $realPlugin
$keyBefore = Test-Path $appKey
Check 'precondition: no earlier installer registration' (-not $keyBefore -and -not (Test-Path $uninsDir))
if ($keyBefore) { throw 'An earlier installer registration exists; uninstall it first.' }

# Reset the isolated copies (only inside test-env\installer).
foreach ($c in $obsA, $obsB) {
    foreach ($d in "$c\plugins\$name", "$c\config\obs-studio\plugin_config\$name") {
        if ($d -notlike '*\test-env\installer\*') { throw "refusing to delete $d" }
        if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    }
}

# --- Refusals ------------------------------------------------------------------------
$snapA0 = Snapshot $obsA
Check 'silent install without /MODE is refused' ((Run-Setup 'no-mode' @('/CURRENTUSER')) -ne 0)
Check 'folder that is not OBS is refused' ((Run-Setup 'not-obs' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$(Join-Path $root 'test-env\installer')`"")) -ne 0)
Check '/MODE=standard without admin rights is refused' ((Run-Setup 'standard-no-admin' @('/CURRENTUSER', '/MODE=standard')) -ne 0)
Check 'refusals changed nothing' (((Compare-Snapshot $snapA0 (Snapshot $obsA) @()).Count -eq 0) -and -not (Test-Path $appKey))

# --- Fresh install (obs-a) ---------------------------------------------------------------
$code = Run-Setup 'fresh-a' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsA`"")
Check 'fresh install exit code 0' ($code -eq 0) "exit $code"
$bad = Plugin-Matches $obsA
Check 'installed files are exactly the tested 1.1.0 payload (no .pdb)' ($bad.Count -eq 0) ($bad -join ', ')
$changed = Compare-Snapshot $snapA0 (Snapshot $obsA) @("plugins\$name\")
Check 'nothing else in the OBS folder changed' ($changed.Count -eq 0) ($changed -join ', ')
Check 'uninstall entry registered (current user)' (Test-Path $appKey)
Check 'uninstaller kept outside the OBS folder' ((Test-Path "$uninsDir\unins000.exe") -and -not (Get-ChildItem "$obsA\plugins\$name" -Filter 'unins*' -ErrorAction SilentlyContinue))
Check 'plugin loads in OBS (smoke scenario, obs-a)' (Run-Smoke $obsA 'a')
$stateA = "$obsA\config\obs-studio\plugin_config\$name\dock-state.json"
Check 'dock settings file written by the plugin' (Test-Path $stateA)
$stateHashA = (Get-FileHash $stateA).Hash

# --- Reinstall (same version) -------------------------------------------------------------
$code = Run-Setup 'reinstall-a' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsA`"")
Check 'reinstall exit code 0' ($code -eq 0) "exit $code"
Check 'reinstall keeps plugin settings' ((Get-FileHash $stateA).Hash -eq $stateHashA)
Check 'reinstall leaves the tested payload' ((Plugin-Matches $obsA).Count -eq 0)

# --- Refusal while that OBS is running ------------------------------------------------------
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$obsA\bin\64bit\obs64.exe"; $psi.WorkingDirectory = "$obsA\bin\64bit"
$psi.Arguments = '--portable --multi --disable-updater --disable-missing-files-check'; $psi.UseShellExecute = $false
$obs = [System.Diagnostics.Process]::Start($psi)
Start-Sleep -Seconds 12
$code = Run-Setup 'while-running-a' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsA`"")
Check 'install refused while OBS runs from the target folder' ($code -ne 0) "exit $code"
[void]$obs.CloseMainWindow()
if (-not $obs.WaitForExit(30000)) { $obs.Kill(); $obs.WaitForExit(); Check 'isolated OBS closed gracefully' $false 'killed after 30 s' }
Check 'install while OBS ran changed nothing' ((Plugin-Matches $obsA).Count -eq 0)

# --- Upgrade from 1.0.1 with stray .pdb and existing settings (obs-b) --------------------------
$pluginB = "$obsB\plugins\$name"
Copy-Item -Recurse (Join-Path $root "releases\1.0.1\$name") $pluginB
New-Item -ItemType Directory -Force "$obsB\config\obs-studio\plugin_config\$name" | Out-Null
$stateB = "$obsB\config\obs-studio\plugin_config\$name\dock-state.json"
Copy-Item $stateA $stateB
$stateHashB = (Get-FileHash $stateB).Hash
$snapB0 = Snapshot $obsB
$code = Run-Setup 'upgrade-b' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsB`"")
Check 'upgrade 1.0.1 -> 1.1.0 exit code 0' ($code -eq 0) "exit $code"
$bad = Plugin-Matches $obsB
Check 'upgrade result is exactly the tested payload (old .pdb removed)' ($bad.Count -eq 0) ($bad -join ', ')
Check 'upgrade keeps plugin settings' ((Get-FileHash $stateB).Hash -eq $stateHashB)
Check 'upgrade logged the previous version' ([bool](Select-String -Path (Join-Path $out 'setup-upgrade-b.log') -Pattern 'previous: 1\.0\.1' -Quiet))
$changed = Compare-Snapshot $snapB0 (Snapshot $obsB) @("plugins\$name\")
Check 'upgrade changed nothing else' ($changed.Count -eq 0) ($changed -join ', ')
Check 'plugin loads after upgrade (smoke scenario, obs-b)' (Run-Smoke $obsB 'b')

# --- Downgrade protection and non-portable folder warning (obs-b) -------------------------------
$manifestB = "$pluginB\data\manifest.json"
$orig = [IO.File]::ReadAllBytes($manifestB)
[IO.File]::WriteAllText($manifestB, ([Text.Encoding]::UTF8.GetString($orig) -replace '"version": "[^"]+"', '"version": "9.9.9"'))
Check 'silent downgrade over a newer version is refused' ((Run-Setup 'downgrade-b' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsB`"")) -ne 0)
Check 'downgrade allowed with /ALLOWDOWNGRADE' ((Run-Setup 'downgrade-allowed-b' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsB`"", '/ALLOWDOWNGRADE')) -eq 0)
Check 'downgrade restored the tested payload' ((Plugin-Matches $obsB).Count -eq 0)
Rename-Item "$obsB\obs_portable_mode.txt" 'obs_portable_mode.txt.off'
Check 'silent install into a non-portable OBS folder is refused' ((Run-Setup 'not-portable-b' @('/CURRENTUSER', '/MODE=portable', "/OBSDIR=`"$obsB`"")) -ne 0)
Rename-Item "$obsB\obs_portable_mode.txt.off" 'obs_portable_mode.txt'

# --- Uninstall (removes the plugin from every folder this installer wrote to) --------------------
$unins = Get-ChildItem $uninsDir -Filter 'unins*.exe' | Select-Object -First 1
Start-Process -Wait -FilePath $unins.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$(Join-Path $out 'uninstall.log')`""
Check 'uninstall removed obs-a plugin folder' (Wait-Gone "$obsA\plugins\$name")
Check 'uninstall removed obs-b plugin folder' (Wait-Gone "$pluginB")
Check 'uninstall kept plugin settings (obs-a)' ((Test-Path $stateA) -and (Get-FileHash $stateA).Hash -eq $stateHashA)
Check 'uninstall kept plugin settings (obs-b)' ((Test-Path $stateB) -and (Get-FileHash $stateB).Hash -eq $stateHashB)
Check 'uninstall entry removed' (-not (Test-Path $appKey))
$leftover = Join-Path $env:APPDATA 'Multi-Stream Stats'
Check 'uninstaller folder removed' (Wait-Gone $leftover 30) $(if (Test-Path $leftover) { (Get-ChildItem $leftover -Recurse | ForEach-Object FullName) -join ', ' })

# --- Real OBS untouched ---------------------------------------------------------------------------
$realAfter = Snapshot $realPlugin
Check 'real OBS plugin files unchanged (read-only check)' ((Compare-Snapshot $realBefore $realAfter @()).Count -eq 0 -and $realBefore.Count -gt 0)

$failed = @($results | Where-Object { -not $_.pass })
$lines = $results | ForEach-Object { "{0}  {1}{2}" -f $(if ($_.pass) { 'PASS' } else { 'FAIL' }), $_.test, $(if ($_.detail) { "  ($($_.detail))" } else { '' }) }
$lines += "RESULT: $(if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }) ($($results.Count - $failed.Count)/$($results.Count))"
$lines | Set-Content -Encoding utf8 (Join-Path $out 'summary.txt')
$lines[-1]
