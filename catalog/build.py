#!/usr/bin/env python3
"""VitaOS Store catalogue: every PS Vita homebrew source in one file.

    python3 catalog/build.py [out.json]        (GITHUB_TOKEN in the environment lifts GitHub's rate limit)

Sources, merged by title ID (then by name):
  * VitaHomebrewDB (drdecki.github.io/VitaHomebrewDB): the base, VitaDB's successor.
  * CBPS-DB (github.com/KuromeSan/cbps-db): adds the VPKs VitaHomebrewDB lacks.
  * GitHub itself (discover() below): projects neither database lists yet,
    found by search, kept only if a release carries a VPK whose param.sfo
    reads as a Vita app and the project clears a small quality floor.
Then every app whose download is a GitHub release is pointed at that
project's newest release with a VPK, so the Store offers the latest build.

Ports are in: the player brings their own game files, as each port's page
says. Listed but not downloadable ("blocked": the reason, shown on the page):
CBPS "DATA" packages (commercial game files), plugins (they need config.txt
edits, not an install button), the few VPKs that contain a commercial game
itself, piracy tools, adult content and test or joke uploads (BLOCK below). The output keeps
VitaHomebrewDB's JSON shape, with "source" and, for CBPS rows, "icon_url".
"""
import csv
import io
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
import zipfile

VHDB = "https://drdecki.github.io/VitaHomebrewDB/apps.json"
CBPS = "https://raw.githubusercontent.com/KuromeSan/cbps-db/master/cbpsdb.csv"
UA = {"User-Agent": "VitaOS-catalog (github.com/hotdog-face/VitaOS)"}

# What is listed but cannot be downloaded, and why (shown on the app's page).
BLOCK = [
    (re.compile(r"cuphead|henry ?stickmin|super mario|granny|chip and dale|alien vs predator|deltarune", re.I),
     "This VPK contains a commercial game, not just a port of its engine."),
    (re.compile(r"pkgj|\bnps\b|nopaystation|package installer|pkg enabler|decrypt|activate\.vpk|backup license", re.I),
     "A tool for installing pirated packages."),
    (re.compile(r"hentai|lolicop|porn|nsfw", re.I), "Adult content."),
    (re.compile(r"hello.?world|sample|\btests?\b|fucku|pewdiepie|stoner|cocaine|chinaissmall|socal credit|bitcoin by", re.I),
     "A test or joke upload."),
]


def blocked_reason(title):
    for rx, why in BLOCK:
        if rx.search(title):
            return why
    return ""


OWN_HOST = re.compile(r"^https://", re.I)


def get(url, token=None, accept=None):
    h = dict(UA)
    if token and "api.github.com" in url:
        h["Authorization"] = "Bearer " + token
    if accept:
        h["Accept"] = accept
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code in (403, 429) and attempt < 2:
                time.sleep(20)
                continue
            raise
        except Exception:
            if attempt < 2:
                time.sleep(3)
                continue
            raise


def norm(name):
    return re.sub(r"[^a-z0-9]", "", name.lower().replace("vita", ""))


def classify(title, tid):
    t = title.lower()
    if re.search(r"emulat|retroarch|\bemu\b|nes|snes|gba|\bds\b|n64|daedalus|mame|scumm|dosbox|psp|ppsspp|gameboy|genesis", t):
        return "5"
    if re.search(r"manager|installer|tool|ftp|shell|config|player|browser|youtube|downloader|updater|plugin|launcher|"
                 r"vitashell|backup|editor|viewer|reader|music|video|radio|calculator|clock|checker|volume|menu", t):
        return "4"
    return "1"


def readme_blurb(url):
    """The first real paragraph of a project's README, as plain text."""
    if not url or url == "None":
        return ""
    try:
        md = get(url).decode("utf-8", "replace")
    except Exception:
        return ""
    for para in re.split(r"\n\s*\n", md):
        p = para.strip()
        if not p or p.startswith(("#", "!", "<", "[!", "|", "```", "-", "*", ">")) or len(p) < 40:
            continue
        p = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", p)
        p = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", p)
        p = re.sub(r"[*_`]", "", p)
        p = re.sub(r"\s+", " ", p).strip()
        return p[:400]
    return ""


def cbps_rows(have_ids, have_names):
    out = []
    for r in csv.DictReader(io.StringIO(get(CBPS).decode("utf-8", "replace"))):
        if r.get("visible") != "True":
            continue
        tid, title, url = r["id"][:9], r["title"].strip(), r["download_url"]
        if tid in have_ids or norm(title) in have_names or not OWN_HOST.match(url):
            continue
        why = blocked_reason(title)
        if r.get("type") == "DATA":
            why = why or "Game data files: bring your own from the game you own."
        elif r.get("type") == "PLUGIN":
            why = why or "A plugin: install it by hand (it needs a line in ur0:tai/config.txt)."
        elif not url.lower().split("?")[0].endswith(".vpk"):
            continue
        added = float(r.get("time_added") or 0)
        icon_url = r["download_icon0"] if r["download_icon0"].startswith("http") and not why else ""
        out.append({
            "name": title, "titleid": tid, "author": r["credits"], "type": classify(title, tid),
            "description": readme_blurb(r.get("download_readme")), "long_description": "",
            "url": url, "version": "", "date": time.strftime("%Y-%m-%d", time.gmtime(added)) if added else "",
            "icon": ("cbps-%s.png" % tid) if icon_url else "", "icon_url": icon_url,
            "size": "0", "downloads": "0", "screenshots": "", "data": "", "requirements": "",
            "release_page": r.get("download_src") if r.get("download_src", "None") != "None" else "",
            "source": "CBPS-DB", "blocked": why,
        })
        if why:
            out[-1]["url"] = ""                         # listed, never downloadable
        have_ids.add(tid)
        have_names.add(norm(title))
    return out


GH = re.compile(r"^https://github\.com/([^/]+)/([^/]+)/releases/download/([^/]+)/", re.I)


def ver_key(tag):
    return [int(x) for x in re.findall(r"\d+", tag)][:4]


def bump_to_latest(apps, token):
    """Point GitHub-hosted apps at their newest release that carries a VPK."""
    cache, bumped = {}, 0
    for a in apps:
        m = GH.match(a.get("url", "") or "")
        if not m:
            continue
        repo = "%s/%s" % (m.group(1), m.group(2))
        if repo not in cache:
            try:
                cache[repo] = json.loads(get("https://api.github.com/repos/%s/releases/latest" % repo, token,
                                             "application/vnd.github+json"))
            except Exception:
                cache[repo] = None
        rel = cache[repo]
        if not rel or rel.get("draft") or rel.get("prerelease") or rel.get("tag_name") == m.group(3):
            continue
        vpks = [x for x in rel.get("assets", []) if x["name"].lower().endswith(".vpk")]
        if not vpks:
            continue
        old = a["url"].rsplit("/", 1)[1].lower()
        pick = next((x for x in vpks if x["name"].lower() == old), vpks[0] if len(vpks) == 1 else None)
        if not pick:
            continue
        a["url"] = pick["browser_download_url"]
        a["version"] = rel["tag_name"]
        a["size"] = str(pick.get("size") or a.get("size") or 0)
        a["date"] = (rel.get("published_at") or "")[:10] or a.get("date", "")
        a["release_page"] = rel.get("html_url", a.get("release_page", ""))
        bumped += 1
    return bumped, len(cache)


# ---------- discovery: what the two databases do not list yet ----------

SEARCHES = ["topic:psvita", "topic:ps-vita", "topic:vita", "topic:vitasdk", "topic:playstation-vita",
            "topic:psvita-homebrew", "topic:vita-homebrew", "vitasdk in:readme", "vpk vita in:description",
            '"ps vita" port in:description', "psvita in:description", '"ps vita" homebrew in:description']
MIN_STARS, MIN_DOWNLOADS = 2, 20          # the floor: either is enough
SEARCH_PAUSE = 2.2                        # GitHub allows 30 searches a minute
# Kept out of what discovery adds (nobody has vouched for these rows):
# firmware flashers, and torrent or debrid streaming.
SKIP = re.compile(r"firmware installer|devkit firmware|\bmiaki\b|torrent|\btpb|pirate ?bay|real-?debrid", re.I)


def kind(text):
    """VitaDB type for a discovered project: 2 port, 5 emulator, 4 utility, 1 game."""
    if re.search(r"\bemulat", text, re.I):
        return "5"
    if re.search(r"\bport(ed|s)?\b|loader|wrapper|decomp", text, re.I):
        return "2"
    if re.search(r"client|player|manager|installer|tool|reader|monitor|dictionary|vpn|camera|store\b|transfer|sync|"
                 r"engine|launcher|hub\b|plugin|macropad|diagnos", text, re.I):
        return "4"
    return "1"


class RemoteFile(io.RawIOBase):
    """A file on a web server read with Range requests, so a VPK's param.sfo
    can be read without downloading the package."""

    def __init__(self, url):
        self.url, self.pos = url, 0
        with urllib.request.urlopen(urllib.request.Request(url, headers=dict(UA, Range="bytes=0-0")), timeout=30) as r:
            self.size = int(r.headers["Content-Range"].rsplit("/", 1)[1])
        self.tail_at = max(0, self.size - 262144)      # the zip directory is at the end: one request for it
        self.tail = self._get(self.tail_at, self.size - 1)

    def _get(self, a, b):
        with urllib.request.urlopen(urllib.request.Request(self.url, headers=dict(UA, Range="bytes=%d-%d" % (a, b))),
                                    timeout=60) as r:
            return r.read()

    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def readinto(self, buf):
        d = self.read(len(buf))
        buf[:len(d)] = d
        return len(d)

    def read(self, n=-1):
        if n < 0 or self.pos + n > self.size:
            n = self.size - self.pos
        if n <= 0:
            return b""
        if self.pos >= self.tail_at:
            d = self.tail[self.pos - self.tail_at:self.pos - self.tail_at + n]
        else:
            d = self._get(self.pos, self.pos + n - 1)
        self.pos += len(d)
        return d


def sfo_fields(blob):
    """TITLE_ID, TITLE and so on out of a param.sfo."""
    if blob[:4] != b"\0PSF":
        return {}
    key_at, data_at, n = (int.from_bytes(blob[o:o + 4], "little") for o in (8, 12, 16))
    out = {}
    for i in range(n):
        e = blob[20 + 16 * i:36 + 16 * i]
        k = blob[key_at + int.from_bytes(e[0:2], "little"):].split(b"\0", 1)[0].decode("ascii", "replace")
        ln, off = int.from_bytes(e[4:8], "little"), int.from_bytes(e[12:16], "little")
        if e[2:4] == b"\x04\x04":
            out[k] = int.from_bytes(blob[data_at + off:data_at + off + 4], "little")
        else:
            out[k] = blob[data_at + off:data_at + off + ln].split(b"\0", 1)[0].decode("utf-8", "replace")
    return out


def vpk_sfo(url):
    try:
        with zipfile.ZipFile(io.BufferedReader(RemoteFile(url), 65536)) as z:
            info = z.getinfo("sce_sys/param.sfo")
            if info.file_size > 65536:
                return {}
            return sfo_fields(z.read(info))
    except Exception:
        return {}


def repo_icon(repo, branch, token):
    """The project's own icon0.png, if it keeps one in its source tree."""
    try:
        tree = json.loads(get("https://api.github.com/repos/%s/git/trees/%s?recursive=1" % (repo, urllib.parse.quote(branch)),
                              token, "application/vnd.github+json"))
        hits = [t["path"] for t in tree.get("tree", []) if t["path"].lower().endswith("sce_sys/icon0.png")]
        if hits:
            return "https://raw.githubusercontent.com/%s/%s/%s" % (repo, urllib.parse.quote(branch),
                                                                   urllib.parse.quote(min(hits, key=len)))
    except Exception:
        pass
    return ""


def discover(apps, have_ids, have_names, token, previous):
    """Rows for GitHub projects with a VPK release that the catalogue does not have."""
    known = {m.group(1).lower() + "/" + m.group(2).lower()
             for a in apps for m in [GH.match(a.get("url", "") or "")] if m}
    listed = json.dumps([[a.get("url"), a.get("release_page"), a.get("source")] for a in apps]).lower()
    since = time.strftime("%Y-%m-%d", time.gmtime(time.time() - 365 * 86400))
    found = {}
    for q in SEARCHES:
        for page in (1, 2, 3):
            url = "https://api.github.com/search/repositories?" + urllib.parse.urlencode(
                {"q": "%s pushed:>%s fork:false archived:false" % (q, since), "sort": "updated", "per_page": 100, "page": page})
            try:
                items = json.loads(get(url, token, "application/vnd.github+json")).get("items", [])
            except Exception as e:
                print("search failed (%s): %s" % (q, e))
                break
            for it in items:
                found[it["full_name"]] = it
            time.sleep(SEARCH_PAUSE)
            if len(items) < 100:
                break
    old = {a.get("repo", ""): a for a in previous if a.get("source") == "GitHub"}
    out, looked = [], 0
    for repo, it in sorted(found.items(), key=lambda kv: -kv[1]["stargazers_count"]):
        low = repo.lower()
        if low in known or ("github.com/" + low + "/") in listed or ("github.com/" + low + '"') in listed:
            continue
        text = "%s %s" % (repo.split("/")[1], it.get("description") or "")
        if blocked_reason(text) or SKIP.search(text):
            continue                                     # not listed at all: nobody vouched for these
        looked += 1
        try:
            rels = json.loads(get("https://api.github.com/repos/%s/releases?per_page=5" % repo, token,
                                  "application/vnd.github+json"))
        except Exception:
            continue
        rel = next((r for r in rels if not r.get("draft")
                    and any(x["name"].lower().endswith(".vpk") for x in r.get("assets", []))), None)
        if not rel:
            continue
        vpks = [x for x in rel["assets"] if x["name"].lower().endswith(".vpk")]
        pick = max(vpks, key=lambda x: x.get("download_count", 0))
        downloads = sum(x.get("download_count", 0) for x in vpks)
        if it["stargazers_count"] < MIN_STARS and downloads < MIN_DOWNLOADS:
            continue
        prev = old.get(repo)
        if prev and prev.get("url") == pick["browser_download_url"]:
            row = dict(prev)                             # same package as yesterday: its SFO was read then
        else:
            sfo = vpk_sfo(pick["browser_download_url"])
            tid, title = sfo.get("TITLE_ID", ""), re.sub(r"\s+", " ", sfo.get("TITLE", "")).strip()
            if not re.fullmatch(r"[A-Z0-9]{9}", tid) or not title:
                continue                                 # not a Vita package, whatever its name says
            icon_url = repo_icon(repo, it.get("default_branch") or "main", token)
            desc = (it.get("description") or "").strip()
            row = {
                "name": title, "titleid": tid, "author": repo.split("/")[0],
                "type": kind(text),
                "description": desc, "long_description": "", "url": pick["browser_download_url"],
                "icon": ("gh-%s.png" % tid) if icon_url else "", "icon_url": icon_url,
                "screenshots": "", "data": "", "requirements": "", "source": "GitHub", "repo": repo, "blocked": "",
            }
        if blocked_reason(row["name"]) or SKIP.search(row["name"]):
            continue
        row.update({"version": rel["tag_name"], "date": (rel.get("published_at") or "")[:10],
                    "size": str(pick.get("size") or 0), "downloads": str(downloads),
                    "release_page": rel.get("html_url", "")})
        if row["titleid"] in have_ids or norm(row["name"]) in have_names:
            continue
        have_ids.add(row["titleid"])
        have_names.add(norm(row["name"]))
        out.append(row)
    return out, len(found), looked


def previous_catalog():
    try:
        return json.loads(get("https://github.com/hotdog-face/VitaOS/releases/download/catalog/catalog.json"))
    except Exception:
        return []


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "catalog.json"
    token = os.environ.get("GITHUB_TOKEN")
    base = json.loads(get(VHDB))
    for a in base:
        a.setdefault("source", "VitaHomebrewDB")
    ids = {a.get("titleid", "") for a in base}
    names = {norm(a.get("name", "")) for a in base}
    extra = cbps_rows(ids, names)
    apps = base + extra
    bumped, repos = bump_to_latest(apps, token)
    previous = previous_catalog()
    fresh, found, looked = discover(apps, ids, names, token, previous)
    # The Store reads a fixed number of rows (MAX_APPS in src/store.c): keep
    # what cannot be downloaded at the end, so those are what an old build drops.
    apps = [a for a in base + fresh + extra if not a.get("blocked")] + [a for a in apps if a.get("blocked")]
    # A source that answers with half its list must not empty everyone's Store:
    # yesterday's catalogue stays up instead, and the job fails where it is seen.
    if previous and len(apps) < 0.9 * len(previous):
        sys.exit("refusing to publish: %d apps, yesterday had %d (VitaHomebrewDB %d, CBPS-DB %d, GitHub %d)"
                 % (len(apps), len(previous), len(base), len(extra), len(fresh)))
    if len(apps) > 2400:
        print("warning: %d apps, and the Store reads 2400 (MAX_APPS in src/store.c)" % len(apps))
    with open(out, "w", encoding="utf-8") as f:
        json.dump(apps, f, ensure_ascii=False, indent=1)
    print("VitaHomebrewDB %d + CBPS-DB %d (%d listed but blocked) = %d apps; %d moved to a newer GitHub release (%d repos checked)"
          % (len(base), len(extra), sum(1 for a in extra if a["blocked"]), len(apps) - len(fresh), bumped, repos))
    print("GitHub discovery: %d projects found by search, %d not in the catalogue, %d added; %d apps in all"
          % (found, looked, len(fresh), len(apps)))


if __name__ == "__main__":
    main()
