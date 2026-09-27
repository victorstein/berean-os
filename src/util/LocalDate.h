#pragma once

#include "util/CivilDate.h"

// Today's date where the viewer is: the RTC's UTC date shifted by the clock
// offset setting. `shifted` is false when the time could not be read and `out`
// is the bare UTC date. False when the RTC has no date.
bool readLocalDate(CivilDate& out, bool& shifted);
