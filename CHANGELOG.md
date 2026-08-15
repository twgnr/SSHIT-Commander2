# Änderungen

Format nach [Keep a Changelog](https://keepachangelog.com/de/1.1.0/),
Versionierung nach [SemVer](https://semver.org/lang/de/).

## [1.0.0] – Beta (unveröffentlicht)

Erste Fassung.

### Funktionen

- **Dateiverwaltung** in zwei Panes (lokal ⇄ remote): Kopieren/Verschieben mit
  Fortschritt, Bandbreiten-Limit und Pause/Fortsetzen mit Wiederaufnahme am
  Ziel-Offset, Drag & Drop, Massen-Umbenennen, Verzeichnis- und Dateivergleich,
  Prüfsummen, ZIP, Symlinks (lokal und über SFTP), Lesezeichen je Server.
- **SSH/SFTP**: Profilverwaltung, Anmeldung per Passwort, Schlüssel oder Agent,
  PuTTY-PPK-Import, ProxyJump über einen Sprung-Host, Port-Weiterleitungen
  (`-L`/`-R`/`-D` mit SOCKS5), sudo-Dateisystem, Verbindungs-Feinsteuerung pro
  Profil (Keepalive, Timeout, Kompression, Chiffren, Schlüsseltausch).
- **Terminal** mit vollem VT100/xterm-Emulator: Cursor-Adressierung,
  Scrollregionen, Autowrap und Alternate-Screen, sodass `vim`, `htop`, `tmux`
  und `less` korrekt dargestellt werden. Lokales PTY über ConPTY, entfernt über
  die SSH-Shell.
- **Automatisierung**: SFTP-Batch mit Skript-Editor, Live-Log und
  Intervall-Wiederholung; Verzeichnis-Alarme lokal und über die aktive
  SSH-Verbindung, mit Glob-Filtern, Befehlsauslösung, Desktop-Benachrichtigung
  und Signalton; Makro-Manager mit Layern.
- **Werkzeuge**: Editor mit Syntax-Hervorhebung, Datei- und Inhaltssuche,
  Netzwerk-Scanner, CVE-Audit über OSV.dev, Zeichensatz-Konverter inkl. EBCDIC,
  venv-Verwaltung, KI-Chat über ein lokales Ollama.
- **Krypto-Backend wählbar**: standardmäßig WinCNG, optional OpenSSL 3
  (`-DUSE_OPENSSL_BACKEND=ON`) und damit ed25519/curve25519.
- Oberfläche in Deutsch und Englisch, vier Themes plus eigene.

### Sicherheit

- Host-Key-Prüfung findet **vor** jeder Authentifizierung statt — bei
  unbekanntem oder geändertem Schlüssel gehen keine Zugangsdaten an den Server.
  Interoperabel mit OpenSSHs `~/.ssh/known_hosts` (lesend und schreibend).
- Abwehr von Path-Traversal in SFTP-Verzeichnislisten.
- Passwörter und Schlüsselmaterial werden nach Gebrauch im Speicher
  überschrieben; Passwörter liegen im Windows Credential Manager, nicht in
  Konfigurationsdateien.
- Symlinks werden beim rekursiven Kopieren nicht verfolgt.
- Temporäre Dateien werden beim Beenden entfernt.

### Bekannte Grenzen

Siehe [README.md](README.md#bekannte-grenzen) — im Kern: nur Windows, im
Standard-Build kein ed25519/curve25519, kein Agent-Forwarding (libssh2-seitig
nicht möglich), keine systemweiten Makro-Hotkeys.
