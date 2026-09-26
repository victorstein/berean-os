// Link-time body for the one obfuscation symbol PersistableStore.cpp references
// (extractPassword). The real ObfuscationUtils.cpp needs <esp_mac.h> and mbedtls.
// No host suite decodes a password, so this always reports a failed decode.

#include <ObfuscationUtils.h>

namespace obfuscation {

std::string deobfuscateFromBase64(const char*, size_t, bool* ok, bool* tooLong) {
  if (ok) *ok = false;
  if (tooLong) *tooLong = false;
  return "";
}

}  // namespace obfuscation
