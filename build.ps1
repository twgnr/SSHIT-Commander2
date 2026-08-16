# Baut SSHIT-Commander (MSVC + Ninja + Qt 6.8).
#
# Nutzung:  .\build.ps1              konfiguriert bei Bedarf und baut
#           .\build.ps1 -Fresh       loescht build/ und konfiguriert neu
#           .\build.ps1 -Package     baut und schnuert ein verteilbares ZIP
#
# Die Visual-Studio-Umgebung wird ueber vswhere gesucht, damit auch
# Professional/Enterprise/BuildTools und andere Installationspfade funktionieren.
param(
    [switch]$Fresh,
    [switch]$Package,
    # Ueberschreibt die Stufe im Paketnamen, z. B. -Label "beta.1" fuer ein
    # Release mit dem Tag v1.0.0-beta.1. Ohne Angabe wird SSHIT_VERSION_STAGE
    # aus der CMakeLists genommen.
    [string]$Label
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if ($Fresh -and (Test-Path "$root\build")) {
    Remove-Item -Recurse -Force "$root\build"
}

# --- vcvars64.bat finden ---------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
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

# --- Konfigurieren + Bauen -------------------------------------------------
# vcvars64.bat und CMake schreiben Hinweise auf stderr (vswhere, Deprecation).
# Mit ErrorActionPreference=Stop wuerde PowerShell daraus einen Abbruch machen,
# obwohl der Build laeuft -> waehrend des Aufrufs auf Continue schalten und den
# Erfolg ueber den Exit-Code pruefen. vcvars' eigene Ausgabe wird verworfen.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
cmd /c "`"$vcvars`" >nul 2>&1 && cd /d `"$root`" && cmake --preset default && cmake --build --preset default"
$buildCode = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($buildCode -ne 0) { exit $buildCode }

if (-not $Package) { exit 0 }

# --- Verteilbares Paket schnueren ------------------------------------------
# Nur das, was zum Ausfuehren noetig ist: EXE, Qt-DLLs und Qt-Plugin-Ordner.
# Build-Artefakte (Objekte, Tests, CMake-Innereien, PDBs) bleiben draussen.
$build = Join-Path $root "build"
$exe = Join-Path $build "sshit-commander.exe"
if (-not (Test-Path $exe)) { throw "sshit-commander.exe nicht gefunden - Build fehlgeschlagen?" }

# Version und Entwicklungsstufe aus der CMakeLists lesen - eine Quelle fuer
# Programm und Paketname. Ergebnis z. B.: SSHIT-Commander-1.0.0-beta-win64
$cmakeFile = Join-Path $root "CMakeLists.txt"
$version = (Select-String -Path $cmakeFile `
    -Pattern 'project\(.*VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value
if ($Label) {
    $channel = $Label
} else {
    $m = Select-String -Path $cmakeFile -Pattern 'SSHIT_VERSION_STAGE="([^"]*)"'
    $channel = if ($m) { $m.Matches[0].Groups[1].Value } else { "" }
}
# Stabile Fassungen tragen keine Stufe im Namen.
$channel = $channel.ToLowerInvariant()
if ($channel -in @("release", "stable", "final")) { $channel = "" }

$releaseName = "SSHIT-Commander-$version"
if ($channel) { $releaseName += "-$channel" }
$releaseName += "-win64"

$stageDir = Join-Path $build "package\$releaseName"
if (Test-Path (Split-Path $stageDir)) { Remove-Item -Recurse -Force (Split-Path $stageDir) }
New-Item -ItemType Directory -Force $stageDir | Out-Null
$stage = $stageDir

Copy-Item $exe $stage
Get-ChildItem "$build\*.dll" | Copy-Item -Destination $stage
# Von windeployqt angelegte Plugin-Ordner (nur die tatsaechlich vorhandenen).
foreach ($dir in @("platforms", "styles", "imageformats", "iconengines",
                   "generic", "networkinformation", "tls")) {
    $src = Join-Path $build $dir
    if (Test-Path $src) { Copy-Item -Recurse $src $stage }
}
# Begleitende Unterlagen mitgeben. LICENSE und licenses/ sind PFLICHT: Qt wird
# unter der LGPL v3 weitergegeben, die bei Binaerweitergabe den Lizenztext und
# den Verweis auf die Qt-Quellen verlangt; libssh2 (BSD) verlangt den
# Copyright-Hinweis.
foreach ($doc in @("README.md", "LICENSE")) {
    if (Test-Path (Join-Path $root $doc)) { Copy-Item (Join-Path $root $doc) $stage }
}
if (Test-Path (Join-Path $root "licenses")) {
    Copy-Item -Recurse (Join-Path $root "licenses") $stage
} else {
    Write-Warning "Ordner licenses/ fehlt - das Paket erfuellt die LGPL-Auflagen nicht."
}
if (Test-Path (Join-Path $root "plugins\README.txt")) {
    New-Item -ItemType Directory -Force (Join-Path $stage "plugins") | Out-Null
    Copy-Item (Join-Path $root "plugins\README.txt") (Join-Path $stage "plugins")
}

# Aeltere Pakete derselben Version aufraeumen, damit kein veralteter Stand
# neben dem neuen liegt und versehentlich hochgeladen wird.
Get-ChildItem (Join-Path $build "SSHIT-Commander-*.zip") -ErrorAction SilentlyContinue |
    Remove-Item -Force

$zip = Join-Path $build "$releaseName.zip"
Compress-Archive -Path "$stageDir\*" -DestinationPath $zip
Write-Host "Paket erstellt: $zip"
