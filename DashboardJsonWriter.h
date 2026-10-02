#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

class DashboardJsonWriter {
 public:
  DashboardJsonWriter(char* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {
    if (buffer_ && capacity_) buffer_[0] = '\0';
    else failed_ = true;
  }

  bool appendf(const char* format, ...) {
    if (failed_ || !buffer_ || used_ >= capacity_) return false;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer_ + used_, capacity_ - used_, format, args);
    va_end(args);
    if (written < 0 || size_t(written) >= capacity_ - used_) {
      buffer_[used_] = '\0';
      failed_ = true;
      return false;
    }
    used_ += size_t(written);
    return true;
  }

  size_t size() const { return used_; }
  size_t remaining() const { return failed_ || used_ >= capacity_ ? 0 : capacity_ - used_; }
  bool failed() const { return failed_; }

 private:
  char* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
  bool failed_ = false;
};
