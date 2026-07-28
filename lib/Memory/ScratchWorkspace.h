#pragma once

#include <cstddef>
#include <cstdint>

namespace ScratchWorkspace {

class Lease final {
 public:
  Lease() = default;
  Lease(Lease&& other) noexcept;
  Lease& operator=(Lease&& other) noexcept;
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;
  ~Lease();

  explicit operator bool() const { return valid; }
  bool isValid() const { return valid; }
  size_t size() const { return capacity; }

 private:
  friend Lease acquire(size_t bytes, const char* owner);
  friend void releaseLease(Lease& lease);
  bool valid = false;
  size_t capacity = 0;
};

class Borrow final {
 public:
  Borrow() = default;
  Borrow(Borrow&& other) noexcept;
  Borrow& operator=(Borrow&& other) noexcept;
  Borrow(const Borrow&) = delete;
  Borrow& operator=(const Borrow&) = delete;
  ~Borrow();

  explicit operator bool() const { return buffer != nullptr; }
  uint8_t* data() const { return buffer; }
  size_t size() const { return capacity; }

 private:
  friend Borrow borrow(size_t minBytes, const char* owner);
  friend void releaseBorrow(Borrow& borrow);
  uint8_t* buffer = nullptr;
  size_t capacity = 0;
};

Lease acquire(size_t bytes, const char* owner);
Borrow borrow(size_t minBytes, const char* owner);
bool initialize();
void releaseLease(Lease& lease);
void releaseBorrow(Borrow& borrow);

}  // namespace ScratchWorkspace
