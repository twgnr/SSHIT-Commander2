# Offene Punkte der Portierung

Erhoben durch Vergleich der **i18n-Kataloge** beider Projekte. In beiden ist
der deutsche Quelltext selbst der Schluessel, die sichtbaren Bedienelemente
lassen sich also eins zu eins gegenueberstellen. Platzhalter werden vor dem
Vergleich normalisiert ("{name}" im Original, "%1" im Port) — solche Paare
sind dieselbe Funktion und zaehlen nicht als Luecke.

| | Anzahl |
|---|---|
| UI-Texte im Python-Original | 1352 |
| UI-Texte im C++-Port | 1595 |
| **echt offen** | **135** |
| nur andere Platzhalter-Schreibweise | 80 |

Erste Erhebung: 620 offene Punkte.

## Groessenvergleich der Module

Zeilenzahlen ohne Kommentar-/Leerzeilen. C++ faellt normalerweise laenger aus
als Python — ein Verhaeltnis deutlich unter 1,0 ist ein Warnsignal:

| Modul | Python | C++ | Verhaeltnis |
|---|---|---|---|
| `gui/main_window` | 2434 | 998 | 0,41 |
| `gui/user_guide_dialog` -> `help_dialog` | 940 | 474 | 0,50 |
| `gui/macro_manager_dialog` | 1062 | 613 | 0,58 |
| `gui/terminal_widget` | 527 | 448 | 0,85 |
| `gui/server_manager` | 427 | 426 | 1,00 |
| `gui/console_widget` -> `console_panel` | 338 | 394 | 1,17 |
| `gui/editor_dialog` | 320 | 465 | 1,45 |
| `gui/file_panel` | 1689 | 1834 | 1,09 |

Das niedrige Verhaeltnis bei `help_dialog` taeuscht: **alle 32 Themen sind
inhaltlich vorhanden** (Original: 31), der Unterschied ist nur Format — HTML im
Original, kompaktes Markdown im Port. Bei `main_window` verteilt der Port viel
Logik auf `workspace`/`file_panel`; das Zeilenverhaeltnis unterschaetzt die
Abdeckung entsprechend.

## Behobene Funktionsfehler (nicht ueber die Textmetrik sichtbar)

Der Methodenvergleich hat mehrere echte Fehler zutage gefoerdert, die keine
fehlende Zeichenkette waren:

* **Tastenkuerzel-Einstellung war wirkungslos** — `main_window` und `file_panel`
  verdrahteten alle Kuerzel fest und lasen `getShortcuts()` nie. Jetzt
  konfigurierbar, mit 4 Tests abgesichert.
* **Makro-Sequenzen liefen nie** — der Editor konnte sie anlegen, `runKey` hat
  sie ignoriert.
* **Exit-Code der Konsole** wurde erfasst, aber nicht angezeigt.
* **Konsolen-Suche (Strg+F)** war im Platzhalter versprochen, aber nur im
  Terminal vorhanden.

## Nicht abgedeckt von dieser Metrik

**Die libssh2-Schicht ist gegen keinen echten SSH-Server getestet.** Auth,
SFTP, PTY und Tunnel sind gegen die Semantik des Originals gebaut; keiner der
142 Tests wuerde einen Fehler dort bemerken. Das bleibt der groesste
ungepruefte Bereich.

## Noch offene UI-Texte je Modul

| Modul | Anzahl |
|---|---|
| `gui/main_window.py` | 20 |
| `gui/file_panel.py` | 11 |
| `gui/help_dialog.py` | 7 |
| `gui/editor_dialog.py` | 6 |
| `gui/properties_dialog.py` | 6 |
| `gui/tab_favorites_dialog.py` | 4 |
| `gui/ai_chat_panel.py` | 3 |
| `gui/clipboard_dialog.py` | 3 |
| `gui/command_palette.py` | 3 |
| `gui/confirm_dialog.py` | 3 |
| `gui/encoding_converter_dialog.py` | 3 |
| `gui/filealarm_dialog.py` | 3 |
| `gui/githubalarm_dialog.py` | 3 |
| `gui/preview_panel.py` | 3 |
| `gui/server_manager.py` | 3 |
| `gui/venv_dialog.py` | 3 |
| `gui/bookmarks_dialog.py` | 2 |
| `gui/history_dialog.py` | 2 |
| `gui/search_dialog.py` | 2 |
| `gui/security_dialog.py` | 2 |
| `gui/theme_editor_dialog.py` | 2 |
| `gui/user_guide_dialog.py` | 2 |
| `gui/command_builder.py` | 1 |
| `gui/console_panel.py` | 1 |
| `gui/console_widget.py` | 1 |
| `gui/filediff_dialog.py` | 1 |
| `gui/known_hosts_dialog.py` | 1 |
| `gui/settings_dialog.py` | 1 |
| `gui/tunnel_dialog.py` | 1 |

### Vollstaendige Listen

<details><summary><code>gui/main_window.py</code> — 20</summary>

* Aktiver Eintrag gesetzt — Strg+V fügt ihn in die aktive Pane ein.
* Fehlgeschlagen: {error}
* Gespeichert: {path}
* Keine Ausgabe zum Analysieren vorhanden.
* Neues Verzeichnis
* Nichts ausgewählt.
* Quelle nicht mehr vorhanden — Eintrag entfernt.
* SSHIT-Commander
* Server-Profil gespeichert: {name}
* Speichern unter (Remote-Pfad)
* Text in der Zwischenablage — mit Strg+V einfügen.
* Tunnel
* Umgebung aktiviert: {path}
* Umgebung gelöscht: {path}
* ZIP nur für lokale Ordner.
* {done} übertragen, {error} fehlgeschlagen
* {n} geändert
* {n} neu
* Übertragung fertig
* Übertragungen anzeigen

</details>

<details><summary><code>gui/file_panel.py</code> — 11</summary>

*  · Ordner: {pre}{size}
* Als Server-Profil speichern
* Git-Status: {code}
* Netzwerkscanner-Modus schließen
* Offene Ports: {ports}
* Verbindung dieser Seite trennen
* [Netzwerk] · Scan
* remote
* {n} markiert · {size}  ·  {base}
* ⏏ Trennen
* ✕ Netzwerkscanner

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

<details><summary><code>gui/editor_dialog.py</code> — 6</summary>

* Datei wurde extern aktualisiert (neu geladen).
* Encoding
* Umbruch
* Weiter
* Z {line}, Sp {col}   ·   {bytes} Bytes   ·   {enc}   ·   {eol}   ·   {lang}
* Zeilenende

</details>

<details><summary><code>gui/properties_dialog.py</code> — 6</summary>

* Eigentümer
* Lesen
* Oktal: {value}
* Rechte anwenden
* Rechte ändern fehlgeschlagen
* Schreiben

</details>

<details><summary><code>gui/tab_favorites_dialog.py</code> — 4</summary>

* Favorit „{name}“ löschen?
* Gespeicherte Tab-Layouts:
* Überschreiben
* „{name}“ existiert bereits. Überschreiben?

</details>

<details><summary><code>gui/ai_chat_panel.py</code> — 3</summary>

* KI-Assistent
* Kontext gesendet: {title}
* Modell: {model}

</details>

<details><summary><code>gui/clipboard_dialog.py</code> — 3</summary>

* Als aktiv setzen
* Kopierte Texte und Dateien — Doppelklick = aktiv setzen:
* Pfad / Aktion

</details>

<details><summary><code>gui/command_palette.py</code> — 3</summary>

* Befehlspalette — {os}
* Befehlspalette — {os}  ({count} Befehle)
* Vorlage:  {template} ⏎ Beispiel: {example}

</details>

<details><summary><code>gui/confirm_dialog.py</code> — 3</summary>

* Bei fertiger Übertragung benachrichtigen
* Pfad aus Lesezeichen wählen
* {count} Objekt(e) {verb}:

</details>

<details><summary><code>gui/encoding_converter_dialog.py</code> — 3</summary>

* Datei: {path}
* Nach Encoding
* Von Encoding

</details>

<details><summary><code>gui/filealarm_dialog.py</code> — 3</summary>

* Alarm
* Datei-Alarm
* Ereignis

</details>

<details><summary><code>gui/githubalarm_dialog.py</code> — 3</summary>

* Repo bearbeiten
* Repo überwachen
* {repo} – neue Daten

</details>

<details><summary><code>gui/preview_panel.py</code> — 3</summary>

*   ·  gekürzt
* (Bild kann nicht angezeigt werden)
* (keine Vorschau)

</details>

<details><summary><code>gui/server_manager.py</code> — 3</summary>

* Auth
* Server bearbeiten
* Verbindung

</details>

<details><summary><code>gui/venv_dialog.py</code> — 3</summary>

* venv
* venv-Ordner
* venv-Zielordner wählen

</details>

<details><summary><code>gui/bookmarks_dialog.py</code> — 2</summary>

* Lesezeichen — {server}
* Pfade für <b>{server}</b>

</details>

<details><summary><code>gui/history_dialog.py</code> — 2</summary>

* Filtern…
* ★ Zu Favoriten

</details>

<details><summary><code>gui/search_dialog.py</code> — 2</summary>

* Kontext
* Suchbegriff

</details>

<details><summary><code>gui/security_dialog.py</code> — 2</summary>

* Keine Einträge.
* läuft bald ab

</details>

<details><summary><code>gui/theme_editor_dialog.py</code> — 2</summary>

* Neues Theme
* Theme bearbeiten

</details>

<details><summary><code>gui/user_guide_dialog.py</code> — 2</summary>

* Begriff eingeben und Enter drücken …
* Hilfe – Benutzerhandbuch

</details>

<details><summary><code>gui/command_builder.py</code> — 1</summary>

* (leer)

</details>

<details><summary><code>gui/console_panel.py</code> — 1</summary>

* ★ Verlauf

</details>

<details><summary><code>gui/console_widget.py</code> — 1</summary>

* lokal

</details>

<details><summary><code>gui/filediff_dialog.py</code> — 1</summary>

* Dateien sind identisch.

</details>

<details><summary><code>gui/known_hosts_dialog.py</code> — 1</summary>

* Host:Port

</details>

<details><summary><code>gui/settings_dialog.py</code> — 1</summary>

* Doppelte Kürzel

</details>

<details><summary><code>gui/tunnel_dialog.py</code> — 1</summary>

* Tunnel öffnen

</details>
