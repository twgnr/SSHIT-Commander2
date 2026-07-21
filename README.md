# SSHIT-Commander2 (C++)

**1:1-Port von [SSHIT-Commander](../SSHIT-Commander) von Python/PySide6 nach C++/Qt6.**

Ein Dual-Pane-Dateimanager mit integriertem SSH/SFTP-Terminal: zwei Panes für
lokale und entfernte Verzeichnisse, gekoppelt mit einer vollwertigen SSH-Konsole.
Funktionsumfang und Bedienung entsprechen dem Python-Original (siehe dessen
README für die vollständige Feature-Liste).

## Technologie-Mapping

| Python-Original | C++-Port |
|---|---|
| PySide6 / Qt 6 (Python) | Qt 6.8 (C++, Widgets) |
| asyncssh (SSH/SFTP) | libssh2 1.11 (statisch, WinCNG-Crypto) |
| asyncio-Loop im Thread + Qt-Signale | Worker-Thread-Pool (`AsyncBridge`) + Qt-Signale |
| pywinpty (lokales PTY) | Windows ConPTY (`CreatePseudoConsole`) |
| pyte (Terminal-Emulation) | eigener VT/ANSI-Screen-Parser |
| keyring (Credential Manager) | Windows Credential Manager API (`wincred.h`) |
| Python-`json` | `QJsonDocument` |
| urllib/requests (Ollama, OSV.dev, GitHub) | `QNetworkAccessManager` |

## Architektur

Wie im Original strikt geschichtet — die GUI kennt nur die Core-Interfaces:

```
src/
├── main.cpp               # Einstiegspunkt (≙ __main__.py)
└── ncssh/
    ├── config.hpp/.cpp    # Konfig-Pfade, atomares Schreiben
    ├── core/              # UI- und netzwerkfrei: Models, FileSystemProvider,
    │                      # CommandRunner, Befehlskatalog, Profile, i18n, …
    ├── net/               # libssh2: Session, SFTP-Provider, Remote-Runner,
    │                      # Transfer, Tunnel, Ollama, OSV.dev
    └── gui/               # Qt-Widgets-Oberfläche (≙ gui/ des Originals)
        └── bridge.hpp     # Worker-Threads <-> Qt-Signale (≙ bridge.py)
```

Schlüssel-Abstraktionen unverändert: `FileSystemProvider` (lokal vs. SFTP) und
`CommandRunner` (lokal vs. remote). Alle vormals `async`-Methoden sind
blockierend und laufen über die `AsyncBridge` auf Worker-Threads — das Fenster
friert bei SSH-Operationen oder Transfers nie ein.

## Bauen & Starten

Voraussetzungen (bereits eingerichtet):
- Visual Studio 2022 (MSVC) mit C++-Workload
- Qt 6.8.2 MSVC x64 unter `C:\Qt\6.8.2\msvc2022_64`
- libssh2 wird beim ersten Configure automatisch per CMake FetchContent geladen

```powershell
cd C:\Repository\SSHIT-Commander2
.\build.ps1            # konfiguriert + baut (Ninja, RelWithDebInfo)
.\build\sshit-commander.exe
```

`build.ps1 -Fresh` löscht `build/` und konfiguriert neu. `windeployqt` läuft
automatisch nach dem Build — die EXE ist direkt startbar.

Konfiguration/Profile werden wie im Original unter `%APPDATA%\ncssh` gespeichert
und sind mit der Python-Version **kompatibel** (gleiche JSON-Formate, gleicher
Credential-Manager-Dienstname).

## Bewusste Abweichungen vom Original

- `src/ncssh/ui/` (optionale Textual-TUI) wurde nicht portiert — die Qt-GUI ist
  die einzige Oberfläche.
- Python-basierte Plugins (`importlib`) laufen im C++-Port als externe Prozesse.
- Build-Werkzeuge (pip/Nuitka) sind durch CMake/`build.ps1` ersetzt.

Details zu den Portierungs-Konventionen: [PORTING.md](PORTING.md).
