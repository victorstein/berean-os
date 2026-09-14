#include "StudyStore/PubKey.h"

#include <PathFlatten.h>

namespace study {
namespace {

constexpr const char* LOCAL_PREFIX = "local-";

// The key becomes a filename under /.berean/passages/, so anything that could
// traverse or split a path is replaced rather than rejected -- rejecting would
// lose the passage, and these components come from a network response.
std::string sanitise(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    out.push_back((c == '/' || c == '\\' || c == '.' || c == ':') ? '_' : c);
  }
  return out;
}

}  // namespace

std::string resolvePubKey(const PubKeyInputs& in) {
  if (in.isBible && in.canonVerified) return BIBLE_PUB_KEY;

  if (in.registered && !in.registered->symbol.empty()) {
    std::string key = sanitise(in.registered->symbol);
    if (!in.registered->issue.empty()) key += "-" + sanitise(in.registered->issue);
    if (!in.registered->language.empty()) key += "-" + sanitise(in.registered->language);
    return key;
  }

  return LOCAL_PREFIX + pathflatten::toCacheName(in.bookPath);
}

bool pubKeyIsStable(const std::string& key) { return key.rfind(LOCAL_PREFIX, 0) != 0; }

}  // namespace study
