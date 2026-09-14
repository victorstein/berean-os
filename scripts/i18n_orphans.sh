#!/usr/bin/env bash
# Lists STR_* keys defined in english.yaml but no longer referenced in code.
#
# gen_i18n.py builds the key list from English only and strips unused keys at
# build time, so an orphan costs no flash -- but it is the signature of a
# half-finished deletion, and nothing else in the Phase 0 gate set can see one.
set -euo pipefail
cd "$(dirname "$0")/.."
grep -o '^STR_[A-Z0-9_]*' lib/I18n/translations/english.yaml | sort -u | while read -r key; do
  if ! grep -rq "\b${key}\b" src lib --include=*.cpp --include=*.h; then
    echo "orphan: $key"
  fi
done
