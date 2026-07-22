# Offene Punkte der Portierung

Erhoben durch Vergleich der **i18n-Kataloge** beider Projekte. Das ist der
belastbarste Massstab, den es hier gibt: in beiden Projekten ist der deutsche
Quelltext selbst der Schluessel, also lassen sich die sichtbaren
Oberflaechen-Elemente eins zu eins gegenueberstellen.

| | Anzahl |
|---|---|
| UI-Texte im Python-Original | 1352 |
| UI-Texte im C++-Port | 1333 |
| in beiden identisch | 895 |
| **nur im Original — noch offen** | **457** |

Zum Vergleich: die erste Erhebung ergab 620 offene Punkte. Ein Teil der
verbleibenden sind blosse Umformulierungen (die Funktion steckt dann unter
anderem Wortlaut im Port), der Rest sind echte Luecken.

## Bereits geschlossen

| Modul | vorher | jetzt |
|---|---|---|
| `gui/file_panel` | 73 | 38 |
| `gui/main_window` | 114 | 86 |
| `gui/settings_dialog` | 42 | 0 |
| `gui/server_manager` | 42 | 19 |
| `gui/search_dialog` | 31 | 0 |
| `gui/venv_dialog` | 23 | 9 |

Inhaltlich kamen dabei unter anderem dazu: Kontextmenue der Pane (rund 25 statt
7 Eintraege), Verlauf mit Vor/Zurueck, Markieren nach Muster, Tippsuche,
Zwischenablage, frei waehlbare Spalten, Breadcrumb-Pfadleiste, Kachelansicht,
Miniaturansichten, das Panes-Menue, Tab-Verwaltung, Broadcast, Konfigurations-
Im-/Export, Modell-Download, Erreichbarkeitstest, die 14 Regex-Vorlagen und
Rueckgaengig beim Massen-Umbenennen sowie Suche, Mitschnitt und Links im
Terminal. ZIP, Entpacken und Pruefsumme lagen fertig im Kern, hingen aber an
keinem Menue — das ist jetzt angebunden.

## Groessenvergleich der Module

Zeilenzahlen ohne Kommentar-/Leerzeilen. C++ faellt normalerweise laenger aus
als Python — ein Verhaeltnis deutlich unter 1,0 ist daher ein Warnsignal:

| Modul | Python | C++ | Verhaeltnis |
|---|---|---|---|
| `gui/main_window` | 2434 | 688 | 0,28 |
| `gui/macro_manager_dialog` | 1062 | 539 | 0,51 |
| `gui/user_guide_dialog` -> `help_dialog` | 940 | 425 | 0,45 |
| `gui/bulk_rename_dialog` | 506 | 427 | 0,84 |
| `gui/terminal_widget` | 527 | 448 | 0,85 |
| `gui/server_manager` | 427 | 403 | 0,94 |
| `gui/file_panel` | 1689 | 1614 | 0,96 |
| `gui/search_dialog` | 275 | 265 | 0,96 |
| `gui/settings_dialog` | 524 | 508 | 0,97 |

`main_window` ist damit die groesste verbliebene Baustelle.

## Noch offene UI-Texte je Modul

| Modul | Anzahl |
|---|---|
| `gui/main_window.py` | 86 |
| `gui/file_panel.py` | 38 |
| `gui/help_dialog.py` | 31 |
| `gui/netscan_dialog.py` | 31 |
| `gui/security_dialog.py` | 24 |
| `gui/server_manager.py` | 19 |
| `gui/encoding_converter_dialog.py` | 18 |
| `gui/editor_dialog.py` | 16 |
| `gui/filealarm_dialog.py` | 16 |
| `gui/console_widget.py` | 14 |
| `gui/transfer_dialog.py` | 12 |
| `gui/theme_editor_dialog.py` | 11 |
| `gui/tunnel_dialog.py` | 10 |
| `gui/tab_favorites_dialog.py` | 9 |
| `gui/venv_dialog.py` | 9 |
| `gui/diff_dialog.py` | 8 |
| `gui/ai_chat_panel.py` | 7 |
| `gui/host_key_dialog.py` | 7 |
| `gui/properties_dialog.py` | 7 |
| `gui/command_palette.py` | 6 |
| `gui/githubalarm_dialog.py` | 6 |
| `gui/settings_dialog.py` | 6 |
| `gui/command_builder.py` | 5 |
| `gui/search_dialog.py` | 5 |
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

<details><summary><code>gui/main_window.py</code> — 86</summary>

*   ·  {count} Tunnel
* (Binärdatei — keine Vorschau)
* (Vorschau nicht verfügbar)
* Abgebrochen — Host-Key nicht bestätigt.
* Aktiver Eintrag gesetzt — Strg+V fügt ihn in die aktive Pane ein.
* Aktiver Text in der Zwischenablage — mit Strg+V einfügen.
* Alarm Trigger …
* Alarm Trigger: {name}
* Alarm ausgelöst — zum Anzeigen klicken
* Bekannte Host-Keys …
* Benutzername für {host}:
* Bitte eine Datei auswählen.
* Datei-Encoding konvertieren …
* Datei-Suche (Name) …
* Datei-Vergleich …
* Destruktiver Befehl
* Die Datei ist leer.
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
* Lesezeichen exportiert: {path}
* Links und rechts je eine Datei markieren.
* Lokales Dateisystem
* Löschen fehlgeschlagen
* Massen-Umbenennen …
* Nein, alle
* Netzwerkscanner …
* Neue Daten im Repository ({when})
* Neues Verzeichnis
* Nichts ausgewählt.
* Nichts zu übertragen (alle übersprungen).
* Passphrase für den Schlüssel: ⏎ {path}
* Passwort für {target}:
* Quelle nicht mehr vorhanden — Eintrag entfernt.
* SSHIT-Commander
* Server-Profil gespeichert: {name}
* Sicherheits-Audit (CVE) …
* Speichern unter (Remote-Pfad)
* Tab-Favoriten — Tab-Layouts speichern/öffnen
* Terminalausgabe erklären
* Text in der Zwischenablage — mit Strg+V einfügen.
* Tunnel
* Umgebung aktiviert: {path}
* Umgebung gelöscht: {path}
* Verbinde zu {target} …
* Verbindung fehlgeschlagen
* Verbindung getrennt: {label}
* Verbindung verloren — verbinde neu: {target}
* Verbindung zu {label} trennen?
* Verbindung(en) dieses Tabs trennen? ⏎ {labels}
* Verbunden: {label}  ·  {os}
* Verzeichnis-Vergleich …
* ZIP erstellt: {name} ({n} Einträge)
* ZIP nur für lokale Ordner.
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
* Überschreiben?
* Übertragung abgeschlossen
* Übertragung fertig
* Übertragungen anzeigen

</details>

<details><summary><code>gui/file_panel.py</code> — 38</summary>

*  · Ordner: {pre}{size}
* (keine Lesezeichen)
* Aktuellen Pfad als Lesezeichen (pro Server)
* Als Server-Profil speichern
* Anzeige filtern (Ctrl+F) — Wildcards * ? möglich, z.B. *.py
* Dienste
* Diese Pane mit sudo-Rechten (root) anzeigen
* Erneut scannen
* Filter…
* Git-Status: {code}
* IP kopieren
* Ja
* Kontextmenü: Weboberfläche im Browser öffnen
* Latenz
* Laufwerk / Mountpunkt wechseln
* Lesezeichen dieses Servers
* Nein
* Netzwerkscanner schließen
* Netzwerkscanner-Modus schließen
* Offene Ports: {ports}
* RDP wird nur unter Windows unterstützt.
* RDP öffnen
* SSH verbinden (Port 22 nicht offen)
* Suche Freigaben auf {host} …
* Verbindung dieser Seite trennen
* Verwalten…
* Wake-on-LAN fehlgeschlagen.
* Wake-on-LAN gesendet an {mac}
* Wake-on-LAN senden
* Weboberfläche öffnen
* [Netzwerk] · Scan
* remote
* {base} · ausgewählt: {name} ({size})
* {count} Einträge ({dirs} Ordner, {files} Dateien)
* {n} markiert · {size}  ·  {base}
* ⏏ Trennen
* ★ Aktuellen Pfad merken
* ✕ Netzwerkscanner

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

<details><summary><code>gui/server_manager.py</code> — 19</summary>

*  ⏎  ⏎ Hinweis: Passwörter werden nicht übernommen.
* Aus Datei importieren …
* Ausgewählte importieren
* Auth
* Datei konnte nicht gelesen werden: {error}
* Export-Dateien (*.reg *.ini);;Alle Dateien (*.*)
* Exportierte PuTTY-/WinSCP-Datei wählen
* Gefundene Verbindungen auswählen, die importiert werden sollen:
* Keine Sitzungen automatisch gefunden
* Keine Sitzungen in der Datei gefunden.
* Profil '{name}' löschen?
* SSH-Agent
* SSH-Key
* Server bearbeiten
* Verbindung
* Verbindungen importieren
* {count} Verbindung(en) importiert.
* {host}:{port} nicht erreichbar: ⏎ {error}
* {target}  (bereits vorhanden)

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

<details><summary><code>gui/venv_dialog.py</code> — 9</summary>

* Bekannte Umgebungen (venv/pipenv):
* Info:
* Neue Umgebung:
* Projekt:
* Python
* Umgebung wirklich löschen (Ordner wird entfernt)? ⏎ {path}
* venv
* venv-Ordner
* venv-Zielordner wählen

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

<details><summary><code>gui/settings_dialog.py</code> — 6</summary>

* Doppelte Kürzel
* Lade {model} …
* Theme „{name}“ löschen?
* ✓ Verbunden mit Ollama {version}
* ✓ {count} Modell(e) verfügbar.
* ✓ {model} geladen.

</details>

<details><summary><code>gui/command_builder.py</code> — 5</summary>

* (leer)
* Assistent — {name}
* In Konsole einfügen
* als Benutzer (-u), leer = root
* mit sudo ausführen

</details>

<details><summary><code>gui/search_dialog.py</code> — 5</summary>

*  — max. {cap} angezeigt
* Kontext
* Suchbegriff
* {count} Treffer
* {count} Treffer …

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
