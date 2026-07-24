# Portierungs-Konventionen: SSHIT-Commander (Python) → SSHIT-Commander2 (C++)

Dieses Dokument ist der **verbindliche Vertrag** für alle portierten Module.
Quelle: `c:\Repository\SSHIT-Commander\src\ncssh\` (Python/PySide6).
Ziel:   `c:\Repository\SSHIT-Commander2\src\ncssh\` (C++20/Qt 6.8).

## Struktur & Naming

- 1 Python-Modul `core/foo.py` → `src/ncssh/core/foo.hpp` + `foo.cpp` (gleicher Name).
- Namespaces: `ncssh` (config), `ncssh::core`, `ncssh::net`, `ncssh::gui`.
- Includes projektweit absolut: `#include "ncssh/core/models.hpp"` (`src/` ist Include-Root).
- Python `snake_case`-Funktionen/Attribute → C++ `camelCase`. Klassennamen unverändert.
- Docstrings/Kommentare: die deutschen Kommentare des Originals sinngemäß übernehmen
  (keine Kommentare, die nur den Port beschreiben). In .cpp/.hpp-Dateien **keine
  Umlaute in Kommentaren** (ae/oe/ue), Strings dürfen Umlaute enthalten (UTF-8, /utf-8 gesetzt).
- Der Kopfkommentar jeder Datei nennt das Python-Original: `// (Port von core/foo.py)`.

## Typ-Mapping

| Python | C++ |
|---|---|
| `str` | `QString` |
| `bytes` | `QByteArray` |
| `int`/`float` | `int`/`qint64`/`double` (Dateigrößen: `qint64`) |
| `datetime` | `QDateTime` (ungültig = None) |
| `list[T]` | `std::vector<T>` (Qt-Typen in GUI: auch `QStringList`) |
| `dict[str, T]` | `QHash<QString, T>` oder `QJsonObject` bei JSON-Nähe |
| `T \| None` | `std::optional<T>`; QString/QDateTime: leer/ungültig = None |
| dataclass | `struct` mit Default-Initialisierern |
| Enum | `enum class` |
| JSON (`json`-Modul) | `QJsonDocument`/`QJsonObject`/`QJsonArray` |
| `pathlib.Path` | `QString` + `QDir`/`QFileInfo` |
| Exceptions | `std::runtime_error` mit UTF-8-Meldung (`.toStdString()`) |
| `re` | `QRegularExpression` |
| HTTP (`urllib`/`requests`) | `QNetworkAccessManager` (blockierend via `QEventLoop` im Worker) |
| `subprocess` | `QProcess` |

## Async-Modell (WICHTIG)

Python nutzt `async def` + asyncio-Loop im Thread. C++-Ersatz:

- **Alle** `async def`-Methoden werden **blockierende** C++-Methoden.
- Die GUI ruft sie NIE direkt, sondern über `ncssh::gui::AsyncBridge`
  (`src/ncssh/gui/bridge.hpp` — LESEN!):
  - `bridge->run<T>(job, onDone, onError)` ≙ `bridge.run_coro(...)`
  - `bridge->run(voidJob, onDone, onError)` (void-Variante)
  - `bridge->stream(job, onLine, onFinished, onError)` ≙ `bridge.stream(...)`
  - Abbruch: `bridge->cancel(task)`; Jobs prüfen kooperativ `CancelTokenPtr`.
- Async-Generatoren (`yield` von Zeilen) → Callback `LineCallback` + `CancelTokenPtr`
  (siehe `core/runner.hpp`).

## Fundament (bereits vorhanden — dagegen bauen, NICHT neu erfinden)

- `ncssh/config.hpp` — `configDir()`, `atomicWriteText()`, `profilesFile()` …
- `ncssh/core/models.hpp` — `FileEntry`, `EntryType`, `TunnelSpec`, `ServerProfile`, `CommandResult`
- `ncssh/core/filesystem.hpp` — `FileSystemProvider`, `LocalFileSystem`
- `ncssh/core/runner.hpp` — `CommandRunner`, `LocalCommandRunner`, `LineCallback`
- `ncssh/core/settings.hpp` — `getSetting`/`setSetting` (+ typed helpers)
- `ncssh/core/secrets.hpp` — `setSecret`/`getSecret`/`deleteSecret` (Credential Manager)
- `ncssh/core/i18n.hpp` — Übersetzung: **`_t("deutscher Text")`** statt Python `tr(...)`.
  Platzhalter: Python `tr("{n} Dateien").format(n=n)` → `_t("%1 Dateien").arg(n)`.
  Die JSON-Kataloge nutzen `{name}`-Platzhalter — beim Portieren auf `%1`-Form umstellen
  ist NICHT nötig: Schlüssel ist der deutsche Quelltext; verwende im C++-Code `%1`/`%2`
  und passe den Schlüssel entsprechend an (`_t("%1 Dateien")`).
- `ncssh/gui/bridge.hpp` — `AsyncBridge`, `BridgeTask`, `CancelToken`

## Qt-GUI-Portierung (PySide6 → Qt C++)

- PySide6-API ist nahezu identisch zur C++-API: `QVBoxLayout`, `QTreeWidget`,
  `Signal`/`Slot` → `signals:`/Q_OBJECT + `connect(sender, &A::sig, this, &B::slot)`.
- Python `Signal(str)` → `signals: void name(const QString &);` — Klasse braucht
  `Q_OBJECT` (AUTOMOC ist aktiv, Header wird automatisch gemoct).
- Lambda-Connects sind ok: `connect(btn, &QPushButton::clicked, this, [this]{ ... });`
- Widget-Ownership: Qt-Parent-System nutzen (`new QWidget(this)`), keine smart pointer
  für Widgets mit Parent.
- Qt-Klassen, die es nur in PySide6-Python-Syntax gibt, 1:1 mit dem C++-Pendant ersetzen.
- **Keine neuen externen Abhängigkeiten.** Verfügbar: Qt6 (Core, Gui, Widgets, Network,
  Concurrent, Svg), libssh2 (`#include <libssh2.h>`, `<libssh2_sftp.h>`), WinAPI.

## Build

- CMake sammelt `src/**/*.cpp|hpp` per Glob — neue Dateien einfach anlegen, kein
  CMake-Edit nötig. MSVC-Flags: `/utf-8 /EHsc /permissive-`; `NOMINMAX` und
  `WIN32_LEAN_AND_MEAN` sind definiert.
- Ressourcen: `:/i18n/en.json`, `:/assets/sshit.png` (qrc). Neue Assets in
  `resources/resources.qrc` ergänzen.

### Krypto-Backend (WinCNG vs. OpenSSL)

- **Standard: WinCNG** (`-DUSE_OPENSSL_BACKEND=OFF`). Kein OpenSSL nötig, aber
  **ohne ed25519/curve25519**. Mit `ENABLE_ECDSA_WINCNG` sind ecdsa-Hostkeys und
  ecdh-sha2-nistp*-KEX aktiv, sodass Standard-OpenSSH-Server (die mehrere
  Verfahren anbieten) funktionieren. Es scheitert nur bei Servern, die
  ausschließlich ed25519/curve25519 anbieten, oder bei ed25519-**Login-Keys**.
- **Opt-in: OpenSSL** (`-DUSE_OPENSSL_BACKEND=ON`) → ed25519/curve25519 + ecdsa.
  `cmake/OpenSSLBackend.cmake` stellt OpenSSL bereit:
  1. `-DOPENSSL_ROOT_DIR=<pfad>` auf ein vorgebautes statisches OpenSSL, **oder**
  2. aus Quellcode bauen (OpenSSL 3.3.2, `Configure VC-WIN64A no-asm no-shared`,
     `nmake` — **kein nasm nötig**). Ergebnis wird im Build-Verzeichnis gecacht.
- **Voraussetzung Weg 2:** ein vollständiges perl (Strawberry/ActiveState/Oracle)
  mit `Locale::Maketext::Simple` — Git-Bash-perl reicht NICHT. Notfalls
  `-DOPENSSL_PERL=<pfad/perl.exe>` erzwingen. Aufruf aus einer MSVC-Umgebung
  (vcvars), damit `nmake`/`cl` verfügbar sind.
- libssh2 nutzt dann seinen OpenSSL-Pfad (`openssl.c`), in dem
  `LIBSSH2_ED25519=1` für OpenSSL ≥ 1.1.1 gilt und X25519-KEV fest einkompiliert
  ist. Statisch gelinkt (keine libcrypto-DLL). Verifiziert: App + 184 Tests bauen
  gegen beide Backends; ein Live-ed25519-Handshake ist mangels Testserver hier
  offen. OpenSSL bei Nutzung auf einem gepflegten Zweig aktuell halten (CVEs).

## Was NICHT portiert wird

- `src/ncssh/ui/` (Textual-TUI) — entfällt; die Qt-GUI ist die einzige Oberfläche.
- `smoke_gui.py`, Nuitka-/pip-Build-Skripte — durch CMake ersetzt.
