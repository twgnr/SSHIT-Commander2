# SSHIT-Commander — User Guide

> This guide is also built into the app under *Help → Help* (`F1`).
> The file is generated from the in-app manual by `tools/gen_user_guide.py` —
> do not edit it by hand; change the topics in
> `src/ncssh/gui/help_dialog.cpp` and run the script again.
>
> Deutsche Fassung: [user-guide.de.md](user-guide.de.md)

## Contents

- [Overview](#overview)
- [The Interface](#the-interface)
- [Server Manager & Connecting](#server-manager--connecting)
- [Managing Files](#managing-files)
- [Filter & Sorting](#filter--sorting)
- [Viewing & Editing](#viewing--editing)
- [Terminal / Console](#terminal--console)
- [Parameter Window & Path Completion](#parameter-window--path-completion)
- [Transfers (Transfer Queue)](#transfers-transfer-queue)
- [SFTP Batch & Scheduled Tasks](#sftp-batch--scheduled-tasks)
- [Command Palette & Wizard](#command-palette--wizard)
- [File Search (by name)](#file-search-by-name)
- [Content search (grep)](#content-search-grep)
- [Bulk rename](#bulk-rename)
- [Convert file encoding](#convert-file-encoding)
- [File & Directory Comparison](#file--directory-comparison)
- [Manage venv](#manage-venv)
- [Macro manager](#macro-manager)
- [Plugins](#plugins)
- [Network scanner](#network-scanner)
- [Alarm Trigger (File Alarm)](#alarm-trigger-file-alarm)
- [GitHub repo alarm](#github-repo-alarm)
- [Clipboard Management](#clipboard-management)
- [Properties & Permissions (chmod)](#properties--permissions-chmod)
- [SSH keys](#ssh-keys)
- [SSH tunnel / port forwarding](#ssh-tunnel--port-forwarding)
- [Server Info](#server-info)
- [AI Features](#ai-features)
- [View & Themes](#view--themes)
- [Bookmarks](#bookmarks)
- [Settings](#settings)
- [Panes & Tabs](#panes--tabs)
- [sudo & Other Users](#sudo--other-users)
- [Security](#security)
- [App Lock & Two-Factor](#app-lock--two-factor)
- [Crash Reports](#crash-reports)
- [Shortcuts](#shortcuts)
- [Keyboard shortcut reference](#keyboard-shortcut-reference)

## Overview

**SSHIT-Commander** is a dual-pane file manager with an integrated SSH/SFTP terminal.

- A **pane** on each side — local or remote, operated the same way.
- Below each pane a **console** with two modes: *Commands* and *Terminal*.
- Any number of **tabs**, each with its own connection.
- All network work runs on background threads — the window never freezes during SSH operations or transfers.

## The Interface

- **Menu bar**: *Actions · Tools · Plugins · Clipboard · Panes · View · Help*.
- **Tab bar**: one tab per workspace; the title shows the connection.
- **Pane header**: title and chips — *Filter* (filter & sorting) and, when connected, *ⓘ Info* (server info), *⏏ Disconnect* and *sudo*. Below it the path row: drive selection (local; the tooltip shows the free space), back/forward (right-click: recently visited folders), up, the breadcrumb (click the empty area to type a path; right-click: copy or edit the path), bookmarks and reload.
- **Pane status line**: number and size of the entries, active filters and the selection.
- **Console header**: icons for *Parameters* (only for a known command), *Command palette* and *History*, then *AI* (explain output), *+* (another terminal), *Terminal* (switch mode) and `⤢` (undock into a window of its own). While a connection is being set up, also *Cancel connection*.
- **Status bar**: messages, transfer results, alarm events, host key status and the number of open tunnels.

## Server Manager & Connecting

`F9`, *Actions → Connect via SSH* or the toolbar icon open the **server manager**. On the left are the connections with a filter box, *New server* and *Import (PuTTY/WinSCP/SSH)*; on the right is the form.

**Fields**: *Display name*, *Host*, *Port* (22), *Username*, *Authentication* (*SSH key*, *Password* or *SSH agent* — Pageant or the OpenSSH agent), *Key path* (PuTTY PPK too, converted when connecting; 🔑 opens the key dialog), *Password* with *Store password/passphrase securely in the OS keyring*, *Host key checking* (see *Security*), *ProxyJump*, *Start directory* and *Tab color*.

**Fine-tuning**: *Keepalive* (seconds, 0 = off), *Connection timeout*, *Ciphers* and *Key exchange* (preferred algorithms, comma-separated). *SSH compression* compresses the traffic (zlib) — useful on slow connections. The SSH library does not support *SSH agent forwarding*.

- **ProxyJump**: `[user@]host[:port]` — one jump host. It logs in with the target's key (for key authentication), otherwise through the agent.
- **Environment variables**: one assignment `NAME=value` per line, `#` starts a comment. They apply to the terminal and the console. The server only accepts what its `AcceptEnv` list allows; the terminal reports rejected names.
- **Import** takes over sessions from PuTTY, WinSCP and `~/.ssh/config`; *Import from file …* reads exported `.reg` and `.ini` files. Passwords are not imported; profiles that already exist are marked and not preselected.
- *Tunnels (auto-start)* lists the tunnels that open automatically on connect (created in the tunnel dialog); *Remove* deletes one, effective with *Save*.
- *Last* shows the last successful connection, *Test reachability* only checks whether the port answers.

**Connecting**: *Connect* or double-click. The connection goes into the **active** (blue-framed) pane; the app asks for missing details such as user, password or passphrase. If the other pane of the tab is already connected, it asks: *Connect in a new tab* or *Replace connection*. The pane's console shows the progress; *Cancel connection* in the console header cancels, and after 30 s without an answer it stops anyway. If a connection drops, the app reconnects automatically.

**Disconnecting**: *⏏ Disconnect* in the pane header.

## Managing Files

- **Navigating**: a double-click opens a folder or shows a file (like `F3`); `Enter` opens folders and starts files with their default program (remote files as a local copy). `Backspace` goes up, `Alt+←`/`Alt+→` back and forward, `Tab` switches panes. You can type a path straight into the breadcrumb; typing in the list jumps to the matching entry.
- `F3` View · `F4` Edit · `F5` Copy · `F6` Rename · `F7` New folder · `F8` Delete — freely assignable under *Tools → Settings → Keyboard shortcuts*.
- **Selecting**: `Space` or `Ins`, Ctrl/Shift-click, `Num +`/`Num −` by pattern, `Num *` inverts, `Ctrl+A` selects everything. The F keys work on the selection.
- **Copying and moving** also with `Ctrl+C`/`Ctrl+X` and `Ctrl+V` or through the context menu (*Move → other pane*).
- **Columns**: right-click the column header to show or hide size, modified, created, accessed, type, extension, permissions and owner. Clicking a column header sorts by it, clicking again reverses the direction. Multi-level sorting and filtering: see *Filter & Sorting*.
- **Quick filter** `Ctrl+F`: shows a filter line (`*.log` or part of a name); `Esc` clears and hides it.
- **Drag & drop**: drag between the panes (remote too) or drop files from Explorer.
- *View → Tile view* (or the context menu) switches between list and tiles.
- **Colours**: executable files are highlighted (can be turned off, colour selectable in the settings). In local Git repositories the name is coloured by its Git status: orange = modified, green = new (including files and folders not yet added — and then everything inside them), red = deleted or conflict, blue = renamed or copied. A folder shows changes inside it; the tooltip names the status.

## Filter & Sorting

The **Filter** chip in the pane header opens *Filter and sorting* for that pane.

**Filter**

- *Show*: files and folders, files only or folders only.
- *Name* (several patterns separated by `;`, wildcards `*` and `?`), *Starts with*, *Ends with* and *Regular expression*; optionally *Match case*.
- *File extensions*.
- *Date*: modified or created — within a period, older than or newer than (minutes to years).
- *Size*: from and to, in B, KB, MB or GB.
- Folders are only affected by the name and date rules, and only with *Apply name and date rules to folders too*.

**Sorting**: *Sort by* plus up to three further levels (*then by*), each ascending or descending; *Folders first* keeps folders at the top.

*Apply* shows the result right away, *Reset* clears everything. While a filter is active, the chip is highlighted and the status line names the hidden entries. Filter and sorting apply per pane, stay when you change folders and are not saved. The quick filter `Ctrl+F` applies on top.

## Viewing & Editing

- `F3` **View**: text read-only (the first 200 KB); images (PNG/JPG/GIF/SVG …) as a preview with dimensions and file size.
- `F4` **Edit** opens the editor with **syntax highlighting** for 22 languages (among them Shell, PowerShell, Batch, Python, C/C++, C#, Java/Kotlin, JavaScript/TypeScript, Go, Rust, PHP, SQL, JSON, XML/HTML, YAML, INI/TOML, Markdown, Dockerfile and Makefile), **line numbers** and a **minimap**. The language is detected from the extension, a shebang and the content; a choice in the syntax box is remembered per extension.
- **Character set**: the editor detects the encoding (BOM, UTF-16, UTF-8, otherwise Windows-1252) and saves in the same encoding. For an unchanged file, switching the encoding box re-reads the file. If characters don't fit the encoding, saving offers UTF-8. *Convert encoding …* opens the converter.
- **Line endings**: LF or CRLF is detected and saved as chosen in the *Line ending* box. *Wrap* wraps long lines in the display only.
- **Search & Replace** in the bar below the text (*Aa* = match case, *Replace*, *All*). Matches are highlighted in the minimap; clicking the minimap jumps there.
- `Ctrl+S` saves, `Ctrl+Shift+S` saves as, `Ctrl+F` searches, `Ctrl+G` goes to a line.
- **Large-file protection**: files over 5 MB open read-only and show the beginning.
- If a local file changes on disk, the editor reloads it — with unsaved changes it asks first. It also asks before closing with unsaved changes.
- **Explain with AI** and **AI error analysis**: see *AI Features*.

## Terminal / Console

Each pane has its own console with two modes, switched with **Terminal** in the console header.

**Commands**: command → output.

- `↑`/`↓` steps through the saved history.
- `Ctrl+C`, `Esc` or **■** cancels the running command.
- `Ctrl+F` searches the output.
- `Tab` completes paths; the **Parameters** icon shows the options of the typed command (see *Parameter Window & Path Completion*).
- A `cd` syncs the pane, and changing the pane's folder sets the console's working directory.

**Terminal**: a real shell — locally through ConPTY with PowerShell or cmd (*Settings → General → Local shell*), remotely over SSH — with a full VT100/xterm emulator: `vim`, `htop`, `tmux`, `mc`, `less`, colours and the shell's own tab completion work.

- **Copy**: `Ctrl+Shift+C` or `Ctrl+Ins`; `Ctrl+C` copies when text is selected and interrupts otherwise.
- **Paste**: `Ctrl+Shift+V`, `Ctrl+V` or `Shift+Ins`.
- On remote Linux shells `Ctrl+Z` is undo; `Ctrl+Shift+Z` sends a real `^Z` (suspend the program).
- `Shift+PgUp/PgDn` scrolls back (10,000 lines), `Ctrl+Shift+F` searches it (`F3`/`Shift+F3` next or previous match).
- In full-screen programs, mouse clicks and the wheel go to the program; holding `Shift` still selects text.
- Right-click: copy, paste, copy all, open link, clear and **Start logging …** — writes the session without control codes to a file until *Stop logging*.
- **+** opens further terminals as tabs (double-click renames). After a shell exits, `Enter` restarts it.
- Font and font size: *Settings → General*.

**In both modes**:

- `Ctrl+Shift+K` (*Panes → Command to both consoles …*) sends a command to both consoles of the tab.
- `⤢` detaches the console into a window of its own; the pane then fills the whole column. *⤵ Dock* or closing the window docks it again.

## Parameter Window & Path Completion

**Completing paths**: in the console's command line (*Commands* mode) and in the fields of the parameter window, `Tab` completes the word before the cursor to a file or folder name — locally or on the connected server. Each further `Tab` jumps to the next match (folders first); names with spaces are quoted, and after `cd` only folders are offered. In terminal mode the shell completes by itself.

**Parameter window**: when the console recognises the typed command (from the command catalogue or as a common Unix tool), the **Parameters** icon appears in the console header. It opens a window that can stay open while you work:

- **Arguments (in the command's order)** — one field each, e.g. source and target for `cp`; *Variant* chooses between several forms of the command. *Will be appended* shows the result, *Append arguments* adds it.
- **Options (double-click appends)** — with a filter box. Options from the catalogue are bold; if an option needs a value, the window asks for it. On Linux/Unix the app also reads the command's `--help` output and adds those options.

Everything is appended to the console's command line, in both modes.

## Transfers (Transfer Queue)

`F5` opens *Copy / Transfer*: target folder (prefilled with the other side's path, editable or via *Browse…*) and, for exactly one item, the **name** too — copy and rename in one step. *Notify when transfer completes* reports the end in the status bar; with *Don't ask again*, `F5` copies straight into the other pane from then on.

**Conflicts**: if the target already exists, the app asks: *Yes*, *Yes, all*, *No*, *No, all* or *Cancel*. This applies to `F5`, drag & drop, paste and move. Source = target is skipped, a folder into itself is refused.

`Ctrl+T` opens **Transfers**: direction, progress, size, speed and remaining time per job. Jobs start immediately and run in parallel.

- *Pause / Resume* and *Restart* continue from the size already transferred; finished files are skipped.
- *Cancel* and *Remove finished*.
- **Limit** (KB/s, 0 = unlimited) throttles each job separately; it applies from the next start or resume.
- After the transfer the size is checked (✓). When **moving**, the source is deleted only after a successful check.
- Files of 8 MB and up and folders go over a **separate SSH connection** so browsing stays responsive; if it cannot be opened, the existing one is used.

The folder browser works on remote servers too.

## SFTP Batch & Scheduled Tasks

*Actions → SFTP batch / scheduled tasks …* (`Ctrl+Shift+B`) runs a script over the current tab's connection. Relative paths start at the folder of the remote or the local pane.

One command per line; paths with spaces go in `"…"`, `#` starts a comment:

- `cd PATH`, `lcd PATH`, `pwd`, `lpwd` — change or show the folder (remote/local).
- `put LOCAL [REMOTE]`, `get REMOTE [LOCAL]` — upload or download; if the target is a folder, the file goes into it. Existing files are overwritten without asking; there are no wildcards.
- `mkdir PATH`, `rm FILE`, `rmdir PATH` (**deletes recursively**), `rename OLD NEW` or `mv`, `chmod OCTAL PATH`, `ln TARGET LINK` and `echo TEXT`.
- `set NAME VALUE` defines your own variable.

**Variables** in any line: `$heute`/`$today` (`2026-10-08`), `$jetzt`/`$now` (`2026-10-08_14-30-05`), `$zeit`/`$time`, `$jahr`/`$year`, `$monat`/`$month`, `$tag`/`$day` and your own as `$NAME`. `${NAME}` delimits the name when letters, digits or `_` follow directly (e.g. `${day}_log`); `$$` gives a `$`. All time variables refer to the start of the run. An unknown variable is an error for that line — e.g. `get logs/app.log app-$today.log`.

*Run* starts the script. The log shows each line with ✓ or ✗ and a summary at the end; *Stop* cancels between two lines. With *Stop on error* the script ends at the first error, otherwise it carries on.

**Repeat every** N minutes runs the script regularly — first after one interval and only **while the dialog is open**. The script and the interval are remembered when you run it; *Load …* and *Save …* work with script files.

## Command Palette & Wizard

`Ctrl+P` (or the icon in the console header) opens the **command catalogue** — with a search box, sortable by command, category, platform and description.

- **OS filter**: Current OS · Both · Linux/Unix only · Windows only · Cross-platform. The server's OS is detected when connecting.
- **Dangerous commands** are marked in red.
- *Paste* writes the command template into the console.
- **Wizard …** (or a double-click) opens the parameter editor: text fields, drop-downs and checkboxes with a **live preview**, plus **sudo** on Linux (optionally as another user). *Insert into console* takes over the finished command, *Run* starts it right away.

## File Search (by name)

`Ctrl+Shift+F` searches for file names.

- **Root**: start folder; *…* picks it, *⌂* sets the drive root.
- **File name**: wildcards (`*.log`) or **regex**; *Ignore case* is on by default. *File filter* narrows to patterns (comma-separated).
- **Advanced**: *Exclude files*, *Exclude folders* (e.g. `.git,node_modules`), *Kind* (everything, files only, folders only) and *Limits* — max depth, min size and modified ≤ days.
- Matches come in live; *Stop* cancels.
- Double-clicking a match jumps to its folder.

## Content search (grep)

`Ctrl+Alt+F` searches file **contents**.

- **Search term** literally or as a **regex**; *Ignore case* is on by default, *Whole word* restricts to word boundaries.
- **Binary files** are skipped unless *Include binary files* is ticked.
- *File filter* and, under **Advanced**, the exclusions and limits as in the file search, plus *Filenames only*, *Lines without a match* (inverted) and *Lines before/after each match* (context).
- Result lines look like `path:line:text`; a double-click opens the file's folder.

## Bulk rename

`Ctrl+Shift+R` renames the selected files in one go. The *Scope* chooses name, extension or the whole name.

**Search & Replace** — mode *Text (literal)*, *Wildcards* (`*` and `?`) or *Regex*; optionally *Ignore case* and *Replace all occurrences*. *Regex template* offers ready-made patterns, e.g. remove brackets with their content, leading numbers, digits or copy suffixes, or replace spaces and special characters.

**Remove & insert** — trim at the front or back, remove a text and insert a text at a character position (negative counts from the end).

**Text & extension** — prefix, suffix, case (lower, UPPER, word starts, sentence start), spaces (keep, `_`, `-`, remove) and extension (unchanged, lower, UPPER, set).

**Numbering** (*active*) — start, step, digits, separator and position (front, back, at a position) plus the **order** in which numbers are assigned (input, name, name descending, natural `1,2,10`, extension).

The **preview** shows old → new; changed names are green, conflicts red. *Number conflicts automatically* appends ` (1)`, ` (2)` and so on. Renaming runs in a **safe order** — swaps (`a↔b`) and chains (`a→b→c`) work via temporary names. If a step fails, the previous ones are rolled back; *Undo* reverts a completed rename.

## Convert file encoding

*Tools → Convert file encoding* converts text files between character sets — UTF-8/16/32, Windows-1250/1251/1252, ISO 8859-1/15, DOS/OEM 437/850, Mac Roman, KOI8-R, Shift-JIS, GBK, Big5 and **EBCDIC** (cp037, cp500, cp273, cp1140, cp1141, cp1047, cp875).

- The **source encoding is detected automatically** and preselected.
- The **preview** shows the source with the selected codec.
- **Error strategy**: strict (report), replace or ignore.
- Output to a new file or **overwrite the original**.
- **Repair with AI …** lets the AI reconstruct damaged text; the preview shows the result, and it is only saved with *Convert*.

## File & Directory Comparison

- `Ctrl+Shift+D` **File compare**: two selected files as a coloured unified diff (green = added, red = removed), with a line count.
- `Ctrl+D` **Directory compare**: compares both panes (*left only*, *right only*, *newer on left*, *newer on right*, *identical*), optionally **recursive**. *Differences only* (on by default) hides what is equal, *Refresh* compares again.
- *→ Sync to right* or *← Sync to left* transfers the selected entries through the transfer queue — without asking; existing files are overwritten.

## Manage venv

*Tools → Manage venv* creates virtual Python environments.

- **Project folder**, **venv path** (default `.venv`), the **Python versions** found to choose from and the **installation** (suggested automatically, e.g. from `requirements.txt`; runs after activation).
- *Ignore dependencies* appends `--skip-lock` (pipenv) or `--no-deps` (pip).
- The **command preview** shows what will run; *Create & activate* sends it to the active console.
- Below are the **known environments** with type (venv/pipenv), version, project and path. *Activate* or a double-click activates an environment in the console, *Delete environment* removes it including its folder. *Save info* stores a *Note for the selected environment*.

## Macro manager

*Tools → Macro manager* offers a grid of freely assignable keys.

- **Layers** (pages) on the left: create, edit (name, assigned program, rows × columns), delete. *Export …* writes all layers as JSON, *Import …* takes over selected ones (equal names are renamed).
- *Switch layer automatically to match the application* watches the foreground program and switches accordingly; *Use active application* fills it in while editing.
- **Edit mode**: clicking a key opens the editor. **Run mode**: clicking triggers the action, a long press still opens the editor. Right-click: edit, run, clear. In run mode the window can be docked to a screen edge.

In the **key editor**: label and its position, icon (as background), font colour and style, a **global shortcut** and the **action**. Depending on the action type, the matching editor appears — text, number, layer, window, SSH command, JSON or several steps.

**Global shortcuts** work system-wide, even when SSHIT-Commander is not in the foreground and the macro manager was never opened. They fire only after the modifier keys are released and are paused while the key editor is open.

**Actions**: start programs and commands, open files/URLs, screenshot, lock screen · type or paste text, press/hold shortcuts and keys · move, click, scroll the mouse · media and volume, choose audio device · focus, manage, cycle windows · switch layers · HTTP requests, commands to the SSH console or to all consoles · delay, several actions (all on every press), sequence (the next step on each press, then from the start) · clipboard, multi-state key and command selection.

## Plugins

*Plugins → Manage plugins …* hooks in standalone programs.

- **Program** (relative to the `plugins/` folder or absolute), **parameters** with the placeholder `{path}` for the selected item (without a placeholder the path is appended), **working directory** (default: the program's folder).
- **Show in context menu** adds the plugin to the pane; *Applies to* restricts it to files, folders or both.
- **Test** starts the plugin right away.
- All plugins are also listed directly in the *Plugins* menu, as is *Open plugin folder*.
- Centrally provided plugins (from `plugins/plugins.json`) are read-only and marked *(central)*.

## Network scanner

*Tools → Network scanner …* scans the local network.

- **IP range**: CIDR (`192.168.1.0/24`), ranges (`10.0.0.1-50`), lists or single names. The local /24 is prefilled.
- **Ports**: presets (common ports, SMB only, web, remote access, all important) or your own (`22,80,8000-8100`).
- **Options**: additional ping (ICMP), show responding hosts only, resolve host names, detect shares (SMB), identify devices (banner, web title, OS, NetBIOS), MAC address + vendor, parallelism, timeout per port and auto-rescan. *Load last scan* shows the last result without scanning again.
- The table shows IP, name, MAC, **vendor** (from the OUI table), OS guess, open ports with service names, plus web interface and shares. A double-click opens a web interface that was found.
- *Close* hands the hosts over as a **file system** (`net://`) to the pane chosen under *Results in*: host → share → files.

## Alarm Trigger (File Alarm)

*Tools → Alarm trigger …* (or *Set alarm trigger for directory …* in the pane's context menu) watches folders for changes.

Per alarm:

- *Display name* (optional) and *Folder to watch*.
- *Detected changes*: created, modified, deleted; plus *Include subfolders*, *Watch folders too* and *active*.
- *Only these patterns* and *Ignore these patterns*: wildcards separated by `;` (e.g. `*.log;*.tmp`).
- **Remote**: an alarm created in a connected tab watches the folder on that server (type the path). If the server is not connected, the alarm pauses.
- *Command on trigger*: a local command, once per check cycle. Placeholders `{path}`, `{kind}` (created/modified/deleted), `{name}` (the alarm's name) and `{count}` — also available as the environment variables `ALARM_PATH`, `ALARM_KIND`, `ALARM_NAME` and `ALARM_COUNT`.

*On trigger* applies to all alarms: **desktop notification** (on by default) and **sound**. A message also appears in the status bar; clicking it shows the events. The check runs every few seconds by comparing snapshots.

## GitHub repo alarm

*Tools → GitHub repo alarm …* reports new pushes.

- **Repository** as `owner/repo` or as a GitHub URL (also `git@github.com:owner/repo.git`). The tick in the *Active* column turns watching a repository on or off.
- An optional **token** (*Save token*) raises the API limit, allows private repositories and is kept in the keyring, not in plain text.
- The timestamp of the latest push is checked at the interval set under *Settings → General* (default 15 minutes); if it changes, the status bar reports new data. *Check now* checks immediately.
- **Local folder** assigns the local clone (*Choose folder …*, *Remove assignment*); a local pane also recognises the clone by itself. If it has changes, every folder above it up to the drive is highlighted in the panes — green if only new files were added, orange otherwise.

## Clipboard Management

*Clipboard → Clipboard manager* keeps a history of copied texts and files (up to 100 entries, in memory only).

- A double-click makes an entry the **active** clipboard content, inserts it into the active console and closes the window.
- *Paste* writes the entry into the active console.
- *Delete entry* and *Clear all* tidy up the list.

## Properties & Permissions (chmod)

Context menu → **Properties** shows name, path, type, size, timestamps, owner/group and any symlink target. For local folders the size is calculated recursively.

The **chmod editor** below has rwx checkboxes for owner, group and others plus an **octal field** — both representations keep each other up to date. *Apply* writes the permissions.

## SSH keys

🔑 in the server manager or *Tools → Generate / convert SSH keys …* opens the **key dialog**.

- **Generate**: Ed25519 (recommended), RSA 4096/3072 or ECDSA nistp256, with an optional comment. Keys are generated with Windows' `ssh-keygen`, without a passphrase.
- The **public key** is shown and can be copied — it belongs in `~/.ssh/authorized_keys` on the server.
- **Save keys …** writes the private and the `.pub` file; the private key gets restrictive permissions.
- **Convert**: OpenSSH → PPK and PPK → OpenSSH.
- After saving, or after PPK → OpenSSH, the path is taken over into the profile.

## SSH tunnel / port forwarding

`Ctrl+Shift+T` or *Actions → SSH tunnel* opens port forwarding (the tab must be connected):

- **Local (-L)**: a local port is forwarded to a target behind the server — e.g. `127.0.0.1:8080` → `localhost:80` on the server.
- **Remote (-R)**: a port on the server points to a local target.
- **Dynamic / SOCKS (-D)**: a SOCKS5 proxy over the SSH connection.

*Save in the server profile (auto-start on connect)* stores the tunnel in the profile; it then opens on every connection to that server. Saved tunnels are listed in the server manager under *Tunnels (auto-start)*, where they can be removed. Open tunnels are listed under *Active forwardings* and can be **stopped** one by one; the status bar shows how many there are. They end when you disconnect or close the tab.

## Server Info

The **ⓘ Info** chip in the header of a connected pane opens the server info — the key facts about a Linux/Unix server, gathered in a single SSH call.

- **Header**: host name, system, kernel, uptime and load, and the logged-in user.
- **Configuration files**: important files by category, with access (*read & write*, *read only*, *no access (sudo required)*), plus `.env` and docker-compose files found under `/home`, `/root`, `/var/www`, `/srv` and `/opt`. *Open / Edit* or a double-click opens the file in the editor — with the sudo chip's rights when it is active; *Copy path* copies the path.
- **Users** (system accounts on request), **Ports**, **Services** and **Storage**.

*Refresh* queries again. There is no server info for Windows servers.

## AI Features

The AI assistant explains output and files. It **only advises** and never runs anything.

**Setting up** under *Settings → AI*: *Enable AI assistant*, then choose the **provider**:

- **Ollama (local)** — the model runs on your own Ollama server (default: this computer); the content stays there. *Download model* pulls new models straight into Ollama.
- **Anthropic Claude**, **OpenAI**, **Google Gemini** — cloud services with your own **API key**.
- **OpenAI-compatible** — e.g. LM Studio or your own server; enter the *Address*, the API key is optional.

*Load models* fetches the available models, *Test connection* checks the settings. API keys are kept in the Windows Credential Manager.

**Privacy**: before content goes to any provider other than Ollama for the first time, the app asks — *Send — don't ask again* or *Cancel*. The consent applies per provider; *Ask again before sending* on the same page withdraws it.

**Using it**:

- **AI** in the console header explains the visible output or an error.
- *Tools → AI*: explain terminal output, explain file / question about the file and AI error analysis (source code) for the selected file.
- In the editor: **Explain with AI** (a selection takes precedence over the whole file) and **AI error analysis** (errors only, with severity and a suggested fix).
- In the encoding converter: **Repair with AI …**.

Answers appear in a chat window with **follow-up questions** (`Ctrl+Enter` sends, *Stop* cancels).

## View & Themes

*View → Theme* switches between **Dark**, **Midnight**, **Light**, **High contrast** and your own themes; the choice is saved and also applies to the terminal.

*View → Theme editor* creates your own colour schemes: choose a base, click the colour fields (background, surfaces, borders, text, accents, scroll bars, terminal colours) and save under your own name. The **preview** below shows the result right away. Built-in themes cannot be overwritten or deleted.

Also in the *View* menu: **Hidden files** (`Ctrl+.`), **Tile view** and the **preview panel** (`Ctrl+F2`), which shows the selected file read-only between pane and console (text or image). Fonts and font sizes: *Settings → General*.

## Bookmarks

Path bookmarks are kept **per connection** (profile name or `local`).

- The star in the path row bookmarks the current path or removes it again.
- The bookmark button next to it opens a menu: a click jumps there, plus *★ Bookmark current path* and *Manage…*.
- `Ctrl+B` or *Panes → Bookmarks …* opens the manager: *Go to*, *Remove*, *Export …* and *Import …*.
- *Actions → Export bookmarks …* or *Import bookmarks …* shares bookmarks between computers.

## Settings

`Ctrl+,` opens the settings with four tabs. Most changes apply right after *Save*; a language change needs a restart, which the app offers straight away.

**General** — language, theme (your own themes can be deleted), font sizes for editor, terminal and panes, terminal font, **date format** (tokens like `DD.MM.YYYY HH24:MI`), hide hidden files, program logos and image previews as icons, **natural sorting** (`1, 2, 10`), compact view, highlight executable files (colour selectable), confirmation before copying (`F5`) and deleting (`F8`), **local shell** (PowerShell or cmd), restore tabs on start, connect to the last server on start, the GitHub alarm's check interval and the default start path.

**AI** — provider, address, API key and model (see *AI Features*).

**Keyboard shortcuts** — every action can be reassigned; on saving, **duplicates** are reported, including clashes with hard-wired keys. *Reset to defaults* restores the defaults.

**Security** — app password, two-factor login and recovery codes (see *App Lock & Two-Factor*); these changes apply immediately.

**Export configuration** or **Import configuration** (below the tabs) saves settings, server profiles, bookmarks, tab favourites and history in one JSON file. Passwords, tokens and API keys, the app lock, macros, plugins and host keys are not included. **Caution:** the server profiles are stored in the file unencrypted, even while the app lock is active — keep the file safe. When importing you choose the sections; the app should be restarted afterwards.

## Panes & Tabs

- **New tab**: `Ctrl+Shift+N` or *Actions → New tab*. Each tab has its own panes, consoles and its own SSH connection. `Ctrl+Shift+E` renames the tab, `Ctrl+W` closes it.
- Tabs can be moved; the title shows the connection, and the profile's *Tab color* tints it.
- The **active pane** has a blue frame, `Tab` switches sides. Connecting and other actions apply to it.
- *Panes* menu: **Show file system only**, **Show terminal only**, **Stack panes vertically** (instead of side by side), **Swap panes** (`Ctrl+U`, connection and console move along), **Sync panes** (`Ctrl+E`, the other pane jumps to the active pane's folder — only with the same file system), **Show status** (`Ctrl+F9`, follows the other pane's cursor) and **Command to both consoles …** (`Ctrl+Shift+K`).
- **Tab favourites** (*Actions → Tab favourites*) save the current set of tabs under a name and restore it later.
- With *Restore tabs on start*, the open tabs are saved on exit and restored on start.
- The **sudo chip**: see *sudo & Other Users*.

## sudo & Other Users

For Linux/Unix servers the pane header shows the **sudo** chip.

- **Click**: the pane works as **root**. No password is needed for root itself or with NOPASSWD; otherwise the app asks for your own sudo password — or, if the user is not in the sudo, wheel or admin group, for the root password (`su`).
- **Right-click**: *Run pane as …* lists the server's users (system accounts in a submenu). Another user runs via `sudo -u` (your own password) or `su` (the target user's password).
- As another user, listing, viewing, editing, creating, renaming, deleting, permissions and transfers work.
- The chip shows the mode (*sudo*, *sudo: name*, *su: name*) and the pane gets an orange frame. Clicking again or picking *(logged in)* in the menu returns to your own user.
- The **console** does not switch users — type `sudo` or `su` there yourself.
- Passwords are kept **in memory only** and never appear on a command line.

## Security

- Passwords, passphrases, tokens and API keys are kept in the **Windows Credential Manager**, never in plain text on disk. Server passwords only if *Store password/passphrase securely in the OS keyring* is ticked in the profile.
- **Host key checking** per profile: *Trust on first use (accept-new)*, *Strict (known only)* or *Ignore (insecure)*.
- A **changed** host key aborts the connection **before login** — no credentials go to the server. With *Strict* this also applies to unknown keys.
- With *accept-new*, the app shows a **new** server's fingerprint **before logging in**: *Trust and save* remembers it, *Connect this time only* applies to this tab (including automatic reconnects), *Cancel* stops. The app logs in only after your confirmation; this also applies to a jump host (ProxyJump).
- Keys the system `ssh` already trusts (`~/.ssh/known_hosts`) are accepted; the app adds confirmed keys there too.
- The status bar shows whether the host key is known, new or unchecked. *Tools → Known host keys …* shows and cleans up the app's store — needed when a server has been reinstalled.
- The sudo password is kept in memory only and never appears on a command line.
- Remote files downloaded for viewing or editing are deleted on exit.
- *Tools → Security audit (CVE) …* checks OS, packages, `sshd_config`, firewall, open ports and accounts and matches core components against **OSV.dev**.
- The **app lock** additionally encrypts the server profiles (see *App Lock & Two-Factor*).
- The AI assistant never runs anything; cloud providers only receive content after consent.

## App Lock & Two-Factor

*Settings → Security* protects the app with a password that is asked for on start.

- **Ask for a password when the app starts** sets the app password (at least 8 characters). *Change password …* changes it; turning it off requires the current password.
- While the lock is active, the **server profiles are encrypted** (AES-256-GCM). Settings, history, bookmarks and tab favourites stay unencrypted; server passwords are in the Credential Manager anyway.
- **Two-factor authentication (authenticator app)**: scan the QR code with an authenticator app (or type in the key), enter a confirmation code and click *Activate*.
- Then **8 recovery codes** appear — this one time only. Copy them or save them as a file and keep them safe. Each code works once instead of the confirmation code; *New recovery codes …* replaces all of them.
- **Unlocking**: the password and, if active, the 6-digit code or a recovery code. After repeated failed attempts the waiting time grows (up to 60 s).
- The authenticator secret is bound to the Windows account; under another account only the recovery codes help.
- The lock applies when the app starts.

**Forgot the password?** It cannot be reset. If the lock is bypassed by deleting `applock.json`, the app puts the encrypted profiles aside as `servers.json.locked-…` and starts with an empty profile list.

## Crash Reports

If the app crashes, it writes a report to `%APPDATA%\ncssh\crashes`: a minidump file (`.dmp`) and a short text file with the error code and the call stack. The text file contains only addresses, no file or session contents.

On the next start the app points this out once; *Open folder* shows the files. Internal errors that were caught go to `errors.log` in the same folder.

When reporting a bug (e.g. as a GitHub issue), attach both files of the crash.

## Shortcuts

The complete, currently configured list is on the **Keyboard shortcuts** tab of this window (*Help → Keyboard shortcuts*); change it under *Settings → Keyboard shortcuts*.

Hard-wired (not configurable): `Ctrl+Q` (quit), `Ctrl+Shift+K` (command to both consoles), `Ctrl+F2` (preview panel), `Backspace` (up), `Enter` (open, or start with the default program), `Ctrl+F` (the pane's quick filter or search in the console output), `Esc` (clear and close the filter), `Ctrl+Shift+C`/`Ctrl+Shift+V` (terminal copy/paste), `Shift+PgUp/PgDn` (scrollback), `Ctrl+Shift+F` (search the terminal buffer) and the editor keys `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+F` and `Ctrl+G`.

## Keyboard shortcut reference

Default assignments. Every action in these tables can be reassigned under *Tools → Settings → Keyboard shortcuts*; “—” means no default shortcut.

### Actions

| Action | Shortcut |
|---|---|
| Connect via SSH | F9 |
| Command palette | Ctrl+P |
| History & favorites | Ctrl+H |
| Transfers | Ctrl+T |
| SSH tunnel | Ctrl+Shift+T |
| SFTP batch / scheduled tasks | Ctrl+Shift+B |
| Reload | Ctrl+R |
| Disconnect | — |

### Files

| Action | Shortcut |
|---|---|
| View | F3 |
| Edit | F4 |
| Copy | F5 |
| Rename | F6 |
| New folder | F7 |
| Delete | F8 |
| Bulk rename | Ctrl+Shift+R |

### Tools

| Action | Shortcut |
|---|---|
| File search (name) | Ctrl+Shift+F |
| Content search (grep) | Ctrl+Alt+F |
| Directory compare | Ctrl+D |
| File compare | Ctrl+Shift+D |
| Convert file encoding | — |
| Manage venv | — |
| Hidden files | Ctrl+. |
| Sync panes | Ctrl+E |
| Swap panes | Ctrl+U |
| Bookmarks of the active pane | Ctrl+B |
| Show status (follows the cursor of the other pane) | Ctrl+F9 |

### Tabs & app

| Action | Shortcut |
|---|---|
| Tab favourites | — |
| New tab | Ctrl+Shift+N |
| Rename tab | Ctrl+Shift+E |
| Close tab | Ctrl+W |
| Settings | Ctrl+, |
| Help (manual) | F1 |

### Fixed application shortcuts

These keys are hard-wired and cannot be changed.

| Action | Shortcut |
|---|---|
| Quit | Ctrl+Q |
| Command to both consoles … | Ctrl+Shift+K |
| Preview panel | Ctrl+F2 |

### Mouse & fixed keys

| Area | Action | Shortcut |
|---|---|---|
| Navigation | Open folder / view file | Double-click |
| Navigation | Open folder / open file with its default program | Enter |
| Navigation | Back / Forward | Alt+←  /  Alt+→ |
| Navigation | Parent folder | Backspace |
| Navigation | Switch pane | Tab |
| Navigation | Filter pane | Ctrl+F |
| Navigation | Sync / Swap panes | Panes menu |
| Files | Mark | Space / Ins |
| Files | Mark / unmark by pattern | Num +  /  Num − |
| Files | Invert selection | Num * |
| Files | Select all (Ctrl+A) | Ctrl+A |
| Files | Copy / Paste (into this pane) | Ctrl+C  /  Ctrl+V |
| Files | Context menu (permissions, properties, …) | Right-click |
| Files | Run — open with OS default program | Enter / context menu |
| Files | Transfer | Drag + release |
| Console / terminal | History | ↑  /  ↓ |
| Console / terminal | Scrollback | Shift+PgUp / PgDn |
| Console / terminal | Copy selection | Ctrl+Shift+C / Ctrl+Ins / Ctrl+C with a selection |
| Console / terminal | Paste | Ctrl+Shift+V / Ctrl+V / Shift+Ins |
| Console / terminal | Select word | Double-click |
| Console / terminal | Search in buffer | Ctrl+Shift+F |
| Console / terminal | Search the output (Commands mode) | Ctrl+F |
| Console / terminal | Cancel the running command (Commands mode) | Ctrl+C / Esc |
