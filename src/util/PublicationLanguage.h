#pragma once

#include <I18nKeys.h>

#include <cstdint>

#include "CrossPointSettings.h"

// The name of the language publications are downloaded in, which can differ from
// the interface language the rest of the screen is written in.
inline StrId publicationLanguageNameId(const uint8_t language) {
  return language == CrossPointSettings::PUB_LANG_ENGLISH ? StrId::STR_LANG_ENGLISH : StrId::STR_LANG_SPANISH;
}
