# Offene Punkte der Portierung

Erhoben am 22.07.2026 durch Vergleich der **i18n-Kataloge** beider Projekte.
Das ist der belastbarste Massstab, den es hier gibt: in beiden Projekten ist der
deutsche Quelltext selbst der Schluessel, also lassen sich die sichtbaren
Oberflaechen-Elemente eins zu eins gegenueberstellen.

| | Anzahl |
|---|---|
| UI-Texte im Python-Original | 1352 |
| UI-Texte im C++-Port | 1106 |
| in beiden identisch | 732 |
| **nur im Original — nicht portiert** | **620** |
| nur im Port (Umformulierungen + Neues) | 374 |

Ein Teil der 620 sind Umformulierungen (dann steckt die Funktion unter anderem
Wortlaut im Port). Der Rest sind echte Luecken. Stichproben im Code bestaetigen
die Groessenordnung — folgende Begriffe kommen im gesamten C++-Baum **gar nicht**
vor:

* `breadcrumb` / Pfadleiste zum Klicken
* `thumbnail` / Bild-Vorschau als Icon in der Dateiliste
* Type-Ahead-Suche in der Pane
* Vor/Zurueck-Navigation (Alt+Links / Alt+Rechts)
* „Nach Muster markieren" / „Markierung aufheben" (Num +/-), Auswahl umkehren
* Kachelansicht, Spaltenauswahl im Kopfzeilen-Menue
* „Oeffnen mit …", „Rechte aendern …" im Kontextmenue
* Terminal: Zeichengitter, Mitschnitt (Logging), Suche im Puffer, Maus-Auswahl
* „Nur Terminal anzeigen" / „Nur Dateisystem anzeigen", „Panes untereinander"
* „Ueber SSHIT-Commander" (Info-Dialog)

Ausserdem existieren einige Funktionen **nur im Kern**, ohne Anbindung an die
Oberflaeche: ZIP erstellen, Archiv entpacken, Pruefsumme berechnen,
Wake-on-LAN, RDP oeffnen (alle in `core/`, aber in keinem Menue).

## Groessenvergleich der Module

Zum Abgleich die Zeilenzahlen (ohne Kommentar-/Leerzeilen). C++ ist normalerweise
laenger als Python — ein Verhaeltnis deutlich unter 1,0 ist daher ein Warnsignal:

| Modul | Python | C++ | Verhaeltnis |
|---|---|---|---|
| `gui/main_window` | 2434 | 520 | 0,21 |
| `gui/terminal_widget` | 527 | 182 | 0,35 |
| `gui/file_panel` | 1689 | 697 | 0,41 |
| `gui/user_guide_dialog` -> `help_dialog` | 940 | 425 | 0,45 |
| `gui/settings_dialog` | 524 | 243 | 0,46 |
| `gui/bulk_rename_dialog` | 506 | 248 | 0,49 |
| `gui/macro_manager_dialog` | 1062 | 539 | 0,51 |

Konkret beim `bulk_rename_dialog` nachgeprueft — im Port fehlen: die gesamte
Filter-/Sammelgruppe (Endungen, Namensmuster, rekursiv, Ordner, versteckte
Dateien, „nur markierte"), die 14 Regex-Vorlagen, Entfernen/Zuschneiden,
Einfuegen an Position, Geltungsbereich (Name/Endung/ganz), Zaehler-Reset pro
Ordner, die Regex-Fehleranzeige und **Rueckgaengig**. Der Kern
(`core/bulkrename`) kann all das bereits — nur der Dialog fragt es nicht ab.

## Nicht portierte UI-Texte je Modul

| Modul | Anzahl |
|---|---|
| `gui/main_window.py` | 114 |
| `gui/file_panel.py` | 73 |
| `gui/server_manager.py` | 42 |
| `gui/settings_dialog.py` | 42 |
| `gui/help_dialog.py` | 31 |
| `gui/netscan_dialog.py` | 31 |
| `gui/search_dialog.py` | 31 |
| `gui/security_dialog.py` | 24 |
| `gui/venv_dialog.py` | 23 |
| `gui/encoding_converter_dialog.py` | 18 |
| `gui/editor_dialog.py` | 16 |
| `gui/filealarm_dialog.py` | 16 |
| `gui/console_widget.py` | 14 |
| `gui/transfer_dialog.py` | 12 |
| `gui/theme_editor_dialog.py` | 11 |
| `gui/tunnel_dialog.py` | 10 |
| `gui/tab_favorites_dialog.py` | 9 |
| `gui/diff_dialog.py` | 8 |
| `gui/ai_chat_panel.py` | 7 |
| `gui/host_key_dialog.py` | 7 |
| `gui/properties_dialog.py` | 7 |
| `gui/command_palette.py` | 6 |
| `gui/githubalarm_dialog.py` | 6 |
| `gui/command_builder.py` | 5 |
| `gui/clipboard_dialog.py` | 4 |
| `gui/console_panel.py` | 4 |
| `gui/confirm_dialog.py` | 3 |
| `gui/preview_panel.py` | 3 |
| `gui/user_guide_dialog.py` | 3 |
| `gui/bookmarks_dialog.py` | 2 |
| `gui/history_dialog.py` | 2 |
| `gui/filediff_dialog.py` | 1 |
| `gui/known_hosts_dialog.py` | 1 |

### Vollstaendige Listen

<details><summary><code>gui/main_window.py</code> — 114</summary>

*   ·  {count} Tunnel
* &Aktionen
* &Ansicht
* &Hilfe
* &Panes
* &Tools
* (Binärdatei — keine Vorschau)
* (Vorschau nicht verfügbar)
* Abgebrochen — Host-Key nicht bestätigt.
* Aktiver Eintrag gesetzt — Strg+V fügt ihn in die aktive Pane ein.
* Aktiver Text in der Zwischenablage — mit Strg+V einfügen.
* Alarm Trigger …
* Alarm Trigger: {name}
* Alarm ausgelöst — zum Anzeigen klicken
* Archivname:
* Befehl an beide Konsolen …
* Befehl an beide Konsolen:
* Bekannte Host-Keys …
* Benutzername
* Benutzername für {host}:
* Bitte Dateien/Ordner auswählen.
* Bitte eine Datei auswählen.
* Broadcast
* Datei-Encoding konvertieren …
* Datei-Suche (Name) …
* Datei-Vergleich …
* Destruktiver Befehl
* Die Datei ist leer.
* Diese Seite ist nicht verbunden.
* Einstellungen gespeichert (Pane-Schrift sofort; Terminal/Editor ab nächstem Öffnen).
* Einstellungen …
* Fehlgeschlagen: {error}
* Frage (leer = Datei erklären):
* Gespeichert: {path}
* GitHub Repo Alarm …
* GitHub: neue Daten — zum Anzeigen klicken
* GitHub: {repo}
* Host-Key bekannt und gepinnt
* Host-Key neu / unbestätigt
* Host-Key-Prüfung deaktiviert (unsicher)
* Inhalts-Suche (grep) …
* Ja, alle
* KI-Fehleranalyse: {path}
* KI-Frage
* KI: {path}
* Keine Ausgabe zum Analysieren vorhanden.
* Key-Passphrase
* Lesezeichen exportieren …
* Lesezeichen exportiert: {path}
* Lesezeichen importieren …
* Links und rechts je eine Datei markieren.
* Lokales Dateisystem
* Löschen fehlgeschlagen
* Massen-Umbenennen …
* Nein, alle
* Netzwerkscanner …
* Neue Datei
* Neue Daten im Repository ({when})
* Neues Verzeichnis
* Nichts ausgewählt.
* Nichts zu übertragen (alle übersprungen).
* Nur Dateisystem anzeigen
* Nur Terminal anzeigen
* Panes untereinander anzeigen
* Passphrase für den Schlüssel: ⏎ {path}
* Passwort für {target}:
* Prüfsumme
* Quelle nicht mehr vorhanden — Eintrag entfernt.
* SSHIT-Commander
* Server-Profil gespeichert: {name}
* Sicherheits-Audit (CVE) …
* Speichern unter (Remote-Pfad)
* Tab umbenennen …
* Tab-Favoriten — Tab-Layouts speichern/öffnen
* Terminalausgabe erklären
* Text in der Zwischenablage — mit Strg+V einfügen.
* Tunnel
* Umgebung aktiviert: {path}
* Umgebung gelöscht: {path}
* Verbinde zu {target} …
* Verbindung fehlgeschlagen
* Verbindung getrennt: {label}
* Verbindung trennen
* Verbindung verloren — verbinde neu: {target}
* Verbindung zu {label} trennen?
* Verbindung(en) dieses Tabs trennen? ⏎ {labels}
* Verbunden: {label}  ·  {os}
* Verschieben
* Verzeichnis-Vergleich …
* Vorschau-Panel
* ZIP erstellen
* ZIP erstellt: {name} ({n} Einträge)
* ZIP nur für lokale Ordner.
* ZIP-Archiv erstellen
* Zielpfad:
* sudo-Authentifizierung fehlgeschlagen.
* sudo-Passwort für {host}:
* venv verwalten …
* venv wird im Terminal angelegt: {path}
* {count} Datei(en) übertragen
* {count} Lesezeichen importiert.
* {count} Objekt(e) unwiderruflich löschen?
* {done} übertragen, {error} fehlgeschlagen
* {done} übertragen, {error} fehlgeschlagen — Details in den Übertragungen
* {n} Repo(s) mit neuen Daten — zum Anzeigen klicken
* {n} gelöscht
* {n} geändert
* {n} neu
* Über SSHIT-Commander
* Über SSHIT-Commander …
* Überschreiben?
* Übertragung abgeschlossen
* Übertragung fertig
* Übertragungen anzeigen

</details>

<details><summary><code>gui/file_panel.py</code> — 73</summary>

*  · Ordner: {pre}{size}
* (keine Lesezeichen)
* Aktualisieren (Ctrl+R)
* Aktuellen Pfad als Lesezeichen (pro Server)
* Alles markieren (Strg+A)
* Als Server-Profil speichern
* Anderes Programm wählen …
* Ansehen (F3)
* Anzeige filtern (Ctrl+F) — Wildcards * ? möglich, z.B. *.py
* Ausführen (Standardprogramm)
* Auswahl umkehren (Num *)
* Bearbeiten (F4)
* Dienste
* Diese Pane mit sudo-Rechten (root) anzeigen
* Eigenschaften …
* Einfügen (Ctrl+V)
* Erneut scannen
* Filter…
* Git-Status: {code}
* IP kopieren
* Ja
* Kontextmenü: Weboberfläche im Browser öffnen
* Kopieren (Ctrl+C)
* Kopieren → andere Pane (F5)
* Latenz
* Laufwerk / Mountpunkt wechseln
* Lesezeichen dieses Servers
* Löschen (F8)
* Markieren
* Markierung nach Muster aufheben
* Markierung nach Muster aufheben … (Num -)
* Muster (Wildcards, z.B. *.jpg):
* Nach Muster markieren
* Nach Muster markieren … (Num +)
* Nein
* Netzwerkscanner schließen
* Netzwerkscanner-Modus schließen
* Neue Datei …
* Neuer Ordner (F7)
* Offene Ports: {ports}
* Pfad kopieren
* Prüfsumme berechnen …
* RDP wird nur unter Windows unterstützt.
* RDP öffnen
* Rechte ändern …
* Rechts in die leere Fläche klicken, um den Pfad einzugeben
* SSH verbinden (Port 22 nicht offen)
* Spalten anzeigen
* Standardprogramm
* Suche Freigaben auf {host} …
* Symlink
* Umbenennen / Verschieben (F6)
* Verbindung dieser Seite trennen
* Verschieben → andere Pane
* Verwalten…
* Vor (Alt+→)
* Wake-on-LAN fehlgeschlagen.
* Wake-on-LAN gesendet an {mac}
* Wake-on-LAN senden
* Weboberfläche öffnen
* ZIP-Archiv erstellen …
* Zugriff
* Zurück (Alt+←)
* [Netzwerk] · Scan
* remote
* {base} · ausgewählt: {name} ({size})
* {count} Einträge ({dirs} Ordner, {files} Dateien)
* {n} markiert · {size}  ·  {base}
* Öffnen mit
* Übergeordneter Ordner
* ⏏ Trennen
* ★ Aktuellen Pfad merken
* ✕ Netzwerkscanner

</details>

<details><summary><code>gui/server_manager.py</code> — 42</summary>

*  ⏎  ⏎ Hinweis: Passwörter werden nicht übernommen.
* Anzeigename *
* Aus Datei importieren …
* Ausgewählte importieren
* Auth
* Authentifizierung
* Beim ersten Mal vertrauen (accept-new)
* Datei konnte nicht gelesen werden: {error}
* Erreichbarkeit
* Erreichbarkeit testen
* Es wurde keine Verbindung ausgewählt.
* Export-Dateien (*.reg *.ini);;Alle Dateien (*.*)
* Exportierte PuTTY-/WinSCP-Datei wählen
* Fehlende Angaben
* Filtern … (Name, Host, Benutzer)
* Gefundene Verbindungen auswählen, die importiert werden sollen:
* Host *
* Host-Key-Prüfung
* Ignorieren (unsicher)
* Import (PuTTY/WinSCP/SSH)
* Keine Sitzungen automatisch gefunden
* Keine Sitzungen in der Datei gefunden.
* Key-Pfad
* Name und Host sind Pflicht.
* Neuer Server
* Passwort/Passphrase sicher im OS-Keyring speichern
* Profil '{name}' löschen?
* ProxyJump
* ProxyJump, z.B. user@jump:22  (optional)
* SSH-Agent
* SSH-Key
* Server bearbeiten
* Server-Verwaltung
* Strikt (nur bekannte)
* Tab-Farbe
* Tab-Farbe wählen …
* Verbindung
* Verbindungen importieren
* Zuletzt
* {count} Verbindung(en) importiert.
* {host}:{port} nicht erreichbar: ⏎ {error}
* {target}  (bereits vorhanden)

</details>

<details><summary><code>gui/settings_dialog.py</code> — 42</summary>

* Ausführbare Dateien farblich hervorheben
* Beim Start automatisch zum letzten Server verbinden
* Bestehende Daten der gewählten Bereiche werden überschrieben:
* Bild-Vorschau als Icon (Thumbnails)
* Datumsformat (Pane)
* Die Datei enthält keine Daten.
* Doppelte Kürzel
* Editor-Schriftgröße
* Export fehlgeschlagen
* Farbe für ausführbare Dateien
* Farbe wählen …
* Google-Modell, leistungsfähig (~5 GB)
* Guter Allrounder (~5 GB)
* Import fehlgeschlagen
* KI-Assistent aktivieren (lokales Ollama)
* Klein & schnell, guter Allrounder (~2 GB)
* Kompakt & solide (~4 GB)
* Konfiguration exportieren
* Konfiguration importieren
* Lade {model} …
* Modell laden
* Modell wählen oder Tag eintippen …
* Natürliche Sortierung (1, 2, 10)
* Pane-Schriftgröße (Dateiliste)
* Programm-Logos vor Dateinamen anzeigen
* Sehr klein und schnell (~2 GB)
* Spezialist für Code & Konfigurationsdateien (~5 GB)
* Standard-Startpfad (lokal)
* Stark & vielseitig (~5 GB)
* Startpfad wählen
* Terminal-Schriftgröße
* Teste Verbindung …
* Theme löschen
* Theme „{name}“ löschen?
* Token: DD MM YYYY YY HH24 HH12 MI SS MON — z. B. DD.MM.YYYY HH24:MI
* Verbindung testen
* Verbunden, aber kein Modell installiert — unten laden.
* Was importieren?
* leer = aktuelles Verzeichnis
* ✓ Verbunden mit Ollama {version}
* ✓ {count} Modell(e) verfügbar.
* ✓ {model} geladen.

</details>

<details><summary><code>gui/help_dialog.py</code> — 31</summary>

* Ansehen / Bearbeiten (interner Editor)
* Ausführen — mit OS-Standardprogramm öffnen
* Auswahl kopieren (Auto-Copy)
* Datei- / Inhalts-Suche
* Doppelklick
* Doppelklick / Enter
* Doppelklick / Kontextmenü
* Historie
* Inline umbenennen
* Konsole / Terminal
* Kontextmenü (Rechte, Eigenschaften, …)
* Kopieren / Einfügen (in diese Pane)
* Kopieren → andere Pane / Umbenennen
* Navigation
* Neuer Ordner / Löschen
* Neuer Tab / Tab schließen
* Ordner öffnen / Datei ausführen (Textdateien: interner Editor)
* Pane filtern
* Pane wechseln
* Panes synchronisieren / tauschen
* Rechtsklick
* Scrollback
* Shift+Bild↑ / ↓
* Shift+Einfg / Strg+Shift+V
* Space / Einfg
* Strg+Einfg / Strg+Shift+C
* Transfers / Tunnel
* Verzeichnis- / Datei-Vergleich
* Wort markieren
* Ziehen + loslassen
* Zurück / Vor

</details>

<details><summary><code>gui/netscan_dialog.py</code> — 31</summary>

* Alle wichtigen
* Auto-Rescan
* Bitte eine gültige IP-Range angeben.
* CIDR, Bereich oder Komma-Liste von IPs/Hostnamen
* Eigene
* Ergebnisse in
* Fernzugriff (22,3389,…)
* Freigaben erkennen (SMB)
* Geräte identifizieren (Banner, Web-Titel, OS, NetBIOS)
* Host-Liste automatisch in diesem Intervall neu scannen
* Hostnamen auflösen (Reverse-DNS)
* Häufige Ports
* IP-Range
* Letzten Scan laden
* Linke Pane
* MAC-Adresse + Hersteller (ARP/OUI)
* Nur SMB (139,445)
* Nur antwortende Hosts anzeigen
* Parallel
* Ping (ICMP) zusätzlich
* Port-Vorauswahl
* Ports / Bereich
* Ports und Bereiche, z. B. 22,80,443,1-1024
* Rechte Pane
* Scanne {count} Adressen …
* Stop
* Timeout je Port
* Web (80,443,…)
* aus
* {count} Host(s) gefunden …
* {count} Host(s) gefunden.

</details>

<details><summary><code>gui/search_dialog.py</code> — 31</summary>

*  (abgebrochen)
*  — max. {cap} angezeigt
* Auf Laufwerks-/Systemwurzel setzen
* Binärdateien einbeziehen
* Datei-Filter
* Dateien ausschließen
* Dateiname
* Erweitert ▴
* Erweitert ▾
* Geändert ≤ Tage
* Groß/klein ignorieren
* Inhalt (grep)
* Invertiert
* Max-Tiefe
* Min-Größe (KB)
* Name/Muster, z.B. *.log
* Nur Dateien
* Nur Dateinamen
* Nur Ordner
* Regex im Datei-Inhalt
* Regex, z.B. ^conf.*\.ya?ml$
* Root
* Text im Datei-Inhalt
* Zeilen ohne Treffer
* Zeilen vor/nach jedem Treffer
* z.B. *.min.js,*.map
* z.B. *.py,*.txt
* z.B. .git,node_modules
* {count} Treffer
* {count} Treffer …
* ∞

</details>

<details><summary><code>gui/security_dialog.py</code> — 24</summary>

* , davon {count} kritisch
* Audit nicht möglich
* Automatische Sicherheitsupdates (unattended-upgrades) sind nicht aktiviert.
* Automatische Updates
* CVE / Info
* Die Firewall (firewalld) ist inaktiv.
* Die Firewall (ufw) ist inaktiv.
* Ein Neustart steht aus (z. B. nach Kernel-Update). System neu starten.
* Eintrag auswählen für Details …
* Fehler beim Scan
* Keine Detailbeschreibung verfügbar.
* Keine Einträge.
* Keine Versionsdaten für den OSV-Abgleich gefunden.
* Keine offenen Sicherheitsupdates gefunden.
* Neustart nötig
* OSV-Online-Abgleich nicht möglich: {error}
* Online-CVE-Abgleich (OSV) für diese Distribution nicht verfügbar.
* Scanne …
* Sicherheitsupdate → {ver}
* Unbekannt
* Zertifikat ist abgelaufen oder läuft in < 21 Tagen ab — erneuern.
* läuft bald ab
* {os}  ·  Kernel {kernel}  ·  {count} Sicherheitshinweis(e)
* Öffentlich erreichbare Ports: {ports}

</details>

<details><summary><code>gui/venv_dialog.py</code> — 23</summary>

* (kein Projekt hinterlegt)
* Abhängigkeiten ignorieren (pipenv --skip-lock / pip --no-deps)
* Aktivieren
* Bekannte Umgebungen (venv/pipenv):
* Bitte einen gültigen Projektordner wählen.
* Bitte einen venv-Zielordner angeben.
* Erstellen & aktivieren
* Info
* Info speichern
* Info:
* Kurze Notiz zu dieser Umgebung (optional)
* Neue Umgebung:
* Notiz zur ausgewählten Umgebung
* Ordner mit pyproject.toml / requirements.txt …
* Projekt:
* Projektordner wählen
* Python
* Umgebung wirklich löschen (Ordner wird entfernt)? ⏎ {path}
* Wird nach dem Aktivieren der venv ausgeführt.
* Zielordner der virtuellen Umgebung
* venv
* venv-Pfad:
* venv-Zielordner wählen

</details>

<details><summary><code>gui/encoding_converter_dialog.py</code> — 18</summary>

*  (Hinweis: Kontext war zu lang und wurde gekürzt)
* Bitte einen Zielpfad angeben.
* Datei geschrieben: {path}
* Datei: {path}
* Durchsuchen …
* Encoding konvertieren
* KI repariert … (kann je nach Modell dauern)
* KI-Reparatur übernommen.
* Konvertierung
* Mit KI reparieren …
* Nach Encoding
* Neue Datei:
* Quelldatei überschreiben
* Speichern fehlgeschlagen
* Von Encoding
* Vorschau (KI-repariert) — wird beim Konvertieren gespeichert:
* Vorschau (Quelle):
* Ziel wählen

</details>

<details><summary><code>gui/editor_dialog.py</code> — 16</summary>

*   (schreibgeschützt)
*  · geändert
* '{title}' wurde geändert. Vor dem Schließen speichern?
* Datei (oder Auswahl) vom lokalen Modell erklären lassen
* Datei in einen anderen Zeichensatz umwandeln (inkl. EBCDIC)
* Datei wurde extern aktualisiert (neu geladen).
* Encoding
* Encoding konvertieren …
* Ersetzen durch
* KI Fehleranalyse
* Quellcode vom lokalen Modell auf Fehler prüfen lassen
* Umbruch
* Weiter
* Z {line}, Sp {col}   ·   {bytes} Bytes   ·   {enc}   ·   {eol}   ·   {lang}
* Zeilenende
* ⚠ Datei zu groß — schreibgeschützt geöffnet (nur der Anfang wird gezeigt).

</details>

<details><summary><code>gui/filealarm_dialog.py</code> — 16</summary>

* Alarm
* Alarm Trigger
* Alarm bearbeiten
* Anzeigename (optional)
* Bearbeiten …
* Bitte einen gültigen Ordner wählen.
* Bitte mindestens ein Ereignis auswählen.
* Ereignis
* Erkannte Änderungen:
* Neu erstellt
* Neu …
* Neuer Datei-Alarm
* Ordner mitüberwachen
* Ordner wählen
* Zu überwachender Ordner
* Überwachte Ordner — Häkchen schaltet einen Alarm an/aus:

</details>

<details><summary><code>gui/console_widget.py</code> — 14</summary>

* Befehl eingeben und Enter…   (↑/↓ = Historie, Strg+F = suchen, cd, clear)
* In Ausgabe suchen…   (Enter = weiter, Shift+Enter = zurück, Esc = schließen)
* Laufenden Befehl abbrechen (Esc)
* [Es läuft bereits ein Befehl — Stop/Esc bricht ihn ab]
* [Fehler] {msg}
* ^C abgebrochen
* cd: kein Verzeichnis: {target}
* lokal
* läuft…
* ■ abgebrochen
* ✓ fertig
* ✓ fertig (Exit 0)
* ✗ Exit {code}
* ✗ Fehler

</details>

<details><summary><code>gui/transfer_dialog.py</code> — 12</summary>

*  · ETA {time}
* Abgeschlossene entfernen
* Wiederaufnehmen
* abgebrochen
* fertig
* fertig ✓
* läuft
* wartet
* ↑ Upload
* → Lokal
* ↓ Download
* ↻ Remote

</details>

<details><summary><code>gui/theme_editor_dialog.py</code> — 11</summary>

* Ausgewählt
* Beispiel-Eintrag 1
* Beispiel-Eintrag 2
* Dieser Name ist von einem eingebauten Theme belegt.
* Ein Theme mit diesem Namen existiert bereits.
* Gedämpfter Text
* Inaktiv
* Knopf
* Neues Theme
* Theme bearbeiten
* Theme-Name

</details>

<details><summary><code>gui/tunnel_dialog.py</code> — 10</summary>

* Aktive Weiterleitungen
* Im Server-Profil speichern (Auto-Start beim Verbinden)
* Listen-Host
* Listen-Port
* Listen/Ziel
* Neue Weiterleitung über: {label}
* Tunnel öffnen
* startet…
* z.B. localhost oder DB-Host
* ⚠ Keine aktive SSH-Verbindung — bitte zuerst per F9 verbinden.

</details>

<details><summary><code>gui/tab_favorites_dialog.py</code> — 9</summary>

* Aktuelle Tabs speichern …
* Favorit speichern
* Favorit „{name}“ löschen?
* Gespeicherte Tab-Layouts:
* Keine Tabs zum Speichern.
* Name bereits vergeben oder ungültig.
* {name}  ({n} Tabs)
* „{name}“ existiert bereits. Überschreiben?
* „{name}“ mit den aktuellen Tabs überschreiben?

</details>

<details><summary><code>gui/diff_dialog.py</code> — 8</summary>

* Aktualisieren
* Links: [{llabel}] {lpath}    ·    Rechts: [{rlabel}] {rpath}
* Vergleiche rekursiv …
* Verzeichnisvergleich
* identisch
* {count} Objekt(e) in die Transfer-Queue gestellt.
* ← Links angleichen
* → Rechts angleichen

</details>

<details><summary><code>gui/ai_chat_panel.py</code> — 7</summary>

* Fehler: {msg}
* Frage eingeben …  (Strg+Enter = senden)
* KI-Assistent
* Kontext gesendet: {title}
* Modell: {model}
* Senden
* ■ Stop

</details>

<details><summary><code>gui/host_key_dialog.py</code> — 7</summary>

* <b>Host:</b> {host}:{port}<br><b>Key-Typ:</b> {algo}
* Erhalten:  
* Erwartet:  
* Fingerprint:  
* Host-Key geändert
* Trotzdem vertrauen
* unbekannt

</details>

<details><summary><code>gui/properties_dialog.py</code> — 7</summary>

* Eigenschaften — {name}
* Eigentümer
* Lesen
* Oktal: {value}
* Rechte anwenden
* Rechte ändern fehlgeschlagen
* Schreiben

</details>

<details><summary><code>gui/command_palette.py</code> — 6</summary>

* Befehlspalette — {os}
* Befehlspalette — {os}  ({count} Befehle)
* Beide
* OS:
* Suche (Name, Kategorie, Beschreibung)…
* Vorlage:  {template} ⏎ Beispiel: {example}

</details>

<details><summary><code>gui/githubalarm_dialog.py</code> — 6</summary>

* Bitte ein gültiges Repo angeben (owner/repo).
* GitHub-Token (für private Repos / höheres Limit):
* Repo bearbeiten
* Repo überwachen
* {repo} – neue Daten
* Überwachte Repositories — Häkchen schaltet an/aus:

</details>

<details><summary><code>gui/command_builder.py</code> — 5</summary>

* (leer)
* Assistent — {name}
* In Konsole einfügen
* als Benutzer (-u), leer = root
* mit sudo ausführen

</details>

<details><summary><code>gui/clipboard_dialog.py</code> — 4</summary>

* Als aktiv setzen
* Inhalt / Name
* Kopierte Texte und Dateien — Doppelklick = aktiv setzen:
* Pfad / Aktion

</details>

<details><summary><code>gui/console_panel.py</code> — 4</summary>

* Ausgabe/Fehler mit KI erklären
* ★ Verlauf
* ⤢ Abdocken
* ⤵ Andocken

</details>

<details><summary><code>gui/confirm_dialog.py</code> — 3</summary>

* Bei fertiger Übertragung benachrichtigen
* Pfad aus Lesezeichen wählen
* {count} Objekt(e) {verb}:

</details>

<details><summary><code>gui/preview_panel.py</code> — 3</summary>

*   ·  gekürzt
* (Bild kann nicht angezeigt werden)
* (keine Vorschau)

</details>

<details><summary><code>gui/user_guide_dialog.py</code> — 3</summary>

* Begriff eingeben und Enter drücken …
* Hilfe – Benutzerhandbuch
* Suchen:

</details>

<details><summary><code>gui/bookmarks_dialog.py</code> — 2</summary>

* Lesezeichen — {server}
* Pfade für <b>{server}</b>

</details>

<details><summary><code>gui/history_dialog.py</code> — 2</summary>

* Filtern…
* ★ Zu Favoriten

</details>

<details><summary><code>gui/filediff_dialog.py</code> — 1</summary>

* Dateien sind identisch.

</details>

<details><summary><code>gui/known_hosts_dialog.py</code> — 1</summary>

* Host:Port

</details>
