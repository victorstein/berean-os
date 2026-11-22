# Tarjetas (preaching cards) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A first **Tarjetas** tab in Predicación that lists the owner's printed preaching cards and opens each as verse cards built from the scriptures its jw.org article(s) cite.

**Architecture:** A Python script beside the cards (`rfid-cards/src/cards_json.py`) fetches each card's article, resolves every cited verse in a local NWT EPUB, and writes `preaching-cards.json`; the owner uploads it to the SD root over File Transfer. The firmware reads it, read-only, through a new `PreachingHandouts` reader shaped like `PreachingPassages`, and turns a handout into the same `TopicCards` the existing topic, show-mode and reader-return screens already use.

**Tech Stack:** Python 3 stdlib (`html.parser`, `urllib`, `zipfile`, `unittest`); C++20 / ESP32-S3 firmware with ArduinoJson 7 (PSRAM allocator); GoogleTest host suite `StorageIoTest`.

**Spec:** `docs/superpowers/specs/2026-11-22-preaching-cards-design.md`. In code a printed card is a **handout** — "card" already names a layout card (`src/activities/preaching/PreachingCardLayout.h`).

**Code style (applies to every task, and to any subagent's prompt):** names carry the meaning; no comments that restate code. A comment only for a non-obvious *why*. Follow the repo `CLAUDE.md`: `makeUniqueNoThrow`, `LOG_*`, `tr()`, `.reserve()` before push loops, no explicit `close()` on local files. Never add Claude/assistant attribution to commits.

**Worktree:** `/Volumes/stein/Documents/development/personal/berean-os/.claude/worktrees/preaching-cards`, branch `feature/preaching-cards`. Run every berean-os command from there. `pio` is `/Volumes/stein/.platformio/penv/bin/pio`.

---

## File map

| File | Change | Responsibility |
|---|---|---|
| `/Volumes/stein/Documents/rfid-cards/src/cards_json.py` | create | Fetch articles, resolve verses in the EPUB, write the JSON |
| `/Volumes/stein/Documents/rfid-cards/src/test_cards_json.py` | create | Network-free self-check of the script |
| `src/study/PreachingHandouts.{h,cpp}` | create | Read `/preaching-cards.json`: summaries for the list, one handout's verses |
| `test/storage_io/PreachingHandoutsTest.cpp` | create | Host tests for the reader |
| `test/storage_io/CMakeLists.txt` | modify | Build the new test and source into `StorageIoTest` |
| `docs/file-formats.md` | modify | Document the file |
| `lib/I18n/translations/{english,spanish}.yaml` | modify | Three strings |
| `src/activities/preaching/PreachingOrigin.h` | modify | `Kind::Handout` |
| `src/activities/preaching/PreachingContent.{h,cpp}` | modify | `TopicCards` holds a handout; `loadHandout` |
| `src/activities/preaching/PreachingTopicActivity.cpp` | modify | Origin and Bible opening for a handout verse |
| `src/activities/preaching/PreachingSession.{h,cpp}` | modify | Hold the handout summaries |
| `src/activities/preaching/PreachingActivity.{h,cpp}` | modify | The Tarjetas tab, first and default |

`rfid-cards` is not a git repository: Tasks 1–2 have no commit step.

---

### Task 0: Bootstrap the worktree

- [ ] **Step 1: Init submodules and the formatter**

Run: `./bin/bootstrap`
Expected: exits 0; `.venv/bin/clang-format` exists.

- [ ] **Step 2: Configure the host test tree**

Run: `cmake -S test -B build/test && cmake --build build/test --target StorageIoTest -j`
Expected: builds. If the first build fails in several suites at once, re-run the same command once: a fresh configure races on gtest (known, harmless).

---

### Task 1: The generator's self-check (failing)

**Files:**
- Create: `/Volumes/stein/Documents/rfid-cards/src/test_cards_json.py`

- [ ] **Step 1: Write the tests**

The fixtures are made-up text, never NWT text.

```python
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import cards_json

CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Libro 3</title><link rel="stylesheet" href="x.css"/></head><body>
<h2>A heading that is not verse text</h2>
<p><span id="chapter3_verse1"></span><span class="w_ch"><strong>3</strong> </span>First made-up verse<span id="footnotesource1"></span><a epub:type="noteref" href="#footnote1">*</a> ends here.
<span id="chapter3_verse2"></span><strong><sup>2</sup></strong> Second line one</p><p>second line two.
<span id="chapter3_verse3"></span><strong><sup>3</sup></strong> Third.</p>
<aside epub:type="footnote"><div epub:type="footnote" id="footnote1"><p>A footnote.</p></div></aside>
</body></html>"""

SINGLE_CHAPTER_BOOK = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Judas</title></head><body>
<p><span id="chapter1_verse14"></span><strong><sup>14</sup></strong> Made-up fourteen.</p>
</body></html>"""


def make_epub(directory, files):
    path = Path(directory) / "bible.epub"
    with zipfile.ZipFile(path, "w") as epub:
        for name, text in files.items():
            epub.writestr(f"OEBPS/{name}", text)
    return path


class Labels(unittest.TestCase):
    def test_forms(self):
        self.assertEqual(cards_json.label("Mateo", 6, 34, 34), "Mateo 6:34")
        self.assertEqual(cards_json.label("Filipenses", 4, 6, 7), "Filipenses 4:6, 7")
        self.assertEqual(cards_json.label("1 Juan", 2, 15, 17), "1 Juan 2:15-17")
        self.assertEqual(cards_json.label("Judas", None, 14, 14), "Judas 14")

    def test_targets(self):
        self.assertEqual(cards_json.parse_targets("40006034"), [(40006034, 40006034)])
        self.assertEqual(cards_json.parse_targets("23035005-23035006"), [(23035005, 23035006)])
        self.assertEqual(cards_json.parse_targets("1001001,1001003-1001004"),
                         [(1001001, 1001001), (1001003, 1001004)])


class Articles(unittest.TestCase):
    def test_ranges_in_order_with_both_quote_styles_and_no_repeats(self):
        html = ('<html><head><link rel="canonical" href="https://x.test/a/"></head><body><h1> Un  título </h1>'
                '<a class=" jsBibleLink" data-targetverses="40006034" href="/m/6">Mateo 6:34</a>'
                "<a class=' jsBibleLink' data-targetverses='23035005-23035006' href='/i/35'>Isaías 35:5, 6</a>"
                '<a class="jsBibleLink" data-targetverses="40006034">again</a>'
                '<a data-targetverses="1001001">not a Bible link</a></body></html>')
        article = cards_json.parse_article(html)
        self.assertEqual(article.heading, "Un título")
        self.assertEqual(article.ranges, [(40006034, 40006034), (23035005, 23035006)])

    def test_issue_articles_are_the_pages_one_level_down_in_page_order(self):
        html = ('<link rel="canonical" href="https://x.test/revistas/numero1/">'
                '<a href="/revistas/numero1/b/">B</a><a href="/revistas/numero1/a/#x">A</a>'
                '<a href="/revistas/numero1/b/">B again</a><a href="/revistas/">up</a>'
                '<a href="/revistas/numero1/a/deeper/">deeper</a>')
        page = cards_json.parse_article(html)
        self.assertEqual(cards_json.issue_articles("https://x.test/revistas/numero1/", page),
                         ["https://x.test/revistas/numero1/b/", "https://x.test/revistas/numero1/a/"])


class BibleText(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.bible = cards_json.Bible(make_epub(self.directory.name, {
            "1001061144-split3.xhtml": CHAPTER,
            "1001061169.xhtml": SINGLE_CHAPTER_BOOK,
        }))

    def tearDown(self):
        self.directory.cleanup()

    def test_a_verse_skips_numbers_notes_headings_and_footnotes(self):
        self.assertEqual(self.bible.verse(40003001, 40003001), {
            "ref": "Libro 3:1",
            "href": "1001061144-split3.xhtml#chapter3_verse1",
            "text": "First made-up verse ends here.",
        })

    def test_a_range_joins_its_verses_and_poetry_lines(self):
        verse = self.bible.verse(40003002, 40003003)
        self.assertEqual(verse["ref"], "Libro 3:2, 3")
        self.assertEqual(verse["text"], "Second line one second line two. Third.")
        self.assertEqual(verse["href"], "1001061144-split3.xhtml#chapter3_verse2")

    def test_a_single_chapter_book_is_labelled_without_its_chapter(self):
        self.assertEqual(self.bible.verse(65001014, 65001014)["ref"], "Judas 14")

    def test_an_unresolved_range_fails(self):
        for start, end in [(40004001, 40004001), (40003009, 40003009), (40003003, 40004001)]:
            with self.subTest(start=start, end=end), self.assertRaises(ValueError):
                self.bible.verse(start, end)


class Build(unittest.TestCase):
    PAGES = {
        "https://x.test/card": ("https://x.test/art/",
                                '<h1>Art</h1><a class="jsBibleLink" data-targetverses="40003001">x</a>'),
        "https://x.test/issue": ("https://x.test/num/",
                                 '<link rel="canonical" href="https://x.test/num/">'
                                 '<a href="/num/one/">1</a><a href="/num/two/">2</a>'),
        "https://x.test/num/one/": ("https://x.test/num/one/",
                                    '<h1>One</h1><a class="jsBibleLink" data-targetverses="40003002-40003003">x</a>'),
        "https://x.test/num/two/": ("https://x.test/num/two/", "<h1>Two, with no links</h1>"),
        "https://x.test/empty": ("https://x.test/empty/", "<h1>Nothing</h1>"),
    }

    def test_a_single_article_card_and_an_issue_card(self):
        with tempfile.TemporaryDirectory() as directory:
            bible = cards_json.Bible(make_epub(directory, {"1001061144-split3.xhtml": CHAPTER}))
            document, payload = cards_json.build(
                [("Card", "https://x.test/card"), ("Issue", "https://x.test/issue")], bible, self.PAGES.__getitem__)
        self.assertEqual(document["v"], 1)
        card, issue = document["cards"]
        self.assertEqual(card["title"], "Card")
        self.assertEqual([section["heading"] for section in card["sections"]], [""])
        self.assertEqual([section["heading"] for section in issue["sections"]], ["One"])
        self.assertEqual(issue["sections"][0]["verses"][0]["ref"], "Libro 3:2, 3")
        self.assertEqual(json.loads(payload), document)

    def test_a_page_with_no_links_and_no_articles_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            bible = cards_json.Bible(make_epub(directory, {"1001061144-split3.xhtml": CHAPTER}))
            with self.assertRaises(ValueError):
                cards_json.build([("Empty", "https://x.test/empty")], bible, self.PAGES.__getitem__)


class Cards(unittest.TestCase):
    def test_cards_come_from_the_readmes_in_folder_order(self):
        with tempfile.TemporaryDirectory() as directory:
            for folder, title, url in [("02-b", "Second", "https://x.test/2"), ("01-a", "First", "https://x.test/1")]:
                (Path(directory) / folder).mkdir()
                (Path(directory) / folder / "README.txt").write_text(f"{title}\nQR / NFC: {url}\nPhoto: x\n")
            self.assertEqual(cards_json.read_cards(directory),
                             [("First", "https://x.test/1"), ("Second", "https://x.test/2")])

    def test_a_readme_without_the_url_line_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "01-a").mkdir()
            (Path(directory) / "01-a" / "README.txt").write_text("Title only\n")
            with self.assertRaises(ValueError):
                cards_json.read_cards(directory)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Volumes/stein/Documents/rfid-cards/src && python3 -m unittest test_cards_json -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'cards_json'`.

---

### Task 2: The generator

**Files:**
- Create: `/Volumes/stein/Documents/rfid-cards/src/cards_json.py`

- [ ] **Step 1: Write the script**

```python
#!/usr/bin/env python3
"""Writes preaching-cards.json, the Tarjetas tab of berean-os's Predicación.

Each printed card is a folder under final/ whose README.txt carries the title on its first line and
"QR / NFC: <url>" on its second. Every card's article -- every article of the issue, when the link
is a whole magazine issue -- is fetched, and each scripture it links is looked up in a local NWT
EPUB for its text, its label and the link the reader opens. Upload the result to the SD card's root
through berean-os's File Transfer.

    python3 src/cards_json.py --epub ~/Downloads/nwt_S.epub
"""

import argparse
import json
import re
import sys
import urllib.request
import zipfile
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urldefrag, urljoin

FORMAT_VERSION = 1
MAX_FILE_BYTES = 128 * 1024  # PreachingHandouts::MAX_FILE_BYTES
MAX_HREF_BYTES = 63  # PreachingHandouts::MAX_HREF_BYTES
# Book B's chapter 1 is <BOOK_FILE_BASE + B>.xhtml and chapter C >= 2 is <...>-splitC.xhtml, in
# both nwt_S and nwt_E.
BOOK_FILE_BASE = 1001061104
USER_AGENT = "Mozilla/5.0"
BLOCK_TAGS = {"p", "div", "br", "li"}


def split_target(target):
    return target // 1_000_000, target // 1000 % 1000, target % 1000


def parse_targets(value):
    ranges = []
    for part in value.split(","):
        start, _, end = part.strip().partition("-")
        ranges.append((int(start), int(end or start)))
    return ranges


def label(book, chapter, first, last):
    prefix = f"{book} {chapter}:" if chapter else f"{book} "
    if last == first:
        return f"{prefix}{first}"
    if last == first + 1:
        return f"{prefix}{first}, {last}"
    return f"{prefix}{first}-{last}"


def chapter_file(book, chapter):
    base = BOOK_FILE_BASE + book
    return f"{base}.xhtml" if chapter == 1 else f"{base}-split{chapter}.xhtml"


@dataclass
class Article:
    canonical: str = ""
    heading: str = ""
    ranges: list = field(default_factory=list)
    links: list = field(default_factory=list)


class ArticleParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.article = Article()
        self._in_heading = False
        self._heading_done = False

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "link" and attributes.get("rel") == "canonical":
            self.article.canonical = attributes.get("href") or ""
        elif tag == "a":
            if attributes.get("href"):
                self.article.links.append(attributes["href"])
            targets = attributes.get("data-targetverses")
            if targets and "jsBibleLink" in (attributes.get("class") or "").split():
                self.article.ranges.extend(parse_targets(targets))
        elif tag == "h1" and not self._heading_done:
            self._in_heading = True

    def handle_endtag(self, tag):
        if tag == "h1" and self._in_heading:
            self._in_heading = False
            self._heading_done = True

    def handle_data(self, data):
        if self._in_heading:
            self.article.heading += data


def parse_article(html):
    parser = ArticleParser()
    parser.feed(html)
    article = parser.article
    article.heading = " ".join(article.heading.split())
    article.ranges = list(dict.fromkeys(article.ranges))
    return article


def issue_articles(page_url, page):
    base = page.canonical or page_url
    if not base.endswith("/"):
        base += "/"
    found = []
    for link in page.links:
        url = urldefrag(urljoin(page_url, link)).url
        below = url[len(base):].strip("/") if url.startswith(base) else ""
        if below and "/" not in below and url not in found:
            found.append(url)
    return found


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.geturl(), response.read().decode("utf-8")


class ChapterParser(HTMLParser):
    HIDDEN_TAGS = {"head", "sup", "aside", "h1", "h2", "h3", "h4", "h5", "h6"}

    def __init__(self, chapter):
        super().__init__(convert_charrefs=True)
        self.marker = re.compile(rf"chapter{chapter}_verse(\d+)")
        self.title = ""
        self.verses = {}
        self._verse = None
        self._hides = []  # per open element: whether its text is not verse text
        self._hidden = 0
        self._in_title = False

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        marker = self.marker.fullmatch(attributes.get("id") or "")
        if marker:
            self._verse = int(marker.group(1))
            self.verses.setdefault(self._verse, "")
        if tag in BLOCK_TAGS:
            self._space()
        hides = (tag in self.HIDDEN_TAGS
                 or "w_ch" in (attributes.get("class") or "").split()
                 or attributes.get("epub:type") == "noteref")
        self._hides.append(hides)
        self._hidden += hides
        if tag == "title":
            self._in_title = True

    def handle_endtag(self, tag):
        if tag == "title":
            self._in_title = False
        if self._hides:
            self._hidden -= self._hides.pop()
        if tag in BLOCK_TAGS:
            self._space()

    def handle_data(self, data):
        if self._in_title:
            self.title += data
        elif self._verse is not None and not self._hidden:
            self.verses[self._verse] += data

    def _space(self):
        if self._verse is not None and not self._hidden:
            self.verses[self._verse] += " "


class Bible:
    def __init__(self, epub_path):
        self.epub = zipfile.ZipFile(epub_path)
        self._chapters = {}

    def _chapter(self, name, chapter):
        if name not in self._chapters:
            try:
                xhtml = self.epub.read(f"OEBPS/{name}").decode("utf-8")
            except KeyError:
                raise ValueError(f"{name} is not in the EPUB") from None
            parser = ChapterParser(chapter)
            parser.feed(xhtml)
            verses = {number: " ".join(text.split()) for number, text in parser.verses.items()}
            self._chapters[name] = (" ".join(parser.title.split()), verses)
        return self._chapters[name]

    def verse(self, start, end):
        book, chapter, first = split_target(start)
        end_book, end_chapter, last = split_target(end)
        if (book, chapter) != (end_book, end_chapter) or last < first:
            raise ValueError(f"{start}-{end} is not a range within one chapter")
        name = chapter_file(book, chapter)
        title, verses = self._chapter(name, chapter)
        missing = [number for number in range(first, last + 1) if not verses.get(number)]
        if missing:
            raise ValueError(f"{start}-{end}: verses {missing} have no text in {name}")
        named = re.fullmatch(r"(.+?)(?: (\d+))?", title)
        if not named:
            raise ValueError(f"{name} has no title to name its book")
        href = f"{name}#chapter{chapter}_verse{first}"
        if len(href.encode()) > MAX_HREF_BYTES:
            raise ValueError(f"{href} is longer than {MAX_HREF_BYTES} bytes")
        return {
            "ref": label(named.group(1), chapter if named.group(2) else None, first, last),
            "href": href,
            "text": " ".join(verses[number] for number in range(first, last + 1)),
        }


def card_sections(url, bible, fetch):
    page_url, html = fetch(url)
    page = parse_article(html)
    articles = [("", page)] if page.ranges else []
    if not articles:
        for article_url in issue_articles(page_url, page):
            article = parse_article(fetch(article_url)[1])
            if article.ranges:
                articles.append((article.heading, article))
    if not articles:
        raise ValueError(f"{url}: no scripture links, and no articles below it")
    return [{"heading": heading, "verses": [bible.verse(*span) for span in article.ranges]}
            for heading, article in articles]


def read_cards(final_dir):
    cards = []
    for readme in sorted(Path(final_dir).glob("*/README.txt")):
        lines = readme.read_text(encoding="utf-8").splitlines()
        if len(lines) < 2 or not lines[1].startswith("QR / NFC:"):
            raise ValueError(f"{readme}: expected a title line, then 'QR / NFC: <url>'")
        cards.append((lines[0].strip(), lines[1].removeprefix("QR / NFC:").strip()))
    return cards


def build(cards, bible, fetch=fetch):
    document = {"v": FORMAT_VERSION,
                "cards": [{"title": title, "sections": card_sections(url, bible, fetch)} for title, url in cards]}
    payload = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(payload) > MAX_FILE_BYTES:
        raise ValueError(f"{len(payload):,} bytes is over the device's {MAX_FILE_BYTES:,}-byte cap")
    return document, payload


def main():
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--epub", required=True, type=Path, help="a local NWT EPUB, e.g. nwt_S.epub")
    parser.add_argument("--final", default=here.parent / "final", type=Path)
    parser.add_argument("--out", default=here.parent / "out" / "preaching-cards.json", type=Path)
    args = parser.parse_args()
    try:
        document, payload = build(read_cards(args.final), Bible(args.epub))
    except ValueError as error:
        sys.exit(f"cards_json: {error}")
    args.out.write_bytes(payload)
    for card in document["cards"]:
        verses = sum(len(section["verses"]) for section in card["sections"])
        print(f"{verses:4}  {card['title']}  ({len(card['sections'])} section(s))")
    print(f"{len(payload):,} bytes -> {args.out}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the self-check**

Run: `cd /Volumes/stein/Documents/rfid-cards/src && python3 -m unittest test_cards_json -v`
Expected: all tests PASS (12).

- [ ] **Step 3: Generate the real file**

Run: `cd /Volumes/stein/Documents/rfid-cards && python3 src/cards_json.py --epub /Volumes/stein/Downloads/nwt_S.epub`
Expected: eight lines, one per card, then `N bytes -> .../out/preaching-cards.json` with N under 131,072. Approximate verse counts from 2026-11-22: Esperanza ~24, Familia 9, Trabajo 4, Salud mental ~35 over ~8 sections, Estrés ~16, Biblia útil ~60 over ~7 sections, Dinero 5, Dormir ~3. A `cards_json: ...` error means a range did not resolve: read the message, fix the cause (do not skip the range), and re-run.

- [ ] **Step 4: Spot-check the output**

Run: `python3 -c "import json;d=json.load(open('/Volumes/stein/Documents/rfid-cards/out/preaching-cards.json'));c=d['cards'][4];print(c['title']);[print(v['ref'],'|',v['href'],'|',v['text'][:70]) for s in c['sections'] for v in s['verses'][:4]]"`
Expected: `Menos estrés`, then `Mateo 6:34 | 1001061144-split6.xhtml#chapter6_verse34 | Así que nunca se angustien por el día siguiente...` and three more. No verse number, no `*`, no footnote text in any quote.

---

### Task 3: `PreachingHandouts` host tests (failing)

**Files:**
- Create: `test/storage_io/PreachingHandoutsTest.cpp`
- Modify: `test/storage_io/CMakeLists.txt`

- [ ] **Step 1: Write the tests**

```cpp
// The read-only /preaching-cards.json passes behind Predicación's Tarjetas.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingHandouts.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

namespace {

using PreachingHandouts::Status;

const std::string FILE_PATH = PreachingHandouts::FILE_PATH;

const char* const TWO_HANDOUTS = R"({"v":1,"cards":[
  {"title":"Stress","sections":[{"heading":"","verses":[
    {"ref":"Book 1:2","href":"b-split1.xhtml#chapter1_verse2","text":"Made-up verse one."},
    {"ref":"Book 3:4, 5","href":"b-split3.xhtml#chapter3_verse4","text":"Made-up verse two."}]}]},
  {"title":"An issue","sections":[
    {"heading":"First article","verses":[{"ref":"Book 6:7","href":"b-split6.xhtml#chapter6_verse7","text":"Seven."}]},
    {"heading":"Second article","verses":[{"ref":"Book 8:9","href":"b-split8.xhtml#chapter8_verse9","text":"Nine."}]}]}]})";

void* failAllocation(size_t) { return nullptr; }
void releaseNothing(void*) {}

class PreachingHandoutsIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingHandoutsIo, SummariesListEveryHandoutWithItsVerseCount) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  std::vector<PreachingHandouts::Summary> summaries;
  ASSERT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Ok);
  ASSERT_EQ(summaries.size(), 2u);
  EXPECT_EQ(summaries[0].title, "Stress");
  EXPECT_EQ(summaries[0].verses, 2);
  EXPECT_EQ(summaries[1].title, "An issue");
  EXPECT_EQ(summaries[1].verses, 2);
}

TEST_F(PreachingHandoutsIo, LoadsOneHandoutsSectionsAndVerses) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(1, {}, handout), Status::Ok);
  EXPECT_EQ(handout.title, "An issue");
  ASSERT_EQ(handout.headings.size(), 2u);
  EXPECT_EQ(handout.headings[0], "First article");
  EXPECT_EQ(handout.headings[1], "Second article");
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[0].section, 0);
  EXPECT_EQ(handout.verses[0].reference, "Book 6:7");
  EXPECT_EQ(handout.verses[0].href, "b-split6.xhtml#chapter6_verse7");
  EXPECT_EQ(handout.verses[0].text, "Seven.");
  EXPECT_EQ(handout.verses[1].section, 1);
  EXPECT_EQ(handout.verses[1].text, "Nine.");
}

TEST_F(PreachingHandoutsIo, ASingleArticleHandoutHasOneEmptyHeading) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ASSERT_EQ(handout.headings.size(), 1u);
  EXPECT_TRUE(handout.headings[0].empty());
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[1].reference, "Book 3:4, 5");
}

TEST_F(PreachingHandoutsIo, AMissingFileIsMissing) {
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Missing);
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(0, {}, handout), Status::Missing);
}

TEST_F(PreachingHandoutsIo, ANewerFormatIsRefusedAsTooNew) {
  storage_fake::putFile(FILE_PATH, R"({"v":2,"cards":[]})");
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::TooNew);
  EXPECT_TRUE(summaries.empty());
}

TEST_F(PreachingHandoutsIo, UnreadableFilesAreUnreadable) {
  for (const char* contents : {"", R"({"v":0,"cards":[]})", R"({"cards":[]})", R"({"v":1})", R"({"v":1,"cards":[{)"}) {
    storage_fake::putFile(FILE_PATH, contents);
    std::vector<PreachingHandouts::Summary> summaries;
    EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Unreadable) << contents;
    EXPECT_TRUE(summaries.empty()) << contents;
  }
  storage_fake::putFile(FILE_PATH, std::string(PreachingHandouts::MAX_FILE_BYTES + 1, ' '));
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Unreadable);
}

TEST_F(PreachingHandoutsIo, AnIndexPastTheEndIsUnreadable) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(2, {}, handout), Status::Unreadable);
  EXPECT_TRUE(handout.verses.empty());
}

TEST_F(PreachingHandoutsIo, AVerseWhoseHrefCannotOpenIsSkipped) {
  const std::string tooLong(PreachingHandouts::MAX_HREF_BYTES + 1, 'x');
  storage_fake::putFile(FILE_PATH, R"({"v":1,"cards":[{"title":"T","sections":[{"heading":"","verses":[)"
                                   R"({"ref":"A","href":"","text":"a"},)"
                                   R"({"ref":"B","href":")" + tooLong + R"(","text":"b"},)"
                                   R"({"ref":"C","href":"c.xhtml#chapter1_verse1","text":"c"}]}]}]})");
  std::vector<PreachingHandouts::Summary> summaries;
  ASSERT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Ok);
  EXPECT_EQ(summaries[0].verses, 1);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ASSERT_EQ(handout.verses.size(), 1u);
  EXPECT_EQ(handout.verses[0].reference, "C");
}

TEST_F(PreachingHandoutsIo, ATextThatCannotBeAllocatedIsOutOfMemory) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  study::PassageDoc::Allocators allocators;
  allocators.text = {failAllocation, releaseNothing};
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(0, allocators, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.verses.empty());
}

}  // namespace
```

- [ ] **Step 2: Add the test and source to `StorageIoTest`**

In `test/storage_io/CMakeLists.txt`, after the `PreachingPassagesTest.cpp` line add:

```cmake
  PreachingHandoutsTest.cpp
```

and after `${REPO_ROOT}/src/study/PreachingPassages.cpp` add:

```cmake
  ${REPO_ROOT}/src/study/PreachingHandouts.cpp
```

In the comment at the top of that file, change `the TagPaletteFile, PassageFile and PreachingTopicsFile loaders` to `the TagPaletteFile, PassageFile and PreachingTopicsFile loaders and the read-only Predicación passes`.

- [ ] **Step 3: Run it to see it fail**

Run: `cmake --build build/test --target StorageIoTest -j`
Expected: FAIL — `PreachingHandouts.h: No such file or directory`.

---

### Task 4: `PreachingHandouts`

**Files:**
- Create: `src/study/PreachingHandouts.h`, `src/study/PreachingHandouts.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

// Read-only passes over /preaching-cards.json, the owner's printed preaching cards ("handouts")
// with every verse their jw.org articles cite, for Predicación's Tarjetas. A script beside the
// cards writes it (rfid-cards/src/cards_json.py) and the owner uploads it over File Transfer,
// which refuses dot-directories: hence the SD root, outside /.berean. The device never writes it.
//
// The firmware must pass PsramJsonAllocator::json() and ::passageDoc(), as for PreachingPassages.
namespace PreachingHandouts {

inline constexpr char FILE_PATH[] = "/preaching-cards.json";
inline constexpr int FORMAT_VERSION = 1;
// cards_json.py refuses to write past the same cap.
inline constexpr size_t MAX_FILE_BYTES = 128 * 1024;
// ReaderEntryIntent::HREF_BYTES less its NUL; cards_json.py checks the same bound.
inline constexpr size_t MAX_HREF_BYTES = 63;
// So a handout's index fits PreachingOrigin's uint8_t.
inline constexpr size_t MAX_HANDOUTS = 200;
inline constexpr size_t MAX_TITLE_BYTES = 96;

enum class Status : uint8_t { Ok, Missing, Unreadable, TooNew, OutOfMemory };

struct Summary {
  std::string title;
  uint16_t verses = 0;
};

struct Verse {
  uint16_t section = 0;  // index into Handout::headings
  std::string reference;
  std::string href;
  study::PassageText text;
};

struct Handout {
  std::string title;
  std::vector<std::string> headings;  // one per section; empty for a single-article handout
  std::vector<Verse> verses;
};

// Each handout's title and the number of verses load() would return, in file order. `out` is
// empty unless the result is Ok.
Status summaries(ArduinoJson::Allocator* json, std::vector<Summary>& out);

// The handout at `index`, its verse texts through `allocators.text`. `out` is empty unless the
// result is Ok; an index past the end is Unreadable.
Status load(size_t index, const study::PassageDoc::Allocators& allocators, Handout& out);

}  // namespace PreachingHandouts
```

- [ ] **Step 2: Write the implementation**

```cpp
#include "PreachingHandouts.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstring>
#include <utility>

namespace PreachingHandouts {
namespace {

constexpr const char* MODULE = "PREACH";
constexpr size_t READ_BUFFER_BYTES = 512;

// The parser pulls one character per read() and every HalFile::read takes storageMutex, hence the
// buffer. PreachingPassages.cpp's twin is file-local too.
class JsonFileReader {
 public:
  explicit JsonFileReader(serialization::BufferedFileReader& in) : in_(in) {}

  int read() {
    uint8_t c = 0;
    return in_.read(&c, 1) == 1 ? c : -1;
  }

 private:
  serialization::BufferedFileReader& in_;
};

JsonDocument newDocument(ArduinoJson::Allocator* allocator) {
  return allocator ? JsonDocument(allocator) : JsonDocument();
}

bool hrefOpens(const char* href) {
  const size_t bytes = strlen(href);
  return bytes > 0 && bytes <= MAX_HREF_BYTES;
}

// Both passes parse through here, so the filtered deserializeJson template is instantiated once.
Status readFiltered(JsonDocument& doc, const JsonDocument& filter) {
  if (filter.overflowed()) {
    LOG_ERR(MODULE, "OOM: handout filter");
    return Status::OutOfMemory;
  }
  // Checked first: a failed open logs, and a card with no handouts file is ordinary.
  if (!Storage.exists(FILE_PATH)) return Status::Missing;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, FILE_PATH, file)) return Status::Unreadable;
  const size_t bytes = file.size();
  if (bytes == 0 || bytes > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "%s: %u bytes, outside 1..%u", FILE_PATH, static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_FILE_BYTES));
    return Status::Unreadable;
  }

  serialization::BufferedFileReader buffered(file, READ_BUFFER_BYTES);
  JsonFileReader reader(buffered);
  const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  if (error == DeserializationError::NoMemory || doc.overflowed()) {
    LOG_ERR(MODULE, "OOM: parsing %s", FILE_PATH);
    return Status::OutOfMemory;
  }
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", FILE_PATH, error.c_str());
    return Status::Unreadable;
  }
  const int version = doc["v"] | 0;
  if (version > FORMAT_VERSION) {
    LOG_ERR(MODULE, "%s: format v%d is newer than v%d", FILE_PATH, version, FORMAT_VERSION);
    return Status::TooNew;
  }
  if (version < 1 || !doc["cards"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "%s: no v%d handouts", FILE_PATH, FORMAT_VERSION);
    return Status::Unreadable;
  }
  return Status::Ok;
}

}  // namespace

Status summaries(ArduinoJson::Allocator* json, std::vector<Summary>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["cards"][0]["title"] = true;
  filter["cards"][0]["sections"][0]["verses"][0]["href"] = true;

  JsonDocument doc = newDocument(json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst handouts = doc["cards"].as<JsonArrayConst>();
  out.reserve(handouts.size() < MAX_HANDOUTS ? handouts.size() : MAX_HANDOUTS);
  for (const JsonVariantConst handout : handouts) {
    if (out.size() == MAX_HANDOUTS) break;
    Summary summary{utf8SafeSummary(handout["title"] | "", MAX_TITLE_BYTES), 0};
    for (const JsonVariantConst section : handout["sections"].as<JsonArrayConst>()) {
      for (const JsonVariantConst verse : section["verses"].as<JsonArrayConst>()) {
        if (hrefOpens(verse["href"] | "") && summary.verses < UINT16_MAX) ++summary.verses;
      }
    }
    out.push_back(std::move(summary));
  }
  LOG_INF(MODULE, "Handouts: %u, %lu ms", static_cast<unsigned>(out.size()), millis() - started);
  return Status::Ok;
}

Status load(const size_t index, const study::PassageDoc::Allocators& allocators, Handout& out) {
  out = Handout{};
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["cards"][0]["title"] = true;
  filter["cards"][0]["sections"][0]["heading"] = true;
  for (const char* key : {"ref", "href", "text"}) filter["cards"][0]["sections"][0]["verses"][0][key] = true;

  JsonDocument doc = newDocument(allocators.json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst handouts = doc["cards"].as<JsonArrayConst>();
  if (index >= handouts.size() || index >= MAX_HANDOUTS) {
    LOG_ERR(MODULE, "No handout %u of %u", static_cast<unsigned>(index), static_cast<unsigned>(handouts.size()));
    return Status::Unreadable;
  }
  const JsonVariantConst handout = handouts[index];
  const JsonArrayConst sections = handout["sections"].as<JsonArrayConst>();
  size_t verseCount = 0;
  for (const JsonVariantConst section : sections) verseCount += section["verses"].size();

  out.title = utf8SafeSummary(handout["title"] | "", MAX_TITLE_BYTES);
  out.headings.reserve(sections.size());
  out.verses.reserve(verseCount);
  for (const JsonVariantConst section : sections) {
    if (out.headings.size() == UINT16_MAX) break;
    const auto sectionIndex = static_cast<uint16_t>(out.headings.size());
    out.headings.push_back(utf8SafeSummary(section["heading"] | "", MAX_TITLE_BYTES));
    for (const JsonVariantConst verse : section["verses"].as<JsonArrayConst>()) {
      const char* href = verse["href"] | "";
      if (!hrefOpens(href)) {
        LOG_ERR(MODULE, "Skipping \"%s\": href of %u bytes", verse["ref"] | "", static_cast<unsigned>(strlen(href)));
        continue;
      }
      Verse entry{sectionIndex, utf8SafeSummary(verse["ref"] | "", study::PassageDoc::MAX_REFERENCE_BYTES), href,
                  study::PassageText(allocators.text)};
      if (!entry.text.assign(verse["text"] | "")) {
        LOG_ERR(MODULE, "OOM: text of handout %u", static_cast<unsigned>(index));
        out = Handout{};
        return Status::OutOfMemory;
      }
      out.verses.push_back(std::move(entry));
    }
  }
  LOG_INF(MODULE, "Handout %u: %u verses, %lu ms", static_cast<unsigned>(index),
          static_cast<unsigned>(out.verses.size()), millis() - started);
  return Status::Ok;
}

}  // namespace PreachingHandouts
```

- [ ] **Step 3: Run the tests**

Run: `cmake --build build/test --target StorageIoTest -j && ctest --test-dir build/test -R PreachingHandouts --output-on-failure`
Expected: 9 tests PASS. If `millis` is undefined on the host, check `test/stubs/Arduino.h` declares it (PreachingPassages.cpp already uses it in the same executable, so it should).

- [ ] **Step 4: Run the whole storage suite**

Run: `ctest --test-dir build/test -R StorageIo --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/study/PreachingHandouts.h src/study/PreachingHandouts.cpp test/storage_io/PreachingHandoutsTest.cpp test/storage_io/CMakeLists.txt
git commit -m "feat(preaching): read the preaching cards file"
```

---

### Task 5: Document the file

**Files:**
- Modify: `docs/file-formats.md` (insert before the `## \`/.berean/passages/<pubkey>.json\`` heading, line ~577)

- [ ] **Step 1: Add the section**

````markdown
## `/preaching-cards.json`

The owner's printed preaching cards (Predicación's Tarjetas), each with every verse its jw.org
article cites. Written on a Mac by `rfid-cards/src/cards_json.py` and uploaded through File
Transfer, which refuses dot-directories: hence the SD root. Read-only on the device, by
`src/study/PreachingHandouts.{h,cpp}` (host-tested in `test/storage_io/PreachingHandoutsTest.cpp`),
on the loop task.

### Version 1

- `v` — format version. An absent, zero or negative `v` is unreadable; a newer one is refused.

```json
{"v":1,"cards":[{"title":"Menos estrés","sections":[{"heading":"","verses":[
  {"ref":"Mateo 6:34","href":"1001061144-split6.xhtml#chapter6_verse34","text":"…"}]}]}]}
```

- `cards` — in display order; at most 200 are read.
- `title` — the card's title, cut at 96 bytes.
- `sections` — one per article; `heading` is the article's title, empty for a card that links a
  single article.
- `ref` — the label, cut at 48 bytes. `text` — the verse text, whole. `href` — the NWT EPUB's own
  `<file>#chapterC_verseV` link, opened as Preguntas opens its references. A verse with an empty
  href or one over 63 bytes is skipped.

A file over 128 KB is refused before parsing. The eight cards of 2026-11-22 come to about 50 KB.
````

- [ ] **Step 2: Commit**

```bash
git add docs/file-formats.md
git commit -m "docs: describe the preaching cards file"
```

---

### Task 6: Strings

**Files:**
- Modify: `lib/I18n/translations/english.yaml` (after `STR_PREACHING_SAVE_FAILED`, line ~498)
- Modify: `lib/I18n/translations/spanish.yaml` (after `STR_PREACHING_SAVE_FAILED`, line ~445)

- [ ] **Step 1: English**

```yaml
STR_PREACHING_CARDS: "Cards"
STR_PREACHING_NO_CARDS: "No cards yet. Upload preaching-cards.json to the SD card's root with File Transfer."
STR_PREACHING_CARDS_UNREADABLE: "Can't read preaching-cards.json"
```

- [ ] **Step 2: Spanish**

```yaml
STR_PREACHING_CARDS: "Tarjetas"
STR_PREACHING_NO_CARDS: "Aún no hay tarjetas. Sube preaching-cards.json a la raíz de la tarjeta SD con Transferir archivos."
STR_PREACHING_CARDS_UNREADABLE: "No se puede leer preaching-cards.json"
```

Before writing "Transferir archivos", check the Spanish label the File Transfer entry actually uses: `grep -n "FILE_TRANSFER" lib/I18n/translations/spanish.yaml`, and use that wording.

- [ ] **Step 3: Regenerate and check**

Run: `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
Expected: exits 0. (The generated headers are gitignored; do not stage them.)

- [ ] **Step 4: Commit**

```bash
git add lib/I18n/translations/english.yaml lib/I18n/translations/spanish.yaml
git commit -m "feat(preaching): add the Tarjetas strings"
```

---

### Task 7: A handout as topic cards

**Files:**
- Modify: `src/activities/preaching/PreachingOrigin.h`
- Modify: `src/activities/preaching/PreachingContent.h`, `PreachingContent.cpp`
- Modify: `src/activities/preaching/PreachingTopicActivity.cpp:163-166,202-232`

- [ ] **Step 1: `PreachingOrigin` gains `Handout`**

In `PreachingOrigin.h`, the comment's first line becomes `// Where in Predicación a screen, or the reader, came from: a question, a tag topic or a handout,`, and:

```cpp
  enum class Kind : uint8_t { None, Question, Tag, Handout };

  Kind kind = Kind::None;
  uint8_t question = 0;  // 1..IntroSection::QUESTION_COUNT when kind == Question
  uint16_t tag = 0;      // the raw study::TagId when kind == Tag
  uint8_t handout = 0;   // index in /preaching-cards.json when kind == Handout
  uint16_t verse = 0;    // ordinal among the topic's Verse cards
```

and after `ofTag`:

```cpp
  static constexpr PreachingOrigin ofHandout(const int handout, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Handout;
    origin.handout = static_cast<uint8_t>(handout);
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }
```

- [ ] **Step 2: `TopicCards` holds a handout**

In `PreachingContent.h` add `#include "study/PreachingHandouts.h"` beside the `PreachingPassages` include. Change the struct's comment first line to `// One topic's cards: a question read from the Bible, the Bible passages carrying a tag, or a handout.` and its `cards` sentence to `` `cards` views into `parsed`, `passages`, `handout` and the session's question labels, ``. Add members after `passages`:

```cpp
  int handoutIndex = 0;
  PreachingHandouts::Handout handout;
```

Change the `verseSource` comment to `// Per Verse ordinal: its block in `parsed` (a question), its passage (a tag) or its verse in `handout`.`

`LoadResult` and the declarations:

```cpp
enum class LoadResult : uint8_t { Ok, Unreadable, OutOfMemory, VersesUnavailable, HandoutsUnavailable };
```

```cpp
LoadResult loadHandout(int handout, TopicCards& out);
```

- [ ] **Step 3: `loadHandout` and its message**

In `PreachingContent.cpp`, after `loadTag`:

```cpp
LoadResult loadHandout(const int handout, TopicCards& out) {
  if (handout < 0) return LoadResult::Unreadable;
  const PreachingHandouts::Status status =
      PreachingHandouts::load(static_cast<size_t>(handout), PsramJsonAllocator::passageDoc(), out.handout);
  switch (status) {
    case PreachingHandouts::Status::Ok:
      break;
    case PreachingHandouts::Status::OutOfMemory:
      return LoadResult::OutOfMemory;
    case PreachingHandouts::Status::Missing:
    case PreachingHandouts::Status::Unreadable:
    case PreachingHandouts::Status::TooNew:
      return LoadResult::HandoutsUnavailable;
  }

  out.kind = PreachingOrigin::Kind::Handout;
  out.handoutIndex = handout;
  out.header = out.handout.title;
  const auto& verses = out.handout.verses;
  out.cards.reserve(verses.size() + out.handout.headings.size());
  out.verseSource.reserve(verses.size());
  int section = -1;
  for (size_t index = 0; index < verses.size(); ++index) {
    const PreachingHandouts::Verse& verse = verses[index];
    if (verse.section != section) {
      section = verse.section;
      const std::string& heading = out.handout.headings[verse.section];
      if (!heading.empty()) out.cards.push_back({preaching::CardKind::Heading, heading, {}});
    }
    out.cards.push_back({preaching::CardKind::Verse, verse.text.view(), verse.reference});
    out.verseSource.push_back(static_cast<uint16_t>(index));
  }
  return LoadResult::Ok;
}
```

Replace `failureMessage`:

```cpp
const char* failureMessage(const LoadResult result) {
  switch (result) {
    case LoadResult::VersesUnavailable:
      return tr(STR_PREACHING_VERSES_UNREADABLE);
    case LoadResult::HandoutsUnavailable:
      return tr(STR_PREACHING_CARDS_UNREADABLE);
    default:
      return tr(STR_PREACHING_OPEN_FAILED);
  }
}
```

- [ ] **Step 4: The topic screen returns to and opens a handout verse**

In `PreachingTopicActivity.cpp`, after the includes:

```cpp
static_assert(PreachingHandouts::MAX_HREF_BYTES < ReaderEntryIntent::HREF_BYTES, "a handout href fits the intent");
static_assert(PreachingHandouts::MAX_HANDOUTS - 1 <= UINT8_MAX, "a handout index fits PreachingOrigin");
```

(add `#include <cstdint>` if the file lacks it). In `originAt`, before `return {};`:

```cpp
  if (topic->kind == PreachingOrigin::Kind::Handout) return PreachingOrigin::ofHandout(topic->handoutIndex, verse);
```

In `openBible`, replace the block from `ReaderEntryIntent intent;` through the closing `}` of the `else` with:

```cpp
  ReaderEntryIntent intent;
  const std::string* href = nullptr;
  if (topic->kind == PreachingOrigin::Kind::Question) href = &topic->parsed.blocks[source].href;
  if (topic->kind == PreachingOrigin::Kind::Handout) href = &topic->handout.verses[source].href;
  if (href) {
    [[maybe_unused]] const unsigned long started = millis();
    const bool resolves = ReaderEntryIntent::openHref(*href, originAt(verse), intent) &&
                          session.book()->resolveHrefToSpineIndex(*href) >= 0;
    LOG_DBG(MODULE, "Reference check: %lu ms", millis() - started);
    if (!resolves) {
      LOG_ERR(MODULE, "Reference not opened: \"%s\"", href->c_str());
      reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
      return;
    }
  } else {
    const PreachingPassages::Passage& passage = topic->passages[source];
    intent = ReaderEntryIntent::of(ReaderEntryIntent::Kind::OpenAt);
    intent.unit = passage.start;
    intent.spineHint = passage.spine;
    intent.origin = originAt(verse);
  }
```

- [ ] **Step 5: Build**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`
Expected: SUCCESS, no new warnings in `src/activities/preaching/` or `src/study/PreachingHandouts.cpp`. (First build in a fresh worktree takes 3–4 minutes.)

- [ ] **Step 6: Commit**

```bash
git add src/activities/preaching/PreachingOrigin.h src/activities/preaching/PreachingContent.h src/activities/preaching/PreachingContent.cpp src/activities/preaching/PreachingTopicActivity.cpp
git commit -m "feat(preaching): open a preaching card as verse cards"
```

---

### Task 8: The Tarjetas tab

**Files:**
- Modify: `src/activities/preaching/PreachingSession.h`, `PreachingSession.cpp`
- Modify: `src/activities/preaching/PreachingActivity.h`, `PreachingActivity.cpp`

- [ ] **Step 1: The session holds the summaries**

`PreachingSession.h`: add `#include "study/PreachingHandouts.h"`; add "the handouts" to the class comment's list of what is shared (`...; the picked topics; the handouts; and the Mis temas counts once made.`). Public, after `count(...)`:

```cpp
  // Read the first time Tarjetas shows; held until Predicación exits.
  void ensureHandouts();
  PreachingHandouts::Status handoutsStatus() const { return handoutStatus; }
  const std::vector<PreachingHandouts::Summary>& handouts() const { return handoutSummaries; }
```

Private, after `counts`:

```cpp
  bool handoutsLoaded = false;
  PreachingHandouts::Status handoutStatus = PreachingHandouts::Status::Missing;
  std::vector<PreachingHandouts::Summary> handoutSummaries;
```

`PreachingSession.cpp`, at the end:

```cpp
void PreachingSession::ensureHandouts() {
  if (handoutsLoaded) return;
  handoutsLoaded = true;
  handoutStatus = PreachingHandouts::summaries(PsramJsonAllocator::json(), handoutSummaries);
}
```

- [ ] **Step 2: The activity's declarations**

`PreachingActivity.h`: class comment becomes `// Predicación: the owner's printed cards (Tarjetas), the Bible's twenty preaching questions (Preguntas) and the owner's picked tags (Mis temas). Opening one pushes PreachingTopicActivity. ...` (rest unchanged).

```cpp
  enum class Tab : uint8_t { Handouts, Questions, Topics, Count };
```

```cpp
  Tab tab = Tab::Handouts;
```

After `openTag`:

```cpp
  bool openHandout(int handout, int verse);
```

- [ ] **Step 3: Enter on the right tab**

In `PreachingActivity::onEnter`, replace the `if (origin.kind == PreachingOrigin::Kind::Tag) { ... }` block's surroundings so the tab is chosen per origin and Tarjetas is read before the first rows:

```cpp
  if (origin.kind == PreachingOrigin::Kind::Tag) {
    // Only the picks: the counts wait until Mis temas is shown, so a return from the Bible reads
    // bible.json once, for the topic.
    session.ensureTopics();
    tab = Tab::Topics;
  } else if (origin.kind == PreachingOrigin::Kind::Question) {
    tab = Tab::Questions;
  }
  if (tab == Tab::Handouts) session.ensureHandouts();
```

and extend the origin dispatch after `seedSelection();`:

```cpp
  if (origin.kind == PreachingOrigin::Kind::Question) {
    openQuestion(origin.question, origin.verse);
  } else if (origin.kind == PreachingOrigin::Kind::Handout) {
    openHandout(origin.handout, origin.verse);
  } else if (origin.kind == PreachingOrigin::Kind::Tag && !openTag(study::toTagId(origin.tag), origin.verse)) {
```

(the Tag branch's body is unchanged).

- [ ] **Step 4: Tab label, rows, selection, stepping**

```cpp
const char* PreachingActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::Handouts:
      return tr(STR_PREACHING_CARDS);
    case Tab::Questions:
      return tr(STR_PREACHING_QUESTIONS);
    default:
      return tr(STR_PREACHING_MY_TOPICS);
  }
}
```

In `rebuildRows`, after the two `clear()` calls and before `if (tab == Tab::Questions)`:

```cpp
  if (tab == Tab::Handouts) {
    const auto& handouts = session.handouts();
    rowValues.reserve(handouts.size());
    rowItems.reserve(handouts.size());
    for (const auto& handout : handouts) rowValues.push_back(std::to_string(handout.verses));
    for (size_t row = 0; row < handouts.size(); ++row) {
      fui::ListItem item;
      item.label = handouts[row].title.c_str();
      item.value = rowValues[row].c_str();
      item.actionValue = static_cast<int16_t>(row);
      rowItems.push_back(item);
    }
    return;
  }
```

In `seedSelection`, after the `Tag` branch:

```cpp
  } else if (origin.kind == PreachingOrigin::Kind::Handout) {
    row = origin.handout;
```

In `showTab`, first line:

```cpp
  if (next == Tab::Handouts) session.ensureHandouts();
```

`stepTab` cycles three tabs, honouring the direction it is given (`UiTabListActivity.cpp:70-71` passes ±1):

```cpp
void PreachingActivity::stepTab(const int direction) {
  const bool onTabBar = ringPos() == 0;
  constexpr int TAB_COUNT = static_cast<int>(Tab::Count);
  const int step = direction < 0 ? TAB_COUNT - 1 : 1;
  showTab(static_cast<Tab>((static_cast<int>(tab) + step) % TAB_COUNT));
  auto& nav = activeNav();
  nav.selected = onTabBar || listCount() == 0 ? 0 : 1;
  nav.followOnBuild = true;
  requestUpdate();
}
```

- [ ] **Step 5: The empty and unreadable notices**

In `buildScreen`, after `buildTabBar(screen);`:

```cpp
  if (tab == Tab::Handouts && session.handouts().empty()) {
    const PreachingHandouts::Status status = session.handoutsStatus();
    const bool absent = status == PreachingHandouts::Status::Missing || status == PreachingHandouts::Status::Ok;
    fui::TextStyle style = screen.theme().bodyText;
    style.align = fui::TextAlign::Center;
    style.maxLines = 3;
    const int lineHeight = screen.target().lineHeight(style.font);
    const fui::Rect notice =
        screen.takeTop(static_cast<int16_t>(3 * lineHeight), static_cast<int16_t>(metrics.verticalSpacing));
    screen.target().text(notice, absent ? tr(STR_PREACHING_NO_CARDS) : tr(STR_PREACHING_CARDS_UNREADABLE), style);
    return;
  }
```

- [ ] **Step 6: Open a handout, and come back to it**

In `activateIndex`, after `app.clearTapFlash();`:

```cpp
  if (tab == Tab::Handouts) {
    openHandout(index, NO_VERSE);
    return;
  }
```

After `openTag`:

```cpp
bool PreachingActivity::openHandout(const int handout, const int verse) {
  auto cards = makeUniqueNoThrow<TopicCards>();
  if (!cards) {
    LOG_ERR(MODULE, "OOM: handout %d", handout);
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return false;
  }
  const auto result = PreachingContent::loadHandout(handout, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportOpenFailure(PreachingContent::failureMessage(result));
    return false;
  }
  return pushTopic(std::move(cards), verse);
}
```

In `onTopicResult`, after the `Tag` branch:

```cpp
  } else if (at.kind == PreachingOrigin::Kind::Handout) {
    showTab(Tab::Handouts);
    moveRingTo(at.handout < listCount() ? at.handout + 1 : (listCount() > 0 ? 1 : 0));
  }
```

- [ ] **Step 7: Build**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`
Expected: SUCCESS, no new warnings.

- [ ] **Step 8: Commit**

```bash
git add src/activities/preaching/PreachingSession.h src/activities/preaching/PreachingSession.cpp src/activities/preaching/PreachingActivity.h src/activities/preaching/PreachingActivity.cpp
git commit -m "feat(preaching): add the Tarjetas tab, first in Predicación"
```

---

### Task 9: Whole-tree checks

- [ ] **Step 1: Host tests**

Run: `cmake --build build/test -j && ctest --test-dir build/test --output-on-failure -j`
Expected: all PASS (`ui_layout` builds `PreachingOrigin`; the new enum value must not break it).

- [ ] **Step 2: Static analysis**

Run: `/Volumes/stein/.platformio/penv/bin/pio check -e x4pro`
Expected: no new defects in the touched files. (`pio run` does not cover this.)

- [ ] **Step 3: Format the whole tree, as CI does**

Run: `./bin/clang-format-fix`
Expected: exits 0. If it changed files, review `git diff`, then:

```bash
git add -u
git commit -m "style: format the preaching cards change"
```

- [ ] **Step 4: Release build**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release`
Expected: SUCCESS; note the firmware size against the 6,553,600 B app partition.

- [ ] **Step 5: Stop**

Do not push or open a PR without the owner's approval (`CLAUDE.md`, Git rules). Report: commits, test counts, firmware size, and the device checklist below.

---

### Device checklist (the owner)

1. Flash the `x4pro` dev build (side-load `firmware.bin` via `POST /upload?path=/` with `curl -H "Expect:"`, then Settings → SD firmware update) to keep serial logs.
2. Before uploading the file: Predicación opens on **Tarjetas** with the "No cards yet" notice; Preguntas and Mis temas still work; Left/Right on the tab bar cycle all three tabs both ways.
3. Upload `rfid-cards/out/preaching-cards.json` to the SD root over File Transfer. Reopen Predicación: eight rows with verse counts. Confirm the file does not show up as a book in Biblioteca.
4. Open **Menos estrés**: verse cards, no headings. Open **La salud mental**: article headings between its verses.
5. Tap a quote → show mode; tap a reference → the Bible at that verse; left-edge swipe → back on the same card, Tarjetas selected on that row.
6. Serial: the `Handouts: N, X ms` and `Handout I: N verses, X ms` lines — note the times; free heap equal across Tarjetas → Bible → back.
7. Upload a copy with `"v":2` → Tarjetas says it can't read the file; the other tabs work.
