## Summary

* **What does this change, and why?**
* **What did you verify, and how?**

## Scope check

bereanOS is a study device, not a general e-reader. See [SCOPE.md](https://github.com/victorstein/berean-os/blob/main/SCOPE.md) and
[ROADMAP.md](https://github.com/victorstein/berean-os/blob/main/ROADMAP.md).

- [ ] I have read SCOPE.md and ROADMAP.md.
- [ ] This is not notes, JW Library interop, on-device full-text search, or a return of the general
      e-reader features the fork removed.
- [ ] This is not an interactive app, an active-connectivity feature, or PDF rendering.
- [ ] This does not add support for a board other than the Xteink X4 Pro.
- [ ] If it belongs to a later phase, I say below why it is landing now.

## Verification

- [ ] `./bin/clang-format-fix` over the whole tree (what CI checks; `-g` misses new files).
- [ ] `pio run -e x4pro` builds, and `pio check` passes.
- [ ] The host suite under `test/` passes.
- [ ] Tested on hardware, or marked clearly as untested.

If this touches a persisted format — settings, the highlight store, a cache file — say which format
version moved and what an older build does when it meets the new file.

If it touches `freeink-sdk/`, `lib/hal/`, OTA, or the input layer, say so explicitly: those are the
places a mistake is not recoverable from the device.

## Cost

* Memory, flash, or extra panel refreshes, if known.
* Anything a reviewer should look at first.

---

### AI usage

No restriction on AI tools here, but please say how they were used — it sets the right context for
review.

Did you use AI tools to help write this code? _**< YES | PARTIALLY | NO >**_
