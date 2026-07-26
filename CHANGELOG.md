# Änderungen

Format nach [Keep a Changelog](https://keepachangelog.com/de/1.1.0/),
Versionierung nach [SemVer](https://semver.org/lang/de/).

## [1.0.0] – Beta (unveröffentlicht)

Erste Fassung: vollständiger Port des Python/PySide6-Vorgängers nach C++20/Qt 6
mit libssh2, anschließend über dessen Funktionsumfang hinaus ausgebaut.

### Neu gegenüber der Python-Fassung

- **Terminal-Emulator**: vollwertiges VT100/xterm-Zellengitter mit Cursor-
  Adressierung, Scrollregionen, Autowrap und Alternate-Screen — `vim`, `htop`,
  `tmux` und `less` werden korrekt dargestellt.
- **ProxyJump/Bastion**: Verbindung über einen Sprung-Host (`direct-tcpip`-Kanal
  mit eigenem Pump-Thread).
- **known_hosts-Interop**: Host-Keys werden gegen OpenSSHs `~/.ssh/known_hosts`
  geprüft und dort auf Wunsch eingetragen.
- **Verbindungs-Feinsteuerung pro Profil**: Keepalive, Timeout, Kompression,
  Chiffren, Schlüsseltausch.
- **Krypto-Backend wählbar**: optional OpenSSL 3 statt WinCNG
  (`-DUSE_OPENSSL_BACKEND=ON`) und damit ed25519/curve25519.
- **Übertragungen**: Bandbreiten-Limit sowie Pause/Fortsetzen mit Wiederaufnahme
  am Ziel-Offset.
- **Symlinks anlegen** (lokal und über SFTP).
- **Tunnel-Presets** aus dem Serverprofil öffnen automatisch beim Verbinden.
- **SFTP-Batch**: skriptbare Dateiaufgaben mit Editor, Live-Log und
  Intervall-Wiederholung.
- **Alarm Trigger** erweitert: Überwachung entfernter Verzeichnisse über die
  aktive SSH-Verbindung, Glob-Filter je Alarm, Befehl bei Auslösung sowie
  Desktop-Benachrichtigung und Signalton.

### Behoben

Während des Ports gefundene und korrigierte Fehler:

- SSH-Keepalive wurde vor dem Handshake gesetzt und brach jeden Schlüsseltausch
  ab („Unable to exchange encryption keys").
- ECDSA/ECDH fehlten im WinCNG-Build; moderne Server waren dadurch nicht
  erreichbar.
- Fehlender `keyboard-interactive`-Fallback bei Passwort-Anmeldung (PAM).
- Wiederaufnahme abgebrochener Übertragungen kopierte in Wahrheit neu.
- Abbrechen einer Übertragung war wirkungslos (Task-Handle wurde nie abgelegt).
- Tastenkürzel-Einstellungen wurden nie gelesen; sechs weitere Optionen waren
  ohne Wirkung.
- Makro-Sequenzen wurden angelegt, aber nie ausgeführt.

### Sicherheit

- Host-Key-Prüfung findet **vor** jeder Authentifizierung statt — bei
  unbekanntem oder geändertem Schlüssel gehen keine Zugangsdaten an den Server.
- Abwehr von Path-Traversal in SFTP-Verzeichnislisten.
- Passwörter und Schlüsselmaterial werden nach Gebrauch im Speicher überschrieben;
  Passwörter liegen im Windows Credential Manager, nicht in Konfigurationsdateien.
- Symlinks werden beim rekursiven Kopieren nicht verfolgt.
- Temporäre Dateien werden beim Beenden entfernt.

### Bekannte Grenzen

Siehe [README.md](README.md#bekannte-grenzen) — im Kern: nur Windows, im
Standard-Build kein ed25519/curve25519, kein Agent-Forwarding (libssh2-seitig
nicht möglich), keine systemweiten Makro-Hotkeys.
