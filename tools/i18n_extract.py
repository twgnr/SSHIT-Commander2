#!/usr/bin/env python3
"""Pflegt die Übersetzungskataloge unter i18n/.

Durchsucht die C++-Quellen nach ``_t("…")``-Aufrufen (der deutsche Quelltext
ist der Schlüssel) und gleicht sie mit den Katalogdateien ab:

* neue Schlüssel werden mit leerem Wert ergänzt (= Fallback auf Deutsch),
* verwaiste Schlüssel werden gemeldet und optional entfernt,
* vorhandene Übersetzungen bleiben unangetastet.

Nutzung::

    python tools/i18n_extract.py            # Bericht + fehlende Schlüssel ergänzen
    python tools/i18n_extract.py --check     # nur prüfen (Exit 1 bei Lücken)
    python tools/i18n_extract.py --prune     # zusätzlich verwaiste entfernen

(Port von tools/i18n_extract.py des Python-Originals; dort wurde nach ``tr("…")``
in .py-Dateien gesucht.)
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
I18N = ROOT / "i18n"

# _t("…") mit optionalem Whitespace.
CALL_RE = re.compile(r'_t\(\s*"((?:[^"\\]|\\.)*)"')
# Direkt anschließendes Fortsetzungs-Literal einer C++-String-Konkatenation
# (`_t("teil eins"\n   "teil zwei")`) — wird ab der aktuellen Position geprüft.
CONT_RE = re.compile(r'\s*"((?:[^"\\]|\\.)*)"')


def unescape(text: str) -> str:
    """C++-Escapes in den echten String wandeln (\\n, \\", \\\\ …)."""
    return (text.replace('\\"', '"').replace("\\n", "\n")
                .replace("\\t", "\t").replace("\\\\", "\\"))


def extract_keys() -> set[str]:
    """Alle _t()-Schlüssel aus den C++-Quellen einsammeln."""
    keys: set[str] = set()
    for path in sorted(SRC.rglob("*.?pp")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in CALL_RE.finditer(text):
            parts = [match.group(1)]
            # Angrenzende Literale gehören zum selben Argument.
            pos = match.end()
            while (cont := CONT_RE.match(text, pos)) is not None:
                parts.append(cont.group(1))
                pos = cont.end()
            key = unescape("".join(parts))
            if key.strip():
                keys.add(key)
    return keys


def sync(code: str, keys: set[str], *, prune: bool, check: bool) -> int:
    """Katalog <code>.json mit den Schlüsseln abgleichen. Gibt die Anzahl der
    offenen Punkte zurück (fehlend + verwaist)."""
    path = I18N / f"{code}.json"
    catalog: dict[str, str] = {}
    if path.exists():
        catalog = json.loads(path.read_text(encoding="utf-8"))

    missing = sorted(k for k in keys if k not in catalog)
    orphaned = sorted(k for k in catalog if k not in keys)
    untranslated = sorted(k for k, v in catalog.items() if k in keys and not v)

    print(f"\n[{code}]")
    print(f"  Schlüssel im Code : {len(keys)}")
    print(f"  im Katalog        : {len(catalog)}")
    print(f"  fehlend           : {len(missing)}")
    print(f"  leer (Fallback DE): {len(untranslated)}")
    print(f"  verwaist          : {len(orphaned)}")

    if check:
        for k in missing[:20]:
            print(f"    + {k[:70]}")
        return len(missing) + len(orphaned)

    for key in missing:
        catalog[key] = ""          # leer = Fallback auf den deutschen Quelltext
    if prune:
        for key in orphaned:
            catalog.pop(key, None)

    # Stabil sortiert schreiben, damit Diffs lesbar bleiben.
    path.write_text(
        json.dumps(dict(sorted(catalog.items())), indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"  -> {path.relative_to(ROOT)} geschrieben")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="nur prüfen, nichts schreiben (Exit 1 bei Lücken)")
    parser.add_argument("--prune", action="store_true",
                        help="verwaiste Schlüssel entfernen")
    args = parser.parse_args()

    keys = extract_keys()
    if not keys:
        print("Keine _t()-Aufrufe gefunden — falscher Ordner?", file=sys.stderr)
        return 2

    issues = 0
    for catalog in sorted(I18N.glob("*.json")):
        issues += sync(catalog.stem, keys, prune=args.prune, check=args.check)
    return 1 if (args.check and issues) else 0


if __name__ == "__main__":
    raise SystemExit(main())
