# Spec review 0 — issue #113

Reviewed: `docs/superpowers/specs/2026-09-26-issue-113-design.md`
Against: issue #113 (`gh issue view 113 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-26-issue-113-research.md`.

## What was verified and holds

- **Call sites.** `grep -n 'errorMessage_\|RenderLock\|state_ = '` on
  `FontDownloadActivity.cpp` shows exactly the 11 literal assignments at
  `:92, 102, 113, 120, 148, 295, 358, 370, 380, 392, 432`, with the four
  file-name sites (`:358/370/380/392`) and `:295`, `:432` each inside an existing
  `RenderLock` scope, and `:92-148` outside it, as the spec says. No other
  user-facing literal exists in the file (the remaining quoted strings are JSON
  field names, the log tag at `:24`, and `formatSize` units).
- **Render path.** `:674-681` draws `errorMessage_.c_str()` with
  `drawCenteredText` under `tr(STR_FONT_INSTALL_FAILED)`. `drawCenteredText`
  (`GfxRenderer.cpp:624-628`) centres with no clip or wrap.
- **A5.** `main.cpp:402-406` returns before `SETTINGS.loadFromFile()` and
  `I18N.setLanguage(...)` (`main.cpp:411-414`); `I18n()` initialises
  `Language::EN` (`I18n.h:33`). The SD screen can only ever be English. Correct.
- **A2, A3.** `STR_DOWNLOAD_FAILED` (`english.yaml:216`) and
  `STR_CHECKSUM_MISMATCH` (`english.yaml:460`) are as quoted; the
  `snprintf(..., tr(STR_BIBLE_SEARCH_STATUS), ...)` precedent is at
  `BibleSearchActivity.cpp:830`. `goToFullScreenMessage(std::string, ...)` is
  `ActivityManager.h:91`. Both files already include `<I18n.h>`
  (`FontDownloadActivity.cpp:6`, `main.cpp:15`).
- **Red/green, simulated.** In a scratch copy of `src/ lib/ scripts/`, rewriting
  the 12 call sites and running
  `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` exits 1 with
  `CRITICAL: 11 string(s) used in source but missing from english.yaml:` listing
  exactly the 11 spec keys. Appending the 11 English and Spanish lines exactly as
  tabled gives exit 0 and `String keys: 486` (baseline 475). With `-v`, zero
  `missing in ES` lines for the new keys. The values parse with the one-line
  regex at `gen_i18n.py:86`.
- **A1.** `.claude/agents/ui-dev.md:22-27` does name the YAML files and
  `main.cpp` as report-don't-edit append points. The issue text explicitly asks
  for those edits, the spec states the deviation and a fallback. No finding.

## Findings

### MAJOR 1 — A4's protection fails: two Spanish file-name messages overflow the panel where the English fits

**Claim.** A4: "each Spanish string is kept close to its English length … Where a
literal Spanish translation runs long, the shorter idiomatic form wins", so a
Spanish string will not "push an otherwise-fitting line off the panel".

**Problem.** For the four `%s` keys the file name is the manifest's
`files[].name`, which is the `.cpfont` file name
(`scripts/generate-font-manifest.py:177`, `"name": filepath.name`), named
`<FamilyName>_<size>.cpfont` (`generate-font-manifest.py:16`) from the families in
`lib/EpdFont/scripts/sd-fonts.yaml` (e.g. `NotoSerifExtended`, `Merriweather`,
`AtkinsonHyperlegibleNext`). With real names, two of the proposed Spanish prefixes
turn a fitting English line into a clipped one on the 480 px portrait width.

**Evidence.** Widths summed from the `advanceX` column of
`lib/EpdFont/builtinFonts/ubuntu_10_regular.h` (the `ui10FontFamily` behind
`UI_10_FONT_ID`, `main.cpp:327`; 12.4 fixed point per `EpdFontData.h:125-133`;
kerning ignored, so ±a few px):

| Message | English px | Spanish px |
|---|---|---|
| `…: Merriweather_12.cpfont`, `STR_FONT_FILE_INVALID` | 384 | **530** |
| `…: NotoSerifExtended_14.cpfont`, `STR_FONT_FILE_INVALID` | 437 | **583** |
| `…: Merriweather_12.cpfont`, `STR_FONT_FILE_CHECKSUM_MISMATCH` | 433 | **514** |

"Archivo de tipografía no válido: " is 299 px against 153 px for "Invalid font
file: "; "Suma de verificación errónea: " is 283 px against 202 px. The other two
format keys are fine ("Falló la descarga: " +2 px; "No se pudo verificar: " is
shorter than its English). All seven plain keys fit (max 394 px, "No se pudo
crear la carpeta de tipografías").

**Fix.** Shorten the two prefixes, and state the measurement in A4:
- `STR_FONT_FILE_INVALID`: `"Archivo no válido: %s"` (403 / 456 px on the two
  names above, +19 px over English).
- `STR_FONT_FILE_CHECKSUM_MISMATCH`: `"Suma no coincide: %s"` (405 / 458 px,
  shorter than English), which also matches the existing wording of
  `STR_CHECKSUM_MISMATCH` in Spanish ("La suma de verificación no coincide…",
  `spanish.yaml:402`).
Add a device-test bullet: Spanish UI, provoke one file-level error on a
long-named family, confirm the line is not clipped.

### MINOR 1 — Testing step 2's "no INFO fallback line" check is vacuous as written

**Claim.** "The run should produce no `INFO ... using English fallback` line for
Spanish on the new keys."

**Problem.** Those lines are printed only under `verbose` (`gen_i18n.py:217-222`,
`if verbose:`), and the command in step 1/2 has no `-v`, so the check always
passes.

**Evidence.** Without `-v` the green run prints only the summary block; with
`-v` it prints 4851 fallback lines for other languages and 0 for `ES` on the new
keys.

**Fix.** Make step 2
`python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ -v | grep "missing in ES"`
and require no new key in the output.

### MINOR 2 — Step 5 implies `-Wformat` checks the new `snprintf` calls; it cannot

**Claim.** "`pio run` … succeeds with no new warnings, including `-Wformat` on the
new `snprintf` calls."

**Problem.** The format argument is `tr(...)`, a runtime `const char*`
(`I18n.h:39`), so GCC's format checking does not inspect it. Specifier agreement
rests entirely on the manual check in step 4. As worded, a reader could believe
the build guards it.

**Evidence.** Same pattern already builds cleanly at `BibleSearchActivity.cpp:830`.

**Fix.** Drop the `-Wformat` clause, or say explicitly that the compiler cannot
check a translated format string and step 4 is the only guard.

### MINOR 3 — Spanish wording drifts from existing keys for the same concept

**Claim.** A7: the existing Spanish file is the style reference.

**Problem.** `STR_FONT_FILE_DOWNLOAD_FAILED` uses "Falló la descarga" while the
existing `STR_DOWNLOAD_FAILED` is "Fallo de descarga" (`spanish.yaml:207`).
"No se pudo verificar: %s" for "Failed to compute checksum" drops the checksum
sense and reads the same as a mismatch.

**Fix.** Either is acceptable to ship. If aligning: "Fallo de descarga: %s"
(check width, it is near-identical), and keep "No se pudo verificar" but note in
A7 that it is deliberately shortened. The checksum-mismatch wording is covered
by MAJOR 1.

## Verdict rationale

MAJOR 1 changes two Spanish values within the latitude A4 and A7 already grant
("unless review objects"); it reverses no decision and needs no human judgment.
The MINORs are test-plan and wording corrections. All are fixable inline.

VERDICT: CLEAR
