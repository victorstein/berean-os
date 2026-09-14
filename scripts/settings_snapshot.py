#!/usr/bin/env python3
"""Dump the settings key set from SettingsList.h for before/after diffing.

The host suite compiles nothing from SettingsList.h or CrossPointSettings.*, so
a row deleted by accident -- or one left behind pointing at a deleted store --
is invisible to every other gate in this phase.
"""
import re
from pathlib import Path

src = Path(__file__).resolve().parent.parent / "src" / "SettingsList.h"
keys = sorted(set(re.findall(r"StrId::(STR_[A-Z0-9_]+)", src.read_text(encoding="utf-8"))))
print("\n".join(keys))
