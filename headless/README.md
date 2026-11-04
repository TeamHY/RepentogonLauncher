# rgon-cli

Headless front-end for the REPENTOGON launcher core. It is meant to be driven by
other programs (e.g. [Cartridge](https://github.com/TeamHY/cartridge)) and does
not depend on wxWidgets.

It reuses the upstream sources for installation validation, the Repentogon
installer (GitHub/Steam Workshop download, game file copy, exe patching) and the
regular launch routine. Only the GUI is replaced by a JSON command line
interface, so a launch through rgon-cli behaves like a launch through the GUI
launcher.

## Build

rgon-cli must be built for **Win32**, like the GUI launcher (the game is 32-bit).

```sh
git submodule update --init --depth 1 deps/curl deps/zlib deps/inih deps/rapidjson deps/libzip
cmake -A Win32 -B build -DLAUNCHER_BUILD_GUI=OFF   # Visual Studio generator
cmake --build build --config Release --target rgon-cli
cmake --install build --config Release --component rgon-cli --prefix dist/rgon-cli
```

`build/Release/rgon-cli/` also contains a runnable layout after each build.

The `rgon-cli` workflow builds every change and, when started manually with
`release` checked, publishes `rgon-cli.zip` and `rgon-cli.zip.sha256` under the
tag `cli-v<headless/VERSION>`.

## Package layout

```
rgon-cli.exe
*.dll                    zlib, inih, steam_api and the x86 Visual C++ runtime
steam_appid.txt
launcher-data/patch/     exe patches (from /patch)
```

rgon-cli moves to its own folder at startup: downloads, patches and the log
files (`rgon-cli-<command>.log`) live there. The folder must be writable.

## Commands

```
rgon-cli version
rgon-cli status  --isaac <path>
rgon-cli install --isaac <path> [--force-repentogon-update] [--unstable-updates]
rgon-cli launch  --isaac <path> [--launch-mode repentogon|vanilla] [--console]
                 [--luadebug] [--luaheapsize <size>] [--set-stage <id>]
                 [--set-stage-type <id>] [--load-room <path>]
```

`--isaac` accepts `isaac-ng.exe` or the game folder. Options use the same names
as the GUI launcher (`--name value` or `--name=value`). The GUI launcher
configuration file is never read nor written.

`launch` returns as soon as the game is running (it does not wait for the game
to exit). `install` refuses to run while another `install` is in progress.

## Output

rgon-cli is a GUI subsystem executable (no console window when started by
another program). It writes UTF-8 [JSON Lines](https://jsonlines.org/) on the
standard output it inherits; when started from a terminal without redirection
it attaches to that console.

Every line is an object with a `type`:

```jsonc
{"type":"log","level":"info","message":"Downloading Repentogon release ..."}
{"type":"download","file":"./launcher-data/REPENTOGON.zip","bytes":1048576}
{"type":"result","command":"install","ok":true,"outcome":"installed", ...}
```

The `result` line is always the last one. Failed results carry an `error` code
and usually a `message`.

| error                       | meaning                                                         |
|-----------------------------|-----------------------------------------------------------------|
| `usage`                     | bad command line (`usage` holds the help text)                  |
| `invalid_isaac`             | `--isaac` is not a valid Isaac executable                       |
| `isaac_incompatible`        | the game version cannot be used with Repentogon (install)       |
| `busy`                      | another install is running                                      |
| `check_updates_failed`      | the Repentogon release could not be fetched                     |
| `launcher_update_required`  | the Repentogon release needs a newer launcher core              |
| `install_failed`            | see `installResult` and `rgon-cli-install.log`                  |
| `repentogon_not_installed`  | launch: Repentogon is missing or incomplete                     |
| `repentogon_incompatible`   | launch: the Repentogon game copy is not compatible              |
| `launch_failed`             | launch: the game could not be started, see `rgon-cli-launch.log`|

Exit codes: `0` success, `1` failure, `2` usage error, `3` busy.

### Status payload

`status`, `install` and most failures include:

```jsonc
{
  "isaac": {
    "valid": true, "exePath": "...\\isaac-ng.exe", "folderPath": "...\\",
    "version": "v1.9.7.12.J273", "compatible": true, "needsPatch": false,
    "reason": "Should just work."
  },
  "repentogon": {
    "folderPath": "...\\Repentogon",
    "installed": true,        // DLLs and game copy are valid
    "launchable": true,       // same rule as the GUI launcher's launch button
    "state": "modern",        // none | broken | legacy | modern
    "version": "1.1.2", "zhlVersion": "1.1.2", "loaderVersion": "1.1.2",
    "versionsMatch": true,
    "files": [{"name": "libzhl.dll", "found": true}, ...],
    "executable": { /* same fields as "isaac", for the Repentogon game copy */ }
  }
}
```

`launch` results add `mode`, `pid` and `exePath`; `install` results add
`outcome` (`installed` or `up_to_date`) and `installResult`; `version` returns
`cliVersion` and `launcherVersion` (the launcher core version, `/version.txt`).

## Upstream changes

To keep merges with TeamREPENTOGON/Launcher simple, upstream files are only
touched where wxWidgets had to be made optional (`LAUNCHER_HEADLESS`) and in the
injector:

- `CMakeLists.txt`: `LAUNCHER_BUILD_GUI` / `LAUNCHER_BUILD_HEADLESS` options.
- `include/launcher/cli.h`, `src/standalone_rgon_folder.cpp`,
  `src/repentogon_installer.cpp`: wxWidgets includes / message box guarded.
- `src/injector.cpp`, `include/launcher/injector.h`: the module holding the
  loader is looked up from the code address instead of a hard-coded DLL name,
  and `Launcher::LaunchDetached` starts the game without waiting for it.
