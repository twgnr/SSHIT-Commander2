SSHIT-Commander — Plugin-Ordner
================================

Lege hier externe Programme ab (am besten je Programm in einem eigenen
Unterordner). Verlinke die ausführbare Datei anschließend im Programm unter:

    Menü „Plugins" → „Plugins verwalten …"

Dort vergibst du einen Namen und optional:
  - Parameter (Platzhalter {path} = Pfad des gewählten Elements)
  - Arbeitsverzeichnis
  - Anzeige im Kontextmenü von Dateien/Ordnern

Beispiel-Parameter:  --open {path}


Zentrale Plugins (Firmen-Rollout)
---------------------------------
Lege optional eine Datei "plugins.json" in DIESEN Ordner. Die darin gelisteten
Plugins werden zusätzlich angezeigt (Quelle: "Zentral"), ohne in den Benutzer-
Einstellungen zu landen. So lässt sich SSHIT-Commander z. B. mit einem vorkonfi-
gurierten Passwort-Manager-Plugin für alle ausrollen. Diese Datei wird NICHT ins
Repository eingecheckt (.gitignore).

Hat ein Benutzer ein eigenes Plugin mit demselben Programmpfad, hat das eigene
Vorrang und der zentrale Eintrag wird ignoriert. Zentrale Plugins sind in der
Verwaltung schreibgeschützt.

Pfade: "exe" und "working_dir" dürfen relativ angegeben werden — sie werden dann
immer von DIESEM plugins-Ordner aus aufgelöst (absolute Pfade bleiben unverändert).
So ist der Rollout portabel, egal wohin SSHIT-Commander installiert wird.

Format (Liste von Objekten):
[
  {
    "name": "Passwort-Manager",
    "exe": "pwd-manager/pwd.exe",
    "args": "{path}",
    "working_dir": "",
    "context": true,
    "targets": "both"
  }
]

