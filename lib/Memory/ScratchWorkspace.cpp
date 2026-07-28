#include "ScratchWorkspace.h"

#include <Arduino.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstdlib>

namespace {
uint8_t* gBuffer = nullptr;
size_t gCapacity = 0;
bool gLeased = false;
bool gBorrowed = false;
bool gReleasePending = false;
const char* gLeaseOwner = nullptr;
const char* gBorrowOwner = nullptr;
std::atomic<SemaphoreHandle_t> gMutex{nullptr};

bool ensureMutex() {
  SemaphoreHandle_t mutex = gMutex.load(std::memory_order_acquire);
  if (mutex) return true;

  SemaphoreHandle_t created = xSemaphoreCreateMutex();
  if (!created) {
    LOG_ERR("SCR", "Failed to create scratch workspace mutex");
    return false;
  }

  mutex = nullptr;
  if (!gMutex.compare_exchange_strong(mutex, created, std::memory_order_release, std::memory_order_acquire)) {
#ifdef SIMULATOR
    delete created;
#else
    vSemaphoreDelete(created);
#endif
  }
  return true;
}

bool lock() {
  if (!ensureMutex()) return false;
  SemaphoreHandle_t mutex = gMutex.load(std::memory_order_acquire);
  return mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

void unlock() {
  SemaphoreHandle_t mutex = gMutex.load(std::memory_order_acquire);
  if (mutex) xSemaphoreGive(mutex);
}

}  // namespace

namespace ScratchWorkspace {

bool initialize() { return ensureMutex(); }

void releaseLease(Lease& lease) {
  if (!lease.valid) return;
  if (!lock()) {
    lease.valid = false;
    lease.capacity = 0;
    return;
  }

  if (gBorrowed) {
    LOG_ERR("SCR", "Deferring scratch workspace release while borrowed by %s", gBorrowOwner ? gBorrowOwner : "unknown");
    gReleasePending = true;
    lease.valid = false;
    lease.capacity = 0;
    unlock();
    return;
  }
  free(gBuffer);
  gBuffer = nullptr;
  gCapacity = 0;
  gLeased = false;
  gReleasePending = false;
  gLeaseOwner = nullptr;
  lease.valid = false;
  lease.capacity = 0;
  unlock();
}

void releaseBorrow(Borrow& borrow) {
  if (!borrow.buffer) return;
  if (lock()) {
    gBorrowed = false;
    gBorrowOwner = nullptr;
    if (gReleasePending) {
      free(gBuffer);
      gBuffer = nullptr;
      gCapacity = 0;
      gLeased = false;
      gReleasePending = false;
      gLeaseOwner = nullptr;
    }
    unlock();
  }
  borrow.buffer = nullptr;
  borrow.capacity = 0;
}

Lease::Lease(Lease&& other) noexcept : valid(other.valid), capacity(other.capacity) {
  other.valid = false;
  other.capacity = 0;
}

Lease& Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    releaseLease(*this);
    valid = other.valid;
    capacity = other.capacity;
    other.valid = false;
    other.capacity = 0;
  }
  return *this;
}

Lease::~Lease() { releaseLease(*this); }

Borrow::Borrow(Borrow&& other) noexcept : buffer(other.buffer), capacity(other.capacity) {
  other.buffer = nullptr;
  other.capacity = 0;
}

Borrow& Borrow::operator=(Borrow&& other) noexcept {
  if (this != &other) {
    releaseBorrow(*this);
    buffer = other.buffer;
    capacity = other.capacity;
    other.buffer = nullptr;
    other.capacity = 0;
  }
  return *this;
}

Borrow::~Borrow() { releaseBorrow(*this); }

Lease acquire(const size_t bytes, const char* owner) {
  Lease lease;
  if (bytes == 0 || !lock()) return lease;
  if (gLeased) {
    LOG_DBG("SCR", "Scratch workspace already leased by %s", gLeaseOwner ? gLeaseOwner : "unknown");
    unlock();
    return lease;
  }

  gBuffer = static_cast<uint8_t*>(malloc(bytes));
  if (!gBuffer) {
    LOG_ERR("SCR", "Failed to allocate scratch workspace for %s (%u bytes, free=%u, maxAlloc=%u)",
            owner ? owner : "unknown", static_cast<unsigned>(bytes), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    unlock();
    return lease;
  }

  gCapacity = bytes;
  gLeased = true;
  gBorrowed = false;
  gReleasePending = false;
  gLeaseOwner = owner;
  lease.valid = true;
  lease.capacity = bytes;
  unlock();
  return lease;
}

Borrow borrow(const size_t minBytes, const char* owner) {
  Borrow borrowed;
  if (minBytes == 0 || !lock()) return borrowed;
  if (!gLeased || gBorrowed || gCapacity < minBytes) {
    unlock();
    return borrowed;
  }

  gBorrowed = true;
  gBorrowOwner = owner;
  borrowed.buffer = gBuffer;
  borrowed.capacity = gCapacity;
  unlock();
  return borrowed;
}

}  // namespace ScratchWorkspace
