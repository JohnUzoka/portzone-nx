#!/usr/bin/env python3
"""Check ports.json and build sphaira.json from it.

For each port, the port's own portzone.json is read from the top of its
repository's default branch. Ports that have one with a release go into
sphaira.json, Sphaira's store format; the rest are left out, since Sphaira
would guess where they install.

  build_sphaira.py                 check, then write sphaira.json
  build_sphaira.py --check         check only (pull requests)
  build_sphaira.py --drafts DIR    read DIR/<id>/portzone.json before the repository's

Only raw.githubusercontent.com is read: no GitHub API, no rate limit.
"""

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

import jsonschema
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parent.parent
PORTS = ROOT / "ports.json"
SPHAIRA = ROOT / "sphaira.json"
PORTS_SCHEMA = ROOT / "schema" / "ports.schema.json"
PORTZONE_SCHEMA = ROOT / "schema" / "portzone.schema.json"


def schema_errors(schema_path, document, where):
    # Both schemas by their $id, so ports.schema.json can use portzone.schema.json's definitions.
    registry = Registry().with_resources(
        (s["$id"], Resource.from_contents(s))
        for s in (json.loads(p.read_text()) for p in (PORTS_SCHEMA, PORTZONE_SCHEMA)))
    validator = jsonschema.Draft202012Validator(json.loads(schema_path.read_text()), registry=registry)
    return [f"{where}: {'/'.join(map(str, e.absolute_path)) or '(top)'}: {e.message}"
            for e in validator.iter_errors(document)]


def fetch_portzone(port, drafts):
    """The port's portzone.json, or None when it has none."""
    if drafts:
        draft = drafts / port["id"] / "portzone.json"
        if draft.exists():
            return json.loads(draft.read_text()), str(draft)
    repo = port["url"].removeprefix("https://github.com/")
    url = f"https://raw.githubusercontent.com/{repo}/HEAD/portzone.json"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "portzone"}),
                                    timeout=30) as response:
            return json.load(response), url
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None, url
        raise


def sd_relative(path):
    return path.removeprefix("sdmc:/")


def file_text(file):
    text = file["path"] + (file["match"] if "match" in file else "")
    return f"{text} ({file['note']})" if "note" in file else text


def render_details(port, store):
    """Plain text for Sphaira's details pane."""
    description = store.get("description", port["description"])
    lines = [description, "", "Install"]
    lines += [f"{n}. {step}" for n, step in enumerate(store["steps"], 1)]

    data = store.get("game_data")
    if data:
        lines += ["", f"Your files, in {sd_relative(data['dir'])}/"]
        for item in data["inputs"]:
            kind = "required" if item.get("required", True) else "optional"
            options = item["options"]
            if len(options) == 1:
                lines.append(f"- {item['label']} ({kind}): {', '.join(map(file_text, options[0]['files']))}")
                continue
            lines.append(f"- {item['label']} ({kind}), one of:")
            for option in options:
                lines.append(f"  - {option['label']}: {', '.join(map(file_text, option['files']))}")
        prepare = data.get("prepare")
        if prepare:
            where = "On the Switch" if prepare["where"] == "switch" else "On a PC"
            tool = f", with {prepare['tool']['name']} ({prepare['tool']['url']})" if "tool" in prepare else ""
            lines += ["", f"{where}{tool}: {prepare['summary']}"]

    launch = store.get("launch", {})
    notes = []
    if launch.get("full_memory", True):
        notes.append("Start it with full memory: hold R while starting a game, or use a forwarder. Not from the Album.")
    notes += launch.get("notes", [])
    if notes:
        lines += ["", "Launch"] + [f"- {note}" for note in notes]
    return "\n".join(lines)


def images(port, store):
    """Icon and banner URLs, in the app's order: portzone.json, then ports.json, then the owner's avatar."""
    repo = port["url"].removeprefix("https://github.com/")
    raw = f"https://raw.githubusercontent.com/{repo}/HEAD/"
    icon = raw + store["icon"] if "icon" in store else port.get("icon", f"https://github.com/{repo.split('/')[0]}.png?size=256")
    banner = raw + store["banner"] if "banner" in store else port.get("banner")
    return icon, banner


def sphaira_install(store):
    """Sphaira's asset/target/stripPrefix for the port, or None when Sphaira cannot do what the steps say."""
    steps = store["install"].get("steps", [])
    if len(steps) != 1:
        return None
    step = steps[0]
    if "unzip" in step:
        fields = {"asset": step["asset"], "target": step["unzip"]}
        if "from" in step:
            fields["stripPrefix"] = step["from"]
        return fields
    if step["asset"].lower().endswith(".nro") and "name" not in step:
        return {"asset": step["asset"], "target": step["copy"]}
    return None  # Sphaira copies only .nro files, under their own name


def sphaira_entry(port, store, install):
    icon, banner = images(port, store)
    entry = {
        "name": port["id"],
        "title": port["title"],
        "author": port["author"],
        "description": store.get("description", port["description"]),
        "details": render_details(port, store),
        "url": port["url"],
        "release": "latest",
        **install,
        "marker": store["install"]["binary"],
        "icon": icon,
    }
    if banner:
        entry["banner_url"] = banner
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="check only; do not write sphaira.json")
    parser.add_argument("--drafts", type=Path, help="folder of <id>/portzone.json read before the repositories")
    args = parser.parse_args()

    index = json.loads(PORTS.read_text())
    errors = schema_errors(PORTS_SCHEMA, index, PORTS.name)
    if errors:
        print("\n".join(f"::error::{e}" for e in errors))
        return 1

    ids = [port["id"] for port in index["ports"]]
    errors += [f"{PORTS.name}: id {i!r} is listed twice" for i in sorted({i for i in ids if ids.count(i) > 1})]
    urls = [port["url"].lower() for port in index["ports"]]
    errors += [f"{PORTS.name}: {u} is listed twice" for u in sorted({u for u in urls if urls.count(u) > 1})]

    packages = []
    for port in index["ports"]:
        try:
            store, source = fetch_portzone(port, args.drafts)
        except (OSError, json.JSONDecodeError) as e:
            errors.append(f"{port['id']}: portzone.json could not be read: {e}")
            continue
        if store is None:
            print(f"{port['id']}: no portzone.json, manual install only")
            continue
        found = schema_errors(PORTZONE_SCHEMA, store, source)
        if found:
            errors += found
            continue
        platform = store.get("platform", port.get("platform", {}))
        if "switch" not in platform.get("runs_on", ["switch"]):
            print(f"{port['id']}: does not run on the Switch (1); not in sphaira.json")
            continue
        install = sphaira_install(store)
        if install is None:
            print(f"{port['id']}: install steps Sphaira cannot follow (none, several, or a renamed/non-.nro copy); not in sphaira.json")
            continue
        packages.append(sphaira_entry(port, store, install))
        print(f"{port['id']}: {source}")

    for error in errors:
        print(f"::error::{error}")
    if errors or args.check:
        return 1 if errors else 0

    text = json.dumps({"packages": packages}, indent=2, ensure_ascii=False) + "\n"
    if not SPHAIRA.exists() or SPHAIRA.read_text() != text:
        SPHAIRA.write_text(text)
        print(f"wrote {SPHAIRA.name}: {len(packages)} of {len(index['ports'])} ports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
