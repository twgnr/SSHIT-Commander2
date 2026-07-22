# Portierungs-Status: SSHIT-Commander → SSHIT-Commander2 (C++)

Stand des 1:1-Umbaus von Python/PySide6 nach C++/Qt6 + libssh2.

## Vollständig portiert (1:1)

### Fundament
- `config` · `core/models` · `core/settings` · `core/secrets` (Windows Credential Manager)
- `core/i18n` (deutscher Quelltext als Schlüssel, EN-Katalog aus `:/i18n/en.json`)
- `gui/bridge` (Worker-Thread-Pool statt asyncio-Loop, Qt-Signal-Rückgabe + Abbruch-Token)

### core/ (Logik, UI- und netzwerkfrei) — alle Module
`filesystem` · `runner` · `commands` (kompletter Befehlskatalog) · `dataproviders` ·
`lsparse` · `dateformat` · `natsort` · `profiles` · `history` · `bookmarks` ·
`hostkeys` · `tabfavorites` · `shortcuts` · `configio` · `bulkrename` · `diff` ·
`filediff` · `encodings` (inkl. EBCDIC über WinAPI-Codepages) · `fileops` ·
`filesearch` · `search` · `netfs` · `netscan` · `oui_data` · `secaudit` ·
`venvtools` · `ai` · `markdown` · `plugins` · `ppk` (PPK↔OpenSSH) · `keytools` ·
`importers` · `macros` · `macroactions` (Tastatur/Maus/Fenster nativ über WinAPI) ·
`filealarm` · `githubalarm` · `appmonitor` · `openwith` · `gitstatus` · `assets` ·
`execfile`

### net/ (asyncssh → libssh2)
- `ssh` — Verbindung, Host-Key-Prüfung (TOFU/strict/ignore, MITM-Schutz vor der Auth),
  Auth per Key/Passwort/Agent, PPK-Auto-Konvertierung, **SFTP-Dateisystem**,
  **Remote-Runner** (Befehl→Ausgabe + echtes PTY), **interaktive PTY-Shell**
- `session` — Session-Manager · `sudofs` — sudo-Dateisystem · `transfer` — Up-/Download/lokal
  mit gestreamtem SFTP + Fortschritt · `tunnels` — -L / -R / -D(SOCKS5) über eigene Pump-Threads
- `ollama` · `cve` (OSV.dev) — HTTP über QNetworkAccessManager

### gui/ (PySide6 → Qt6 Widgets)
- `style` — alle 4 Themes + Theme-Editor-API + QSS 1:1, benutzerdefinierte Themes
- `bridge` · `app` (Einstiegspunkt) · `main_window` (Tabs, Menüs, Toolbar, Statusleiste, Theme-Wechsel)
- `workspace` (Tab = zwei Panes + zwei Konsolen + Verbindung, CWD-Sync bidirektional, Transfer per F5)
- `file_panel` (Pfadzeile, Dateitabelle, Navigation, F3 Ansehen / F4 Bearbeiten /
  F5 Übertragen / F6 Umbenennen / F7 Ordner / F8 Löschen, Kontextmenü, versteckte Dateien)
- `console_panel` (Befehl→Ausgabe über den Runner, Historie ↑/↓, CWD-Sync, `cd`-Auflösung, Strg+C)
- `server_manager` (Profile anlegen/bearbeiten/löschen, Verbinden, Import aus PuTTY/WinSCP/ssh-config)

## Build & Toolchain
- CMake + Ninja + MSVC 2022; Qt 6.8.2 (`C:\Qt\6.8.2\msvc2022_64`)
- libssh2 1.11 wird via CMake FetchContent gebaut (WinCNG-Crypto, kein OpenSSL nötig)
- `windeployqt` läuft automatisch nach dem Build
- Bauen: `.\build.ps1` (bzw. `-Fresh` für Neubau)

## Bewusst vereinfacht bzw. noch offen (GUI-Detaildialoge)

Die **Kern-Architektur und die komplette Logik-/Netzwerkschicht** sind vollständig
und 1:1 portiert. Bei der Qt-Oberfläche wurde die **tragende Struktur** (Hauptfenster,
Tabs, Dual-Pane, Konsole, Verbindung, Transfer, Themes) vollständig umgesetzt; einige
der zahlreichen **Zusatz-Dialoge** des Originals sind noch nicht als eigene C++-Widgets
ausgeführt und werden über die vorhandenen Menüs/Backends erreichbar gemacht:

- Befehlspalette & Befehlsassistent (Backend `core/commands` steht)
- Massen-Umbenennen-Dialog (Backend `core/bulkrename` steht)
- Datei-/Inhalts-Suche-Dialog (Backend `core/filesearch` + `core/search` steht)
- Encoding-Konverter-, Diff-, Sicherheits-Audit-, venv-, Netzscan-, Makro-Manager-,
  Tunnel-, Transfer-Queue-, Einstellungs-, KI-Chat-, Plugins-, Hilfe-Dialog
  (jeweiliges Backend ist portiert und aufrufbar)

Diese Dialoge sind reine UI-Schichten auf bereits portierter Logik — sie lassen sich
inkrementell ergänzen, ohne die Architektur zu ändern.

> Hinweis zur Portierung: Die parallele Erstellung mehrerer GUI-Dialoge durch
> Hintergrund-Agenten wurde durch ein Konto-Ausgabenlimit unterbrochen; der Kern
> (core + net + tragende GUI) wurde anschließend direkt fertiggestellt und baut sauber.
