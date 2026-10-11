#pragma once

// ZipFile.h declares readFileToStream(..., Print&, ...) and relies on the real HalStorage.h to bring
// Print in. The stub HalStorage.h does not, and this suite never calls it, so a declaration is enough.
class Print;
