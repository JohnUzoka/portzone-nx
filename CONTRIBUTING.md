# Adding a port

Written for people and for coding agents. Follow it exactly; the pull request check enforces the formats.

A port gets into PortZone in up to two parts:

| Part | Where | Required | What players get |
|---|---|---|---|
| An entry in `ports.json` | This repository, by pull request | Yes | The port is listed. Without a `portzone.json`, the app shows its README and release files, and the player picks where things go. |
| `portzone.json` | The top of the port's own repository, default branch | No | One-button install, a checklist of the player's files, automatic file placement. See [docs/portzone-json.md](docs/portzone-json.md). |

Anyone can add the `ports.json` entry. Only the port's own repository can carry `portzone.json`; until it does, a
draft can wait in `store/drafts/<id>/portzone.json` (the app reads it when the repository has none).

## The `ports.json` entry

Add one object to `ports` in [`store/ports.json`](store/ports.json), keeping the list sorted by `id`:

```json
{
  "id": "<lowercase id: a-z 0-9 . _ ->",
  "url": "https://github.com/<owner>/<repo>",
  "title": "<the game's name, as players know it>",
  "author": "<who made the Switch port>",
  "description": "<see below>",
  "platform": { "original": "<the system the game was made for, like Xbox 360>" }
}
```

- `id`: short and stable; Sphaira's package name. Use the folder the port already uses if it has one (`sa2-nx`,
  `oot3dre`). A port without a `portzone.json` installs to `switch/<repository name>` by default.
- `url`: the repository whose **releases** hold the Switch build. Exactly `https://github.com/<owner>/<repo>`: no
  `/releases`, no trailing slash, no `.git`.
- `icon`, `banner` (optional): HTTPS PNG or JPEG, for ports whose `portzone.json` has none (sizes in the guide).
- `platform` (optional): as in `portzone.json`, for ports without one. The systems are listed in the schema.
- `description`: one or two sentences, at most 511 characters: `<what it is: the game, and how it was made
  (decompilation, recompilation, source port)>. Needs your own <exactly what the player must own>.` Plain and factual,
  no "amazing", emoji or version numbers; name the format the player must own ("your own Xbox disc image").

## Where the facts come from

In this order: the port's README, its install docs, its latest release notes, its issues. **Never guess a file name,
folder or version.** If something is not written down anywhere, leave it out and say so in the pull request.

## Rules

- **No game data.** Never link to ROMs, disc images, keys, firmware or extracted game files.
- **No ways around copy protection.** Say which file the player needs, not how to get it past protection: no
  decryption tools or key and firmware dumping guides, and no links to them.
- **Names are only for naming.** No logos, box art or other artwork you don't have the rights to in `icon` or
  `banner`, and nothing that suggests a company endorses the port.
- **Projects only.** List open repositories whose releases hold the port. No repackaged copies of someone else's
  build, and nothing that bundles a game's own files.
- Don't copy descriptions from other stores word for word (HB App Store metadata is CC-BY-SA).
- One port per pull request.

## Checking your work

```sh
pip install jsonschema
python3 store/scripts/build_sphaira.py --check
# with drafts not yet in their repositories:
python3 store/scripts/build_sphaira.py --check --drafts store/drafts
```

## Pull request checklist

- [ ] `ports.json` entry added, list still sorted by `id`.
- [ ] `python3 store/scripts/build_sphaira.py --check` passes.
- [ ] Every file name, folder and version comes from the port's own docs (link them in the pull request).
- [ ] The description follows the shape above.
- [ ] No links to game data.
