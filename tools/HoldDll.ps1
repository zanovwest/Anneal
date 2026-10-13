param([string]$Dll, [string]$Flag)
# Keeps a DLL loaded, like a DAW holding a plug-in, so an update has to cope with it being in use.
Add-Type -Name K -Namespace W -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern System.IntPtr LoadLibraryW(string path);
'@
$h = [W.K]::LoadLibraryW($Dll)
Set-Content -Path $Flag -Value ([int64]$h)
Start-Sleep -Seconds 120
