#pragma once

// Positions the reader returns to when Back is pressed after following a
// citation. The ring lives here, free of firmware includes, so the wrap
// arithmetic can be tested on the host: reproducing a wrap on the device means
// following citations until the ring turns over and noticing which of them Back
// lands on, and an index-by-count read of a wrapped ring is off by one.
struct SavedPosition {
  int spineIndex;
  int pageNumber;
};

class ReturnStack {
 public:
  // The left-edge swipe is this device's only Return and goes inert once the
  // ring empties, so an evicted entry strands the reader mid-chain rather than
  // costing one step. At 8 bytes a slot, buying the boundary well past any
  // chain a study session walks is the cheap side of that trade.
  static constexpr int CAPACITY = 16;

  // At capacity the oldest entry is evicted, trading the article origin for
  // every individual Back being one correct step back.
  void push(const SavedPosition p) {
    slots_[top_] = p;
    top_ = (top_ + 1) % CAPACITY;
    if (count_ < CAPACITY) count_++;
  }

  bool pop(SavedPosition& out) {
    if (count_ == 0) return false;
    top_ = (top_ + CAPACITY - 1) % CAPACITY;
    count_--;
    out = slots_[top_];
    return true;
  }

  // Undoes a push for a caller that only discovers the navigation failed after
  // pushing. Unlike pop it cannot restore an entry the push evicted.
  void unpush() {
    top_ = (top_ + CAPACITY - 1) % CAPACITY;
    if (count_ > 0) count_--;
  }

  void clear() {
    top_ = 0;
    count_ = 0;
  }

  int count() const { return count_; }

  // Physically-oldest retained entry, which is not slots_[0] once the ring has
  // wrapped.
  const SavedPosition* oldest() const { return count_ == 0 ? nullptr : &slots_[(top_ - count_ + CAPACITY) % CAPACITY]; }

 private:
  SavedPosition slots_[CAPACITY] = {};
  int top_ = 0;
  int count_ = 0;
};

static_assert(sizeof(SavedPosition) == 8,
              "ReturnStack budgets CAPACITY * sizeof(SavedPosition) of internal SRAM -- widening SavedPosition "
              "(Unit addressing, 2026-09-13-berean-os-design.md:511) multiplies by CAPACITY, so decide the "
              "capacity again when this trips");
