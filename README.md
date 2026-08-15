# SSHIT-Commander

**Dual-Pane-Dateimanager mit SSH/SFTP und Terminal** für Windows.

Zwei Panes für lokale und entfernte Verzeichnisse, gekoppelt mit einer
vollwertigen SSH-Konsole und einem echten Terminal. Geschrieben in C++20 mit
Qt 6 und libssh2.

> **Status: Beta.** Die Anwendung ist funktionsfähig und durch 186 automatische
> Tests abgesichert, die SSH-Schicht ist gegen einen echten OpenSSH-Server
> validiert. Breite Praxiserprobung gegen unterschiedliche Server steht noch aus
> — siehe [Bekannte Grenzen](#bekannte-grenzen).

## Funktionsumfang

- **Dateiverwaltung**: zwei Panes (lokal ⇄ remote), Kopieren/Verschieben mit
  Fortschritt, Bandbreiten-Limit, Pause/Fortsetzen, Drag & Drop, Massen-Umbenennen,
  Verzeichnis- und Dateivergleich, Prüfsummen, ZIP, Symlinks, Lesezeichen.
- **SSH/SFTP**: Profilverwaltung, Passwort/Schlüssel/Agent, PuTTY-PPK-Import,
  Host-Key-Prüfung (TOFU) mit OpenSSH-`known_hosts`-Interop, ProxyJump/Bastion,
  Port-Weiterleitungen (`-L`/`-R`/`-D` mit SOCKS5), sudo-Dateisystem.
- **Terminal**: echtes PTY (lokal über ConPTY, remote über SSH) mit vollem
  VT100/xterm-Emulator — `vim`, `htop`, `tmux` und `less` laufen.
- **Automatisierung**: SFTP-Batch mit Skript-Editor und Zeitplanung,
  Verzeichnis-Alarme (lokal und remote) mit Befehlsauslösung, Makro-Manager mit
  Layern.
- **Werkzeuge**: Editor mit Syntax-Hervorhebung, Datei-/Inhaltssuche,
  Netzwerk-Scanner, CVE-Audit (OSV.dev), Zeichensatz-Konverter, venv-Verwaltung,
  KI-Chat über ein lokales Ollama.
- Deutsch und Englisch, vier Themes plus eigene.

## Architektur

Strikt geschichtet — die GUI kennt nur die Core-Interfaces:

```
src/
├── main.cpp               # Einstiegspunkt
└── ncssh/
    ├── config.hpp/.cpp    # Konfig-Pfade, atomares Schreiben
    ├── core/              # UI- und netzwerkfrei: Models, FileSystemProvider,
    │                      # CommandRunner, Befehlskatalog, Profile, i18n, …
    ├── net/               # libssh2: Session, SFTP-Provider, Remote-Runner,
    │                      # Transfer, Tunnel, Ollama, OSV.dev
    └── gui/               # Qt-Widgets-Oberfläche
        └── bridge.hpp     # Worker-Threads <-> Qt-Signale
```

Schlüssel-Abstraktionen: `FileSystemProvider` (lokal vs. SFTP) und
`CommandRunner` (lokal vs. remote). Alle blockierenden Operationen laufen über
die `AsyncBridge` auf Worker-Threads — das Fenster friert bei SSH-Operationen
oder Transfers nie ein.

## Bauen

**Voraussetzungen**

- Windows 10/11 (x64)
- Visual Studio 2022 mit C++-Workload (MSVC, CMake, Ninja)
- Qt 6.8 für MSVC x64
- Internetzugang beim ersten Configure — libssh2 wird per CMake FetchContent geholt

**Übersetzen**

```powershell
.\build.ps1            # konfiguriert + baut (Ninja, RelWithDebInfo)
.\build.ps1 -Fresh     # build/ löschen und neu konfigurieren
.\build\sshit-commander.exe
```

Liegt Qt nicht unter `C:\Qt\6.8.2\msvc2022_64`, den Pfad einmalig übergeben:

```powershell
cmake --preset default -DCMAKE_PREFIX_PATH="D:/Qt/6.8.2/msvc2022_64"
```

`windeployqt` läuft automatisch nach dem Build und legt die benötigten Qt-DLLs
und -Plugins neben die EXE — der Ordner `build\` ist damit bereits lauffähig.

**Verteilbares Paket** (nur EXE, Qt-DLLs/Plugins und Dokumente, ohne
Build-Innereien):

```powershell
.\build.ps1 -Package
```

Erzeugt `build\SSHIT-Commander-<Version>.zip`.

**Tests**

```powershell
.\test.ps1             # 186 Tests (oder: ctest --test-dir build)
```

### Krypto-Backend (WinCNG oder OpenSSL)

- **Standard: WinCNG** (`-DUSE_OPENSSL_BACKEND=OFF`). Kein OpenSSL nötig, aber
  **ohne ed25519/curve25519**. Mit `ENABLE_ECDSA_WINCNG` sind ecdsa-Hostkeys und
  ecdh-sha2-nistp*-KEX aktiv, sodass Standard-OpenSSH-Server (die mehrere
  Verfahren anbieten) funktionieren. Es scheitert nur bei Servern, die
  ausschließlich ed25519/curve25519 anbieten, oder bei ed25519-**Login-Keys**.
- **Opt-in: OpenSSL** (`-DUSE_OPENSSL_BACKEND=ON`) → ed25519/curve25519 + ecdsa.
  `cmake/OpenSSLBackend.cmake` stellt OpenSSL bereit:
  1. `-DOPENSSL_ROOT_DIR=<pfad>` auf ein vorgebautes statisches OpenSSL, **oder**
  2. aus Quellcode bauen (OpenSSL 3.3.2, `Configure VC-WIN64A no-asm no-shared`,
     `nmake` — **kein nasm nötig**). Ergebnis wird im Build-Verzeichnis gecacht.
- **Voraussetzung für Weg 2:** ein vollständiges perl (Strawberry/ActiveState)
  mit `Locale::Maketext::Simple` — das perl aus Git-Bash reicht NICHT. Notfalls
  `-DOPENSSL_PERL=<pfad/perl.exe>` erzwingen. Aufruf aus einer MSVC-Umgebung
  (vcvars), damit `nmake`/`cl` verfügbar sind.
- libssh2 nutzt dann seinen OpenSSL-Pfad, in dem `LIBSSH2_ED25519=1` für
  OpenSSL ≥ 1.1.1 gilt und X25519 fest einkompiliert ist. Statisch gelinkt
  (keine libcrypto-DLL). OpenSSL bei Nutzung auf einem gepflegten Zweig aktuell
  halten (CVEs).

## Konfiguration

Einstellungen, Profile und Lesezeichen liegen unter `%APPDATA%\ncssh`.
Passwörter und Token landen im **Windows Credential Manager**, nicht in den
Konfigurationsdateien.

## Bekannte Grenzen

- **Nur Windows.** ConPTY, Credential Manager und die Makro-Aktionen nutzen
  WinAPI direkt.
- Im Standard-Build (WinCNG) fehlen **ed25519-Hostkeys und curve25519**. Server
  mit ecdsa/rsa und ecdh/DH — die OpenSSH-Standardkonfiguration — funktionieren;
  nur Server, die *ausschließlich* ed25519/curve25519 anbieten, sowie
  ed25519-**Login-Keys** brauchen den OpenSSL-Build.
- **Agent-Forwarding** ist nicht möglich: libssh2 kann die vom Server
  zurückgeöffneten Agent-Kanäle nicht annehmen. Die Option ist deshalb bewusst
  deaktiviert statt scheinbar vorhanden.
- Keine systemweiten Makro-Hotkeys (Makrotasten lösen per Klick aus).
- Nicht breit erprobt: exotische Auth-/SFTP-/Tunnel-Kombinationen,
  ProxyJump und ed25519-Handshakes mangels passender Testserver.

## Drittanbieter-Komponenten

SSHIT-Commander nutzt folgende freie Bibliotheken. Die Rechte daran liegen bei
den jeweiligen Urhebern; die genannten Lizenzbedingungen gelten unabhängig von
denen dieses Programms.

| Komponente | Verwendung | Lizenz |
|---|---|---|
| **Qt 6.8** (Core, Gui, Widgets, Network, Concurrent, Svg) | Anwendungs-Grundlage: Oberfläche, Ereignisschleife, Threads, JSON, HTTP, Bild-/SVG-Anzeige | LGPL v3 |
| **libssh2 1.11** | SSH-Verbindung, Authentifizierung, SFTP, PTY, Tunnel | BSD-3-Clause |
| **OpenSSL 3** *(optional)* | nur im Build mit `-DUSE_OPENSSL_BACKEND=ON` als Krypto-Backend | Apache-2.0 |

Qt ist **dynamisch** eingebunden: die `Qt6*.dll` liegen neben der ausführbaren
Datei und lassen sich durch eine eigene, passende Qt-Fassung ersetzen. Der
Qt-Quellcode ist bei <https://www.qt.io/download-open-source> bzw.
<https://code.qt.io> erhältlich, libssh2 unter <https://www.libssh2.org>.

> Hinweis: Bei der Weitergabe fertiger Programmdateien (nicht des Quellcodes)
> verlangt die LGPL zusätzlich, den Lizenztext beizulegen und auf die
> Bezugsquelle des Qt-Quellcodes hinzuweisen. Für die reine Veröffentlichung
> dieses Quellcode-Repositorys genügt der Hinweis oben.

## Lizenz

Copyright (c) Tobias Wagner. Für dieses Programm ist noch keine Lizenz
festgelegt — damit gelten die gesetzlichen Vorgaben (alle Rechte vorbehalten).
Die oben genannten Drittanbieter-Komponenten behalten ihre eigenen Lizenzen.
