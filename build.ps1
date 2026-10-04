# Baut SSHIT-Commander (MSVC + Ninja + Qt 6.8).
#
# Nutzung:  .\build.ps1              konfiguriert bei Bedarf und baut
#           .\build.ps1 -Fresh       loescht build/ und konfiguriert neu
#           .\build.ps1 -Package     baut und schnuert ein verteilbares ZIP
#
# Signieren (Authenticode, vor dem Packen; ohne Angabe bleibt die EXE
# unsigniert und -Package warnt). Genau EINE Quelle angeben:
#   -SignThumbprint <SHA1>      Zertifikat aus dem Windows-Zertifikatspeicher
#                               (auch USB-Token/HSM mit Treiber)
#   -SignPfx <datei.pfx>        Zertifikatsdatei; Passwort NUR ueber die
#                               Umgebungsvariable SSHIT_SIGN_PFX_PASSWORD
#   -SignDlib <dll> -SignMetadata <json>
#                               Azure Trusted Signing (Azure.CodeSigning.Dlib.dll
#                               + metadata.json mit Endpoint/Account/Profil)
# Gleichwertig per Umgebungsvariable: SSHIT_SIGN_THUMBPRINT, SSHIT_SIGN_PFX,
# SSHIT_SIGN_DLIB, SSHIT_SIGN_METADATA. Zeitstempel: -TimestampUrl (Standard
# DigiCert) — ohne Zeitstempel wird die Signatur mit Ablauf des Zertifikats
# ungueltig.
#
# Die Visual-Studio-Umgebung wird ueber vswhere gesucht, damit auch
# Professional/Enterprise/BuildTools und andere Installationspfade funktionieren.
param(
    [switch]$Fresh,
    [switch]$Package,
    # Ueberschreibt die Stufe im Paketnamen, z. B. -Label "beta.1" fuer ein
    # Release mit dem Tag v1.1.0-beta.1. Ohne Angabe wird SSHIT_VERSION_STAGE
    # aus der CMakeLists genommen.
    [string]$Label,
    [string]$SignThumbprint = $env:SSHIT_SIGN_THUMBPRINT,
    [string]$SignPfx = $env:SSHIT_SIGN_PFX,
    [string]$SignDlib = $env:SSHIT_SIGN_DLIB,
    [string]$SignMetadata = $env:SSHIT_SIGN_METADATA,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if ($Fresh -and (Test-Path "$root\build")) {
    Remove-Item -Recurse -Force "$root\build"
}

# --- vcvars64.bat finden ---------------------------------------------------
# Vorhandener Build-Ordner: GENAU das Visual Studio nehmen, mit dem er
# konfiguriert wurde. Sonst mischt ein neueres VS (z. B. 18 neben 2022) seine
# Header mit dem im CMake-Cache hinterlegten Compiler -> "STL1001: Unexpected
# compiler version".
$vcvars = $null
$cache = Join-Path $root "build\CMakeCache.txt"
if (-not $Fresh -and (Test-Path $cache)) {
    $line = Select-String -Path $cache -Pattern '^CMAKE_CXX_COMPILER:\w+=(.+)$' | Select-Object -First 1
    if ($line) {
        $compiler = $line.Matches[0].Groups[1].Value
        $idx = $compiler.IndexOf("/VC/Tools/")
        if ($idx -lt 0) { $idx = $compiler.IndexOf("\VC\Tools\") }
        if ($idx -gt 0) {
            $candidate = Join-Path $compiler.Substring(0, $idx) "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $candidate) { $vcvars = $candidate }
        }
    }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not $vcvars -and (Test-Path $vswhere)) {
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

# --- Signieren (Authenticode) ----------------------------------------------
# Signiert wird die EXE im Build-Ordner — so ist sie auch im Paket signiert.
# Qt-DLLs bringen ihre eigene Signatur (The Qt Company) mit.
$signed = $false
$signSources = @($SignThumbprint, $SignPfx, $SignDlib) | Where-Object { $_ }
if (@($signSources).Count -gt 1) {
    throw "Signieren: nur EINE Quelle angeben (-SignThumbprint, -SignPfx oder -SignDlib)."
}
if (@($signSources).Count -eq 1) {
    $exeToSign = Join-Path $root "build\sshit-commander.exe"
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" `
                    -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending | Select-Object -First 1
    if (-not $signtool) { throw "signtool.exe nicht gefunden - Windows SDK installieren." }
    # /d und /du erscheinen im UAC-/SmartScreen-Dialog als Programmname und Link.
    $signArgs = @("sign", "/fd", "SHA256", "/d", "SSHIT-Commander",
                  "/du", "https://github.com/twgnr/SSHIT-Commander2")
    if ($TimestampUrl) { $signArgs += @("/tr", $TimestampUrl, "/td", "SHA256") }
    if ($SignThumbprint) {
        $signArgs += @("/sha1", $SignThumbprint)
    } elseif ($SignPfx) {
        if (-not (Test-Path $SignPfx)) { throw "PFX-Datei nicht gefunden: $SignPfx" }
        $signArgs += @("/f", $SignPfx)
        if ($env:SSHIT_SIGN_PFX_PASSWORD) { $signArgs += @("/p", $env:SSHIT_SIGN_PFX_PASSWORD) }
    } else {
        if (-not $SignMetadata) { throw "Trusted Signing: -SignMetadata (metadata.json) fehlt." }
        $signArgs += @("/dlib", $SignDlib, "/dmdf", $SignMetadata)
    }
    & $signtool.FullName @signArgs $exeToSign
    if ($LASTEXITCODE -ne 0) { throw "Signieren fehlgeschlagen (signtool Exit $LASTEXITCODE)." }
    $sig = Get-AuthenticodeSignature $exeToSign
    if ($sig.Status -in @("NotSigned", "HashMismatch")) {
        throw "Die EXE traegt danach keine gueltige Signatur ($($sig.Status))."
    }
    Write-Host "Signiert von: $($sig.SignerCertificate.Subject) (Windows-Status: $($sig.Status))"
    $signed = $true
}

if (-not $Package) { exit 0 }

# --- Verteilbares Paket schnueren ------------------------------------------
# Nur das, was zum Ausfuehren noetig ist: EXE, Qt-DLLs und Qt-Plugin-Ordner.
# Build-Artefakte (Objekte, Tests, CMake-Innereien, PDBs) bleiben draussen.
$build = Join-Path $root "build"
$exe = Join-Path $build "sshit-commander.exe"
if (-not (Test-Path $exe)) { throw "sshit-commander.exe nicht gefunden - Build fehlgeschlagen?" }

# Version und Entwicklungsstufe aus der CMakeLists lesen - eine Quelle fuer
# Programm und Paketname. Ergebnis z. B.: SSHIT-Commander-1.0.5-win64
# (mit Stufe: SSHIT-Commander-1.1.0-beta.1-win64)
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

# Symbole zum Paket aufbewahren (NICHT ausliefern): Absturzberichte nennen nur
# Modul+Offset — erst mit exe+pdb GENAU dieses Builds werden daraus Codezeilen.
# Ordner je Paket-Hash, damit spaetere Builds sie nicht ueberschreiben.
$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash.Substring(0, 8)
$symDir = Join-Path $build "symbols\$releaseName-$zipHash"
New-Item -ItemType Directory -Force $symDir | Out-Null
Copy-Item $exe $symDir
$pdb = [System.IO.Path]::ChangeExtension($exe, ".pdb")
if (Test-Path $pdb) { Copy-Item $pdb $symDir }
Write-Host "Symbole fuer Absturzberichte: $symDir"
if (-not $signed) {
    Write-Warning ("Die EXE ist NICHT signiert - Windows SmartScreen warnt beim ersten Start. " +
                   "Signieren mit -SignThumbprint, -SignPfx oder -SignDlib (siehe Kopf von build.ps1).")
}
