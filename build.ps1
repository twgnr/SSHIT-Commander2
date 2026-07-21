# Baut SSHIT-Commander2 (MSVC + Ninja + Qt 6.8).
# Nutzung:  .\build.ps1            (konfiguriert bei Bedarf und baut)
#           .\build.ps1 -Fresh    (loescht build/ und konfiguriert neu)
param([switch]$Fresh)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if ($Fresh -and (Test-Path "$root\build")) {
    Remove-Item -Recurse -Force "$root\build"
}

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && cd /d `"$root`" && cmake --preset default && cmake --build --preset default"
exit $LASTEXITCODE
