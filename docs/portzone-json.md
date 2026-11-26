# portzone.json: a guide for porters

PortZone lists every port in its store, with or without help from the porter. A `portzone.json` in your repository
turns your port from **Manual** (PortZone shows your README and release files; the player picks where things go) into
**Easy Setup** or **Build on PC**:

| Without `portzone.json` | With it |
|---|---|
| The player reads your README and picks a release file and a folder | One **Install** button that puts your release where it belongs, and **Update** when you publish a new one |
| The player works out which of their files go where | A checklist of what they must bring, with Found / Missing for each file, and the folder layout |
| Files are sent or moved by hand | Players drop their files (even a whole drive's worth) on PortZone's upload page or anywhere on the SD card; PortZone puts each where you said |
| Generic description and the owner's avatar | Your description, icon and banner; your system tag in Browse's filter |
| | Your port in Sphaira's store too (single-file installs) |

Which of the three a port is comes from this file alone:

| Kind | When | Download button |
|---|---|---|
| **Manual** | No `portzone.json` | The player picks one of your release files |
| **Easy Setup** | A `portzone.json` without a PC step | **Install** for the release files `install.steps` names |
| **Build on PC** | `game_data.prepare.where: "pc"` | **Install** only if `install.steps` names release files; none when your PC tool makes everything, `.nro` included (leave `steps` out) |

You don't need to ask anyone: once the file is on your default branch, a game's page in PortZone picks it up the next
time a player opens it, and the store's daily rebuild puts it in every list. Until then, the store may keep a draft for
you in `store/drafts/<id>/portzone.json`; yours replaces it.

## Where it goes

- One file named exactly `portzone.json`, at the **top** of your repository, on its **default branch**. PortZone reads
  `https://raw.githubusercontent.com/<owner>/<repo>/HEAD/portzone.json`. A repository that makes several games (one
  builder for Terraria and tModLoader) has one file per game; the store's `ports.json` entry for each names its file
  with `manifest` (`"manifest": "portzone/tmodloader.json"`).
- Images it names (`icon`, `banner`) are paths in the same repository and branch.
- Your releases stay as they are: PortZone downloads the release files you name from GitHub Releases.

```text
your-repo/
  portzone.json          <- here
  README.md
  res/switch/icon.jpg    <- "icon": "res/switch/icon.jpg"
  docs/banner.jpg        <- "banner": "docs/banner.jpg"
```

## Layout

```text
portzone.json
├─ format: 1
├─ description, icon, banner, platform   how the port looks in the store
├─ release                               optional: allow prereleases
├─ install                               what to do with your release files, and where the .nro ends up
│   ├─ binary
│   └─ steps[]                           unzip or copy, one release file each
├─ launch                                full memory, notes for starting it
├─ game_data                             what the player brings, and where it goes
│   ├─ dir                               the game folder; every path below is relative to it
│   ├─ inputs[]                          things the player brings
│   │   └─ options[]                     alternative ways to bring them
│   │       └─ files[]                   files and folders, with optional size, sha256, match, markers
│   ├─ prepare                           a conversion on first start, or on a PC first
│   └─ generated[]                       what the port creates itself (settings, saves, logs)
└─ steps[]                               the instructions, one action each
```

The full reference with every field is the schema,
[`store/schema/portzone.schema.json`](../store/schema/portzone.schema.json). Put its URL in `$schema` and most editors
check your file as you type.

## Template

Start from this and delete what doesn't apply:

```json
{
  "$schema": "https://raw.githubusercontent.com/JohnUzoka/portzone-nx/main/store/schema/portzone.schema.json",
  "format": 1,
  "description": "<What it is and how it was made>. Needs your own <exactly what the player must own>.",
  "icon": "res/switch/icon.jpg",
  "banner": "docs/banner.jpg",
  "platform": { "original": "<Xbox 360, 3DS, PC, Source...>", "runs_on": ["switch"] },
  "install": {
    "binary": "sdmc:/switch/<folder>/<game>.nro",
    "steps": [{ "asset": "<end of the release file's name, like -switch.zip>", "unzip": "sdmc:/" }]
  },
  "launch": { "full_memory": true, "notes": ["<anything about starting it>"] },
  "game_data": {
    "dir": "sdmc:/switch/<folder>",
    "inputs": [
      {
        "id": "game",
        "label": "<what the player brings, with the versions that work>",
        "options": [
          { "label": "<one way to provide it>", "files": [{ "path": "<file or *.ext>", "note": "<what it is>" }] }
        ]
      }
    ],
    "prepare": {
      "where": "switch",
      "summary": "<what the first start does with the files>",
      "done": ["<a file the conversion leaves>"]
    },
    "generated": [{ "path": "<file or folder/>", "note": "<what the port writes there>" }]
  },
  "steps": ["<one action per step, in order>"]
}
```

## Examples

Real files in this repository's store, each a different kind of port:

| Port | Shows |
|---|---|
| [`haloce-nx`](../store/drafts/haloce-nx/portzone.json) | A disc image (`.iso` or `.xiso` as options), an optional folder of maps (`match`), extraction on first start (`prepare.where: "switch"`), many `generated` folders |
| [`triaevum`](../store/drafts/triaevum/portzone.json) | A cartridge dump or its extracted files, an optional texture pack found by a marker, a file the game can also download itself |
| [`sa2-nx`](../store/drafts/sa2-nx/portzone.json) | A web installer on a PC (`prepare.where: "pc"` with `tool`), a single `.nro` copied into place |
| [`nfsmw-nx`](../store/drafts/nfsmw-nx/portzone.json) | A PC installer that builds everything, including the `.nro` (no `install.steps`), a disc folder found by `markers` |
| [`terraria`](../store/drafts/terraria/portzone.json) | A PC build whose output spans `switch/` and `mono/`: `game_data.dir` is the card's root (`"sdmc:/"`), no `install.steps` |

## Fields

### How the port looks

- **`description`**: one or two sentences, at most 511 characters: `<what it is: the game, and how it was made
  (decompilation, recompilation, source port)>. Needs your own <exactly what the player must own>.` Plain and factual;
  no "amazing", emoji or version numbers; don't start with the title. Example: `Sonic Adventure 2 (Xbox Live Arcade)
  recompiled to run natively at 60 FPS. Needs your own Xbox 360 copy, converted on a PC.`
- **`icon`**: square PNG or JPEG, 256x256 or larger, at most 1 MB. Most ports already have one for their `.nro`.
  Without one PortZone shows your GitHub avatar.
- **`banner`**: 16:9 PNG or JPEG (1280x720), at most 2 MB, across the top of the port's page. A screenshot of the game
  running on a Switch works well. No logos or box art you don't have the rights to.
- **`platform.original`**: what the port was made from, from the schema's list: the system the game came out on
  (`N64`, `3DS`, `Xbox 360`...), `PC`, or `Source` when the game's own source code is the starting point. A missing
  system goes into the schema's list by pull request.
- **`platform.runs_on`**: `["switch"]`, `["switch2"]` or both. Omitted means `["switch"]`. PortZone shows the others
  but won't install them.

### Installing

- **`install.steps`**: what to do with your release files, in order. Each step takes one file and either:
  - `"unzip": "<folder>"`: extracts the zip there, keeping its folders. A zip that holds `switch/<folder>/...`
    unzips to `"sdmc:/"`. Add `"from": "bundle/"` to take only that folder of the zip.
  - `"copy": "<folder>"`: puts the file there as it is. Add `"name"` to rename it.

  `asset` matches the **end** of a file name, ignoring case; make it specific enough to skip debug builds
  (`halo-switch-release.zip`, not `.zip`). PortZone uses the newest stable release that has a file for every step. A
  later step overwrites what an earlier one wrote, so "unzip the data, then copy the newer `.nro` over it" works. Omit
  `steps` when a PC tool makes everything.
- **`install.binary`**: where the `.nro` ends up. Its presence is how PortZone and Sphaira know the port is installed.
- **`release.prerelease`**: `true` when your releases are all prereleases. Otherwise players can turn on **Test
  builds** per game themselves.
- **`launch.full_memory`** (default `true`): the port needs title takeover or a forwarder, not the Album applet.
  **`launch.notes`**: what breaks if done wrong (forwarder settings), in-game menus worth knowing.

### What the player brings

- **`game_data.dir`**: the game folder. Every path below is relative to it and case-insensitive. When the player's
  files go in several top-level folders (a PC build that writes `switch/Terraria.nro` and `mono/`), use `"sdmc:/"`,
  the card's root, and give paths from there (`switch/Terraria.nro`, `mono/`). Players then drop the build's folder,
  or its `switch` and `mono` folders together, and each part goes where it belongs.
- **Inputs and options**: an input is something the player brings; it is met when **every** file of **any one** of its
  options is present. Use options for alternatives (`.iso` or `.xiso`; a dump or its extracted files), and
  `"required": false` for extras such as an expansion. An option's `url` is only for files their owner gives away
  (shareware, a mod page).
- **Files**: `*` and `?` work in the last part (`*.3ds`). A trailing `/` is a folder:
  - `"match": "*.map"`: the folder must hold files of that kind; only those are sent or moved into it.
  - `"markers": ["liblist.gam", "halflife.wad"]`: for whole game folders (a PC game's data, a mod pack). 2 to 5 files
    inside it that prove it's the right one, usually what your port checks or loads first. Never list every file.
    The folder holding every marker is the one copied, with everything in it, whatever the player named it. Pick
    markers only that game has: a `config.cfg` proves nothing.
  - `size` and `sha256`: only when exactly one version works and you know the values. Hashes are checked for files up
    to 64 MiB on the SD card, and for any size as files are uploaded.
- How PortZone finds the files the player offers: a file by its name (and `size`), with missing leading folders allowed
  (`import/code.bin` is found as `dump/code.bin` when the option's other files are beside it); a folder by its
  `markers`, else its name and `match`. When two ports want the same file, or two files fit one input, the player
  chooses.

### Conversions and what the port makes

- **`prepare.where: "switch"`**: your port converts the player's files on first start. List what it leaves in `done`,
  so a player who deleted their disc image after converting still shows as ready.
- **`prepare.where: "pc"`**: the player runs a tool on a computer first; `inputs` are the files that tool makes. The
  store lists the port under Build on PC. **`prepare.tool`** (`name`, `url`) links to your web installer, your tool's
  download page, or a guide when there is no tool; PortZone's upload page shows it as step 1.
- **`generated`**: settings, saves, logs and caches your port creates, so players know they aren't missing.

### Steps

One action per step, in order, starting with a verb ("Copy", "Extract", "Start"). Exact paths without `sdmc:/`
(`switch/oot3dre/`). Say which versions or regions work and which don't.

## Checking your file

```sh
pip install jsonschema
python3 store/scripts/build_store.py --check --drafts <a folder holding <id>/portzone.json>
```

run from a clone of this repository, checks your file against the schema before you commit it. To see it in PortZone
on a PC, see [development.md](development.md) (`--drafts`).

## Rules

- **No game data.** Never link to ROMs, disc images, keys, firmware or extracted game files.
- **No ways around copy protection.** Say which file the player needs ("your own cartridge dump, .3ds"), not how to
  get it past protection: no decryption tools or key and firmware dumping guides, and no links to them.
- **Facts from your docs.** Every file name, folder and version must match what your README and releases say.
