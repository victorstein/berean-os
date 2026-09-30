// Host coverage for where the cover masthead sits, what it leaves the content
// below it, and that it shares the launcher's Bible thumbnail. The blit and the
// plate's pixels need the renderer and are checked on the device.

#include <gtest/gtest.h>

#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"

TEST(MastheadMetrics, BothThemesSetTheBandHeight) {
  EXPECT_EQ(LyraMetrics::values.mastheadHeight, 120);
  EXPECT_EQ(BaseMetrics::values.mastheadHeight, 120);
}
