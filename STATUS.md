# Portierungs-Status: SSHIT-Commander → SSHIT-Commander2 (C++)

Stand des 1:1-Umbaus von Python/PySide6 nach C++/Qt6 + libssh2.

## Vollständig portiert

### Fundament
- `config` · `core/models` · `core/settings` · `core/secrets` (Windows Credential Manager)
- `core/i18n` (deutscher Quelltext als Schlüssel, EN-Katalog aus `:/i18n/en.json`)
- `gui/bridge` (Worker-Thread-Pool statt asyncio-Loop, Qt-Signal-Rückgabe + Abbruch-Token)

### core/ — alle Module
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
- `ssh` — Verbindung, Host-Key-Prüfung (TOFU/strict/ignore, MITM-Schutz **vor** der Auth),
  Auth per Key/Passwort/Agent, PPK-Auto-Konvertierung, **SFTP-Dateisystem**,
  **Remote-Runner** (Befehl→Ausgabe + echtes PTY), **interaktive PTY-Shell**
- `session` · `sudofs` (sudo-Dateisystem) · `transfer` (gestreamtes SFTP mit Fortschritt) ·
  `tunnels` (-L / -R / -D mit SOCKS5, eigene Pump-Threads)
- `ollama` · `cve` (OSV.dev)

### gui/ (PySide6 → Qt6 Widgets)
| Modul | Inhalt |
|---|---|
| `style` | alle 4 Themes + benutzerdefinierte, QSS 1:1, Theme-Editor-API |
| `app` · `main_window` | Tabs, Menüs (Aktionen/Tools/Ansicht/Hilfe), Toolbar, Statusleiste |
| `workspace` | Tab = zwei Panes + zwei Konsolen + Verbindung, CWD-Sync bidirektional |
| `file_panel` | Navigation, F3–F8, Kontextmenü, Eigenschaften, **Lesezeichen ☆/★ pro Server**, Wildcard-Filter (`Strg+F`), klickbare Spalten-Sortierung, **Bild-Vorschau**, **sudo-Chip** |
| `console_panel` | **Modus „Befehle“ ⇄ „Terminal“**, Historie ↑/↓, `cd`-Auflösung, Strg+C |
| `ansi` | ANSI/VT-Renderer (SGR 16/256/Truecolor, CR/LF, Zeile löschen) |
| `shell_backends` | lokales PTY über **Windows ConPTY** (ersetzt pywinpty), Remote via SSH-Shell |
| `terminal_widget` | echtes interaktives Terminal: Farben, Tasten/Steuerzeichen, Scrollback, Copy/Paste |
| `transfer_manager` · `transfer_dialog` | Queue mit Fortschritt, Tempo, ETA, Verifikation, Abbrechen/Wiederholen |
| `command_palette` · `command_builder` | sortierbarer Katalog mit OS-Filter + Assistent mit Live-Vorschau und sudo |
| `history_dialog` | Verlauf & Favoriten |
| `search_dialog` | Datei-/Inhalts-Suche mit Live-Ergebnissen, Abbruch |
| `bulk_rename_dialog` | alle Regeln, Live-Vorschau, Konflikte, gefahrlose Reihenfolge |
| `filediff_dialog` | farbiger Unified-Diff |
| `settings_dialog` | Allgemein · KI (Ollama testen/Modelle laden) · Tastenkürzel (Dublettenprüfung) |
| `known_hosts_dialog` | TOFU-Store ansehen/bereinigen |
| `properties_dialog` | Größe/Owner/Datum + chmod-Editor (rwx + Oktal) |
| `editor_dialog` · `highlighter` | Editor mit Zeilennummern, Syntax-Highlighting (JSON/XML/YAML/Python/INI/Shell), Suchen/Ersetzen, Gehe-zu-Zeile, Großdatei-Schutz |
| `tunnel_dialog` | SSH-Tunnel öffnen/stoppen (-L/-R/-D) |
| `bookmarks_dialog` | Lesezeichen anspringen/entfernen/exportieren/importieren |
| `diff_dialog` | Verzeichnis-Vergleich/Sync, optional rekursiv, Angleichen über die Queue |
| `netscan_dialog` | Netzwerk-Scanner: Ziele/Ports, Live-Hosts, MAC/Hersteller, Freigaben, Web |
| `venv_dialog` | venv/pipenv anlegen & aktivieren, bekannte Umgebungen verwalten |
| `encoding_converter_dialog` | Zeichensatz-Konverter inkl. EBCDIC, Auto-Erkennung, Vorschau |
| `security_dialog` | CVE-Audit: OS/Pakete/sshd/ufw/Ports/Konten + OSV.dev-Abgleich |
| `plugins_dialog` | Externe Plugins verwalten und testen |
| `ai_chat_panel` | KI-Chat (lokales Ollama) mit Folgefragen; Buttons in Konsole und Editor |
| `theme_editor_dialog` | eigene Farbschemata anlegen/bearbeiten/löschen mit Live-Vorschau |
| `clipboard_manager` | Historie kopierter Texte, Eintrag aktiv setzen/einfügen |
| `help_dialog` | Tastenkürzel-Übersicht + Handbuch mit Themenliste und Volltextsuche |
| `tab_favorites_dialog` | Tab-Konstellationen sichern/wiederherstellen |
| `filealarm_dialog` | Verzeichnis-Überwachung (Polling) mit Ereignisliste |
| `githubalarm_dialog` | Repo-Überwachung auf neue Pushes, Token im Schlüsselbund |
| `macro_manager_dialog` · `macro_key_editor` | Tastenraster mit Layern, Bearbeiten-/Ausführen-Modus, kontextabhängiger Layerwechsel, Icon/Beschriftung/Aktion je Taste inkl. Sequenz-Editor |
| `minimap` | Editor-Minimap mit sichtbarem Bereich und farbigen Suchtreffern |

## Build & Toolchain
- CMake + Ninja + MSVC 2022; Qt 6.8.2 (`C:\Qt\6.8.2\msvc2022_64`)
- libssh2 1.11 via CMake FetchContent (WinCNG-Crypto, kein OpenSSL nötig)
- `windeployqt` läuft automatisch nach dem Build
- Bauen: `.\build.ps1` (bzw. `-Fresh` für Neubau)
- Konfiguration unter `%APPDATA%\ncssh` — **formatkompatibel** mit der Python-Version

Dazu im `file_panel`/`workspace` integriert: **Drag & Drop** (zwischen den Panes,
auch remote, sowie aus dem Explorer), **abdockbare Konsolen-Spalte** („⤢") und der
**Netzwerk-Modus** einer Pane (`net://` — Hosts aus dem Scanner, Freigaben, Dateien).

## Bekannte Einschränkungen

- Die libssh2-Schicht (Auth, SFTP, PTY, Tunnel) ist gegen die Semantik des Originals
  gebaut, aber **noch nicht gegen einen echten Server getestet** — dort ist am ehesten
  Nacharbeit zu erwarten.
- Globale Makro-Hotkeys (systemweite Tastenkürzel außerhalb des Fensters) sind nicht
  umgesetzt; im Makro-Manager lösen Tasten per Klick aus.
- Der Terminal-Modus rendert über einen ANSI-Parser auf `QPlainTextEdit`, nicht über
  ein volles Zeichengitter wie pyte — Vollbild-TUIs (`htop`, `vim`) laufen, exotische
  Cursor-Steuerung kann abweichen.
