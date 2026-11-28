#!/usr/bin/env python3
"""Check ports.json and build the store's generated files from it.

For each port, the port's own portzone.json is read from the top of its
repository's default branch, else the store's draft (drafts/<id>/portzone.json).

  manifests.json  every port's portzone.json, so PortZone reads one file at start
                  instead of one per port, and "art": fallback icons from
                  SteamGridDB for ports with no icon of their own
  sphaira.json    Sphaira's store format: ports whose own repository has a
                  portzone.json with a release Sphaira can install (no drafts)
  art.json        which SteamGridDB game each port is (reviewable; see CONTRIBUTING.md)

  build_store.py                   check, then write all three
  build_store.py --check           check only (pull requests); no SteamGridDB lookups
  build_store.py --drafts DIR      read DIR/<id>/portzone.json before the repository's
                                   (to check a portzone.json before committing it)
  build_store.py --review-out MD   also write the ports whose game is unsure to MD
                                   (empty when there are none; not written without the key)

portzone.json files come from raw.githubusercontent.com only: no GitHub API, no rate
limit. Icons are looked up with the SteamGridDB API key in SGDB_API_KEY; without it
the lookups are skipped and art.json is used as it is.
"""

import argparse
import json
import os
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from difflib import SequenceMatcher
from pathlib import Path

import jsonschema
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parent.parent
PORTS = ROOT / "ports.json"
SPHAIRA = ROOT / "sphaira.json"
MANIFESTS = ROOT / "manifests.json"
DRAFTS = ROOT / "drafts"
ART = ROOT / "art.json"
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
    """(portzone.json or None, where it came from, whether it is the port's own)."""
    if drafts:
        draft = drafts / port["id"] / "portzone.json"
        if draft.exists():
            return json.loads(draft.read_text()), str(draft), True
    repo = port["url"].removeprefix("https://github.com/")
    url = f"https://raw.githubusercontent.com/{repo}/HEAD/{port.get('manifest', 'portzone.json')}"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "portzone"}),
                                    timeout=30) as response:
            return json.load(response), url, True
    except urllib.error.HTTPError as e:
        if e.code != 404:
            raise
    draft = DRAFTS / port["id"] / "portzone.json"
    if draft.exists():
        return json.loads(draft.read_text()), f"drafts/{port['id']}/portzone.json", False
    return None, url, False


def fetch(port, drafts):
    try:
        return fetch_portzone(port, drafts)
    except (OSError, json.JSONDecodeError) as e:
        return e, None, False


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
        folder = sd_relative(data["dir"])
        lines += ["", f"Your files, in {folder}/" if folder else "Your files, on the SD card"]
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


def write_if_changed(path, document):
    text = json.dumps(document, indent=2, ensure_ascii=False) + "\n"
    if not path.exists() or path.read_text() != text:
        path.write_text(text)
        return True
    return False


# SteamGridDB fallback icons ----------------------------------------------------------------------------------------
# art.json says which SteamGridDB game each port is; manifests.json's "art" holds the icon of every confident or
# reviewed match, for ports with no icon of their own. The app always prefers a port's own icon.

SGDB_API = "https://www.steamgriddb.com/api/v2"
SGDB_AGENT = "PortZone store"
SGDB_GAME_PAGE = "https://www.steamgriddb.com/game/"
ART_STATUSES = ("auto", "reviewed", "review", "none")
ART_USED = ("auto", "reviewed")
ART_FIELDS = ("status", "game", "name", "icon", "candidates")
ICON_MAX_BYTES = 1 << 20
ICON_MIMES = ("image/png", "image/jpeg")
CANDIDATES = 5
REVIEW_MAX_CHARS = 60000  # GitHub issue bodies hold 65536


class SteamGridDB:
    """The store's few SteamGridDB calls. The key goes only into the Authorization header of API requests: never
    into URLs, messages or files, and never to the image CDN."""

    def __init__(self, key):
        self._key = key

    @staticmethod
    def _fetch(url, method="GET", headers=None):
        """(body, response headers), retrying with backoff on 429, 5xx and network errors."""
        request = urllib.request.Request(url, method=method, headers={"User-Agent": SGDB_AGENT, **(headers or {})})
        for attempt in range(6):
            try:
                with urllib.request.urlopen(request, timeout=30) as response:
                    return response.read(), response.headers
            except urllib.error.HTTPError as e:
                if (e.code != 429 and e.code < 500) or attempt == 5:
                    raise
                retry_after = e.headers.get("Retry-After", "")
                delay = int(retry_after) if retry_after.isdigit() else 2 ** attempt
            except OSError:
                if attempt == 5:
                    raise
                delay = 2 ** attempt
            time.sleep(min(delay, 60))

    def _api(self, path):
        """The response's data, or None when SteamGridDB has nothing there (404)."""
        try:
            body, _ = self._fetch(SGDB_API + path, headers={"Authorization": f"Bearer {self._key}"})
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            raise
        return json.loads(body)["data"]

    def search(self, term):
        return self._api("/search/autocomplete/" + urllib.parse.quote(term.replace("/", " "), safe="")) or []

    def game_name(self, game):
        data = self._api(f"/games/id/{game}")
        return data["name"] if data else None

    def icons(self, game, mime):
        """The game's static icons; the API filters PNG itself but has no JPEG filter, so JPEG means all of them."""
        query = {"types": "static", "nsfw": "false", "humor": "false", "epilepsy": "false"}
        if mime == "image/png":
            query["mimes"] = mime
        return self._api(f"/icons/game/{game}?{urllib.parse.urlencode(query)}") or []

    def size(self, url):
        """Content-Length of an image, or None when the CDN doesn't say or doesn't have it."""
        try:
            _, headers = self._fetch(url, method="HEAD")
        except urllib.error.HTTPError:
            return None
        length = headers.get("Content-Length", "")
        return int(length) if length.isdigit() else None


def normalize_title(name):
    """Lowercase; & as "and"; no diacritics, punctuation or leading "the"; single spaces."""
    text = "".join(c for c in unicodedata.normalize("NFKD", name) if not unicodedata.combining(c))
    text = text.lower().replace("&", " and ")
    text = re.sub(r"['’]", "", text)
    text = re.sub(r"[^\w\s]|_", " ", text)
    text = " ".join(text.split())
    return text.removeprefix("the ")


ROMAN = {"ii": "2", "iii": "3", "iv": "4", "vi": "6", "vii": "7", "viii": "8", "ix": "9"}


def sequel_numbers(normalized):
    """The numbers in a normalized title, Roman numerals as digits: "Red Alert 2" and "Red Alert 3" are not close."""
    return sorted(ROMAN.get(word, word) for word in normalized.split() if word.isdigit() or word in ROMAN)


def ranked(term, results):
    """[(similarity, result)], most similar first."""
    wanted = normalize_title(term)
    scored = [(SequenceMatcher(None, wanted, normalize_title(r["name"])).ratio(), r) for r in results]
    return sorted(scored, key=lambda s: -s[0])


def confident_match(term, results):
    """The one result that is clearly the term's game, else None: the only one with the same normalized name, or
    one at least 0.93 similar, 0.08 ahead of the next, and with the same numbers."""
    wanted = normalize_title(term)
    exact = [r for r in results if normalize_title(r["name"]) == wanted]
    if exact:
        return exact[0] if len(exact) == 1 else None
    scored = ranked(term, results)
    if not scored:
        return None
    (best, result), runner_up = scored[0], scored[1][0] if len(scored) > 1 else 0.0
    if best >= 0.93 and best - runner_up >= 0.08 \
            and sequel_numbers(normalize_title(result["name"])) == sequel_numbers(wanted):
        return result
    return None


def search_terms(port):
    """(title terms, the id as a last resort): the title, then the title without a trailing parenthetical."""
    title = port["title"]
    terms = [title]
    bare = re.sub(r"\s*\([^()]*\)\s*$", "", title)
    if bare and bare != title:
        terms.append(bare)
    return terms, " ".join(re.split(r"[-_.\s]+", port["id"])).strip()


def match_game(sgdb, port):
    """A fresh art.json entry from searching SteamGridDB (no icon yet)."""
    title_terms, id_term = search_terms(port)
    candidates = {}  # game id -> (similarity, result)
    for terms in (title_terms, [id_term]):
        for term in terms:
            results = sgdb.search(term)
            hit = confident_match(term, results)
            if hit:
                return {"status": "auto", "game": hit["id"], "name": hit["name"], "icon": None, "candidates": []}
            for score, result in ranked(term, results):
                if score > candidates.get(result["id"], (-1.0, None))[0]:
                    candidates[result["id"]] = (score, result)
        if candidates:
            break  # the id only when the title finds nothing
    best = sorted(candidates.values(), key=lambda s: -s[0])[:CANDIDATES]
    return {"status": "review", "game": None, "name": None, "icon": None,
            "candidates": [{"game": r["id"], "name": r["name"]} for _, r in best]}


def pick_icon(sgdb, game):
    """The game's best static icon: HTTPS, square-ish, at most 1 MiB; PNG before JPEG; square, then 256-512 px,
    then highest score. None when there is none."""
    for mime in ICON_MIMES:
        icons = [i for i in sgdb.icons(game, mime)
                 if i.get("mime") == mime and str(i.get("url", "")).startswith("https://")
                 and i.get("width") and i.get("height") and 0.8 <= i["width"] / i["height"] <= 1.25]
        icons.sort(key=lambda i: (i["width"] != i["height"], not 256 <= max(i["width"], i["height"]) <= 512,
                                  -(i.get("score") or 0)))
        for icon in icons[:10]:
            size = sgdb.size(icon["url"])
            if size is not None and size <= ICON_MAX_BYTES:
                return icon["url"]
    return None


def art_entry(sgdb, port, entry):
    """The port's art.json entry after this run's lookups."""
    status = entry["status"] if entry else None
    if status == "none" or (status in ART_USED and entry["icon"]):
        return entry
    if status == "reviewed":  # a person chose the game: only fill in its icon
        return {**entry, "name": sgdb.game_name(entry["game"]) or entry["name"], "icon": pick_icon(sgdb, entry["game"])}
    found = match_game(sgdb, port)
    if found["status"] == "auto":
        found["icon"] = pick_icon(sgdb, found["game"])
    return found


def art_problems(art, ids):
    """Mistakes in a hand-edited art.json."""
    if not isinstance(art, dict):
        return [f"{ART.name}: must be an object of port ids"]
    problems = []
    for port_id, entry in art.items():
        where = f"{ART.name}: {port_id}"
        if not isinstance(entry, dict):
            problems.append(f"{where}: must be an object")
            continue
        if port_id not in ids:
            print(f"{where}: not in {PORTS.name}; the next build drops it")
        status, game, icon = entry.get("status"), entry.get("game"), entry.get("icon")
        if status not in ART_STATUSES:
            problems.append(f"{where}: status must be one of {', '.join(ART_STATUSES)}")
        if game is not None and (not isinstance(game, int) or isinstance(game, bool)):
            problems.append(f"{where}: game must be a SteamGridDB game id (a number) or null")
        if status == "reviewed" and game is None:
            problems.append(f"{where}: a reviewed entry needs its game")
        if icon is not None and not (isinstance(icon, str) and icon.startswith("https://")):
            problems.append(f"{where}: icon must be an HTTPS URL or null")
    return problems


def tidy_entry(entry):
    """An art.json entry with every field, in order."""
    return {field: entry.get(field, [] if field == "candidates" else None) for field in ART_FIELDS}


def update_art(ports, own_icon, art, key):
    """(art.json, whether the lookups ran) after looking up the ports that need it; without a key, art.json as it is.
    Ports that have their own icon, or a "none"/"reviewed" entry with its icon, are not looked up."""
    ids = {port["id"] for port in ports}
    art = {i: tidy_entry(e) for i, e in art.items() if i in ids}
    if not key:
        print("SteamGridDB key not set; icons skipped")
        return art, False
    sgdb = SteamGridDB(key)
    todo = [port for port in ports if port["id"] not in own_icon]

    def lookup(port):
        try:
            return art_entry(sgdb, port, art.get(port["id"])), None
        except (OSError, ValueError, KeyError, TypeError) as e:  # URLs and messages only: no request headers
            return None, f"{port['id']}: SteamGridDB lookup failed: {e}"

    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lookup, todo))
    failures = [error for _, error in results if error]
    for port, (entry, _) in zip(todo, results):
        if entry is None or entry == art.get(port["id"]):
            continue
        art[port["id"]] = entry
        print(f"{port['id']}: SteamGridDB {entry['status']}" + (f": {entry['name']}" if entry["name"] else "")
              + ("" if entry["status"] not in ART_USED or entry["icon"] else " (no suitable icon)"))
    for error in failures[:5]:
        print(f"::warning::{error}")
    if len(failures) > 5:
        print(f"::warning::{len(failures) - 5} more SteamGridDB lookups failed; their entries are unchanged")
    return art, True


def art_icons(ports, own_icon, art):
    """manifests.json's "art": port id -> icon URL, for ports without their own icon whose match is used."""
    return {port["id"]: art[port["id"]]["icon"] for port in sorted(ports, key=lambda p: p["id"])
            if port["id"] not in own_icon and port["id"] in art
            and art[port["id"]]["status"] in ART_USED and art[port["id"]]["icon"]}


def art_summary(ports, own_icon, art):
    counts = {status: 0 for status in ART_STATUSES}
    for port in ports:
        if port["id"] not in own_icon and port["id"] in art:
            counts[art[port["id"]]["status"]] += 1
    unmatched = len(ports) - len(own_icon) - sum(counts.values())
    return (", ".join(f"{n} {status}" for status, n in counts.items())
            + f", {len(own_icon)} skipped (own icon)" + (f", {unmatched} not looked up" if unmatched else ""))


def review_markdown(ports, own_icon, art):
    """The ports whose game is unsure, with what to do about them, short enough for an issue; empty when there are
    none."""
    unsure = [port for port in sorted(ports, key=lambda p: p["title"].lower())
              if port["id"] not in own_icon and art.get(port["id"], {}).get("status") == "review"]
    if not unsure:
        return ""
    lines = [
        f"The store's daily build couldn't tell which SteamGridDB game these {len(unsure)} ports are, so PortZone "
        "shows no fallback icon for them yet.",
        "",
        "To settle one, edit its entry in `store/art.json` by pull request:",
        "",
        "- **The right game is known:** set `\"game\"` to its SteamGridDB id (the number in "
        f"`{SGDB_GAME_PAGE}<id>`), `\"status\"` to `\"reviewed\"`, and `\"icon\"` to `null`. The next build picks "
        "its icon.",
        "- **No icon for this port:** set `\"status\"` to `\"none\"`. It is not looked up again.",
        "",
        "A port whose own `portzone.json` or `ports.json` entry gains an `icon` drops off this list by itself.",
        "",
    ]
    for n, port in enumerate(unsure):
        candidates = art[port["id"]]["candidates"]
        found = (", ".join(f"[{c['name']}]({SGDB_GAME_PAGE}{c['game']})" for c in candidates)
                 if candidates else "nothing found")
        line = f"- **{port['title']}** (`{port['id']}`, [repository]({port['url']})): {found}"
        if sum(len(text) + 1 for text in lines) + len(line) > REVIEW_MAX_CHARS - 200:
            lines.append(f"- ...and {len(unsure) - n} more: the other `store/art.json` entries with status `review`.")
            break
        lines.append(line)
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="check only; write nothing")
    parser.add_argument("--drafts", type=Path, help="folder of <id>/portzone.json read before the repositories")
    parser.add_argument("--review-out", type=Path, metavar="MD",
                        help="write the ports whose SteamGridDB game is unsure here, as Markdown (empty when none)")
    args = parser.parse_args()

    index = json.loads(PORTS.read_text())
    errors = schema_errors(PORTS_SCHEMA, index, PORTS.name)
    if errors:
        print("\n".join(f"::error::{e}" for e in errors))
        return 1

    ids = [port["id"] for port in index["ports"]]
    errors += [f"{PORTS.name}: id {i!r} is listed twice" for i in sorted({i for i in ids if ids.count(i) > 1})]
    # A repository may be listed more than once (several games from one build) when each listing names its own file.
    sources = [(port["url"].lower(), port.get("manifest", "portzone.json")) for port in index["ports"]]
    errors += [f"{PORTS.name}: {u} is listed twice with {m}" for u, m in sorted({s for s in sources if sources.count(s) > 1})]

    # Hundreds of small reads: many at once.
    with ThreadPoolExecutor(max_workers=16) as pool:
        fetched = list(pool.map(lambda port: fetch(port, args.drafts), index["ports"]))

    packages, manifests = [], {}
    for port, (store, source, own) in zip(index["ports"], fetched):
        if isinstance(store, Exception):
            errors.append(f"{port['id']}: portzone.json could not be read: {store}")
            continue
        if store is None:
            continue
        found = schema_errors(PORTZONE_SCHEMA, store, source)
        if found:
            errors += found
            continue
        manifests[port["id"]] = {"source": "repository" if own else "store draft", "portzone": store}
        print(f"{port['id']}: {source}")
        if not own:
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
    print(f"{len(manifests)} of {len(index['ports'])} ports have a portzone.json; the rest are manual installs")

    try:
        art = json.loads(ART.read_text()) if ART.exists() else {}
        errors += art_problems(art, set(ids))
    except json.JSONDecodeError as e:
        errors.append(f"{ART.name}: {e}")

    for error in errors:
        print(f"::error::{error}")
    if errors or args.check:
        return 1 if errors else 0

    # A port's own icon: its portzone.json's (repository or draft), else its ports.json entry's.
    own_icon = {port["id"] for port in index["ports"]
                if "icon" in port or "icon" in manifests.get(port["id"], {}).get("portzone", {})}
    art, looked_up = update_art(index["ports"], own_icon, art, os.environ.get("SGDB_API_KEY"))
    print(f"SteamGridDB icons: {art_summary(index['ports'], own_icon, art)}")
    icons = art_icons(index["ports"], own_icon, art)

    if write_if_changed(MANIFESTS, {"format": 1, "manifests": dict(sorted(manifests.items())), "art": icons}):
        print(f"wrote {MANIFESTS.name}: {len(manifests)} ports, {len(icons)} fallback icons")
    if write_if_changed(SPHAIRA, {"packages": packages}):
        print(f"wrote {SPHAIRA.name}: {len(packages)} of {len(index['ports'])} ports")
    if write_if_changed(ART, dict(sorted(art.items()))):
        print(f"wrote {ART.name}: {len(art)} ports")
    if args.review_out and looked_up:
        args.review_out.write_text(review_markdown(index["ports"], own_icon, art))
        print(f"wrote {args.review_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
