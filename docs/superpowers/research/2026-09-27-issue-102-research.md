# Issue #102 research — unreachable fork activities and themes

Branch `refactor/102-remove-unreachable-fork-code`, based on `e41880e3`
(`chore(main): release 1.16.9 (#143)`); `git log HEAD..origin/main` was empty
after `git fetch` on 2026-09-26.

## Headline findings

1. **Lyra is not an "extra" theme — it is the default.** `uiTheme` is
   initialised to `LYRA` (`src/CrossPointSettings.h:315`, enum at `:218`:
   `CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3`). Every device
   that has never touched the setting renders every live screen through
   `LyraTheme` and `LyraMetrics`. Deleting Lyra is a visible redesign of the
   whole UI, not dead-code removal.
2. **Lyra and RoundedRaff still drive live screens**; only Lyra3Covers is dead.
   `UITheme::setTheme` (`src/components/UITheme.cpp:30-52`) installs the theme
   object and its metrics for everything. Live consumers include
   `uiThemeTokens` (`src/components/UIThemeTokens.h:12-43`, list radius/inset/
   selection style/header underline for every FreeInkUI list) and
   `UiTabListActivity` (`UiTabListActivity.cpp:126-181`, per-theme pill and band
   treatments). `LyraTheme` overrides live methods — `fillBatteryIcon`,
   `drawHeader`, `drawTabBar`, `getListRowStep`, `drawButtonHints`,
   `drawSideButtonHints`, … (`lyra/LyraTheme.h:91-115`); RoundedRaff likewise
   (`roundedraff/RoundedRaffTheme.h:96-119`). By contrast `Lyra3CoversTheme`
   is `LyraTheme` plus two home-cover metrics (`homeCoverTileHeight`,
   `homeRecentBooksCount`) and one override, `drawRecentBookCover`
   (`lyra/Lyra3CoversTheme.h:7-22`) — all of which only `HomeActivity` reads.
   On a live screen it is pixel-identical to Lyra.
3. **Removing Lyra without changing the default would crash.** Settings load
   clamps an enum to `enumValues.size()` and falls back to the struct default
   (`src/CrossPointSettings.cpp:165-169`). With a one-entry list and a default
   of `LYRA` (1), the fallback is itself out of range, `setTheme` matches no
   case, and `currentTheme` stays null. Any variant that drops Lyra must also
   move the default.
4. **Dropping only Lyra3Covers and RoundedRaff needs no migration code.** With
   the list shortened to `{Classic, Lyra}`, a stored `2` or `3` fails the
   `< size()` clamp and loads as the default `LYRA`
   (`CrossPointSettings.cpp:169`); the web POST path rejects out-of-range
   values the same way (`src/network/CrossPointWebServer.cpp:1269-1272`). The
   enum constants can stay declared so the values keep their meaning.

Findings 1–3 contradict the issue's premise ("Three extra themes"), so the
set of themes to delete is surfaced as a decision (see end).

## What owns each item, and whether it is reachable

### `HomeActivity` — unreachable. Confirmed.

- `ActivityManager::goHome` always builds `LauncherActivity`
  (`src/activities/ActivityManager.cpp:247-255`); its `initialMenuItem`
  argument is `(void)`-discarded.
- `rg -n HomeActivity src lib test` outside its own files hits only the
  include at `ActivityManager.cpp:16` and comments
  (`BaseTheme.h:229-231`, `RoundedRaffTheme.cpp:190`,
  `lib/GfxRenderer/GfxRenderer.h:379`). `isHomeActivity()` is a virtual on
  `Activity` (`Activity.h:53`) overridden by `LauncherActivity.h:32` — it is
  not a reference to the class and stays.
- `HomeMenuItem` (`ActivityManager.h:20`) is used only by `HomeActivity` and by
  pass-through signatures (`Activity.h:70`, `Activity.cpp:13`,
  `ActivityManager.h:93`) plus one call site in `src/main.cpp:578`. Removing the
  enum means editing `main.cpp`, a shared append-point file that
  `.claude/agents/ui-dev.md` says to report, not edit. Keep it out of scope
  unless the spec decides otherwise.
- `GfxRenderer::getRegionByteSize/copyRegionToBuffer/copyBufferToRegion` are
  **not** HomeActivity-only: `SleepActivity.cpp:460-473` uses them. They stay;
  the comment at `GfxRenderer.h:379` naming HomeActivity goes stale.

### `RecentBooksActivity` — unreachable. Confirmed.

Constructed only in `ActivityManager::goToRecentBooks`
(`ActivityManager.cpp:210-212`), declared at `ActivityManager.h:87`, whose only
caller is `HomeActivity::onRecentsOpen` (`HomeActivity.cpp:322`).
`RecentBooksStore` is **not** dead: `UITheme.cpp:12` includes it and the
launcher's resume strip is out of scope here.

### `ButtonRemapActivity` — unreachable on this device. Confirmed.

- Only constructor: `SettingsActivity.cpp:311-312`, on
  `SettingAction::RemapFrontButtons` (`SettingsActivity.h:16`).
- That action row is inserted only under `!BoardConfig::hasTouch()`
  (`SettingsActivity.cpp:77-80`). `hasTouch()` reads the runtime board profile,
  `ACTIVE.touch.controller != TouchController::None`
  (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1622`).
  `ACTIVE` defaults to `DEFAULT_DEVICE` (`:1532`), which is `XTEINK_X4_PRO`
  when `FREEINK_DEVICE_X4PRO` is set (`:1519-1520`; set in both envs,
  `platformio.ini:169,187`), and that profile's touch entry is
  `TouchController::Gt911` (`BoardConfig.h:1411`). So on this build the row is
  never inserted. `SettingsList.h:453,466` gate on the same function.
- The `frontButton*` fields it edits stay: `CrossPointSettings.cpp:87,188-194`
  still load them, and `MappedInputManager.cpp:390` consumes the mapping.
  Their comments ("managed by RemapFrontButtons sub-activity") go stale.
- `MappedInputManager.cpp:390-391` comments name `ButtonRemapActivity`; stale
  after deletion. The input layer itself must not be touched (ui-dev.md).

### Theme methods — callers are dead code only. Confirmed.

`rg` over `src lib`, excluding `src/components/themes/**`:

| Method | Only non-theme caller |
|---|---|
| `drawButtonMenu` | `HomeActivity.cpp:293` |
| `drawRecentBookCover` | `HomeActivity.cpp:278` |
| `getMenuRowHeight` | `HomeActivity.cpp:233` |
| `drawList` | `ButtonRemapActivity.cpp:135` |

`drawList` and `drawButtonMenu` take `std::function` parameters
(`BaseTheme.h:235-240,252-254`); so does `drawRecentBookCover`
(`BaseTheme.h:251`). Implementations: `BaseTheme.cpp:286,569,798`,
`LyraTheme.cpp:193,397,500`, `Lyra3CoversTheme.cpp:23`,
`RoundedRaffTheme.cpp:103,185,252`. `RoundedRaffTheme.cpp:190` calls
`getMenuRowHeight` from inside its own `drawButtonMenu`, so they go together.
The metrics fields `homeCoverTileHeight`, `homeRecentBooksCount`,
`homeTopPadding`, `menuRowHeight`, `menuSpacing` need a separate check at spec
time for live readers before they are dropped from `ThemeMetrics`.

### Theme setting surface

- Options list: `SettingsList.h:273-276`, strings `STR_THEME_CLASSIC`,
  `STR_THEME_LYRA`, `STR_THEME_LYRA_EXTENDED`, `STR_THEME_ROUNDEDRAFF`.
- Live reload on change: `SettingsActivity.cpp:190-199` and `:187`.
- The web settings API lists options from `enumValues`
  (`CrossPointWebServer.cpp:1187`), so it shrinks with the list.

### `STR_CALIBRE_*` — no users. Confirmed.

`rg -n STR_CALIBRE src lib test -g '!*.yaml' -g '!I18nKeys.h' -g '!I18nStrings*'`
returns nothing. `english.yaml` holds 11:
`WIRELESS, RECEIVING, RECEIVED, INSTRUCTION_1..4, DESC, STARTING, SETUP, STATUS`.
`rg -c '^STR_CALIBRE' lib/I18n/translations` over the 32 YAMLs: 30 files × 11,
`hebrew.yaml` × 12 (an extra `STR_CALIBRE_WEB_URL`, line 52, not in English),
`orangutan.yaml` × 7 (no `INSTRUCTION_*`).

### Keys orphaned by the deletions (beyond the issue's list)

Counted per key: files under `src lib` that reference it, excluding YAML,
generated headers, and the files to be deleted. Zero live files once the three
activities and the Lyra/RoundedRaff directories are gone:

`STR_ALREADY_ASSIGNED`, `STR_BROWSE_FILES`, `STR_HW_BACK_LABEL`,
`STR_HW_CONFIRM_LABEL`, `STR_HW_LEFT_LABEL`, `STR_HW_RIGHT_LABEL`,
`STR_MENU_RECENT_BOOKS`, `STR_NO_RECENT_BOOKS`, `STR_REMAP_CANCEL_HINT`,
`STR_REMAP_PROMPT`, `STR_REMAP_RESET_HINT`, `STR_REMOVE_FROM_RECENTS`,
`STR_RESUME`, `STR_UNASSIGNED`, and `STR_REMAP_FRONT_BUTTONS` (its one other
file is `SettingsActivity.cpp:79`, which goes with the action). Theme-name keys
follow whichever themes are removed. Per the memory note
`gen-i18n-scans-comments-for-str-keys`, any of these left in a comment still
breaks `pio run`.

### `HighlightFile.h` include in `EpubReaderActivity.cpp`

`src/activities/reader/EpubReaderActivity.cpp:26` includes
`../../util/HighlightFile.h`; no other `HighlightFile` token appears in that
file. `HighlightFile::save(` has no call site (`rg -n 'HighlightFile::save\('
src lib` → nothing); the only code user of the header is
`src/study/MigrationRunner.cpp:27,241-249` (`HighlightFile::load`). Comments at
`HighlightsActivity.h:48` and `TagPickerActivity.h:29` still describe `save`.
The issue asks only for the include to go; deleting `save` itself (and
`HighlightFileAction.h`'s save-side logic, host-tested in
`test/highlight_file/`) is a scope question for the spec.

## Tests

`rg -n 'themes/|HomeActivity|RecentBooksActivity|ButtonRemap' test` → no
matches. No host test compiles any file slated for deletion, so no
`test/CMakeLists.txt` change is expected. The verification is `pio run` plus
the flash delta, as the issue states.

## Tool versions

- `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19`
- `python3 --version` → `Python 3.14.7`
- `./bin/bootstrap` → `clang-format 21 already present in .venv/bin`
- `scripts/gen_i18n.py` reports `Unused keys (N)` (`gen_i18n.py:911,946`);
  per the batch note, trust that count, not `scripts/i18n_orphans.sh`.

## Baseline flash

Not yet measured. The first pre-change `pio run -e x4pro` here never produced
output: it queued on the batch's pio lock and was lost, and afterwards
`lib/EpdFont/builtinFonts/notoserif_12_regular.h` was found truncated to 0
bytes (mtime 22:54, the build's start). No build or script step writes that
file (`rg builtinFonts platformio.ini scripts bin` hits only a kerning test
generator and the clang-format exclusion), so the cause is unexplained. It was
restored with `git checkout --`. The baseline is re-measured on this branch
before any code change, per the PR #41 method below.

## Nearest existing examples

- **PR #41** (`4a107d5c`, issue #38): deleted `QrDisplayActivity.{h,cpp}` and
  its menu entry, reported `Flash` from pio's summary line before and after
  (“5,311,502 B, was 5,318,874 — 7,372 B saved”), measured against a local
  pre-change build rather than a number in the issue. This is the model for the
  activity deletions and the PR's flash line.
- **PR #57** (`70041d90`, issue #31): `refactor: drop 22 unreferenced
  translation keys` — removed keys from all 32 YAMLs directly (22 lines per
  file). The issue names it as the precedent for the Calibre keys.
- **PR #142** (`08d98a9f`, issue #103): removed seven `STR_*` keys and gated
  settings rows; the same `SettingsList.h`/`SettingsActivity.cpp` idiom.

## Tier

The change reaches `src/SettingsList.h` and, if Lyra goes,
`src/CrossPointSettings.h`'s default — the persisted-settings surface
(`data-dev`), and it changes what every device looks like by default. Raised to
`heavy`.

## Open decision

Which themes to delete. Recommendation: delete Lyra3Covers and RoundedRaff,
keep Classic and Lyra, keep Lyra as the default, keep all four enum constants
declared. That removes the one truly dead theme plus a live-but-optional
alternative look, needs no migration code (finding 4), and does not redesign
the default UI inside a dead-code cleanup. Removing RoundedRaff does change the
look for anyone who selected it; they fall back to Lyra on next boot.
