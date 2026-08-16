# Contributing to SSHIT-Commander

Thanks for taking an interest. This document describes how the project is built,
the conventions the code follows, and what a good pull request looks like.

## Licensing of contributions

The project is licensed under the **GNU General Public License v3**. By
submitting a pull request you agree that your contribution is published under
that same licence.

## Getting set up

Prerequisites and build commands are in the [README](README.md#building). The
short version:

```powershell
.\build.ps1            # configure + build
.\test.ps1             # run the test suite
```

CMake picks up source files with a glob, so a new `.cpp`/`.hpp` under `src/`
needs no CMake edit — just create it and rebuild.

## Project layout

The code is strictly layered, and the layering is the most important rule:

| Layer | Contains | May depend on |
|---|---|---|
| `src/ncssh/core/` | models, filesystem and command interfaces, parsing, settings, i18n | nothing from `net/` or `gui/` |
| `src/ncssh/net/` | libssh2: session, SFTP, transfers, tunnels, HTTP clients | `core/` |
| `src/ncssh/gui/` | Qt Widgets interface | `core/`, `net/` |

`core/` must stay free of UI **and** networking — that is what keeps it
testable. The GUI talks to `FileSystemProvider` and `CommandRunner`, never to
libssh2 directly, so that local and remote behave identically.

## Threading: never block the GUI

Every operation that can block — anything touching the network, SSH, SFTP or a
subprocess — runs on a worker thread through `AsyncBridge`
(`src/ncssh/gui/bridge.hpp`):

```cpp
bridge->run<Result>(job, onDone, onError);        // one result
bridge->stream(job, onLine, onFinished, onError); // line by line
bridge->cancel(task);                             // cooperative cancel
```

Long-running jobs should check their `CancelTokenPtr` regularly so the user can
abort them. Never call a blocking function directly from a slot.

A libssh2 session is not thread-safe: all access is serialised through the
session mutex. If you add a new SSH operation, take that lock.

## Coding conventions

- **C++20**, MSVC. Compiler flags are `/utf-8 /EHsc /permissive-`.
- Namespaces: `ncssh`, `ncssh::core`, `ncssh::net`, `ncssh::gui`.
- Includes are absolute from `src/`: `#include "ncssh/core/models.hpp"`.
- `camelCase` for functions and members, `m_` prefix for member variables,
  class names in `PascalCase`.
- Qt parent ownership for widgets (`new QWidget(this)`); no smart pointers for
  parented widgets.
- **Comments are written in German**, and in `.cpp`/`.hpp` files they use ASCII
  transliteration (`ae`, `oe`, `ue`, `ss`) rather than umlauts. User-visible
  **strings** do use proper umlauts — the sources are UTF-8.
- Comments should explain *why*, not restate the code.

## Translations (please read before adding UI text)

The German source text **is** the translation key. Wrap every user-visible
string in `_t(...)`:

```cpp
label->setText(_t("Verbindung getrennt"));
status->setText(_t("%1 Dateien kopiert").arg(count));
```

Use Qt-style `%1`, `%2` placeholders, not `{name}`.

Every key needs an English translation in `i18n/en.json`. This is enforced: the
test `i18n.completeness_gate` runs `tools/i18n_extract.py --check` and **fails
the build** if a key is missing or an unused key is left behind.

```powershell
python tools\i18n_extract.py            # add new keys (empty values)
python tools\i18n_extract.py --check    # what the test does
python tools\i18n_extract.py --prune    # drop keys no longer used
```

Fill in the English text before opening a pull request — an empty value falls
back to German and counts as untranslated.

## Tests

The suite uses a small in-house harness rather than an external framework:

```cpp
TEST(suite_name, does_the_thing)
{
    CHECK(condition);
    CHECK_EQ(actual, expected);
    CHECK_THROWS(expression);
}
```

Add a `tests/test_<area>.cpp` file; it is picked up automatically. Guidelines:

- Put logic in `core/` where it can be tested without a server or a screen.
- GUI tests run with `QT_QPA_PLATFORM=offscreen` (handled by `test.ps1`).
- All tests must pass before a pull request is merged.

## Dependencies

**Please do not add external dependencies.** Available are Qt 6 (Core, Gui,
Widgets, Network, Concurrent, Svg), libssh2 and the Win32 API. Nearly everything
can be built from those, and every extra library adds licence and deployment
weight to a program that already ships Qt.

## Pull requests

- One topic per pull request; keep unrelated refactoring out.
- Describe **why** the change is needed, not only what it does.
- Say how you tested it — especially for anything touching SSH, since much of
  that cannot be covered by automated tests.
- Make sure `.\test.ps1` passes and the translation check is clean.

## Reporting bugs

Useful reports include: Windows version, how the program was obtained (release
ZIP or self-built), the server side if relevant (e.g. `sshd -V`, distribution),
what you did, what you expected, what happened instead.

For anything SSH-related, please mention the authentication method and the key
type — several known limitations depend on exactly that, see
[Known limitations](README.md#known-limitations).

**Security issues:** please do not open a public issue. Report them privately to
the repository owner instead.
