# Tests the setup without administrator rights, in a sandbox under %TEMP%: fresh install, update while
# another process has the plug-in DLL loaded (as a DAW would), uninstall. Touches no real VST3 folder.
#   TestSetup.ps1 -Setup <build>\setup-test\Anneal-test-Setup.exe -Built <built bundle>\Contents\x86_64-win\Anneal.vst3
# Use the test setup build.ps1 makes: same script with its own AppId (below), so a real "just for me"
# install of Anneal is never touched. Exit code = number of failed checks.
param([string]$Setup, [string]$Built)
$ErrorActionPreference = "Stop"
$key = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{C3A9A011-1FE8-4D62-BC17-61FF1488E1F2}_is1"
# Short path on purpose: Setup refuses to start with a very long /LOG or /DIR.
$sb = Join-Path $env:TEMP "anneal-setup-test"
if (Test-Path $sb) { Remove-Item -Recurse -Force $sb }
New-Item -ItemType Directory -Force $sb | Out-Null
$vst = "$sb\vst3"; $app = "$sb\app"
$bin = "$vst\Anneal.vst3\Contents\x86_64-win\Anneal.vst3"
$builtHash = (Get-FileHash $Built).Hash
$fails = 0
function Check($ok, $what) { if ($ok) { "[PASS] $what" } else { "[FAIL] $what"; $script:fails++ } }

function Run-Setup($log) {
    $a = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /COMPONENTS=vst3 /VST3DIR=`"$vst`" /DIR=`"$app`" /LOG=`"$log`""
    $p = Start-Process $Setup -ArgumentList $a -Wait -PassThru
    return $p.ExitCode
}

# 1. fresh install
$c = Run-Setup "$sb\install1.log"
Check ($c -eq 0) "fresh install exit 0 (got $c)"
Check ((Test-Path $bin) -and (Get-FileHash $bin).Hash -eq $builtHash) "VST3 binary installed and matches build"
Check (Test-Path "$vst\Anneal.vst3\Contents\Resources\moduleinfo.json") "moduleinfo.json installed"
Check (Test-Path "$app\unins000.exe") "uninstaller in app folder, not in VST3 folder"
Check (-not (Test-Path "$vst\unins000.exe")) "nothing else dropped into VST3 folder"
Check (Test-Path $key) "registered under Apps (per-user test mode)"
if (Test-Path $key) { "       DisplayName: " + (Get-ItemProperty $key).DisplayName + " / version " + (Get-ItemProperty $key).DisplayVersion }

# 2. update while the DLL is loaded by another process
$loader = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\HoldDll.ps1`" -Dll `"$bin`" -Flag `"$sb\loaded.txt`""
for ($i = 0; $i -lt 60 -and -not (Test-Path "$sb\loaded.txt"); $i++) { Start-Sleep -Milliseconds 500 }
$handle = if (Test-Path "$sb\loaded.txt") { (Get-Content "$sb\loaded.txt").Trim() } else { "none" }
Check ($handle -ne "0" -and $handle -ne "none") "test process has the installed DLL loaded (handle $handle)"
$locked = $false
try { [IO.File]::Delete($bin) } catch { $locked = $true }
Check $locked "the loaded DLL cannot be deleted (so this really tests the in-use case)"
# %TEMP%\AnnealOld is shared with real installs (a DAW may still hold one of those), so only the file
# this test adds is checked.
$oldDir = "$env:TEMP\AnnealOld"
$before = @(Get-ChildItem $oldDir -File -ErrorAction SilentlyContinue | ForEach-Object Name)
$c = Run-Setup "$sb\install2.log"
Check ($c -eq 0) "update while loaded: exit 0 (got $c)"
Check ((Test-Path $bin) -and (Get-FileHash $bin).Hash -eq $builtHash) "update while loaded: new binary in place"
$ours = @(Get-ChildItem $oldDir -File -ErrorAction SilentlyContinue | ForEach-Object Name | Where-Object { $before -notcontains $_ })
Check ($ours.Count -eq 1) "loaded binary moved aside to %TEMP%\AnnealOld ($($ours.Count) new)"
Stop-Process -Id $loader.Id -Force
Start-Sleep 1

# 3. uninstall
Start-Process "$app\unins000.exe" -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART" -Wait
for ($i = 0; $i -lt 60 -and (Test-Path "$app\unins000.exe"); $i++) { Start-Sleep -Milliseconds 500 }
Start-Sleep 1
Check (-not (Test-Path "$vst\Anneal.vst3")) "uninstall removes the bundle"
Check (-not (Test-Path $key)) "uninstall removes the Apps entry"
Check (-not (Test-Path "$app\unins000.exe")) "uninstall removes itself"
$left = @($ours | Where-Object { Test-Path (Join-Path $oldDir $_) }).Count
Check ($ours.Count -eq 1 -and $left -eq 0) "moved-aside binary cleaned up once released ($left left)"
Remove-Item -Recurse -Force $sb -ErrorAction SilentlyContinue
"$fails setup check(s) failed"
exit $fails

