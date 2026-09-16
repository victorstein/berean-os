# Show page as QR — investigation

Investigated 2026-09-16 on `ded48d17`, the tip of `main`, for issue #38.

**Verdict: remove it, but not for the reason the issue gives, and not by the
acceptance criteria the issue writes.** Two of the four criteria cannot be met.
The feature is more broken than reported, and the dependency the issue wants to
drop is load-bearing for a different screen.

---

## 1. `QrUtils` has three other callers. The issue says it has none.

This is the finding that changes the shape of the work. The issue asserts
"nothing else uses it; `drawQrCode` has no other caller" and asks the
implementer to verify rather than trust it. Verified — it is false.

| Caller | Payload |
| --- | --- |
| `src/activities/reader/QrDisplayActivity.cpp:41` | current page text |
| `src/activities/network/CrossPointWebServerActivity.cpp:445` | `WIFI:T:nopass;S:<ssid>;;` |
| `src/activities/network/CrossPointWebServerActivity.cpp:463` | `http://berean.local/` |
| `src/activities/network/CrossPointWebServerActivity.cpp:483` | `http://<ip>/` |

`CrossPointWebServerActivity` is compiled and linked: `ActivityManager.cpp:19`
includes it and `ActivityManager.cpp:201` instantiates it inside
`goToFileTransfer()`. It is *unreachable from the UI* — `goToFileTransfer()`'s
only caller is `HomeActivity.cpp:325`, and `HomeActivity` is never instantiated
anywhere (`ActivityManager.cpp:16` includes the header and nothing constructs
the class; `LauncherActivity` took its place). But unreachable is not
unlinked. The translation unit compiles, so deleting `src/util/QrUtils.{h,cpp}`
breaks the build, and so does removing `ricmoo/QRCode` from
`platformio.ini:152`.

**Consequence for the flash argument: removing the library saves nothing.** It
stays linked for the web server either way. Whatever this change saves is
`QrDisplayActivity.{h,cpp}` (67 lines) and the handler — small, and not a
reason to do the work.

## 2. The payload is the current page, not the section.

`Section::getTextFromSectionFile()` (`lib/Epub/Epub/Section.cpp:799-817`) calls
`loadPage(currentPage)` and concatenates the words of **that one page**. The
name misleads; the issue read the name rather than the body and concluded "It
passes the entire section text". The same function backs bookmark summaries
(`EpubReaderActivity.cpp:1778`), which is only coherent at page granularity.

So "a Watchtower study article section or a Bible chapter is comfortably past
2,953 bytes" is not the mechanism. One portrait page at 12-18 pt
(`BUILTIN_READER_POINT_SIZES`, `src/ReaderFontSizes.h:16`; default 14) is on
the order of 1-3 KB, which *straddles* the cap rather than blowing past it.
The truncation is real at the small end of the font range and absent at the
large end. **"Silently truncates every real chapter" is not established.**

The removal case does not need it. See below.

## 3. The real defect is the version ladder, and it is worse than truncation.

`QrUtils.cpp:28-32` picks a QR version by payload length:

```cpp
int version = 4;
if (len > 114)  version = 10;
if (len > 395)  version = 20;
if (len > 1066) version = 30;
if (len > 2110) version = 40;
```

Those thresholds are not byte-mode capacities. Computed from the library's own
tables — `NUM_RAW_DATA_MODULES[v-1] / 8 - NUM_ERROR_CORRECTION_CODEWORDS[Low][v-1]`,
less the 4-bit mode indicator and the character-count bits from `getModeBits`
(8 bits at V4, 16 bits at V10 and above) — the true ECC_LOW byte-mode limits
are:

| Version | Modules | True byte capacity | `QrUtils` uses it up to | Over-filled by |
| --- | --- | --- | --- | --- |
| 4 | 33 | **78** | 114 | 36 |
| 10 | 57 | **271** | 395 | 124 |
| 20 | 97 | **858** | 1066 | 208 |
| 30 | 137 | **1732** | 2110 | 378 |
| 40 | 177 | **2953** | 2953 | 0 |

`MAX_QR_CAPACITY = 2953` (`QrUtils.cpp:19`) is the one constant that is right.
Every rung beneath it is wrong. 114 and 395 are exactly the V4 and V10
*alphanumeric* ECC_LOW capacities; the ladder was built from the wrong column
of a capacity table, and the comment at `QrUtils.cpp:12-15` ("very rough
estimate") admits as much. Reader text is mixed case, so
`isAlphanumeric` (`qrcode.c:126`) is false and the encoder takes the byte-mode
branch (`qrcode.c:681`) — the column that does not apply.

### Over-capacity does not fail. It overruns or it lies.

`ricmoo/QRCode @ 0.0.1`, verified against
`https://raw.githubusercontent.com/ricmoo/QRCode/master/src/qrcode.c`:

- `bb_appendBits` (`qrcode.c:213-219`) writes `data[offset >> 3] |= ...` with
  **no bounds check**. `BitBucket::capacityBytes` is stored by `bb_initBuffer`
  and never read.
- `encodeDataCodewords` (`qrcode.c:633-691`) returns `MODE_NUMERIC` (0),
  `MODE_ALPHANUMERIC` (1) or `MODE_BYTE` (2). It **never returns negative**.
- `qrcode_initBytes` (`qrcode.c:779-853`) returns `-1` only on `mode < 0`, and
  otherwise falls through to `return 0` at `qrcode.c:852`.

Therefore `qrcode_initText` **always returns 0**. The `if (res == 0)` guard at
`QrUtils.cpp:42` is always taken and the `LOG_ERR("QR", "Text too large…")` at
`QrUtils.cpp:64` is dead code — it has never fired on any device. And in
`x4pro-gh_release` (`LOG_LEVEL=0`) it could not be seen even if it did.

What happens instead, by band, with the codeword buffer being the stack VLA
`codewordBytes[bb_getBufferSizeBytes(moduleCount)]` (`qrcode.c:798`):

| Payload bytes | Version chosen | Outcome |
| --- | --- | --- |
| 100-114 | 4 | **writes up to 15 bytes past a stack VLA** |
| 344-395 | 10 | **writes up to 52 bytes past a stack VLA** |
| 859-1066 | 20 | fits the VLA; data overruns the ECC region — silently wrong code |
| 1733-2110 | 30 | fits the VLA; same silent corruption |
| 2111-2953 | 40 | correct, after truncation |

A real reader page lands in the 1733-2110 band or the 2111-2953 band. So the
user is shown either a **corrupt QR that decodes to nothing** or a **truncated
one**, with no error either way. That is the removal case, and it is stronger
than the one in the issue.

The three web-server payloads are 20-50 bytes (`berean` is the AP hostname,
`CrossPointWebServerActivity.cpp:27`; an SSID caps at 32 characters), so they
sit in the V4 band well under 78 bytes and are correct today. **Keeping
`QrUtils` for the web server does not keep a live overflow** — but the latent
one is worth recording, because it fires if any future caller passes 100+ bytes.

## 4. The reader QR renders at 2 pixels per module regardless.

Portrait is 480 x 800 logical (`GfxRenderer.cpp:1856-1868` returns `panelHeight`
for `Portrait`). `QrDisplayActivity.cpp:36` sets `availableWidth = 480 - 40 = 440`,
and `drawQrCode` uses `px = min(width, height) / qrcode.size` with integer
division (`QrUtils.cpp:44-47`). A V40 code is 177 modules, so `px = 440 / 177 = 2`;
V30 is 137 modules, so `px = 3`. This holds for every theme — the height term
never becomes the minimum.

Two device pixels per module on a 1-bit panel with no anti-aliasing is the
geometry, stated as arithmetic. Whether a phone can resolve it is a hardware
question I cannot settle here; it is listed under "not verified" below. The
web-server codes are unaffected: a 33-module V4 code in a 198 px box
(`CrossPointWebServerActivity.cpp:30-31`) is 6 px per module.

---

## What this means for the acceptance criteria

| AC | Status |
| --- | --- |
| 1. `DISPLAY_QR` row, enumerator and handler removed | **Achievable.** `EpubReaderMenuActivity.cpp:77`, `EpubReaderMenuActivity.h:26`, `EpubReaderActivity.cpp:828-839` and the include at `EpubReaderActivity.cpp:39` — all confirmed at the quoted lines. |
| 2. Delete `QrDisplayActivity.{h,cpp}` **and** `QrUtils.{h,cpp}` | **Half achievable.** `QrDisplayActivity` yes; `QrUtils` no — it has three live callers. |
| 3. Remove `ricmoo/QRCode` from `platformio.ini:152` | **Not achievable.** `QrUtils.cpp:4` includes `<qrcode.h>` and stays. |
| 4. `pio run` succeeds; flash noted against 5,318,674 B | Achievable, but expect a saving of single-digit KB, not a library's worth. |

The spec needs to decide one thing: **does the scope stay at the reader, or
does it grow to take the web-server screen with it?** Removing
`CrossPointWebServerActivity` would make AC-2 and AC-3 reachable and would
delete an unreachable ~4,000-line subsystem — but it also deletes the
documented dev side-load path (`docs/webserver.md`, `docs/webserver-endpoints.md`,
`CLAUDE.md` "Flashing"), and that is a separate decision from "remove a broken
reader menu row". Recommendation: keep this change at the reader, restate AC-2
and AC-3 accordingly, and record the web server as its own question.

## Also in scope, unmentioned by the issue

- `USER_GUIDE.md:140` documents the row and must go with it. It also describes
  the feature wrongly — "the current position as a QR code", when the payload is
  the page's text.
- `STR_DISPLAY_QR` is used at `QrDisplayActivity.cpp:34` as well as
  `EpubReaderMenuActivity.cpp:77`. Both go, so the key becomes a genuine orphan
  in the 31 translation YAMLs that define it (`finnish.yaml` never had it) — which is the separate task's problem, per the
  constraint, but that task now has a real orphan to collect rather than a
  half-used key. `STR_SCAN_QR_HINT` is untouched; it belongs to the web server
  (`CrossPointWebServerActivity.cpp:477`).
- `MenuAction` (`EpubReaderMenuActivity.h:14-31`) is an unpersisted enum class;
  removing a middle enumerator renumbers the ones after it with no on-disk
  consequence. Checked because it would have been expensive to discover late.
- `utf8SafeTruncateBuffer` (`QrUtils.cpp:23`) keeps four other callers and is
  not orphaned by any of this.

## Not verified

- **The 5,318,674 B baseline.** Inherited from the issue. This worktree is at
  `ded48d17` with no `.pio/` tree, so nothing was built during this
  investigation and no figure was reproduced.
- **The exact page-text byte count at each font size.** Bounded by geometry in
  §2, not measured. The host `pagination` suite uses a `GfxRendererFake`, so its
  glyph widths would not give a faithful number.
- **Whether a 2 px-per-module code is scannable in practice.** Hardware.
- **That the V4/V10 stack overrun is exploitable or crash-producing.** The write
  is out of bounds by construction; what it lands on is a stack-layout question,
  and no caller reaches those bands today.
