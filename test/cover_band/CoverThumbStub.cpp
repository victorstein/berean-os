// CoverBand::thumbPathFor reaches CoverThumb::pathFor, which opens EPUBs; the
// replay suite never calls it, so the link only needs a body.

#include "util/CoverThumb.h"

namespace CoverThumb {

std::string pathFor(const std::string&, int, bool&) { return {}; }

}  // namespace CoverThumb
