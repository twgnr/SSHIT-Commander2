# SSHIT-Commander

**Dual-pane file manager with SSH/SFTP and a terminal**, for Windows.

Two panes for local and remote directories, paired with a full SSH console and a
real terminal. Written in C++20 with Qt 6 and libssh2.

> **Version 1.0.8.** The application is covered by 382 automated tests, and the
> SSH layer has been validated against a real OpenSSH server. Testing across a
> wider range of servers is still outstanding — see
> [Known limitations](#known-limitations).

![SSHIT-Commander: two file panes, each with its own console, the active side outlined in blue](docs/screenshot-main.png)

**User guide:** [docs/user-guide.md](docs/user-guide.md) (German:
[docs/user-guide.de.md](docs/user-guide.de.md)). The same manual is built into
the app under *Help → Help* (`F1`).

## Features

- **File management**: two panes (local ⇄ remote), copy/move with a progress
  queue, bandwidth limit, pause/resume, drag & drop, bulk rename, directory and
  file comparison, checksums, ZIP, symlinks, permission editor (chmod), file
  preview, tile and list view, per-pane filter and sorting (name with wildcards
  or regex, date, size, extension), per-server bookmarks (with import/export),
  Git status colouring in local repositories — changed entries orange, new ones
  green, including files and folders not yet added (for repos with an active
  GitHub alarm also on every parent folder up to the drive). Large files (8 MB and up) and directory trees are transferred over a
  separate SSH data connection, so browsing stays responsive.
- **SSH/SFTP**: profile management, import of saved sessions from PuTTY,
  WinSCP (directly or from an exported `.reg`/`.ini`) and `~/.ssh/config`,
  authentication by password, key or agent, PuTTY PPK import, host key checking
  (TOFU — a new server's fingerprint is confirmed before any login) with
  OpenSSH `known_hosts` interoperability and a known-hosts manager,
  ProxyJump/bastion, SSH compression (zlib), port forwarding
  (`-L`/`-R`/`-D` with SOCKS5, saved per profile to open on connect), sudo
  filesystem and panes running as another user (sudo/su), environment variables
  per session, key generation (Ed25519/RSA/ECDSA) and OpenSSH ↔ PPK conversion,
  **server info** (system, users, configuration files, open ports and services
  of a Linux server in a single call).
- **Terminal & console**: a real PTY (local via ConPTY with PowerShell or cmd,
  remote via SSH) with a full VT100/xterm emulator — `vim`, `htop`, `tmux`,
  `mc` and `less` all work, including mouse support, bracketed paste, DEC line
  drawing and wide (CJK/emoji) characters, with a selectable monospace font.
  Switchable between command and terminal mode, history, scrollback search,
  session logging, and broadcasting a command to both consoles at once.
  Tab completion of paths (local and SFTP) and a **parameter helper** that
  reads the typed command's options from its `--help` output and asks for
  arguments in the right order.
- **Automation**: **macro manager** with a key grid, layers that can follow the
  foreground application, a sequence editor and system-wide hotkeys ·
  **SFTP batch** with script editor, date and user variables, live log and
  scheduling ·
  **alarm triggers** for directories (local and remote) with glob filters,
  command execution and desktop notifications · **GitHub repo alarm**
  (reports new pushes; token stored in Windows Credential Manager) ·
  **plugins**: hook in external programs, with parameters and context-menu entries.
- **Tools**: editor with syntax highlighting, line numbers and a minimap; it
  detects a file's character set and saves it back in the same encoding ·
  file and content search (grep) · **network scanner** (hosts, ports,
  MAC/vendor, shares) · security audit with CVE lookup via OSV.dev ·
  character-set converter (including EBCDIC) · venv/pipenv management ·
  command palette with catalogue and wizard (options as checkboxes, live
  preview, optional sudo) · clipboard history · history & favourites.
- **AI assistant**: explain terminal output, ask questions about files,
  source-code error analysis. Runs against a local **Ollama** or a cloud
  provider — **Anthropic Claude**, **OpenAI** (and OpenAI-compatible servers),
  **Google Gemini**. Before anything is sent to a cloud provider for the first
  time, the app asks for consent; the consent can be withdrawn in the settings.
  The assistant only advises and never runs anything itself.
- **Security**: optional **app lock** — a password on start, optionally with a
  second factor (TOTP from an authenticator app) and one-time recovery codes.
  While the lock is active, the server profiles are stored encrypted
  (AES-256-GCM). Passwords, tokens and API keys are kept in the Windows
  Credential Manager.
- **User interface**: tabs with saveable layouts (tab favourites) that are
  restored on start, panes horizontal or vertical, swap and synchronise,
  filesystem-only and terminal-only views, undockable consoles, freely
  assignable keyboard shortcuts, four themes plus your own via the theme editor,
  German and English.

## Architecture

Strictly layered — the GUI only ever sees the core interfaces:

```
src/
├── main.cpp               # entry point
└── ncssh/
    ├── config.hpp/.cpp    # config paths, atomic writes
    ├── core/              # free of UI and networking: models, FileSystemProvider,
    │                      # CommandRunner, command catalogue, profiles, i18n, …
    ├── net/               # libssh2: session, SFTP provider, remote runner,
    │                      # transfer, tunnels, sudo filesystem, SFTP batch;
    │                      # HTTP: Ollama, cloud AI providers, OSV.dev
    └── gui/               # Qt Widgets interface
        └── bridge.hpp     # worker threads <-> Qt signals
```

Key abstractions: `FileSystemProvider` (local vs. SFTP) and `CommandRunner`
(local vs. remote). Every blocking operation runs through `AsyncBridge` on
worker threads — the window never freezes during SSH operations or transfers.

## Building

**Prerequisites**

- Windows 10/11 (x64)
- Visual Studio 2022 with the C++ workload (MSVC, CMake, Ninja)
- Qt 6.8 for MSVC x64
- Internet access on the first configure — libssh2, OpenSSL, zlib and the QR
  code library are fetched via CMake FetchContent / git
- A complete perl (Strawberry Perl, ActiveState, or the perl bundled with an
  Oracle installation) for the one-time OpenSSL build — see *Crypto backend*
  below. Not needed with `-DOPENSSL_ROOT_DIR=…` or the WinCNG fallback.

**Compiling**

```powershell
.\build.ps1            # configures + builds (Ninja, RelWithDebInfo)
.\build.ps1 -Fresh     # delete build/ and reconfigure
.\build\sshit-commander.exe
```

If Qt is not installed at `C:\Qt\6.8.2\msvc2022_64`, pass the path once:

```powershell
cmake --preset default -DCMAKE_PREFIX_PATH="D:/Qt/6.8.2/msvc2022_64"
```

`windeployqt` runs automatically after the build and places the required Qt DLLs
and plugins next to the executable — the `build\` folder is then ready to run.

**Distributable package** (executable, Qt DLLs/plugins and documents only, no
build internals):

```powershell
.\build.ps1 -Package
```

Produces `build\SSHIT-Commander-<version>.zip`.

**Code signing** (recommended for releases, otherwise Windows SmartScreen warns
on first start). `build.ps1` signs the executable with `signtool` before
packaging, including an RFC 3161 timestamp. Pass exactly one certificate source:

```powershell
# certificate in the Windows certificate store (also hardware tokens)
.\build.ps1 -Package -SignThumbprint <SHA1 thumbprint>

# PFX file — the password only via environment variable
$env:SSHIT_SIGN_PFX_PASSWORD = "…"
.\build.ps1 -Package -SignPfx C:\certs\codesign.pfx

# Azure Trusted Signing
.\build.ps1 -Package -SignDlib <path>\Azure.CodeSigning.Dlib.dll -SignMetadata metadata.json
```

The same values can be set as `SSHIT_SIGN_THUMBPRINT`, `SSHIT_SIGN_PFX`,
`SSHIT_SIGN_DLIB` and `SSHIT_SIGN_METADATA`. Requires the Windows SDK
(`signtool.exe`).

**Tests**

```powershell
.\test.ps1             # all unit tests (or: ctest --test-dir build)
```

### Crypto backend (WinCNG or OpenSSL)

- **Default: OpenSSL** (`-DUSE_OPENSSL_BACKEND=ON`) → ed25519/curve25519 + ecdsa,
  i.e. ed25519 login keys (the default of ssh-keygen and PuTTYgen) work.
  `cmake/OpenSSLBackend.cmake` provides OpenSSL in one of two ways:
  1. point `-DOPENSSL_ROOT_DIR=<path>` at a prebuilt static OpenSSL, **or**
  2. build from source (OpenSSL 3.3.2, `Configure VC-WIN64A no-asm no-shared`,
     `nmake` — **no nasm needed**). The result is cached in the build directory.
- **Requirement for option 2:** a complete perl (Strawberry/ActiveState) that
  includes `Locale::Maketext::Simple` — the perl shipped with Git Bash is NOT
  sufficient. If needed, force one with `-DOPENSSL_PERL=<path/perl.exe>`. Run
  from an MSVC environment (vcvars) so that `nmake`/`cl` are available.
- libssh2 then uses its OpenSSL path, where `LIBSSH2_ED25519=1` applies for
  OpenSSL ≥ 1.1.1 and X25519 is compiled in. Linked statically (no libcrypto
  DLL). Keep OpenSSL on a maintained branch (CVEs).
- **Fallback: WinCNG** (`-DUSE_OPENSSL_BACKEND=OFF`). No OpenSSL/perl required,
  but **no ed25519/curve25519**: it fails with ed25519 **login keys** and against
  servers that offer *exclusively* ed25519/curve25519. `ENABLE_ECDSA_WINCNG`
  enables ecdsa host keys and ecdh-sha2-nistp* key exchange. The app detects
  unsupported key types up front and says so instead of hanging.

## Configuration

Settings, profiles and bookmarks live in `%APPDATA%\ncssh`. Passwords, tokens
and AI API keys go into the **Windows Credential Manager**, not into the
configuration files.

- **App lock**: its state lives in a separate file, `applock.json`, so that a
  configuration import can neither carry it over nor switch it off. The
  password is stored only as a PBKDF2 hash and the TOTP secret is bound to the
  Windows account (DPAPI). While the lock is active, `servers.json` is
  encrypted, and deleting `applock.json` does not make it readable again.
  Settings, history and bookmarks stay unencrypted.
- **Export/import**: the settings dialog exports the whole configuration as one
  JSON file and imports it again. Secrets and host keys are deliberately left
  out. The server profiles are written in plain text — even while the app lock
  is active — so that the file can be imported on another PC; keep it safe.
- **Crash reports**: after a crash, the app writes a minidump and a short text
  file to `%APPDATA%\ncssh\crashes` and points them out on the next start. The
  text file contains only addresses (module + offset), no file or session
  contents.

## Known limitations

- **Windows only.** ConPTY, the Credential Manager and the macro actions use the
  Win32 API directly.
- Builds with the WinCNG fallback lack **ed25519 and curve25519** (see above).
- **Agent forwarding** is not possible: libssh2 cannot accept the agent channels
  the server opens back. The option is therefore deliberately disabled rather
  than appearing to work.
- Not broadly tested: unusual auth/SFTP/tunnel combinations, ProxyJump and
  ed25519 handshakes, for lack of suitable test servers.

## Third-party components

SSHIT-Commander uses the following free libraries. They remain the property of
their respective authors; the licence terms listed below apply independently of
this program's own licence.

| Component | Used for | Licence |
|---|---|---|
| **Qt 6.8** (Core, Gui, Widgets, Network, Concurrent, Svg) | application foundation: user interface, event loop, threads, JSON, HTTP, image/SVG display | LGPL v3 |
| **libssh2 1.11** | SSH connection, authentication, SFTP, PTY, tunnels | BSD-3-Clause |
| **OpenSSL 3** | crypto backend (default; not in WinCNG fallback builds) | Apache-2.0 |
| **zlib 1.3** | SSH compression (default; not in builds with `-DUSE_ZLIB_COMPRESSION=OFF`) | zlib |
| **QR Code generator** (Project Nayuki) 1.8 | QR code in the two-factor setup dialog | MIT |

Qt is linked **dynamically**: the `Qt6*.dll` files sit next to the executable and
can be replaced with your own compatible build of Qt. Qt's source code is
available at <https://www.qt.io/download-open-source> and <https://code.qt.io>,
libssh2 at <https://www.libssh2.org>.

The full licence texts are in [licenses/](licenses/). `build.ps1 -Package`
copies that folder and `LICENSE` into the distributable ZIP, so a released
build already satisfies the LGPL requirement to ship the licence text and point
to the Qt sources.

## Licence

Copyright (C) 2026 Tobias Wagner

This program is free software: you can redistribute it and/or modify it under
the terms of the **GNU General Public License, version 3**, as published by the
Free Software Foundation. See [LICENSE](LICENSE) for the full text.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

The third-party components listed above keep their own licences.
