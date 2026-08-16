# Baut und startet die Testsuite.
# Nutzung:  .\test.ps1
#
# Die Visual-Studio-Umgebung wird ueber vswhere gesucht, damit auch
# Professional/Enterprise/BuildTools und abweichende Pfade funktionieren.
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# --- vcvars64.bat finden ---------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($vsPath) {
        $candidate = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $candidate) { $vcvars = $candidate }
    }
}
if (-not $vcvars) {
    throw "vcvars64.bat nicht gefunden. Visual Studio 2022 mit C++-Workload installieren."
}

# --- Bauen -----------------------------------------------------------------
# vcvars und CMake schreiben Hinweise auf stderr; mit ErrorActionPreference=Stop
# wuerde PowerShell daraus einen Abbruch machen, obwohl der Build laeuft.
# Erfolg wird deshalb ueber den Exit-Code geprueft.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
cmd /c "`"$vcvars`" >nul 2>&1 && cd /d `"$root`" && cmake --preset default >nul && cmake --build --preset default --target sshit-tests"
$buildCode = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($buildCode -ne 0) { exit $buildCode }

# --- Ausfuehren ------------------------------------------------------------
# Ohne Bildschirm laufen lassen, sonst blitzen bei den GUI-Tests Fenster auf.
$env:QT_QPA_PLATFORM = "offscreen"
& "$root\build\sshit-tests.exe"
exit $LASTEXITCODE
