# Portierungs-Status: SSHIT-Commander → SSHIT-Commander2 (C++)

Stand des Umbaus von Python/PySide6 nach C++/Qt6 + libssh2.

> **Wichtige Einordnung.** Die Portierung ist **abgeschlossen**: jedes Modul des
> Originals hat ein C++-Gegenstück, der Kern (`core/`, `net/`) ist inhaltlich
> nachgezogen und durch **186 Tests** abgesichert. Ein systematischer
> Interaktions-, Menü-, Einstellungs- und Funktions-Audit gegen das Original ist
> durchgeführt: Tasten-/Kontextmenü-Feinheiten, das Strg+F9-Status-Feature,
> sechs wirkungslose Einstellungen, mehrere Persistenz-Lücken (Pane-Ausrichtung,
> Spaltenbreiten, Konsolen-Zustand) und echte Funktionsfehler (Transfer-Resume
> war ein No-Op; keyboard-interactive-Auth fehlte) sind behoben.
>
> Der Port hat inzwischen **mehr UI-Texte als das Original** (1692 gegen 1352) —
> er ist der größere Funktionsumfang. Die rechnerisch 131 offenen Katalog-
> Einträge sind kein Funktionsrückstand, sondern Status-Meldungstexte und
> abweichende Beschriftungen (`Neues Verzeichnis` → **Neuer Ordner**). Details
> und Stichproben: [GAPS.md](GAPS.md).

## Portiert

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
| `ansi` | ANSI/VT-Renderer für den Zeilenpuffer (SGR 16/256/Truecolor, CR/LF, Zeile löschen) |
| `core/terminal_emulator` | **VT100/xterm-Zellengitter** — Cursor-Adressierung, ED/EL, IL/DL, ICH/DCH/ECH, Scrollregion (DECSTBM), Autowrap, Alternate-Screen, DECCKM/DECTCEM/DECOM, OSC-Titel (20 Tests) |
| `shell_backends` | lokales PTY über **Windows ConPTY** (ersetzt pywinpty), Remote via SSH-Shell |
| `terminal_widget` | echtes interaktives Terminal: Farben, Tasten/Steuerzeichen, Scrollback, Copy/Paste, Linien-Cursor; schaltet bei Alternate-Screen auf das Zellengitter um (vim/htop/tmux/less) |
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
| `dir_chooser` | Ordner-Browser **über die Provider-Abstraktion** — funktioniert auch auf entfernten Servern |
| `confirm_dialog` | Bestätigung mit Quelle→Ziel vor Kopieren/Verschieben/Löschen; Zielordner wählbar, bei einer Datei auch der Zielname |
| `key_dialog` | SSH-Schlüssel erzeugen (Ed25519/RSA/ECDSA) und OpenSSH↔PPK konvertieren |
| `host_key_dialog` | Host-Key-Bestätigung mit Fingerprint, „nur diesmal" vs. „vertrauen und speichern" |
| `preview_panel` | Vorschau der markierten Datei (Text/Bild) unter der Pane, über *Ansicht → Vorschau* |
| `file_dialogs` | erzwingt den nicht-nativen Qt-Dialog, damit das App-Theme greift (projektweit eingesetzt) |
| `icons` · `file_icons` | gezeichnete Vektor-Icons für die Toolbar + echte Shell-Icons je Dateityp |

## Build & Toolchain
- CMake + Ninja + MSVC 2022; Qt 6.8.2 (`C:\Qt\6.8.2\msvc2022_64`)
- libssh2 1.11 via CMake FetchContent; Krypto standardmäßig **WinCNG** (kein
  OpenSSL nötig). Optional `-DUSE_OPENSSL_BACKEND=ON` → OpenSSL 3 statisch, damit
  **ed25519/curve25519** verfügbar sind (siehe [PORTING.md](PORTING.md))
- `windeployqt` läuft automatisch nach dem Build
- Bauen: `.\build.ps1` (bzw. `-Fresh` für Neubau)
- Konfiguration unter `%APPDATA%\ncssh` — **formatkompatibel** mit der Python-Version

Dazu im `file_panel`/`workspace` integriert: **Drag & Drop** (zwischen den Panes,
auch remote, sowie aus dem Explorer), **abdockbare Konsolen-Spalte** („⤢") und der
**Netzwerk-Modus** einer Pane (`net://` — Hosts aus dem Scanner, Freigaben, Dateien).

## Vollständigkeit

Jedes Modul des Python-Originals hat ein C++-Gegenstück — auf **Modulebene** ist
die Portierung vollständig (nachgeprüft: die einzigen Namen ohne Gegenstück sind
Python-Artefakte wie `__init__`/`__main__` sowie die unten aufgeführten
Zusammenlegungen). Auf **Funktionsebene** gilt das ebenfalls: die verbleibenden
Katalog-Unterschiede sind Meldungstexte und abweichende Beschriftungen, keine
fehlenden Funktionen — Nachweis und Stichproben in [GAPS.md](GAPS.md).

Einige kleine Module wurden bewusst in die Datei zusammengelegt, zu der sie
gehören:

| Original | liegt jetzt in |
|---|---|
| `clipboard_dialog` | `clipboard_manager` |
| `code_editor` | `editor_dialog` |
| `console_widget` | `console_panel` |
| `filealarm_manager` | `filealarm_dialog` |
| `githubalarm_manager` | `githubalarm_dialog` |
| `image_viewer` | `file_panel` (F3) + `preview_panel` |
| `tunnel_manager` | `tunnel_dialog` |
| `user_guide_dialog` | `help_dialog` (30 Themen) |

Nicht portiert wurde `src/ncssh/ui/` (die optionale Textual-TUI, 7 Dateien /
684 Zeilen) — die Qt-GUI ist die einzige Oberfläche. Begründung in
[PORTING.md](PORTING.md).

### Außerhalb von `src/`

| Original | Port |
|---|---|
| `tests/` (15 Dateien, 120 Tests) | `tests/` — eigenes Harness + CTest, **186 Tests**; alle 15 Testdateien abgedeckt, plus ansi/keytools/shortcuts/tunnels/secrets/profiles/ssh_algs/macro_dock/terminal_emulator/sftp_batch |
| `smoke_gui.py` | `tests/test_smoke_gui.cpp` — Offscreen-Test der Oberfläche (Kernlogik deckt der Rest ab) |
| `tools/i18n_extract.py` | portiert — sucht `_t("…")` in den C++-Quellen |
| `i18n/en.json` | **1692 Schlüssel, 100 % übersetzt**, keine verwaisten Einträge |
| `pyproject.toml`, `Pipfile`, `build-nuitka.ps1` | ersetzt durch `CMakeLists.txt`, `build.ps1`, `test.ps1` |

Tests laufen mit `.\test.ps1` (oder `ctest --test-dir build`).

## Über die Portierung hinaus

Diese Funktionen hat das Python-Original **nicht**; sie sind nach Abschluss der
Portierung ergänzt worden:

| Bereich | Ergänzung |
|---|---|
| Terminal | voller VT100/xterm-Emulator für Vollbild-Anwendungen (s. o.) |
| SSH | **ProxyJump/Bastion** (direct-tcpip + Pump-Thread), **known_hosts-Interop** mit OpenSSH (lesen *und* schreiben), Verbindungs-Feinsteuerung pro Profil (Keepalive, Timeout, Kompression, Chiffren, Schlüsseltausch) |
| Krypto | optionales **OpenSSL-Backend** für ed25519/curve25519 (`-DUSE_OPENSSL_BACKEND=ON`) |
| Transfers | **Bandbreiten-Limit** und **Pause/Fortsetzen** (Resume am Ziel-Offset) |
| Dateien | **Symlink anlegen** (lokal + SFTP) |
| Tunnel | im Profil gespeicherte Presets öffnen **automatisch beim Verbinden** |
| Automatisierung | **SFTP-Batch** mit Skript-Editor, Live-Log und Intervall-Wiederholung |
| Alarm Trigger | **Remote-/SFTP-Überwachung**, Glob-Filter, Befehl bei Auslösung, Desktop-Benachrichtigung + Ton |

## Bekannte Einschränkungen

- Verbleibende GUI-Abweichungen sind überwiegend **Status-Meldungs­texte** und
  abweichende Beschriftungen; die funktionalen Bedienelemente, Menüs und
  Einstellungen sind nachgezogen. Aufstellung in [GAPS.md](GAPS.md).
- Die libssh2-Schicht ist **gegen einen echten OpenSSH-Server validiert**
  (Handshake/KEX, Host-Key, Auth, SFTP). Dabei behoben: Keepalive vor dem Handshake
  (brach jeden KEX ab), fehlendes ECDSA/ECDH im WinCNG-Build,
  keyboard-interactive-Auth-Fallback und ein Transfer-Resume, das in Wahrheit neu
  kopierte. **Nicht** breit getestet sind exotische Auth-/SFTP-/Tunnel-Kombinationen
  gegen diverse Server; ebenfalls ungeprüft sind ProxyJump (mangels Bastion-Host)
  und ein ed25519-Handshake (mangels passendem Server).
- Im Standard-Build (WinCNG) fehlen **ed25519-Hostkeys und curve25519**. Server mit
  ecdsa/rsa-Hostkey und ecdh/DH funktionieren — das ist die Standardkonfiguration
  von OpenSSH. Nur Server, die *ausschließlich* ed25519/curve25519 anbieten, sowie
  ed25519-**Login-Keys** brauchen den OpenSSL-Build.
- **Agent-Forwarding** ist mit libssh2 nicht umsetzbar: die Bibliothek kann das
  Forwarding zwar anfordern, aber die vom Server zurückgeöffneten
  `auth-agent@openssh.com`-Kanäle nicht annehmen. Die Checkbox ist deshalb bewusst
  deaktiviert statt scheinbar vorhanden.
- Globale Makro-Hotkeys (systemweite Tastenkürzel außerhalb des Fensters) gibt es
  nicht — **im Original ebenso wenig**: dessen `_hotkey()` *sendet* Tastenkombinationen
  an andere Anwendungen, es *registriert* keine systemweiten Kürzel; Makrotasten lösen
  dort wie hier per Klick aus. Das ist also keine Portierungslücke, sondern eine
  Eigenschaft beider Programme.
- Die **Textual-TUI** des Originals (`src/ncssh/ui/`) ist bewusst nicht portiert —
  Begründung in [PORTING.md](PORTING.md).
