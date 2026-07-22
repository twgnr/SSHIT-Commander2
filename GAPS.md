# Offene Punkte der Portierung

Erhoben durch Vergleich der **i18n-Kataloge** beider Projekte. Das ist der
belastbarste Massstab, den es hier gibt: in beiden Projekten ist der deutsche
Quelltext selbst der Schluessel, also lassen sich die sichtbaren
Oberflaechen-Elemente eins zu eins gegenueberstellen.

| | Anzahl |
|---|---|
| UI-Texte im Python-Original | 1352 |
| UI-Texte im C++-Port | 1478 |
| in beiden identisch | 1023 |
| **nur im Original — noch offen** | **329** |

Zum Vergleich: die erste Erhebung ergab 620 offene Punkte. Ein Teil der
verbleibenden sind blosse Umformulierungen (die Funktion steckt dann unter
anderem Wortlaut im Port), der Rest sind echte Luecken.

## Bereits geschlossen

| Modul | vorher | jetzt |
|---|---|---|
| `gui/settings_dialog` | 42 | 0 |
| `gui/search_dialog` | 31 | 0 |
| `gui/netscan_dialog` | 31 | 0 |
| `gui/console_widget` | 14 | 0 |
| `gui/encoding_converter_dialog` | 18 | 0 |
| `gui/filealarm_dialog` | 16 | 0 |
| `gui/main_window` | 114 | 61 |
| `gui/file_panel` | 73 | 40 |
| `gui/server_manager` | 42 | 19 |
| `gui/security_dialog` | 24 | 13 |
| `gui/venv_dialog` | 23 | 9 |
| `gui/editor_dialog` | 16 | 8 |
| `gui/help_dialog` | 31 | 7 |

Inhaltlich kamen dabei unter anderem dazu: Kontextmenue der Pane (rund 25 statt
7 Eintraege), Verlauf mit Vor/Zurueck, Markieren nach Muster, Tippsuche,
Zwischenablage, frei waehlbare Spalten, Breadcrumb-Pfadleiste, Kachelansicht,
Miniaturansichten, das Panes-Menue, Tab-Verwaltung, Broadcast, Statusleiste mit
Host-Key-Zustand, die KI-Einstiege fuer Dateien, Konfigurations-Im-/Export,
Modell-Download, Erreichbarkeitstest, die 14 Regex-Vorlagen und Rueckgaengig
beim Massen-Umbenennen, Suche/Mitschnitt/Links im Terminal, die vier fehlenden
Sicherheitspruefungen und die KI-Reparatur im Encoding-Konverter.

Mehrfach lag die Funktion fertig im Kern und war nur nicht angebunden: ZIP
erstellen, Archiv entpacken, Pruefsumme, die halbe Such-Engine, die halbe
Umbenenn-Engine und vier Parser des Sicherheits-Audits.

## Groessenvergleich der Module

Zeilenzahlen ohne Kommentar-/Leerzeilen. C++ faellt normalerweise laenger aus
als Python — ein Verhaeltnis deutlich unter 1,0 ist daher ein Warnsignal:

| Modul | Python | C++ | Verhaeltnis |
|---|---|---|---|
| `gui/main_window` | 2434 | 828 | 0,34 |
| `gui/user_guide_dialog` -> `help_dialog` | 940 | 474 | 0,50 |
| `gui/macro_manager_dialog` | 1062 | 539 | 0,51 |
| `gui/bulk_rename_dialog` | 506 | 427 | 0,84 |
| `gui/terminal_widget` | 527 | 448 | 0,85 |
| `gui/server_manager` | 427 | 403 | 0,94 |
| `gui/file_panel` | 1689 | 1614 | 0,96 |
| `gui/search_dialog` | 275 | 265 | 0,96 |
| `gui/settings_dialog` | 524 | 508 | 0,97 |
| `gui/netscan_dialog` | 196 | 360 | 1,84 |

`main_window` ist die groesste verbliebene Baustelle, danach `help_dialog`
(Handbuchtexte) und `macro_manager_dialog`.

## Noch offene UI-Texte je Modul

| Modul | Anzahl |
|---|---|
| `gui/main_window.py` | 61 |
| `gui/file_panel.py` | 40 |
| `gui/server_manager.py` | 19 |
| `gui/security_dialog.py` | 13 |
| `gui/transfer_dialog.py` | 12 |
| `gui/theme_editor_dialog.py` | 11 |
| `gui/tunnel_dialog.py` | 10 |
| `gui/tab_favorites_dialog.py` | 9 |
| `gui/venv_dialog.py` | 9 |
| `gui/diff_dialog.py` | 8 |
| `gui/editor_dialog.py` | 8 |
| `gui/ai_chat_panel.py` | 7 |
| `gui/help_dialog.py` | 7 |
| `gui/host_key_dialog.py` | 7 |
| `gui/properties_dialog.py` | 7 |
| `gui/command_palette.py` | 6 |
| `gui/console_widget.py` | 6 |
| `gui/githubalarm_dialog.py` | 6 |
| `gui/settings_dialog.py` | 6 |
| `gui/command_builder.py` | 5 |
| `gui/search_dialog.py` | 5 |
| `gui/clipboard_dialog.py` | 4 |
| `gui/console_panel.py` | 4 |
| `gui/encoding_converter_dialog.py` | 4 |
| `gui/filealarm_dialog.py` | 4 |
| `gui/confirm_dialog.py` | 3 |
| `gui/netscan_dialog.py` | 3 |
| `gui/preview_panel.py` | 3 |
| `gui/user_guide_dialog.py` | 3 |
| `gui/bookmarks_dialog.py` | 2 |
| `gui/history_dialog.py` | 2 |
| `gui/filediff_dialog.py` | 1 |
| `gui/known_hosts_dialog.py` | 1 |

### Vollstaendige Listen

<details><summary><code>gui/main_window.py</code> — 61</summary>

*   ·  {count} Tunnel
* (Binärdatei — keine Vorschau)
* (Vorschau nicht verfügbar)
* Abgebrochen — Host-Key nicht bestätigt.
* Aktiver Eintrag gesetzt — Strg+V fügt ihn in die aktive Pane ein.
* Alarm Trigger: {name}
* Alarm ausgelöst — zum Anzeigen klicken
* Benutzername für {host}:
* Destruktiver Befehl
* Einstellungen gespeichert (Pane-Schrift sofort; Terminal/Editor ab nächstem Öffnen).
* Fehlgeschlagen: {error}
* Gespeichert: {path}
* GitHub: neue Daten — zum Anzeigen klicken
* GitHub: {repo}
* Ja, alle
* KI-Fehleranalyse: {path}
* KI: {path}
* Keine Ausgabe zum Analysieren vorhanden.
* Key-Passphrase
* Lesezeichen exportiert: {path}
* Links und rechts je eine Datei markieren.
* Löschen fehlgeschlagen
* Nein, alle
* Neue Daten im Repository ({when})
* Neues Verzeichnis
* Nichts ausgewählt.
* Nichts zu übertragen (alle übersprungen).
* Passphrase für den Schlüssel: ⏎ {path}
* Passwort für {target}:
* Quelle nicht mehr vorhanden — Eintrag entfernt.
* SSHIT-Commander
* Server-Profil gespeichert: {name}
* Speichern unter (Remote-Pfad)
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
* ZIP erstellt: {name} ({n} Einträge)
* ZIP nur für lokale Ordner.
* sudo-Authentifizierung fehlgeschlagen.
* sudo-Passwort für {host}:
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
* Übertragung fertig
* Übertragungen anzeigen

</details>

<details><summary><code>gui/file_panel.py</code> — 40</summary>

*  · Ordner: {pre}{size}
* (keine Lesezeichen)
* Aktuellen Pfad als Lesezeichen (pro Server)
* Als Server-Profil speichern
* Anzeige filtern (Ctrl+F) — Wildcards * ? möglich, z.B. *.py
* Dienste
* Diese Pane mit sudo-Rechten (root) anzeigen
* Erneut scannen
* Filter…
* Freigaben
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
* Web
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

<details><summary><code>gui/security_dialog.py</code> — 13</summary>

* , davon {count} kritisch
* Audit nicht möglich
* Automatische Sicherheitsupdates (unattended-upgrades) sind nicht aktiviert.
* CVE / Info
* Keine Einträge.
* Keine Versionsdaten für den OSV-Abgleich gefunden.
* OSV-Online-Abgleich nicht möglich: {error}
* Online-CVE-Abgleich (OSV) für diese Distribution nicht verfügbar.
* Scanne …
* Sicherheitsupdate → {ver}
* läuft bald ab
* {os}  ·  Kernel {kernel}  ·  {count} Sicherheitshinweis(e)
* Öffentlich erreichbare Ports: {ports}

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

<details><summary><code>gui/editor_dialog.py</code> — 8</summary>

* '{title}' wurde geändert. Vor dem Schließen speichern?
* Datei wurde extern aktualisiert (neu geladen).
* Encoding
* Ersetzen durch
* Umbruch
* Weiter
* Z {line}, Sp {col}   ·   {bytes} Bytes   ·   {enc}   ·   {eol}   ·   {lang}
* Zeilenende

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

<details><summary><code>gui/help_dialog.py</code> — 7</summary>

* Datei- / Inhalts-Suche
* Inline umbenennen
* Konsole / Terminal
* Navigation
* Neuer Tab / Tab schließen
* Transfers / Tunnel
* Verzeichnis- / Datei-Vergleich

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

<details><summary><code>gui/console_widget.py</code> — 6</summary>

* In Ausgabe suchen…   (Enter = weiter, Shift+Enter = zurück, Esc = schließen)
* [Fehler] {msg}
* cd: kein Verzeichnis: {target}
* lokal
* ✓ fertig (Exit 0)
* ✗ Exit {code}

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

<details><summary><code>gui/encoding_converter_dialog.py</code> — 4</summary>

* Datei geschrieben: {path}
* Datei: {path}
* Nach Encoding
* Von Encoding

</details>

<details><summary><code>gui/filealarm_dialog.py</code> — 4</summary>

* Alarm
* Datei-Alarm
* Ereignis
* Überwachte Ordner — Häkchen schaltet einen Alarm an/aus:

</details>

<details><summary><code>gui/confirm_dialog.py</code> — 3</summary>

* Bei fertiger Übertragung benachrichtigen
* Pfad aus Lesezeichen wählen
* {count} Objekt(e) {verb}:

</details>

<details><summary><code>gui/netscan_dialog.py</code> — 3</summary>

* Scanne {count} Adressen …
* {count} Host(s) gefunden …
* {count} Host(s) gefunden.

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
