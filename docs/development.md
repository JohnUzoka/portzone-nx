# Development

## Building

Docker only; nothing is installed on the machine. `library/borealis` is a git submodule (xfangfang/borealis);
`scripts/build.sh` fetches it on the first build and applies `patches/borealis.patch` (the emulator build's font
switch) once.

```sh
scripts/build.sh desktop          # build/portzone, Linux (runs under WSLg on Windows)
scripts/build.sh switch           # build_switch/portzone.nro
scripts/build.sh switch-emulator  # build_switch_emu/portzone.nro, for Ryujinx without firmware
```

The emulator build skips the system fonts (Ryujinx has none without firmware), so text is Inter only and the button
symbols in the bottom bar are boxes, and it sends its log (borealis, debug level) to Ryujinx's console. It is for
testing; release the normal Switch build. The font switch lives in `library/borealis` (`BOREALIS_NO_SHARED_FONTS` in
`switch_font.cpp` and `switch_wrapper.c`).

## Running on a PC

In your browser, which works even when WSL cannot open Linux windows:

```sh
scripts/run-browser.sh
```

then open `http://localhost:6080/vnc.html?autoconnect=1&resize=scale` in Windows. As a Linux window (needs a working
WSLg; when WSLg's compositor crashes, the app crashes with it):

```sh
scripts/run-desktop.sh
```

Both read `store/ports.json` and `store/manifests.json` (run `store/scripts/build_store.py` after changing a draft),
with `./sdmc` standing in for the SD card. Keyboard: arrows move, Enter is A, Escape is B, X and Y are themselves.
Options:

| | |
|---|---|
| `--index URL\|FILE` | `ports.json` to read (default: the store on GitHub) |
| `--sd FOLDER` | the SD card (default `./sdmc`; `sdmc:/` on a Switch) |
| `--drafts FOLDER` | `<id>/portzone.json` files used instead of the store's, to try one before committing it |
| `--console switch\|switch2` | the console to act as, for testing `runs_on` (PC builds only) |
| `--update-repo OWNER/REPO`, `--self FILE.nro` | where PortZone's own updates come from, and the file they replace |

Screenshots of every screen are in `docs/screenshots/`.

## Releasing

Set the version in `CMakeLists.txt` (`VERSION_MAJOR`/`MINOR`/`ALTER`; the first release is 0.1.0), build the Switch
`.nro`, and publish a GitHub release tagged `v<version>` with `build_switch/portzone.nro` attached under that name.
Copies of PortZone already out there offer it as an update. Mark test builds as prereleases: they are not offered.

## The store

No server: the store is static files in `store/`, and every build is downloaded from the port's own GitHub release.

```text
store/ports.json                   every listed port (added by pull request, see CONTRIBUTING.md)
store/schema/ports.schema.json     one ports.json entry
store/schema/portzone.schema.json  a port's own portzone.json (guide: docs/portzone-json.md)
store/drafts/<id>/portzone.json    portzone.json files waiting for their porter
store/manifests.json               generated: every port's portzone.json; do not edit
store/sphaira.json                 generated for Sphaira; do not edit
store/scripts/build_store.py       checks the store, writes the two generated files
```

- **PortZone** reads `store/ports.json` and `store/manifests.json` at start: two requests, whatever the number of
  ports. `manifests.json` holds each port's `portzone.json` from the top of its repository, else the store's draft.
  Opening a game's page re-reads that port's own `portzone.json`
  (`raw.githubusercontent.com/<owner>/<repo>/HEAD/portzone.json`) and rebuilds the page if it changed. A port with
  none is installed by hand from its release files, with its README beside it. Release lists come from the GitHub API
  when a port's page opens, and at start for the ports PortZone installed.
- **Sphaira** reads `store/sphaira.json` as a store source:
  `https://raw.githubusercontent.com/JohnUzoka/portzone-nx/main/store/sphaira.json`. Only ports whose own repository
  has a `portzone.json` with a release are in it.
- `.github/workflows/store.yml` rebuilds both generated files on store changes and once a day, so porters' edits
  reach every list without a pull request; pull requests run `build_store.py --check`.

## On the SD card

It is one `.nro`: everything it needs is in its romfs, including copies of `store/ports.json` and
`store/manifests.json` from build time (used while the store can't be reached) and the upload page
(`resources/web/index.html`). It writes to `sdmc:/config/portzone/` (`settings.json`; `cache/` with the store files
and icons/banners for offline use; `installed/`, one record per port PortZone installed; `tmp/`), to the folders
ports install into, and, when the player sends or moves game files, to the ports' game folders.

## Code

```text
src/config.*       arguments, SD paths
src/http.*         libcurl
src/catalog.*      ports.json + each portzone.json (draft, repository, offline cache)
src/model.*        portzone.json parsing
src/files.*        finding the player's files (case-insensitive, *, folders), moving files
src/github.*       releases and README
src/installer.*    download, SHA-256 check, .nro/.zip install, install records, removal
src/markdown.*     README to plain text blocks
src/images.*       icons and banners, cached on the SD card for a week
src/settings.*     theme, list view, start-up checks, test builds; saved in settings.json
src/state.*        what is on the SD card for a port: installed, files present
src/update.*       PortZone's own updates from its GitHub releases
src/port_updates.* new releases of the games PortZone installed (the start-up check)
src/threads.*      background threads (libnx has no std::thread::detach)
src/matcher.*      which port input offered files meet, and where each goes
src/gamefiles.*    searching the SD card for game files, moving them into place
src/transfer.*     the upload server behind Send from phone or PC (HTTP API in transfer.hpp)
src/ui/            borealis screens
library/borealis   xfangfang/borealis (UI)
third_party/       nlohmann/json, miniz, PicoSHA2
resources/         logo, fonts, icons, CA bundle for the Switch, i18n/ (app text), web/ (the upload page)
```

- `resources/icon/portzone.svg` is the logo; `icon.png` (app header) and `icon.jpg` (homebrew menu, 256x256 JPEG) are
  rendered from it with `rsvg-convert`.
- `resources/font/font.ttf` is [Inter](https://rsms.me/inter/) 4.1 Regular (OFL, `Inter-LICENSE.txt`) with its
  private-use code points removed: the Switch draws its button symbols from those code points, and Inter's own glyphs
  there replaced them.
- On a PC, the console-style fallback fonts come from borealis' own copy at build time; they are not part of
  PortZone's resources, and the Switch build uses the system's.
- App text is in `resources/i18n/<language>/portzone.json` (`"portzone/browse/search"_i18n`, or
  `brls::getStr(key, args...)` with `{0}`-style placeholders), English in `en-US/`; `hints.json` and `scroll.json`
  translate borealis' own footer. A missing key falls back to English. `src/languages.cpp` picks the language before
  borealis starts (the console's, mapped to a translation: `es-419` to `es`, `zh-CN` to `zh-Hans`; or the choice in
  About). The upload page carries its own dictionary for the same languages and follows the browser.
  Adding a language: a folder of the three files, and an entry in `languages()`.
- `std::filesystem::copy_file` can't copy on the Switch, and Horizon's rename doesn't replace a file: use `moveFile` in
  `files.hpp`.
