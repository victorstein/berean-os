# Issue #147 — CI build times

Every change pays for three firmware builds: PR CI, CI on `main` after the squash merge, and the
release build. This note records where the time went, what changed, and what was rejected. Timings
come from the GitHub Actions job and step APIs and from the raw logs, for CI push run 36294652729
and release run 36294682098, both on 2026-09-27.

## Where the time went

| Run | Wall time | Firmware step | Of which: ESP-IDF lib rebuild | Of which: firmware |
|---|---|---|---|---|
| CI `Build x4pro` | 10.6 min | 503 s | 04:35:30 → 04:40:16, **4.8 min** | 3.2 min |
| Release `build-release` | 19.1 min | 1026 s | 04:35:24 → 04:48:07, **12.7 min** | 3.8 min |

Cache restore took 80–84 s per PlatformIO job and the save took 30–56 s. `~/.platformio` is
**3.37 GB** compressed, so three entries fill most of the repository's 10 GB cache quota.

### Root cause 1: the package cache never held the rebuilt libs

`custom_sdkconfig` (`platformio.ini:94`) makes pioarduino rebuild the Arduino/ESP-IDF libs into
`~/.platformio/packages/framework-arduinoespressif32-libs`. That output was always lost:

- `cppcheck` and `Build x4pro` ran in parallel and saved **the same key**. `cppcheck` never compiles
  the libs, finishes first, and wins. CI run 36294652729:
  `cppcheck … Cache saved with key: pio-Linux-d49c…`, then
  `Build x4pro … Failed to save: Unable to reserve cache with key pio-Linux-d49c…, another job may be creating this cache.`
- Every restore therefore got pristine libs with no `sdkconfig` in the libs dir, so the platform
  took the "custom sdkconfig, no custom libs" path (`builder/frameworks/arduino.py:912`) and
  printed `*** Compile Arduino IDF libs ***`.

### Root cause 2: the platform's "already built" marker is a project-dir file

Even with the rebuilt libs restored, the rebuild would have run again.
`matching_custom_sdkconfig()` (`arduino.py:616-642`) only accepts the existing libs when
**`<project>/sdkconfig.defaults`** starts with `# TASMOTA__<md5(custom_sdkconfig + mcu)>`. The lib
build writes that file into the project directory. It is gitignored (`.gitignore:33`), so a fresh
checkout never has it, and `check_reinstall_frwrk()` (`:645-655`) reinstalls the framework and
recompiles. A local tree skips the rebuild only because the file survives between builds there.

### Root cause 3: the key moved on every release

`key: pio-…-${{ hashFiles('platformio.ini') }}`. release-please rewrites `[berean] version` in that
file on every release, so each release build missed the exact key, fell back to `restore-keys`
(pristine libs, see above), and paid a fresh 56 s upload.

### Root cause 4: `-j1` on the release build

`release-publish.yml` ran `pio run … -j1`. The flag came in with upstream CrossPoint's #2983
(`bbca4886`). Neither that commit nor its predecessors say why, and CI has built the near-identical
`x4pro` env in parallel on the same runner image on every run without trouble. Serial compilation
is why the release lib rebuild took 12.7 min against CI's 4.8.

## Changes

| Change | Where | Mechanism |
|---|---|---|
| One composite action for the PlatformIO setup | `.github/actions/setup-platformio/action.yml` | Replaces three copies. `save-cache` input decides between `actions/cache` and `actions/cache/restore`. |
| Only firmware-compiling jobs save the cache | `ci.yml` `cppcheck` restores only | The build job now owns the key, so the saved cache holds the rebuilt libs. |
| `sdkconfig.defaults` cached next to `~/.platformio` | composite action `path:` | The platform's own hash check (`arduino.py:616`) accepts the restored libs. If `custom_sdkconfig` changes, the hash mismatches and the platform rebuilds, as before. |
| Key ignores the release-please version block | composite action, `sed '/x-release-please-start-version/,/x-release-please-end/d' platformio.ini \| sha256sum` | Checked locally: a version bump leaves the key at `ab88b0a4b642c304`; a `custom_sdkconfig` edit moves it. The release build hits the key that the merge build on `main` just saved. New `pio-v2-` prefix, so no pristine `pio-Linux-*` entry is ever restored as a fallback. |
| `-j1` removed | `release-publish.yml` | Parallel compile. Affects only a cold cache or the firmware compile. |
| Release asserts the compiled version | `release-publish.yml`, "Verify compiled version matches the tag" | `strings firmware.bin \| grep -qxF "${TAG#v}"`. Checked against the shipped v1.16.9 asset, which contains `1.16.9` as an exact string. It guards the one property cache reuse could conceivably break. |
| Superseded PR runs cancelled | `ci.yml` `concurrency` | Only for `pull_request`. Push runs on `main` are keyed by SHA and never cancelled. |

The `x4pro` and `x4pro-gh_release` envs share `custom_sdkconfig` and the MCU, so the platform's hash
treats their libs as interchangeable. That is the same sharing every local `~/.platformio` already
relies on when switching envs.

## Rejected or deferred

- **Skipping push-to-main CI.** `main` has no branch protection and no rulesets
  (`gh api …/branches/main/protection` → 404; `…/rulesets` → empty). Nothing requires a PR branch
  to be up to date before merging, so the push run is the only check on the merged tree. It is also
  the only run that writes a `main`-scoped cache, which PRs and the release can read (PR-scoped
  caches are visible only to that PR). Kept. With a warm cache it should cost about what the
  firmware compile costs.
- **PlatformIO's object cache (`build_cache_dir = .cache`, `platformio.ini:3`).** Every object
  would miss. Dev builds put the branch and SHA into the global `BEREAN_VERSION` define
  (`scripts/git_branch.py`), and the release env puts the version there (`platformio.ini:189`), so
  every translation unit's flags change on every commit. Moving the version into a single TU
  would fix that, but it touches the OTA identity path for a ~3 min saving. Deferred.
- **Reusing the merge build for the release, patching the version in afterwards.** The version
  lands only in release-please's own commit, after every PR build. Rewriting a string in the
  image means recomputing the ESP-IDF app image's checksum byte and appended SHA-256, and keeping
  it the same length as the old string. A mistake there bricks OTA silently or makes the device
  re-offer the update forever. With the release compile down to the firmware-only ~3–4 min, the
  saving no longer justifies that risk. Rejected.
- **Trimming the 3.4 GB cache.** `toolchain-riscv32-esp` alone is 2.3 GB locally. pioarduino
  installs it for the S3's RISC-V ULP, and removing it means fighting the platform's package
  resolution. Deferred.

## Caveat

The release job loads `./.github/actions/setup-platformio` from the checked-out tag. A
`workflow_dispatch` re-run for a tag cut before this change would fail at that step. Every tag cut
after the merge carries the action.

## Before and after

| Run | Before | After |
|---|---|---|
| PR CI, cache warm | 10.5–11.5 min | _pending first warm run_ |
| Release publish | 19.5–20 min | _pending first release_ |
