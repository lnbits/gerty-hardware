#pragma once
#include <cstdint>

// One event per press, with debounced press/release and wrap-safe timestamps.
class TapGate {
 public:
  bool update(bool down, uint32_t now) {
    if (down != candidate_) {
      candidate_ = down;
      changedAt_ = now;
    }
    if (candidate_ != pressed_ && now - changedAt_ >= (candidate_ ? 40U : 80U)) {
      pressed_ = candidate_;
      return pressed_;
    }
    return false;
  }
 private:
  bool candidate_ = false;
  bool pressed_ = false;
  uint32_t changedAt_ = 0;
};
