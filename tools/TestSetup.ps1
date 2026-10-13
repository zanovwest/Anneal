# Tests the setup without administrator rights, in a sandbox under %TEMP%: fresh install, update while
# another process has the plug-in DLL loaded (as a DAW would), uninstall. Touches no real VST3 folder.
#   TestSetup.ps1 -Setup dist\Anneal-x.y.z-Setup.exe -Built <built bundle>\Contents\x86_64-win\Anneal.vst3
# Exit code = number of failed checks.
param([string]$Setup, [string]$Built)
$ErrorActionPreference = "Stop"
$key = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{146CC042-90A4-4E67-8055-FE35CBE64CD9}_is1"
if (Test-Path $key) { "[FAIL] a per-user Anneal install exists; not touching it"; exit 1 }
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
$oldDir = "$env:TEMP\AnnealOld"
$before = @(Get-ChildItem $oldDir -ErrorAction SilentlyContinue).Count
$c = Run-Setup "$sb\install2.log"
Check ($c -eq 0) "update while loaded: exit 0 (got $c)"
Check ((Test-Path $bin) -and (Get-FileHash $bin).Hash -eq $builtHash) "update while loaded: new binary in place"
$after = @(Get-ChildItem $oldDir -ErrorAction SilentlyContinue).Count
Check ($after -eq $before + 1) "loaded binary moved aside to %TEMP%\AnnealOld ($before -> $after)"
Stop-Process -Id $loader.Id -Force
Start-Sleep 1

# 3. uninstall
Start-Process "$app\unins000.exe" -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART" -Wait
for ($i = 0; $i -lt 60 -and (Test-Path "$app\unins000.exe"); $i++) { Start-Sleep -Milliseconds 500 }
Start-Sleep 1
Check (-not (Test-Path "$vst\Anneal.vst3")) "uninstall removes the bundle"
Check (-not (Test-Path $key)) "uninstall removes the Apps entry"
Check (-not (Test-Path "$app\unins000.exe")) "uninstall removes itself"
$left = @(Get-ChildItem $oldDir -ErrorAction SilentlyContinue).Count
Check ($left -eq 0) "moved-aside binaries cleaned up once released ($left left)"
Remove-Item -Recurse -Force $sb -ErrorAction SilentlyContinue
"$fails setup check(s) failed"
exit $fails

