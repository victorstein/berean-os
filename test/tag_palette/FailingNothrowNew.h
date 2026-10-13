#pragma once

#include <cstddef>

// Failure injection for the nothrow allocation forms, confined to this suite.
// FailingNothrowNew.cpp replaces the global allocation functions for this
// executable only; it is never linked into another suite.
namespace failing_new {

// Fails the Nth nothrow allocation (1-based) made while the guard is alive,
// then lets every later one succeed. Disarms on destruction, so gtest's own
// allocations outside the guard are never failed.
class ScopedNthNothrowFailure {
 public:
  explicit ScopedNthNothrowFailure(std::size_t n);
  ~ScopedNthNothrowFailure();
  ScopedNthNothrowFailure(const ScopedNthNothrowFailure&) = delete;
  ScopedNthNothrowFailure& operator=(const ScopedNthNothrowFailure&) = delete;

  bool fired() const;
};

}  // namespace failing_new
