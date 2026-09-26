# Issue #113 research: hardcoded text in FontDownloadActivity and the SD-card error screen

Baseline: `HEAD` = `origin/main` = `2f303f6f` (chore(main): release 1.16.0).

## 1. Files that own the behaviour

| File | Role |
|---|---|
| `src/activities/settings/FontDownloadActivity.cpp` | Sets `errorMessage_` to English literals and draws it on the ERROR screen |
| `src/activities/settings/FontDownloadActivity.h:83` | `std::string errorMessage_;` |
| `src/main.cpp:405` | `activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);` |
| `src/activities/ActivityManager.cpp:243-245` | `goToFullScreenMessage(std::string, Style)` moves the string into a `FullScreenMessageActivity` |
| `lib/I18n/I18n.h:39` | `#define tr(id) I18n::getInstance().get(StrId::id)` returns a `const char*` |
| `lib/I18n/translations/english.yaml`, `spanish.yaml` | Where new keys go |
| `scripts/gen_i18n.py` | Generates `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`. It also runs as a PlatformIO `pre:` step (`platformio.ini:131`) |

The only entry to the font browser is `SettingsActivity.cpp:337`, which calls `startActivityForResult(std::make_unique<FontDownloadActivity>(...))`.

## 2. Hardcoded strings in FontDownloadActivity: the full list

`grep -n '"[A-Za-z][^"]*"' src/activities/settings/FontDownloadActivity.cpp`, with `LOG_*` lines and JSON field names (`doc["version"]` and so on) left out:

| Line | Literal | Listed in the issue? |
|---|---|---|
| 92 | `"Failed to fetch font list"` | yes |
| 102 | `"Failed to read font list"` | yes |
| 113 | `"Invalid font manifest"` | yes |
| 120 | `"Unsupported manifest version"` | yes |
| 148 | `"Invalid font manifest"` (the same text as 113) | yes |
| 295 | `"Failed to create font directory"` | yes |
| 358 | `"Download failed: " + file.name` | **no** |
| 370 | `"Failed to compute checksum: " + file.name` | **no** |
| 380 | `"Checksum mismatch: " + file.name` | **no** |
| 392 | `"Invalid font file: " + file.name` | **no** |
| 432 | `"Failed to delete font"` | yes |

The issue lists 7 lines. The file has 4 more hardcoded, user-facing messages, each built by concatenating a label with a manifest file name. They reach the screen by the same path, so a fix that stops at the issue's 7 lines still leaves CLAUDE.md's `tr()` rule broken in this file. Line 24 (`"FontDownload"`) is the activity's log name, not UI text.

## 3. Current control flow

### Font download errors

1. A failure path sets `state_ = ERROR` and assigns `errorMessage_`. The download path does this under `RenderLock lock(*this)`: see 293-295, 356-358, 368-370, 378-380 and 390-392. `onDeleteConfirmationResult` does it at 429-432. `fetchAndParseManifest()` (84-184) assigns `errorMessage_` and returns `false`, and its caller sets the state.
2. The ERROR branch of the render (`FontDownloadActivity.cpp:674-681`) draws `tr(STR_FONT_INSTALL_FAILED)` in bold as the heading. When `errorMessage_` is non-empty, it then draws that string with `renderer.drawCenteredText(UI_10_FONT_ID, ..., errorMessage_.c_str())`. So the heading is already translated and only the detail line is English.
3. `drawCenteredText` (`lib/GfxRenderer/GfxRenderer.h:280`) takes a `const char*` and draws one line. It does not wrap, so a longer translated string can run past the screen width.

Assigning a `tr()` result to `errorMessage_` compiles unchanged, because `std::string` accepts a `const char*`. The file already mixes `tr()` with dynamic text through `std::string(tr(STR_X)) + ...` at `:524`, `:527` and `:653`. `:653` builds `tr(STR_DOWNLOADING) + " " + family.name + " (i/n)"`.

### SD-card error

`src/main.cpp:402-406`:

```cpp
if (!Storage.begin()) {
  LOG_ERR("MAIN", "SD card initialization failed");
  setupDisplayAndFonts(isSilentReboot);
  activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
  return;
}
```

The language is not set until `main.cpp:414`, `I18N.setLanguage(static_cast<Language>(SETTINGS.language));`, and that runs only after `SETTINGS.loadFromFile()` has read the settings from the SD card. The SD-error branch returns before either line runs. `I18n`'s constructor defaults to `Language::EN` (`lib/I18n/I18n.h:33`), so `tr(STR_…)` at line 405 always resolves to the English string. The user's language lives on the card that just failed to mount, so it cannot be known here. Routing this line through `tr()` meets the CLAUDE.md rule and makes the key translatable, but the user will still see English at this point. The spec should say this outright rather than imply a Spanish SD error screen.

`goToFullScreenMessage` is called from only one place: `grep -rn 'goToFullScreenMessage(' src` finds `main.cpp:405` plus the declaration and definition.

## 4. Existing keys that might be reused

The counts come from `grep -l "^KEY:" lib/I18n/translations/*.yaml | wc -l`.

| Key | English | Files that define it | Used at |
|---|---|---|---|
| `STR_DOWNLOAD_FAILED` | "Download failed" | 31 | `PublicationDownloader.cpp:254`, `MeetingDownloadActivity.cpp:223`, `CatalogSearchActivity.cpp:474` |
| `STR_CHECKSUM_MISMATCH` | "Checksum mismatch, download discarded" | 2 (en, es) | `PublicationDownloader.cpp:251` |
| `STR_FONT_INSTALL_FAILED` | "Font installation failed" | 27 | the heading at `FontDownloadActivity.cpp:675` |
| `STR_SD_CARD` | "SD card" | 32 | — |
| `STR_LOADING_FONT_LIST` | "Loading font list..." | 28 | the font browser |

No `STR_SD_CARD_ERROR` key exists, and no manifest, font-directory or font-delete error key exists either (`grep -n 'FAILED\|ERROR\|CHECKSUM\|MANIFEST\|DELETE\|INVALID' english.yaml`).

The Spanish file already uses "tipografía" for "font": `spanish.yaml:340,342,343`. New Spanish strings should use the same word.

## 5. Format-string keys

Keys that take arguments are `printf` format strings, filled at the call site with `snprintf` into a fixed `char[]`:

- `english.yaml:39`: `STR_BIBLE_SEARCH_STATUS: "%s · chapter %d"`, used at `BibleSearchActivity.cpp:830` as `snprintf(statusLine, sizeof(statusLine), tr(STR_BIBLE_SEARCH_STATUS), name, chapter);`
- `english.yaml:10`: `STR_BIBLE_CHAPTERS_READ: "%u of %u chapters read"`
- `english.yaml:348`: `STR_SLEEP_TIMER_VALUE_FORMAT: "%u min"`

The four file-name messages in section 2 can follow either this pattern (a `"...: %s"` key filled with `snprintf`) or the concatenation pattern this file already uses at `:653`. The `%s` form lets a translator move the file name. It also puts a runtime-format `snprintf` on a path that is not hot, and CLAUDE.md's string policy prefers `snprintf` into a `char[]` over `std::string` construction.

## 6. Tools and versions installed

| Tool | Command | Output |
|---|---|---|
| Python | `python3 --version` | `Python 3.14.7` |
| PlatformIO | `~/.platformio/penv/bin/pio --version` | `PlatformIO Core, version 6.1.19` (`pio` is not on `PATH`) |
| gen_i18n | `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` | `Languages: 32`, `String keys: 475`, `Used in code: 463`, `Never used: 12`, `Flash: 229,766 B strings (deduped) + 30,400 B offset tables = 260,166 B`. `git status` stays clean because the outputs are gitignored |

`gen_i18n.py` behaviour that matters here:
- It scans `src/` and `lib/` with `\bSTR_[A-Za-z0-9_]+\b` (`scripts/gen_i18n.py:267`), comments included. A `STR_` name used in source but missing from `english.yaml` fails the script with `CRITICAL ... missing from english.yaml` and `sys.exit(1)` (`:872-881`), and that fails `pio run`.
- A key missing from a non-English file falls back to English and logs an `INFO` line (`:217-222`). Adding the keys to only `english.yaml` and `spanish.yaml` is therefore legal. The other 30 languages will show English.
- It parses YAML with its own regex (`:86`, `^KEY:\s*"(.*)"$`). Values must be double-quoted on one line, and `\"`, `\\` and `\n` are the only escapes.

## 7. Tests

There is no host test for FontDownloadActivity or for I18n string lookup. `ls test/` shows no `font_download` or `i18n` suite. `test/font_page_slots/` covers the SD font page cache, not this activity. `test/CMakeLists.txt` has no I18n target (`grep -n 'I18n' test/CMakeLists.txt` finds nothing). Host tests build without Arduino or HalStorage (CLAUDE.md), and this activity depends on both.

For this change, TDD's failing test is most practically a build gate: add a `tr(STR_NEW_KEY)` call site first, run `gen_i18n.py` or `pio run`, see the `CRITICAL ... missing from english.yaml` failure, then add the YAML key. A mechanical check can confirm that no literal is left, for example `grep -n 'errorMessage_ = "' src/activities/settings/FontDownloadActivity.cpp` returning nothing. Whether a new host test is wanted is a spec decision.

## 8. The nearest existing examples

- **Adding new UI keys to en and es, with call sites switched to `tr()`:** `32f66774` "fix: surface store save refusals on screen (#82)". It added `STR_BOOKMARKS_TOO_LARGE` and similar keys and routed the on-screen messages through `tr()`.
- **A key-only i18n change and how its PR was verified:** `02d9106a` "fix: label the launcher's fourth tile Settings (#53)". Its PR body is the pattern for evidence: `pio run` result, host suite count, full-tree `clang-format-fix`, and a tree-wide `grep` for the key names, comments included.
- **Error text assigned from `tr()` inside a download error path:** `CatalogSearchActivity.cpp:474` (`errorMessage = message ? message : tr(STR_DOWNLOAD_FAILED);`) and `MeetingDownloadActivity.cpp:223` (`fail(tr(STR_DOWNLOAD_FAILED));`).

## 9. A process constraint the spec must settle

`.claude/agents/ui-dev.md` ("Shared files — report, do not edit") names `lib/I18n/translations/*.yaml` and `src/main.cpp` as shared append points. Its rule is to put the exact line in the PR description and let the orchestrator apply it. Issue #113 needs edits in both places: new YAML keys and the `main.cpp:405` call site. This task owns the issue alone in its own worktree, so the likely reading is that it may edit them. The spec phase has to settle that, or raise it as a decision, before any code is written.
