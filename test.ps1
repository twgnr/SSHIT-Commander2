# Baut und startet die Testsuite.
# Nutzung:  .\test.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && cd /d `"$root`" && cmake --preset default >nul && cmake --build --preset default --target sshit-tests"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root\build\sshit-tests.exe"
exit $LASTEXITCODE
