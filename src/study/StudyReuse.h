#pragma once

// Whether a reader may keep the study state StudyStore already holds instead of opening it again.
// Pure, so test/parked_book exercises it without StudyStore's Epub and SD dependencies.
namespace study_reuse {

struct Inputs {
  bool openAndReady = false;   // StudyStore is open and its unit index is ready
  bool sameBook = false;       // it was opened for this very Epub object
  bool loadedCleanly = false;  // that open read the palette and the passages without a Failed result
  bool keyStable = false;      // study::pubKeyIsStable(pubKey)
};

// `keyStillResolves` re-resolves the pubkey against the registry and says whether it is unchanged.
// It is asked only for an unstable (local-) key: a registry write at Home can turn that into a
// registered key, and tags saved under the old one would be lost to every later open.
inline bool mayReuse(const Inputs& in, bool (*keyStillResolves)(void* ctx), void* ctx) {
  if (!in.openAndReady || !in.sameBook || !in.loadedCleanly) return false;
  return in.keyStable || keyStillResolves(ctx);
}

}  // namespace study_reuse
