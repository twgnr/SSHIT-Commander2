#!/usr/bin/env python3
"""Erzeugt die Benutzeranleitung unter docs/ aus dem eingebauten Handbuch.

Quellen (es gibt nur EINE Fassung zu pflegen):

* die Handbuch-Themen in src/ncssh/gui/help_dialog.cpp (deutscher Text =
  i18n-Schlüssel) samt der festen Tasten und Gesten derselben Datei,
* die Standard-Tastenkürzel aus src/ncssh/core/shortcuts.cpp,
* die englischen Übersetzungen aus i18n/en.json.

Die Dateien docs/user-guide.md (englisch) und docs/user-guide.de.md (deutsch)
werden nie von Hand bearbeitet. Der Test i18n.user_guide_up_to_date ruft
``--check`` auf und schlägt fehl, wenn sie nicht mehr zum Code passen.

Nutzung::

    python tools/gen_user_guide.py           # docs/user-guide*.md schreiben
    python tools/gen_user_guide.py --check   # nur prüfen (Exit 1, wenn veraltet)
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

sys.dont_write_bytecode = True   # kein __pycache__ im Quellbaum
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from i18n_extract import CALL_RE, CONT_RE, ROOT, unescape  # noqa: E402

HELP_CPP = ROOT / "src" / "ncssh" / "gui" / "help_dialog.cpp"
SHORTCUTS_CPP = ROOT / "src" / "ncssh" / "core" / "shortcuts.cpp"
EN_JSON = ROOT / "i18n" / "en.json"
DOCS = ROOT / "docs"

LIT = r'"((?:[^"\\]|\\.)*)"'
# {QStringLiteral("id"), _t("Gruppe"), _t("Aktion"), QStringLiteral("Taste") | QString()}
DEF_RE = re.compile(r'\{QStringLiteral\("[^"]*"\),\s*_t\(' + LIT + r'\),\s*_t\(' + LIT
                    + r'\),\s*(?:QStringLiteral\(' + LIT + r'\)|QString\(\))\s*\}')
# {QStringLiteral("Taste"), _t("Aktion")}
FIXED_RE = re.compile(r'\{QStringLiteral\(' + LIT + r'\),\s*_t\(' + LIT + r'\)\}')
# {_t("Gruppe"), _t("Aktion"), _t("Tasten") | QStringLiteral("Tasten")}
GESTURE_RE = re.compile(r'\{_t\(' + LIT + r'\),\s*_t\(' + LIT + r'\),\s*(_t|QStringLiteral)\('
                        + LIT + r'\)\}')

TEXTS = {
    "de": {
        "file": "user-guide.de.md",
        "title": "SSHIT-Commander — Benutzerhandbuch",
        "note": ("> Dieses Handbuch steht auch in der App unter *Hilfe → Hilfe* (`F1`).\n"
                 "> Die Datei wird mit `tools/gen_user_guide.py` aus dem eingebauten Handbuch\n"
                 "> erzeugt — bitte nicht von Hand bearbeiten, sondern die Themen in\n"
                 "> `src/ncssh/gui/help_dialog.cpp` ändern und das Skript erneut ausführen.\n"
                 ">\n"
                 "> English version: [user-guide.md](user-guide.md)"),
        "contents": "Inhalt",
        "reference": "Tastenkürzel-Übersicht",
        "reference_intro": ("Standardbelegung. Unter *Tools → Einstellungen → Tastenkürzel* lässt sich "
                            "jede Aktion dieser Tabellen neu belegen; „—“ heißt: ohne Standardkürzel."),
        "action": "Aktion",
        "key": "Kürzel",
        "fixed": "Feste Programmkürzel",
        "fixed_intro": "Diese Tasten sind fest vergeben und nicht konfigurierbar.",
        "gestures": "Maus & feste Tasten",
        "area": "Bereich",
    },
    "en": {
        "file": "user-guide.md",
        "title": "SSHIT-Commander — User Guide",
        "note": ("> This guide is also built into the app under *Help → Help* (`F1`).\n"
                 "> The file is generated from the in-app manual by `tools/gen_user_guide.py` —\n"
                 "> do not edit it by hand; change the topics in\n"
                 "> `src/ncssh/gui/help_dialog.cpp` and run the script again.\n"
                 ">\n"
                 "> Deutsche Fassung: [user-guide.de.md](user-guide.de.md)"),
        "contents": "Contents",
        "reference": "Keyboard shortcut reference",
        "reference_intro": ("Default assignments. Every action in these tables can be reassigned under "
                            "*Tools → Settings → Keyboard shortcuts*; “—” means no default shortcut."),
        "action": "Action",
        "key": "Shortcut",
        "fixed": "Fixed application shortcuts",
        "fixed_intro": "These keys are hard-wired and cannot be changed.",
        "gestures": "Mouse & fixed keys",
        "area": "Area",
    },
}


def literal_at(text: str, match: re.Match) -> tuple[str, int]:
    """Vollständiges _t()-Argument inkl. angrenzender Fortsetzungs-Literale."""
    parts = [match.group(1)]
    pos = match.end()
    while (cont := CONT_RE.match(text, pos)) is not None:
        parts.append(cont.group(1))
        pos = cont.end()
    return unescape("".join(parts)), pos


def block(text: str, start: str, end: str, path: pathlib.Path) -> str:
    """Quelltext zwischen zwei Markierungen (erste Fundstelle)."""
    a = text.find(start)
    b = text.find(end, a + len(start)) if a >= 0 else -1
    if a < 0 or b < 0:
        raise SystemExit(f"{path.name}: Abschnitt '{start}' nicht gefunden — Format geändert?")
    return text[a:b]


def read_topics(text: str) -> list[tuple[str, str]]:
    """(Titel, Markdown-Text) je Handbuch-Thema, in Anzeigereihenfolge."""
    body = block(text, "const std::vector<Topic> &topics()", "return list;", HELP_CPP)
    strings = []
    pos = 0
    while (match := CALL_RE.search(body, pos)) is not None:
        value, pos = literal_at(body, match)
        strings.append(value)
    if not strings or len(strings) % 2:
        raise SystemExit("help_dialog.cpp: Themen erwartet als Paare {_t(Titel), _t(Text)}")
    return list(zip(strings[0::2], strings[1::2]))


def tr(key: str, lang: str, catalog: dict[str, str]) -> str:
    return (catalog.get(key) or key) if lang == "en" else key


def keys_for(lang: str, keys: str) -> str:
    """Nicht übersetzte Tastennamen an die Sprache anpassen."""
    if lang == "de":
        return re.sub(r"\bCtrl\b", "Strg", keys)
    return re.sub(r"\bStrg\b", "Ctrl", keys)


def cell(value: str) -> str:
    return value.replace("|", "\\|")


def slug(title: str, seen: dict[str, int]) -> str:
    """Anker wie GitHub ihn für eine Überschrift vergibt."""
    s = re.sub(r"[^\w\- ]", "", title.strip().lower()).replace(" ", "-")
    n = seen.get(s, 0)
    seen[s] = n + 1
    return s if n == 0 else f"{s}-{n}"


def render(lang: str, catalog: dict[str, str], help_src: str, shortcuts_src: str) -> str:
    t = TEXTS[lang]
    topics = [(tr(title, lang, catalog), tr(body, lang, catalog))
              for title, body in read_topics(help_src)]

    groups: dict[str, list[tuple[str, str]]] = {}
    defs = block(shortcuts_src, "shortcutDefs()", "return defs;", SHORTCUTS_CPP)
    for m in DEF_RE.finditer(defs):
        group, label, key = (unescape(g or "") for g in m.groups())
        groups.setdefault(tr(group, lang, catalog), []).append(
            (tr(label, lang, catalog), keys_for(lang, key) if key else "—"))
    fixed = [(keys_for(lang, unescape(k)), tr(unescape(label), lang, catalog))
             for k, label in FIXED_RE.findall(
                 block(shortcuts_src, "fixedShortcuts()", "};", SHORTCUTS_CPP))]
    gestures = []
    for group, action, kind, keys in GESTURE_RE.findall(
            block(help_src, "const FixedBinding fixed[]", "};", HELP_CPP)):
        keys = unescape(keys)
        keys = tr(keys, lang, catalog) if kind == "_t" else keys_for(lang, keys)
        gestures.append((tr(unescape(group), lang, catalog),
                         tr(unescape(action), lang, catalog), keys))
    if not groups or not fixed or not gestures:
        raise SystemExit("Tastenkürzel nicht gefunden — Format in shortcuts.cpp/help_dialog.cpp geändert?")

    seen: dict[str, int] = {}
    anchors = [slug(title, seen) for title, _ in topics]
    ref_anchor = slug(t["reference"], seen)

    out = [f"# {t['title']}", "", t["note"], "", f"## {t['contents']}", ""]
    out += [f"- [{title}](#{a})" for (title, _), a in zip(topics, anchors)]
    out += [f"- [{t['reference']}](#{ref_anchor})", ""]
    for title, body in topics:
        out += [f"## {title}", "", body.strip(), ""]

    out += [f"## {t['reference']}", "", t["reference_intro"], ""]
    for group, rows in groups.items():
        out += [f"### {group}", "", f"| {t['action']} | {t['key']} |", "|---|---|"]
        out += [f"| {cell(label)} | {cell(key)} |" for label, key in rows]
        out.append("")
    out += [f"### {t['fixed']}", "", t["fixed_intro"], "",
            f"| {t['action']} | {t['key']} |", "|---|---|"]
    out += [f"| {cell(label)} | {cell(key)} |" for key, label in fixed]
    out += ["", f"### {t['gestures']}", "",
            f"| {t['area']} | {t['action']} | {t['key']} |", "|---|---|---|"]
    out += [f"| {cell(g)} | {cell(a)} | {cell(k)} |" for g, a, k in gestures]
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="nur prüfen, nichts schreiben (Exit 1, wenn veraltet)")
    args = parser.parse_args()

    catalog = json.loads(EN_JSON.read_text(encoding="utf-8"))
    help_src = HELP_CPP.read_text(encoding="utf-8")
    shortcuts_src = SHORTCUTS_CPP.read_text(encoding="utf-8")

    stale = []
    for lang in ("en", "de"):
        path = DOCS / TEXTS[lang]["file"]
        wanted = render(lang, catalog, help_src, shortcuts_src)
        # Zeilenenden ignorieren (git autocrlf checkt mit CRLF aus).
        current = path.read_text(encoding="utf-8").replace("\r\n", "\n") if path.exists() else None
        if current == wanted:
            continue
        if args.check:
            stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.write_text(wanted, encoding="utf-8", newline="\n")
            print(f"-> {path.relative_to(ROOT).as_posix()} geschrieben")
    if stale:
        print("Veraltet (python tools/gen_user_guide.py ausführen): " + ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
