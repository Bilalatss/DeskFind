<p align="center">
  <img src="docs/icon.png" width="80" height="80" alt="">
</p>

<h1 align="center">DeskFind</h1>

<p align="center">
  <b>Ctrl+F for your Windows desktop.</b><br>
  Press Ctrl+F on the desktop and type: the desktop answers first, then the folders on it, then the rest of your PC.
</p>

<p align="center">
  <a href="https://github.com/Bilalatss/DeskFind/releases/latest"><img src="https://img.shields.io/github/v/release/Bilalatss/DeskFind?label=release" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Windows-11-0078D4" alt="Windows 11">
  <img src="https://img.shields.io/badge/size-260%20KB-2ea44f" alt="260 KB">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/Bilalatss/DeskFind" alt="MIT license"></a>
</p>

<p align="center">
  <img src="docs/screenshot.png" width="680" alt="DeskFind open on the desktop with results grouped as Desktop, In desktop folders and Rest of this PC">
</p>

<p align="center"><a href="README.tr.md">Türkçe</a></p>

---

Windows has no quick way to search just the desktop. Start search looks through the whole PC and mixes in apps and
web results, and Explorer's search needs a window first and takes its time.

DeskFind puts a search box on the desktop's own Ctrl+F, and only there: in every other window, Ctrl+F still belongs
to the app you're in.

## Features

- **Desktop first.** Results come in three groups: *Desktop*, *In desktop folders* and *Rest of this PC*.
- **Instant.** The desktop and its folders answer from memory in 1–3 ms per keystroke. The rest of the PC comes from [Everything](https://www.voidtools.com) in about 40–60 ms.
- **Finds the icon.** Ctrl+Enter selects the file's icon on the desktop itself, or opens its folder in Explorer when it lives elsewhere.
- **Learns.** What you open often moves up, and the empty box lists what you opened recently.
- **Skips the noise.** `node_modules`, `.git`, `__pycache__` and similar folders never show up. Neither do the Windows folder, temp files and Windows' *Recent* shortcuts. Build output, app data and logs come after your own files.
- **Tiny.** A 260 KB exe with no runtime to install. It uses under 1 MB of memory while it waits in the tray.
- **Looks native.** Acrylic, rounded corners, light and dark mode and your accent color all follow Windows.
- **English and Turkish**, picked from the Windows display language.
- **Private.** No network access, no telemetry.

## Install

You need Windows 11 and [Everything](https://www.voidtools.com/downloads/) (free). Paste this into PowerShell:

```powershell
irm https://raw.githubusercontent.com/Bilalatss/DeskFind/main/install.ps1 | iex
```

The [script](install.ps1):

- downloads `DeskFind.exe` from the latest release and checks its SHA-256 against the release;
- copies it to `%LOCALAPPDATA%\Programs\DeskFind`;
- adds it to the Start menu and to *Settings › Apps*;
- starts it.

From then on DeskFind starts with Windows; you can turn that off in the tray menu. The script needs no admin rights,
and if Everything is missing it offers to install it with winget. Run the same line again to update.

<details>
<summary>Why a command and not a setup.exe or .msi?</summary>

DeskFind isn't code-signed yet. An unsigned `.exe` or `.msi` downloaded in a browser usually gets a SmartScreen
warning; a file downloaded by PowerShell doesn't. The script is short, so read it before you run it.
</details>

<details>
<summary>Install by hand</summary>

Download `DeskFind.exe` from [Releases](https://github.com/Bilalatss/DeskFind/releases/latest), open PowerShell in that
folder and run `.\DeskFind.exe --install`. Windows may warn you first because the file came from a browser.

You can also run the exe from any folder without installing it. In that case it doesn't show up in the Start menu or
in *Settings › Apps*, but it still turns on starting with Windows from that folder.
</details>

**Uninstall:** use *Settings › Apps › DeskFind › Uninstall*, or run:

```powershell
& "$env:LOCALAPPDATA\Programs\DeskFind\DeskFind.exe" --uninstall
```

This removes the exe, its Start menu entry, its settings, its history and the autostart entry.

## Use

Click an empty spot on the desktop (or press <kbd>Win</kbd>+<kbd>D</kbd>), press <kbd>Ctrl</kbd>+<kbd>F</kbd> and type.

| Key | Action |
| --- | --- |
| <kbd>↑</kbd> <kbd>↓</kbd> · <kbd>PgUp</kbd> <kbd>PgDn</kbd> | Move through the results |
| <kbd>Enter</kbd> or click | Open |
| <kbd>Ctrl</kbd>+<kbd>Enter</kbd> or <kbd>Ctrl</kbd>+click | Show it: select its icon on the desktop, or open its folder |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> | Copy the full path |
| <kbd>Ctrl</kbd>+<kbd>E</kbd> | Continue the search in Everything |
| <kbd>F5</kbd> | Search again |
| <kbd>Esc</kbd> | Close and go back to the desktop |

**Search tips**

- Words match in any order, and case and accents don't matter: `cafe` finds *Café* and `i` finds *ı*.
- Everything's syntax works as well: `ext:pdf`, `*.psd`, `dm:today`, `!draft`, `invoice | receipt`.
- Clicking the tray icon (often hidden under <kbd>^</kbd>) or *DeskFind* in the Start menu also opens the box.
- Right-click the tray icon for *Start with Windows*, *About DeskFind* and *Exit*.

## How it works

| Group | Source |
| --- | --- |
| **Desktop** | Listed straight from disk each time the box opens, so it's always current. It follows Explorer's hidden-file settings and includes the Public desktop. |
| **In desktop folders** | When the box opens, one Everything query loads every file and folder under the desktop into a compact in-memory index (26,000 entries take 1.4 MB). Each keystroke filters it locally. The index is dropped a few seconds after the box closes. |
| **Rest of this PC** | Everything's IPC, with no SDK DLL. Everything sends the 1,000 most recently changed matches and DeskFind ranks them. Answers are cached, and a longer query is filtered locally when a shorter one already returned every match. |

- **Ranking:** the best name match comes first: the exact name, then the start of the name, the start of a word, and anywhere in the name. Among equal matches, what you open often and recently goes up, and build output, caches and app data go down.
- **Drawing:** DeskFind draws its own window. DirectWrite glyphs are rasterized on the CPU into a bitmap shown in a DWM acrylic window. There is no UI framework and no GPU context, so there is very little to keep in memory while it's hidden.
- **Idle:** a few seconds after the box closes, DeskFind frees its caches, trims its memory and asks Windows to run it in efficiency mode.
- **Speed:** while the box is open, DeskFind lets Everything out of Windows 11's efficiency mode. Its queries got about 30% faster in testing.

## Performance

Measured on the author's PC (Windows 11 24H2, about 2.6 million files indexed by Everything):

| | |
| --- | --- |
| Ctrl+F to window on screen | 10–33 ms |
| Desktop and desktop folders, per keystroke | 1–3 ms |
| Rest of the PC, through Everything | 40–60 ms |
| Typing on (filtered from the cache) | ~1 ms |
| Memory while waiting in the tray | 0.3–0.7 MB |
| Memory with the box open | ~5 MB |
| Download | 260 KB |

## Requirements

- **Windows 11**, x64. It is built and tested on 24H2. Windows 10 is untested; the window relies on Windows 11's acrylic and rounded corners.
- **[Everything](https://www.voidtools.com/downloads/) 1.4** by voidtools, the regular version.
  - *Everything Lite* has no IPC, so it won't work.
  - If Everything is installed but not running, DeskFind starts it.
  - Tested with 1.4.1. The 1.5 alpha should work but is untested.
  - Without Everything, only the desktop itself is searched.

## Build from source

You need Visual Studio 2022 or later (or its Build Tools) with *Desktop development with C++*.

```bat
build.cmd            :: bin\DeskFind.exe
build.cmd install    :: build, then install it for this user
build.cmd trace      :: bin\DeskFindTrace.exe, logging timings to %LOCALAPPDATA%\DeskFind\trace.log
```

There is also a headless test of the search pipeline. DeskFind is a windowed program, so wait for it with `start /wait`:

```bat
start /wait bin\DeskFind.exe --selftest out.txt @index "d>de>des" report
```

In this command:

- `>` types one prefix at a time;
- `+` types them in a burst;
- `@index` builds the desktop-folder index first.

| File | What it does |
| --- | --- |
| `src/app.cpp` | The window, keyboard and mouse, painting and the desktop-only hotkey |
| `src/search.cpp` | The three groups, ranking, caching and the desktop-folder index |
| `src/everything.cpp` | Everything IPC on its own thread |
| `src/canvas.cpp`, `src/text.cpp` | The software renderer and DirectWrite text |
| `src/icons.cpp` | File icons, loaded on a background thread |
| `src/history.cpp` | What you opened, for ranking and *Recently opened* |
| `src/tray.cpp`, `src/shell.cpp` | The tray icon and menu; opening, revealing and autostart |
| `src/setup.cpp` | `--install` and `--uninstall` |
| `src/lang.cpp` | English and Turkish text |

Releases are built by [GitHub Actions](.github/workflows/build.yml) from a `v*` tag, which must match `src/version.h`.

## Privacy

DeskFind makes no network connections. *About DeskFind* in the tray menu opens this page in your browser.

On disk it keeps:

- a list of what you opened from it (`history.txt`, up to 300 entries), used for ranking and *Recently opened*;
- an `error.log`, only if something fails.

Both are in `%LOCALAPPDATA%\DeskFind`. In the registry it keeps only its autostart entry, a first-run flag and its
*Settings › Apps* entry. Uninstalling removes all of them.

## FAQ

**Ctrl+F doesn't open it.** The desktop has to be the active window, so click an empty spot on it first. In any other window, Ctrl+F belongs to that window.

**How do I start it again after *Exit*?** Open *DeskFind* from the Start menu.

**Can I use English on a Turkish Windows, or the other way round?** Set a user environment variable `DESKFIND_LANG` to `en` or `tr`. Then choose *Exit* in the tray menu and start DeskFind again from the Start menu.

**Smart App Control blocks it.** Smart App Control blocks unsigned apps and has no exceptions, and DeskFind isn't signed yet.

**Which monitor does it open on?** The one your mouse pointer is on.

## License

[MIT](LICENSE). DeskFind uses [Everything](https://www.voidtools.com) by voidtools through its public IPC interface and isn't affiliated with voidtools.
