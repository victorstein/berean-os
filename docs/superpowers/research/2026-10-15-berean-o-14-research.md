# Research: berean-o-14 — `lib/Preaching`, the intro section and its question parser

Task `t1` of run `berean-os-20261015-predicaci-n-193x`. Spec:
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §1, §4, D2, D7. Branch
`feature/preaching-intro-parser`, cut at `eecbf1d1` (the batch branch
`origin/batch/20261015-predicaci-n-193x` is at the same commit).

## 1. Who owns this behaviour today

Nobody. `lib/Preaching/` and `test/preaching/` do not exist (`ls lib/` and `ls test/` at
`eecbf1d1`). No code names `1001061250` or "Pregunta". The intro section is reachable on the
device only as ordinary spine items through the Bible's TOC, which is the problem the spec states.

The pieces T1 touches or mirrors:

| What | Where |
|---|---|
| The chunk-sink contract T3 will feed the parser through | `src/activities/reader/SpineHtmlStream.h:19` — `using ChunkSink = bool (*)(void* ctx, const char* chunk, size_t length, bool isFinal);` (`stream` itself is `:40`, which is what the spec's `:40` cites) |
| Chunk size the parser will actually see on device | `src/activities/reader/SpineHtmlStream.cpp:14` — `HTML_CHUNK_BYTES = 2048`; the final call may be `sink(ctx, "", 0, true)` (`:66`) |
| The scanner shape the brief names | `lib/Epub/Epub/BibleNavScanner.{h,cpp}`, suite `test/bible_nav_scanner/` |
| A library laid out as `lib/<Name>/<Name>/`, included as `<Name/...>` | `lib/StudyStore/StudyStore/` (no `library.json`); `src/Place.h:3` `#include <StudyStore/Unit.h>`. `lib/BibleSearch/` has the same layout plus a two-line `library.json` |
| HTML entity table | `lib/Epub/Epub/htmlEntities.{h,cpp}`, `lookupHtmlEntity(const char*, size_t)` |
| Nothrow allocation | `lib/Memory/Memory.h` `makeUniqueNoThrow` |
| Host logging stub | `test/stubs/Logging.h` — `LOG_ERR/INF/DBG` are no-ops |

## 2. Control flow of the model, and where it falls short of `CLAUDE.md`

`BibleNav::Scanner` (`lib/Epub/Epub/BibleNavScanner.cpp`):

- Constructor (`:124-142`) allocates `State` with a bare `new (std::nothrow)` and frees it by hand
  on parser-create failure; no `LOG_ERR`. `parser_`/`state_` are `void*` so `expat.h` stays out of
  the header (`.h:65-66`).
- `feed` (`:149-158`) calls `XML_Parse` with `isFinal`; any `XML_STATUS_ERROR` latches `failed_`
  and every later `feed` returns false.
- `take`/`takeBookNav` (`:160-173`) move results out, and return empty after a failed feed —
  the "nothing partial" behaviour the brief asks for.
- Text is collected only inside tracked elements via depth counters (`linkDepth`,
  `headingDepth`, `:25-26`); whitespace runs collapse to one space (`appendCapped`, `:36-53`);
  trailing space trimmed (`trimmed`, `:55-58`).
- **No entity handling.** It sets only element and character handlers (`:137-138`).

Two things the parser needs that `BibleNavScanner` does not show, both established elsewhere:

1. **Entities under `XML_GE=0`.** `platformio.ini:38` builds expat with `-DXML_GE=0`. Expat
   then reports undeclared entities to the *default* handler, not the character handler. The
   repo's idiom is `XML_SetDefaultHandlerExpand(parser, onDefault)` with `lookupHtmlEntity`, and
   the literal run kept when the entity is unknown: `lib/Epub/Epub/VerseAnchors.cpp:46-60,77`,
   `lib/BibleSearch/BibleSearch/VerseTextScanner.cpp:191-203,332`,
   `lib/StudyStore/StudyStore/UnitText.cpp:159-179`.
2. **`makeUniqueNoThrow` + `LOG_ERR`** in a lib scanner constructor:
   `VerseTextScanner.cpp:307-336` (`state_` is a `std::unique_ptr`, `VerseTextScanner.h:71`;
   `parser_` stays `void*`, `:70`). It also records `outOfMemory_` from
   `XML_GetErrorCode(...) == XML_ERROR_NO_MEMORY` (`:348`).

So the nearest example is **`BibleNavScanner`'s shape with `VerseTextScanner`'s allocation and
entity idioms** — no new pattern is needed. Stopping expat early on a cap has precedent in
`XML_StopParser(parser, XML_FALSE)` (`lib/ProgressMapper/ChapterXPathResolver.cpp:292,497`); a
latched `failed_` flag checked on return from `XML_Parse` (as `BibleNavScanner` does) also suffices.

## 3. The real markup (measured, `~/Downloads/nwt_S.epub`)

Extracted the 21 files into the scratchpad and surveyed them with a Python ElementTree script
applying the brief's rules. `nwt_E.epub` is **not** present locally
(`ls ~/Downloads/nwt_E.epub` → No such file), so the English half of D2 rests on the spec's own
2026-10-14 check, not on this note.

**Files and sizes** (`unzip -l`, then `ls -l`): `1001061250.xhtml` 5,388 B (index);
`1001061251`–`1001061270.xhtml` 1,715–4,681 B, Q6 (`1001061256`) the largest. The OPF also lists
`1001061251-extracted.xhtml` … `1001061269-extracted.xhtml` (`content.opf`:
`<item id="chapter1408" href="1001061251-extracted.xhtml" …/>`); each is a ~480 B shell with an
empty `<div class="groupExt">`. An exact-basename match ignores them; a prefix match would not.

**TOC labels** (`toc.xhtml` and `toc.ncx`): `Pregunta 1: ¿Quién es Dios?`,
`Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?`; the index is
`Introducción a la Palabra de Dios` (no prefix — `stripTocPrefix` must return it whole).

**Structure of a question** (`1001061251.xhtml`, `1001061256.xhtml`):

```
<body …><header>
<p class="contextTtl" …><span class="pageNum" …></span><strong>PREGUNTA 1</strong></p>
<h1 …><strong>¿Quién es Dios?</strong></h1>
</header>
<div class="bodyTxt">
<div …><figure><img … alt="…"/></figure></div>
<p … class="p3 sw"><q class="scrp">“Que la gente sepa …”.</q></p>
<p … class="p4 sw"><a href="1001061123-split83.xhtml#chapter83_verse18">Salmo 83:18</a></p>
```

- `<h1>`/`<h2>`/`<h3>` text sits inside `<strong>`, so heading text must be collected through
  child elements (depth-counted, as `BibleNavScanner` does).
- `<span class="pageNum">` is **empty**; its number is in `data-before-text`. It occurs inside
  `contextTtl`, inside a quote paragraph (Q1 `p13`), and inside an `<h2>` (Q6 `p15`).
- `<figure>` holds only an `<img>` with `alt` text, no character data.
- **Entities: none.** `grep -ohE "&[a-zA-Z#0-9]+;"` over all 21 files returns nothing; quotes are
  literal UTF-8 `“ ”`. The entity path is still needed for robustness (brief: "pass through"), and
  is exercised only by synthetic fixtures.
- `[...]` appears literally inside quotes (Q6 `p4`, `p13`).

**Multi-link references** — the link texts carry their own separators and spacing:

```
<a href="1001061125-split9.xhtml#chapter9_verse5">Eclesiastés 9:5,</a><a href="…#chapter9_verse10"> 10</a>
<a href="1001061144-split26.xhtml#chapter26_verse14">Mateo 26:14, 15;</a><a href="1001061144-split27.xhtml#chapter27_verse5"> 27:5</a>
```

Joining the link texts verbatim gives "Eclesiastés 9:5, 10" and "Mateo 26:14, 15; 27:5". Per-link
trimming would break that ("9:5,10"); whitespace handling must collapse runs, not strip a link's
leading space. Not every comma means two links: `Juan 19:23, 24` (Q6 `p14`) is one `<a>`.

**Survey under the brief's rules** (title + Heading + Verse + Text, skipping `contextTtl`,
`figure`, `pageNum`):

| File | Blocks | Block text | Notes |
|---|---|---|---|
| Q6 `1001061256` | 21 | 1,434 B | 10 `<h2>`; one empty trailing `<p>` |
| Q17 `1001061267` | 10 | 1,062 B | 3 `<h2>` |
| Q19 `1001061269` | **33** | 1,656 B | 2 `<h2>` + **8 `<h3>`**; no links |
| Q20 `1001061270` | 7 | 366 B | prose, then one quote + reference |
| Index `1001061250` | 24 | 1,705 B | 20 inline `<a>` in `<li>`; not a question |

Across Q1–Q20: at most **2 links** per reference paragraph; **no** paragraph with more than one
`<q>`; **no** reference without a quote before it and **no** quote without a reference after it
(the two pairing fallbacks are for robustness only); longest href **45 B**
(`1001061123-split119.xhtml#chapter119_verse105`); largest block text 1,705 B. The 64-block and
8 KB caps hold with margin (33 and ~1.7 KB at most).

## 4. Findings that bear on the spec

1. **`<h3>` is not in the brief or spec.** Q19's ten headings are 2 `<h2>` + 8 `<h3>`
   (`grep -c "<h3"` → `1001061269.xhtml:8`; e.g. `<h3 … id="p4"><strong>EL PENTATEUCO (5 LIBROS):</strong></h3>`).
   The spec's "Q19 has ten" counts them as headings. A parser handling only `<h2>` would drop
   eight headings silently, since `<h3>` is not a `<p>` either. Treating `<h3>` as `Heading` is the
   reading consistent with the spec; the design phase should state it.
2. **Empty paragraphs.** 13 of the 21 files end with an empty `<p … class="pNN"></p>` after
   `bodyTxt` (Q6 `p33`). Emitting them would add empty `Text` blocks; the design should skip a
   paragraph whose collected text is empty.
3. **Q19 is the largest by blocks (33), not Q6 (~20).** The spec's "Q6 … about 20 blocks" is right
   for Q6 but Q19 is the closer approach to the 64-block cap.
4. **Q19 prose uses `<strong>` inside `<p>`** for the book-group lines
   (`<p …><strong>Génesis, Éxodo, …</strong></p>`). Under the brief this is a plain `Text` block;
   emphasis is lost. Not a scope change — noted for T3's layout.
5. **The device build may not compile `lib/Preaching` until T3 includes it.** `platformio.ini`
   sets no `lib_ldf_mode` (`grep -n lib_ldf_mode platformio.ini` → nothing), so PlatformIO's
   default dependency finder applies, which builds a `lib/` folder only when `src/` reaches it by
   `#include`. T1 has no `src/` code. Precedent: `lib/StudyStore` landed in `121e1e2f` with
   `Unit.{h,cpp}` and its host test and no `src/` file (`git show --stat 121e1e2f`). The
   acceptance line "builds into `pio run -e x4pro`" will therefore be checked as "the build stays
   green with no new warnings"; the library's own warnings are covered by the host suite, which
   compiles it with `-Wall -Wextra -pedantic` (`test/CMakeLists.txt:42-46`). The implement phase
   should confirm from the build log whether a `Preaching` library was compiled.
6. **`-Wall` reaches `lib/Preaching` once it is compiled**: `scripts/repo_warnings.py` treats
   every `lib/<name>` except `expat`, `miniz`, `uzlib` as repo source.

None of these contradicts D1–D9. Findings 1 and 2 are interpretation the spec phase records; none
reaches another surface, changes a contract or needs a migration, so the tier stays `standard`.

## 5. Host test wiring (the suite to mirror)

`test/bible_nav_scanner/CMakeLists.txt:4-25` compiles the scanner `.cpp` plus
`lib/expat/xmlparse.c`, `xmlrole.c`, `xmltok.c` directly, defines `XML_GE=0 XML_CONTEXT_BYTES=1024`
to match the firmware, and links `crosspoint_test_common` (`REPO_ROOT` and `REPO_ROOT/lib` on the
include path, `-Wall -Wextra -pedantic`) and `GTest::gtest_main`. A suite that uses
`lookupHtmlEntity`, `makeUniqueNoThrow` and `Logging.h` adds `lib/Epub/Epub/htmlEntities.cpp`,
`lib/Memory` and `test/stubs` (`test/bible_search_scanner/CMakeLists.txt:4-21`). Chunk-boundary
tests already exist in this shape (`scanInChunks`, `test/bible_nav_scanner/BibleNavScannerTest.cpp:57`).

`test/CMakeLists.txt` lists suites with `add_subdirectory(...)` (`:51-147`); T1 adds
`add_subdirectory(preaching)` (the brief assigns this one line to T1). Runner, from `test/README`:

```
cmake -S test -B build/test
cmake --build build/test --target <Target>
ctest --test-dir build/test --output-on-failure -j
```

## 6. Installed versions

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| ESP32 platform | pioarduino 55.03.37 | `platformio.ini:15` |
| expat (vendored) | 2.7.3 | `lib/expat/library.json:3`; `expat.h:1023-1025` |
| GoogleTest | v1.17.0 (FetchContent) | `test/CMakeLists.txt:17` |
| CMake | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 | `c++ --version` |
| clang-format | 21, symlinked from the primary checkout | `bin/bootstrap` (`REQUIRED_CLANG_FORMAT_MAJOR=21`); `.venv/bin/clang-format` → primary `.venv` |
| C++ standard | gnu++2a on device, C++20 on host | `platformio.ini:40`; `test/CMakeLists.txt:4` |

Bootstrap has run in this worktree: `freeink-sdk/libs` is populated and `.venv/bin/clang-format`
exists. No `.pio/` or `build/` yet.
