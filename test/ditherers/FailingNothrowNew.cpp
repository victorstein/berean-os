#include "FailingNothrowNew.h"

#include <cstdlib>
#include <new>

// Every replaced form allocates with std::malloc and frees with std::free, so no
// allocation in this binary is ever released by a mismatched function.
// makeUniqueNoThrow uses the scalar nothrow form for T and the array form for
// T[] (lib/Memory/Memory.h), so both are injectable.

namespace {

bool armed = false;
std::size_t remaining = 0;
bool failureFired = false;

bool shouldFailThisNothrowAllocation() {
  if (!armed || remaining == 0) return false;
  if (--remaining > 0) return false;
  failureFired = true;
  return true;
}

void* allocateOrThrow(std::size_t size) {
  void* p = std::malloc(size == 0 ? 1 : size);
  if (!p) throw std::bad_alloc();
  return p;
}

void* allocateOrNull(std::size_t size) noexcept {
  if (shouldFailThisNothrowAllocation()) return nullptr;
  return std::malloc(size == 0 ? 1 : size);
}

}  // namespace

namespace failing_new {

ScopedNthNothrowFailure::ScopedNthNothrowFailure(const std::size_t n) {
  armed = true;
  remaining = n;
  failureFired = false;
}

ScopedNthNothrowFailure::~ScopedNthNothrowFailure() {
  armed = false;
  remaining = 0;
}

bool ScopedNthNothrowFailure::fired() const { return failureFired; }

}  // namespace failing_new

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocateOrNull(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocateOrNull(size); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
