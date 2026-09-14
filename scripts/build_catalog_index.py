#!/usr/bin/env python3
"""Derive bereanOS's slim publication index from JW Library's catalog.

The catalog is a 57.6 MB gzipped SQLite database covering ~320,000 publications
in every language. The device never sees it: this reduces one language to a
tab-separated index of roughly 200 KB (18 KB gzipped), which the firmware
inflates into PSRAM on entering Buscar and frees on exit.

Redistributes symbols, issue codes, years and titles only -- never publication
content. Identifies itself honestly in the User-Agent and runs on a low
frequency; see the posture note in the design document.

WHAT THIS INDEX DOES NOT TELL YOU: whether a publication has an EPUB. The
catalog records exactly one asset type -- application/x-jwpub, JW Library's own
format -- for all 3,768 Spanish publications, so EPUB availability is not
derivable from it. Sampling confirms the split is roughly by age: recent
publications resolve through GETPUBMEDIALINKS, while 1973-2007 material returns
404. Probing all 3,768 to find out would mean 3,768 API calls per build, which
is not a reasonable thing to do to someone else's service on a schedule.

So the device lists publications it may not be able to download, and says so
plainly when a download finds no EPUB rather than reporting a generic failure.
"""
import argparse
import gzip
import hashlib
import json
import sqlite3
import sys
import urllib.request
from datetime import date, timezone, datetime

MANIFEST_URL = "https://app.jw-cdn.org/catalogs/publications/v4/manifest.json"
CATALOG_URL = "https://app.jw-cdn.org/catalogs/publications/v4/{id}/catalog.db.gz"
USER_AGENT = "bereanOS-catalog-indexer/1 (+https://github.com/victorstein/berean-os)"

FORMAT_VERSION = 1
MAGIC = "berean-catalog"


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


def build_index(db_path: str, language_id: int, language_code: str, manifest_id: str) -> str:
    db = sqlite3.connect(db_path)
    table, cols = find_publication_table(db)

    query = (
        f"SELECT DISTINCT {cols['symbol']}, {cols['issue']}, {cols['year']}, {cols['title']} "
        f"FROM {table} WHERE {cols['language']} = ? "
        f"ORDER BY {cols['symbol']}, {cols['issue']}"
    )

    lines = [
        "\t".join([MAGIC, str(FORMAT_VERSION), language_code, manifest_id, date.today().isoformat()])
    ]
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
        lines.append("\t".join([symbol, issue, clean(year), kind, clean(title)]))

    db.close()
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--language-id", type=int, default=1, help="MepsLanguageId (1 = Spanish)")
    parser.add_argument("--language-code", default="S", help="JW language code used by GETPUBMEDIALINKS")
    parser.add_argument("--out", required=True)
    parser.add_argument("--db", help="Use a local catalog.db instead of downloading")
    parser.add_argument("--previous", help="Previous index, to detect that nothing changed")
    args = parser.parse_args()

    manifest_id = current_manifest_id()
    sys.stderr.write(f"manifest: {manifest_id}\n")

    if args.db:
        db_path = args.db
    else:
        # The manifest id is NOT reliably content-addressed: the .gz behind an
        # unchanged id has been observed with a newer last-modified at identical
        # length. So the id is never the freshness test -- the index's own hash
        # is, below.
        payload = fetch(CATALOG_URL.format(id=manifest_id))
        db_path = "catalog.db"
        with open(db_path, "wb") as handle:
            handle.write(gzip.decompress(payload))
        sys.stderr.write(f"catalog: {len(payload):,} B gzipped\n")

    index = build_index(db_path, args.language_id, args.language_code, manifest_id)
    rows = index.count("\n") - 1
    digest = hashlib.sha256(index.encode("utf-8")).hexdigest()

    if args.previous:
        try:
            with open(args.previous, encoding="utf-8") as handle:
                previous = handle.read()
            # Compare the RECORDS only: the header carries a build date that
            # changes daily, which would otherwise republish an identical index
            # every run.
            if previous.split("\n", 1)[1:] == index.split("\n", 1)[1:]:
                sys.stderr.write("unchanged; not republishing\n")
                return 3
        except FileNotFoundError:
            pass

    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write(index)

    sys.stderr.write(
        f"wrote {args.out}: {rows:,} rows, {len(index.encode('utf-8')):,} B, sha256 {digest[:16]}\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
