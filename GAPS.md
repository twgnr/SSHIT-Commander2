# Offene Punkte der Portierung

Erhoben durch Vergleich der **i18n-Kataloge** beider Projekte. In beiden ist der
deutsche Quelltext selbst der Schluessel, die sichtbaren Bedienelemente lassen
sich also eins zu eins gegenueberstellen. Platzhalter werden vor dem Vergleich
normalisiert (`{name}` im Original, `%1` im Port) — solche Paare sind dieselbe
Funktion und zaehlen nicht als Luecke.

| | Anzahl |
|---|---|
| UI-Texte im Python-Original | 1352 |
| UI-Texte im C++-Port | 1692 |
| **rechnerisch offen** | **131** |

Erste Erhebung: 620 offene Punkte.

## Wie diese Zahl zu lesen ist

Der Port hat **mehr** UI-Texte als das Original (1692 gegen 1352) — er ist
inzwischen der groessere Funktionsumfang. Die 131 offenen Eintraege sind
deshalb **kein Funktionsrueckstand**, sondern ueberwiegend:

* **Status- und Meldungstexte** mit Platzhaltern (`Gespeichert: {path}`,
  `{n} neu`, `{done} uebertragen, {error} fehlgeschlagen`),
* **abweichende Beschriftungen** fuer dieselbe Funktion.

Stichprobe (Original -> Port): `Neues Verzeichnis` -> **Neuer Ordner** ·
`Tunnel oeffnen` -> **Oeffnen** (im Tunnel-Dialog) · `Repo ueberwachen` ->
**Hinzufuegen** (GitHub Repo Alarm) · `Theme bearbeiten` -> **Theme-Editor** ·
`Server bearbeiten` -> Bearbeiten direkt im Formular mit **Speichern**.

Fuer die funktional klingenden Eintraege wurde geprueft, ob die Funktion im Port
vorhanden ist: ZIP-Archiv (nur lokal), Netzwerkscanner-Modus, Git-Status,
Pane-Filter, Transfer-Benachrichtigung, Uebertragungs-Ansicht,
Zwischenablage-Historie und venv-Verwaltung sind **alle vorhanden** — nur anders
beschriftet. Es wurden nicht alle 131 Eintraege einzeln nachverfolgt.

**Einschraenkung der Zuordnung:** jeder Text wird dem *ersten* Python-Modul
zugeordnet, das ihn enthaelt. Kurze Allerweltsbegriffe (`Encoding`, `lokal`,
`Kontext`) landen dadurch gelegentlich beim falschen Modul.

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
Abdeckung entsprechend. Der Emulator des Terminals liegt seit dem Ausbau in
`core/terminal_emulator` und zaehlt in der Tabelle nicht mit.

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
* **Transfer-Resume** kopierte in Wahrheit neu (No-Op).
* **Keepalive vor dem Handshake** brach jeden Schluesseltausch ab.
* **Abbrechen einer Uebertragung** war wirkungslos — der `TransferManager` legte
  das Task-Handle nie ab.

## Nicht abgedeckt von dieser Metrik

Die libssh2-Schicht ist inzwischen **gegen einen echten OpenSSH-Server
validiert** (Handshake/KEX, Host-Key, Auth, SFTP). Damit ist die frueher hier
genannte groesste Luecke geschlossen. Weiterhin ungeprueft bleibt die **Breite**:
exotische Auth-/SFTP-/Tunnel-Kombinationen gegen unterschiedliche Server, sowie
ProxyJump (mangels Bastion-Host) und ein ed25519-Handshake (mangels passendem
Server). Keiner der 186 Tests wuerde einen Fehler dort bemerken.

## Noch offene UI-Texte je Modul

| Modul | Anzahl |
|---|---|
| `gui/main_window.py` | 17 |
| `gui/file_panel.py` | 8 |
| `gui/help_dialog.py` | 6 |
| `core/ai.py` | 5 |
| `app.py` | 4 |
| `gui/user_guide_dialog.py` | 4 |
| `gui/ai_chat_panel.py` | 3 |
| `gui/clipboard_dialog.py` | 3 |
| `gui/command_palette.py` | 3 |
| `gui/editor_dialog.py` | 3 |
| `gui/encoding_converter_dialog.py` | 3 |
| `gui/githubalarm_dialog.py` | 3 |
| `gui/preview_panel.py` | 3 |
| `gui/security_dialog.py` | 3 |
| `gui/tab_favorites_dialog.py` | 3 |
| `gui/venv_dialog.py` | 3 |
| `core/commands.py` | 2 |
| `core/filealarm.py` | 2 |
| `gui/bookmarks_dialog.py` | 2 |
| `gui/confirm_dialog.py` | 2 |
| `gui/history_dialog.py` | 2 |
| `gui/settings_dialog.py` | 2 |
| `gui/theme_editor_dialog.py` | 2 |
| `core/githubalarm.py` | 1 |
| `core/macroactions.py` | 1 |
| `core/models.py` | 1 |
| `core/search.py` | 1 |
| `gui/bulk_rename_dialog.py` | 1 |
| `gui/console_panel.py` | 1 |
| `gui/filediff_dialog.py` | 1 |
| `gui/host_key_dialog.py` | 1 |
| `gui/known_hosts_dialog.py` | 1 |
| `gui/properties_dialog.py` | 1 |
| `gui/server_manager.py` | 1 |
| `gui/tunnel_dialog.py` | 1 |
| _(keinem Modul zuordenbar)_ | 31 |

### Vollstaendige Listen

<details><summary><code>gui/main_window.py</code> — 17</summary>

* Aktiver Eintrag gesetzt — Strg+V fügt ihn in die aktive Pane ein.
* Fehlgeschlagen: {error}
* Gespeichert: {path}
* Keine Ausgabe zum Analysieren vorhanden.
* Nichts ausgewählt.
* Quelle nicht mehr vorhanden — Eintrag entfernt.
* Text in der Zwischenablage — mit Strg+V einfügen.
* Umgebung aktiviert: {path}
* Umgebung gelöscht: {path}
* ZIP nur für lokale Ordner.
* {done} übertragen, {error} fehlgeschlagen
* {n} geändert
* {n} neu
* {n} Übertragung(en)
* Überschreiben
* Übertragung abgeschlossen
* Übertragungen anzeigen

</details>

<details><summary><code>gui/file_panel.py</code> — 8</summary>

*  · Ordner: {pre}{size}
* Filter…
* Git-Status: {code}
* Netzwerkscanner-Modus schließen
* Offene Ports: {ports}
* [Netzwerk] · Scan
* {n} markiert · {size}  ·  {base}
* ✕ Netzwerkscanner

</details>

<details><summary><code>gui/help_dialog.py</code> — 6</summary>

* Datei- / Inhalts-Suche
* Inline umbenennen
* Konsole / Terminal
* Neuer Tab / Tab schließen
* Transfers / Tunnel
* Verzeichnis- / Datei-Vergleich

</details>

<details><summary><code>core/ai.py</code> — 5</summary>

* Encoding
* KI-Assistent
* Kontext
* lokal
* läuft

</details>

<details><summary><code>app.py</code> — 4</summary>

* Neues Verzeichnis
* SSHIT-Commander
* Verbindung
* remote

</details>

<details><summary><code>gui/user_guide_dialog.py</code> — 4</summary>

* Begriff eingeben und Enter drücken …
* Hilfe – Benutzerhandbuch
* Suchen:
* venv-Ordner

</details>

<details><summary><code>gui/ai_chat_panel.py</code> — 3</summary>

* Du:
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
* OS:

</details>

<details><summary><code>gui/editor_dialog.py</code> — 3</summary>

* Ersetzen durch
* Z {line}, Sp {col}   ·   {bytes} Bytes   ·   {enc}   ·   {eol}   ·   {lang}
* Zeilenende

</details>

<details><summary><code>gui/encoding_converter_dialog.py</code> — 3</summary>

* Datei: {path}
* Nach Encoding
* Von Encoding

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

<details><summary><code>gui/security_dialog.py</code> — 3</summary>

* Automatische Sicherheitsupdates (unattended-upgrades) sind nicht aktiviert.
* Keine Einträge.
* läuft bald ab

</details>

<details><summary><code>gui/tab_favorites_dialog.py</code> — 3</summary>

* Favorit „{name}“ löschen?
* Gespeicherte Tab-Layouts:
* „{name}“ existiert bereits. Überschreiben?

</details>

<details><summary><code>gui/venv_dialog.py</code> — 3</summary>

* Info:
* Projekt:
* venv-Zielordner wählen

</details>

<details><summary><code>core/commands.py</code> — 2</summary>

* Ereignis
* venv

</details>

<details><summary><code>core/filealarm.py</code> — 2</summary>

* Alarm
* Datei-Alarm

</details>

<details><summary><code>gui/bookmarks_dialog.py</code> — 2</summary>

* Lesezeichen — {server}
* Pfade für <b>{server}</b>

</details>

<details><summary><code>gui/confirm_dialog.py</code> — 2</summary>

* Pfad aus Lesezeichen wählen
* {count} Objekt(e) {verb}:

</details>

<details><summary><code>gui/history_dialog.py</code> — 2</summary>

* Filtern…
* ★ Zu Favoriten

</details>

<details><summary><code>gui/settings_dialog.py</code> — 2</summary>

* Doppelte Kürzel
* Importieren

</details>

<details><summary><code>gui/theme_editor_dialog.py</code> — 2</summary>

* Neues Theme
* Theme bearbeiten

</details>

<details><summary><code>core/githubalarm.py</code> — 1</summary>

* Auth

</details>

<details><summary><code>core/macroactions.py</code> — 1</summary>

* Navigation

</details>

<details><summary><code>core/models.py</code> — 1</summary>

* Weiter

</details>

<details><summary><code>core/search.py</code> — 1</summary>

* Suchbegriff

</details>

<details><summary><code>gui/bulk_rename_dialog.py</code> — 1</summary>

* (leer)

</details>

<details><summary><code>gui/console_panel.py</code> — 1</summary>

* ★ Verlauf

</details>

<details><summary><code>gui/filediff_dialog.py</code> — 1</summary>

* Dateien sind identisch.

</details>

<details><summary><code>gui/host_key_dialog.py</code> — 1</summary>

* Fingerprint:  

</details>

<details><summary><code>gui/known_hosts_dialog.py</code> — 1</summary>

* Host:Port

</details>

<details><summary><code>gui/properties_dialog.py</code> — 1</summary>

* Oktal: {value}

</details>

<details><summary><code>gui/server_manager.py</code> — 1</summary>

* Server bearbeiten

</details>

<details><summary><code>gui/tunnel_dialog.py</code> — 1</summary>

* Tunnel öffnen

</details>

<details><summary>keinem Modul zuordenbar — 31</summary>

Texte, die im Katalog stehen, aber in keiner Python-Quelldatei
woertlich vorkommen (zusammengesetzt oder aus der TUI).

* 

Tipp: Verschlüsselte Keys benötigen die Passphrase. Kann asyncssh den Key nicht lesen (z.B. PuTTY-PPK v3), exportiere ihn in PuTTYgen über 'Conversions → Export OpenSSH key' und hinterlege diese Datei im Profil.
* <p style='color:#9aa4b2'>Befehls-Kürzel lassen sich unter <b>Ansicht → Einstellungen → Tastenkürzel</b> anpassen.</p>
* <span style='color:#f85149'>✗ Ollama nicht erreichbar.</span> Dienst starten (<code>ollama serve</code>) oder installieren: <a href='https://ollama.com/download'>ollama.com/download</a>.
* Audit nur für Linux/Unix. Bitte mit einem Server verbinden (F9) und die aktive Pane auf den Server stellen.
* Bitte zuerst unter Einstellungen → KI den Assistenten aktivieren und ein Modell wählen oder laden.
* Dieser Befehl ist als gefährlich markiert:

{command}

Wirklich jetzt ausführen?
* Ein Sicherheitsupdate steht bereit. Mit 'sudo apt upgrade' einspielen.
* Einstellungen, Server, Lesezeichen, Tab-Favoriten u. a. in eine Datei sichern (ohne Passwörter/Token)
* Erste Verbindung zu diesem Host. Bitte den Fingerprint prüfen, bevor du ihm vertraust.
* Folgende Tastenkombinationen sind mehrfach vergeben:

{lines}

Bitte eindeutig machen.
* Hinweis: MAC/OS-Schätzung nur im selben Subnetz. Freigaben und tieferes Browsing voll unter Windows (UNC), unter Linux best-effort (smbclient).
* Installierte Python-Versionen bzw. Starter (python/py/python3). Eigene Eingabe möglich, z. B. ein voller Pfad.
* Kein Modell gewählt — bitte unter Einstellungen → KI ein Modell wählen oder laden.
* Kein lokales Terminal verfügbar (beide Seiten sind mit einem Server verbunden). Trenne eine Seite, um lokal eine venv anzulegen.
* Klicke in ein Feld und drücke die gewünschte Tastenkombination. Leeren = Kürzel entfernen.
* Konfiguration exportiert nach:
{path}

Hinweis: Passwörter und Token bleiben im Schlüsselbund und sind NICHT enthalten.
* Konvertierung fehlgeschlagen: {msg}

Tipp: anderes Quell-Encoding wählen oder Fehlerstrategie auf „Ersetzen/Ignorieren“ stellen.
* Lokales Modell rekonstruiert fehlerhafte/zerstörte Zeichen aus dem Kontext (best effort). Verlorene Stellen werden geschätzt.
* Noch keine Server gespeichert.
Lege unten einen Server an (Neu) oder nutze den Import (PuTTY/WinSCP/SSH).
* Ollama ist nicht erreichbar. Läuft der Dienst? Starte ihn mit <code>ollama serve</code> oder installiere ihn von <a href='https://ollama.com/download'>ollama.com/download</a>.
* Remote-Datei lokal geöffnet (Änderungen werden nicht zurückgespielt): {name}
* Suchbegriff als regulären Ausdruck behandeln (sonst Wildcards bzw. fester Text)
* Vom Distributor als Sicherheitsupdate markiert. Mit 'sudo {mgr} update {pkg}' beheben.
* Vorlage:  {template}
Beispiel: {example}
* Zugriff verweigert — Datei evtl. vom System gesperrt (z. B. hiberfil.sys/pagefile.sys).
* {count} Bereich(e) importiert. Bitte das Programm neu starten, damit alle Änderungen wirksam werden.
* {host}:{port} ist erreichbar (Port offen).

Hinweis: Das prüft nur die TCP-Erreichbarkeit, nicht Zugangsdaten oder Host-Key.
* {total} CVEs gefunden — Detail-Infos nur für die ersten {shown} geladen.
* ⚠  WARNUNG: Der Host-Key hat sich geändert!
Das kann ein Man-in-the-Middle-Angriff sein.
* ⚠ Datei wurde extern geändert — Ihre lokalen Änderungen weichen ab (Speichern überschreibt die externe Version).
* ⚠ Der Inhalt war sehr lang und wurde gekürzt an das Modell übergeben.

</details>
