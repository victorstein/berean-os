#!/usr/bin/env python3
"""Derive bereanOS's slim publication index from JW Library's catalog.

The catalog is a 57.6 MB gzipped SQLite database covering ~320,000 publications
in every language. The device never sees it: this reduces one language to a
tab-separated index of roughly 245 KB (about 20 KB gzipped), which the firmware
inflates into PSRAM on entering Buscar and frees on exit.

Redistributes symbols, issue codes, years, titles and one EPUB-availability flag
only -- never publication content. Identifies itself honestly in the User-Agent
and runs on a low frequency; see the posture note in the design document.

EPUB AVAILABILITY. The catalog records one asset type -- application/x-jwpub,
JW Library's own format -- for every publication, so whether an entry has an
EPUB is not derivable from it, and it does not follow age either: the 22 April
1980 Awake! has one, the January 1980 Kingdom Ministry does not. The only way to
know is to ask GETPUBMEDIALINKS about that entry.

Asking about all ~3,800 entries on every build would be a weekly burst against
someone else's service. Instead each (symbol, issue) is asked at most once and
the answer is kept in a probe cache published beside the index. A positive
answer is never asked again; a negative one only after 90 days, in case an EPUB
appears later. Requests are at least a second apart, capped per run in count and
in time, and stop at the first 429 or 5xx. Anything that is not a clear answer
records nothing, so a transient failure can never hide a real publication.

Two layouts are written. v2 carries the flag and is what current firmware reads.
v1 is the original layout, byte for byte, published under its original name so a
device that has not updated can still refresh its index.
"""
import argparse
import gzip
import hashlib
import json
import sqlite3
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, timedelta

MANIFEST_URL = "https://app.jw-cdn.org/catalogs/publications/v4/manifest.json"
CATALOG_URL = "https://app.jw-cdn.org/catalogs/publications/v4/{id}/catalog.db.gz"
PUB_MEDIA_URL = "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS"
USER_AGENT = "bereanOS-catalog-indexer/1 (+https://github.com/victorstein/berean-os)"

FORMAT_VERSION = 2
LEGACY_FORMAT_VERSION = 1
MAGIC = "berean-catalog"

PROBES_MAGIC = "berean-probes"
PROBES_VERSION = 1

EPUB_AVAILABLE = "1"
EPUB_UNAVAILABLE = "0"
NEGATIVE_REPROBE_AFTER = timedelta(days=90)
PROBE_TIMEOUT_SECONDS = 15

EXIT_UNCHANGED = 3


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=600) as response:
        return response.read()


def current_manifest_id() -> str:
    return json.loads(fetch(MANIFEST_URL).decode("utf-8"))["current"]


def table_columns(db: sqlite3.Connection, table: str) -> list[str]:
    return [row[1] for row in db.execute(f"PRAGMA table_info({table})")]


def find_publication_table(db: sqlite3.Connection) -> tuple[str, dict[str, str]]:
    """Locate the publication table and the columns we need.

    Introspected rather than hardcoded: this schema is JW's, not ours, and a
    column rename upstream should fail loudly here rather than silently produce
    an index full of empty titles.
    """
    wanted = {
        # KeySymbol, NOT Symbol. GETPUBMEDIALINKS takes pub=w; the Symbol
        # column holds a year-suffixed variant ("w26") that returns 404.
        "symbol": ("KeySymbol", "PublicationSymbol"),
        "issue": ("IssueTagNumber", "Issue", "IssueTag"),
        "year": ("Year", "PublicationYear"),
        "title": ("Title", "DisplayTitle", "ShortTitle"),
        "language": ("MepsLanguageId", "LanguageId"),
    }
    tables = [r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")]
    for table in tables:
        if "publication" not in table.lower():
            continue
        columns = table_columns(db, table)
        resolved = {}
        for key, candidates in wanted.items():
            for candidate in candidates:
                if candidate in columns:
                    resolved[key] = candidate
                    break
        if len(resolved) == len(wanted):
            return table, resolved
    raise SystemExit(
        "No publication table with the expected columns. Tables seen: " + ", ".join(tables)
    )


def normalise_issue(issue: str) -> str:
    """Canonicalise IssueTagNumber to the form GETPUBMEDIALINKS echoes back.

    The column is YYYYMMDD. A monthly issue has a day of 00 and the API reports
    it as YYYYMM (issue=20260700 returns "issue":"202607"); a semi-monthly or
    weekly issue has a real day -- 01, 08, 15 and 22 all occur -- and truncating
    those would collide two issues onto one code.

    This matters beyond the URL: the study store keys a publication on symbol,
    issue and language, so a Buscar download and a Meetings download of the same
    Watchtower must produce the SAME issue string or the tags on it split in two.
    """
    if issue in ("", "0", "00000000"):
        return ""
    if len(issue) == 8 and issue.endswith("00"):
        return issue[:6]
    return issue


def clean(value) -> str:
    """Strip the separators out of a field rather than quoting them.

    A title may contain almost anything; a tab or newline inside one would shift
    every later field of that record. Losing a tab from a title is invisible,
    silently misparsing the row is not.
    """
    if value is None:
        return ""
    return str(value).replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()


def build_rows(db_path: str, language_id: int) -> list[tuple[str, str, str, str, str]]:
    """(symbol, issue, year, kind, title) per publication, in symbol and issue order."""
    db = sqlite3.connect(db_path)
    table, cols = find_publication_table(db)

    query = (
        f"SELECT DISTINCT {cols['symbol']}, {cols['issue']}, {cols['year']}, {cols['title']} "
        f"FROM {table} WHERE {cols['language']} = ? "
        f"ORDER BY {cols['symbol']}, {cols['issue']}"
    )

    rows = []
    seen = set()
    for symbol, issue, year, title in db.execute(query, (language_id,)):
        symbol = clean(symbol)
        if not symbol:
            continue
        issue = normalise_issue(clean(issue))
        key = (symbol, issue)
        if key in seen:
            continue
        seen.add(key)
        kind = "periodical" if issue else "book"
        rows.append((symbol, issue, clean(year), kind, clean(title)))

    db.close()
    return rows


def render_index(rows, version: int, language_code: str, manifest_id: str, built_on: str, cache=None) -> str:
    """v1 is symbol, issue, year, kind, title. v2 puts the EPUB flag before the
    title -- "1", "0", or empty when not yet probed -- so the title stays last
    and a stray tab can only ever land inside it."""
    lines = ["\t".join([MAGIC, str(version), language_code, manifest_id, built_on])]
    for symbol, issue, year, kind, title in rows:
        if version == LEGACY_FORMAT_VERSION:
            lines.append("\t".join([symbol, issue, year, kind, title]))
        else:
            flag = (cache or {}).get((symbol, issue), ("", None))[0]
            lines.append("\t".join([symbol, issue, year, kind, flag, title]))
    return "\n".join(lines) + "\n"


def read_probes(text: str, language_code: str) -> dict:
    """{(symbol, issue): (flag, probed_on)}. A cache that is not exactly what this
    script writes is refused rather than half-read: adopting a corrupt cache
    would re-probe, or hide, entries wholesale."""
    lines = text.split("\n")
    if lines[0].split("\t") != [PROBES_MAGIC, str(PROBES_VERSION), language_code]:
        raise SystemExit(
            f"probe cache header {lines[0]!r} is not {PROBES_MAGIC} v{PROBES_VERSION} for {language_code}"
        )
    cache = {}
    for number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 4 or fields[2] not in (EPUB_AVAILABLE, EPUB_UNAVAILABLE):
            raise SystemExit(f"probe cache line {number} is malformed: {line!r}")
        try:
            probed_on = date.fromisoformat(fields[3])
        except ValueError:
            raise SystemExit(f"probe cache line {number} has an unreadable date: {line!r}")
        cache[(fields[0], fields[1])] = (fields[2], probed_on)
    return cache


def render_probes(cache: dict, rows, language_code: str) -> str:
    """Only rows still in the catalog are kept, so answers for withdrawn entries
    do not accumulate."""
    lines = ["\t".join([PROBES_MAGIC, str(PROBES_VERSION), language_code])]
    for symbol, issue, *_ in rows:
        entry = cache.get((symbol, issue))
        if entry:
            lines.append("\t".join([symbol, issue, entry[0], entry[1].isoformat()]))
    return "\n".join(lines) + "\n"


def seed_from_v2(text: str, language_code: str, today: date) -> dict:
    """Rebuilds the probe cache from a published v2 index whose cache is missing.

    The flags are exactly what devices already see. Dating them today defers the
    next negative re-probe by 90 days rather than repeating the whole backlog.
    """
    lines = text.split("\n")
    header = lines[0].split("\t")
    if len(header) < 3 or header[:3] != [MAGIC, str(FORMAT_VERSION), language_code]:
        raise SystemExit(f"{lines[0]!r} is not a v{FORMAT_VERSION} index for {language_code}")
    cache = {}
    for line in lines[1:]:
        fields = line.split("\t", 5)
        if len(fields) == 6 and fields[4] in (EPUB_AVAILABLE, EPUB_UNAVAILABLE):
            cache[(fields[0], fields[1])] = (fields[4], today)
    return cache


def plan_probes(rows, cache: dict, today: date) -> list:
    """Never-probed rows first, in catalog order, then negatives due a re-probe,
    oldest first. A positive answer is never asked again."""
    unprobed = [(symbol, issue) for symbol, issue, *_ in rows if (symbol, issue) not in cache]
    due = [
        (cache[(symbol, issue)][1], (symbol, issue))
        for symbol, issue, *_ in rows
        if (symbol, issue) in cache
        and cache[(symbol, issue)][0] == EPUB_UNAVAILABLE
        and today - cache[(symbol, issue)][1] > NEGATIVE_REPROBE_AFTER
    ]
    due.sort(key=lambda item: item[0])
    return unprobed + [key for _, key in due]


def probe_url(symbol: str, issue: str, language_code: str) -> str:
    """The request the device makes (pubMediaUrlForSymbol in WolWeekScan.cpp). A
    book omits issue: the API answers an empty one as not found."""
    params = [("output", "json"), ("pub", symbol), ("langwritten", language_code), ("fileformat", "EPUB")]
    if issue:
        params.append(("issue", issue))
    return PUB_MEDIA_URL + "?" + urllib.parse.urlencode(params)


def fetch_status(url: str) -> tuple:
    """(status, body). The status is None when no HTTP answer arrived at all."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=PROBE_TIMEOUT_SECONDS) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, b""
    except (urllib.error.URLError, OSError):
        return None, b""


def classify(status, body: bytes, language_code: str):
    """With fileformat=EPUB, a 200 that lists the EPUB means the device can open
    the entry and a 404 means it cannot. Anything else is not an answer and is
    not recorded: stored as "no EPUB", a transient failure would hide a real
    publication for 90 days."""
    if status == 404:
        return EPUB_UNAVAILABLE
    if status != 200:
        return None
    try:
        data = json.loads(body)
    except ValueError:
        return None
    files = data.get("files") if isinstance(data, dict) else None
    language = files.get(language_code) if isinstance(files, dict) else None
    epub = language.get("EPUB") if isinstance(language, dict) else None
    return EPUB_AVAILABLE if isinstance(epub, list) and epub else None


def is_throttled(status) -> bool:
    return status is not None and (status == 429 or status >= 500)


def run_probes(keys, language_code, get, clock, sleep, interval, max_probes, budget_seconds):
    """({key: flag}, throttled). Spacing is measured start to start."""
    results = {}
    started = clock()
    last_start = None
    for symbol, issue in keys[:max_probes]:
        if last_start is not None:
            wait = interval - (clock() - last_start)
            if wait > 0:
                sleep(wait)
        if clock() - started >= budget_seconds:
            break
        last_start = clock()
        status, body = get(probe_url(symbol, issue, language_code))
        if is_throttled(status):
            return results, True
        flag = classify(status, body, language_code)
        if flag is not None:
            results[(symbol, issue)] = flag
    return results, False


def read_text(path: str) -> str:
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def write_text(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def records_changed(previous_path, index: str) -> bool:
    """Compares the RECORDS only: the header carries a build date that changes
    daily, which would otherwise republish an identical index every run."""
    if not previous_path:
        return True
    try:
        previous = read_text(previous_path)
    except FileNotFoundError:
        return True
    return previous.split("\n", 1)[1:] != index.split("\n", 1)[1:]


def report(path: str, text: str) -> None:
    rows = text.count("\n") - 1
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    sys.stderr.write(f"wrote {path}: {rows:,} rows, {len(text.encode('utf-8')):,} B, sha256 {digest[:16]}\n")


def parse_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--language-id", type=int, default=1, help="MepsLanguageId (1 = Spanish)")
    parser.add_argument("--language-code", default="S", help="JW language code used by GETPUBMEDIALINKS")
    parser.add_argument("--db", help="Use a local catalog.db instead of downloading")
    parser.add_argument("--out", required=True, help="v2 index, with the EPUB flag")
    parser.add_argument("--previous", help="Published v2 index, to detect that nothing changed")
    parser.add_argument("--out-v1", help="v1 index, for firmware that predates the EPUB flag")
    parser.add_argument("--previous-v1", help="Published v1 index, to detect that nothing changed")
    parser.add_argument("--probes-in", help="Published probe cache")
    parser.add_argument("--probes-out", help="Where to write the updated probe cache")
    parser.add_argument("--seed-from-v2", help="Rebuild the probe cache from this v2 index (when --probes-in is absent)")
    parser.add_argument("--no-probe", action="store_true", help="Make no pub-media requests this run")
    parser.add_argument("--max-probes", type=int, default=1000)
    parser.add_argument("--probe-interval", type=float, default=1.0, help="Minimum seconds between probe starts")
    parser.add_argument("--probe-budget-seconds", type=float, default=1500.0)
    parser.add_argument("--throttle-marker", help="File to append 'throttled=1' to on a 429 or 5xx ($GITHUB_OUTPUT)")
    return parser.parse_args(argv)


def build(args, rows, manifest_id: str, today: date, get=fetch_status, clock=time.monotonic, sleep=time.sleep) -> int:
    code = args.language_code
    if args.probes_in:
        cache = read_probes(read_text(args.probes_in), code)
    elif args.seed_from_v2:
        cache = seed_from_v2(read_text(args.seed_from_v2), code, today)
    else:
        cache = {}

    if not args.no_probe:
        keys = plan_probes(rows, cache, today)
        results, throttled = run_probes(
            keys, code, get, clock, sleep, args.probe_interval, args.max_probes, args.probe_budget_seconds
        )
        for key, flag in results.items():
            cache[key] = (flag, today)
        sys.stderr.write(
            f"probes: {len(results):,} answered, {len(keys):,} due{'; throttled, stopped' if throttled else ''}\n"
        )
        if throttled and args.throttle_marker:
            with open(args.throttle_marker, "a", encoding="utf-8") as handle:
                handle.write("throttled=1\n")

    built_on = today.isoformat()
    index = render_index(rows, FORMAT_VERSION, code, manifest_id, built_on, cache)
    legacy = render_index(rows, LEGACY_FORMAT_VERSION, code, manifest_id, built_on)

    written = False
    if records_changed(args.previous, index):
        write_text(args.out, index)
        report(args.out, index)
        written = True
    # v1 moves only when its own records do, so a flag-only change never shows
    # older firmware a new date for the same list.
    if args.out_v1 and records_changed(args.previous_v1, legacy):
        write_text(args.out_v1, legacy)
        report(args.out_v1, legacy)
        written = True
    # Written even when nothing else was: a re-probed negative changes only its
    # date, and losing that would re-probe it every week.
    if args.probes_out:
        write_text(args.probes_out, render_probes(cache, rows, code))

    if not written:
        sys.stderr.write("unchanged; not republishing\n")
        return EXIT_UNCHANGED
    return 0


def main() -> int:
    args = parse_args(sys.argv[1:])

    manifest_id = current_manifest_id()
    sys.stderr.write(f"manifest: {manifest_id}\n")

    if args.db:
        db_path = args.db
    else:
        # The manifest id is NOT reliably content-addressed: the .gz behind an
        # unchanged id has been observed with a newer last-modified at identical
        # length. So the id is never the freshness test -- the index's own
        # records are, in build().
        payload = fetch(CATALOG_URL.format(id=manifest_id))
        db_path = "catalog.db"
        with open(db_path, "wb") as handle:
            handle.write(gzip.decompress(payload))
        sys.stderr.write(f"catalog: {len(payload):,} B gzipped\n")

    return build(args, build_rows(db_path, args.language_id), manifest_id, date.today())


if __name__ == "__main__":
    sys.exit(main())
