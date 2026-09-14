# Phase 0 baseline

Measured 2026-09-14 on `9bbad119`, before any deletion.

| | |
|---|---|
| `x4pro` dev `firmware.bin` (LOG_LEVEL=2) | **5,628,560 bytes** |
| Settings keys in `SettingsList.h` | 147 |
| Pre-existing i18n orphans | 0 |
| Host suite | see below |

The dev build is the figure that matters. The ~200-230 KB expectation was
derived by summing object contributions out of `.pio/build/x4pro/firmware.map`,
which is the dev build; comparing a release build against it mixes two
different `.rodata` log-string sets, and comparing against the *published*
`firmware-x4pro.bin` adds a third measurement from another commit entirely.

Zero pre-existing orphans is worth recording: it means any orphan the gate
reports later belongs to a deletion made in this phase, with no noise to
filter.

## Gates

```sh
python3 scripts/settings_snapshot.py | diff /tmp/settings-baseline.txt -
./scripts/i18n_orphans.sh            | diff /tmp/i18n-orphans-baseline.txt -
```

These exist because the host suite compiles nothing from `src/activities`,
`src/SettingsList.h`, `src/CrossPointSettings.*`, `src/network/html/` or
`lib/I18n` — precisely the surfaces Phase 0 deletes from. Without them the
verification triad is close to a null test.

## Result

_To be filled in by Task 14._
