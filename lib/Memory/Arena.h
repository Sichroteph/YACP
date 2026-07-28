#pragma once

#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>

// Chained slab arena allocator.
//
// Allocations are O(1) bump-pointer within a slab. When a slab fills up, a new
// one is appended from the heap rather than failing immediately. clear() frees
// extra slabs and resets the first, so the initial reservation is preserved for
// reuse without causing heap fragmentation.
//
// Best suited to burst-then-discard lifetimes: parse a chapter into the arena,
// write the results out, then call clear(). The heap sees one large allocation
// and one large free instead of hundreds of small interleaved ones.
//
// NOT thread-safe. Each Arena should be owned by a single task/activity.
//
// Objects stored in an Arena must have trivial destructors, or destructors must
// be called manually before clear() / release().
//
// Example:
//   Arena arena;
//   if (!arena.init(16 * 1024)) { LOG_ERR(TAG, "OOM"); return false; }
//
//   auto* node = arenaNew<HtmlNode>(arena, arg1, arg2);
//   if (!node) { LOG_ERR(TAG, "arena OOM"); return false; }
//
//   arena.clear();   // all nodes gone, no per-object free needed
//   arena.release(); // done with the arena entirely (onExit)

struct ArenaSlab {
  ArenaSlab* next;
  size_t capacity;
  size_t offset;

  uint8_t* data() { return reinterpret_cast<uint8_t*>(this + 1); }
};

struct ArenaCheckpoint {
  ArenaSlab* slab;
  size_t offset;
};

struct Arena {
  ArenaSlab* head = nullptr;
  ArenaSlab* current = nullptr;
  size_t slabSize = 0;

  Arena() = default;
  ~Arena() { release(); }
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  // Allocate the first slab. Must be called before alloc(). Returns false on OOM.
  bool init(size_t slabBytes) {
    slabSize = slabBytes;
    head = current = allocSlab(slabBytes);
    if (!head) {
      LOG_ERR("Arena", "init failed (slab=%u bytes)", (unsigned)slabBytes);
      return false;
    }
    return true;
  }

  // Release all slabs. Arena is unusable until init() is called again.
  void release() {
    ArenaSlab* s = head;
    while (s) {
      ArenaSlab* n = s->next;
      ::free(s);
      s = n;
    }
    head = current = nullptr;
    slabSize = 0;
  }

  // Allocate `size` bytes aligned to `align` (must be a power of two).
  // Returns nullptr only when the heap itself is exhausted.
  void* alloc(size_t size, size_t align = alignof(std::max_align_t)) {
    void* p = tryAlloc(current, size, align);
    if (p) return p;

    // Grow: allocate a new slab large enough for this request.
    const size_t needed = size > slabSize ? size : slabSize;
    ArenaSlab* next = allocSlab(needed);
    if (!next) {
      LOG_ERR("Arena", "OOM growing arena (need %u bytes)", (unsigned)size);
      return nullptr;
    }
    current->next = next;
    current = next;
    return tryAlloc(current, size, align);
  }

  // Free all extra slabs and reset the first slab to empty.
  // Does NOT call destructors on any objects in the arena.
  void clear() {
    ArenaSlab* s = head ? head->next : nullptr;
    while (s) {
      ArenaSlab* n = s->next;
      ::free(s);
      s = n;
    }
    if (head) {
      head->next = nullptr;
      head->offset = 0;
    }
    current = head;
  }

  // Save current position for scratch (temporary) use.
  ArenaCheckpoint save() const { return {current, current ? current->offset : 0}; }

  // Restore to a previously saved checkpoint. Frees any slabs allocated
  // after the checkpoint slab. Must be used in LIFO order.
  void restore(const ArenaCheckpoint& cp) {
    if (!cp.slab) return;
    ArenaSlab* s = cp.slab->next;
    while (s) {
      ArenaSlab* n = s->next;
      ::free(s);
      s = n;
    }
    cp.slab->next = nullptr;
    cp.slab->offset = cp.offset;
    current = cp.slab;
  }

  // Total bytes allocated across all slabs (useful for high-water logging).
  size_t used() const {
    size_t total = 0;
    for (const ArenaSlab* s = head; s; s = s->next) total += s->offset;
    return total;
  }

 private:
  static ArenaSlab* allocSlab(size_t dataSize) {
    auto* s = static_cast<ArenaSlab*>(::malloc(sizeof(ArenaSlab) + dataSize));
    if (!s) return nullptr;
    s->next = nullptr;
    s->capacity = dataSize;
    s->offset = 0;
    return s;
  }

  static void* tryAlloc(ArenaSlab* slab, size_t size, size_t align) {
    if (!slab) return nullptr;
    const size_t aligned = (slab->offset + align - 1u) & ~(align - 1u);
    if (aligned + size > slab->capacity) return nullptr;
    slab->offset = aligned + size;
    return slab->data() + aligned;
  }
};

// Construct a T in the arena using placement new. Returns nullptr on OOM.
// align is deduced from alignof(T) automatically.
template <typename T, typename... Args>
T* arenaNew(Arena& a, Args&&... args) {
  void* mem = a.alloc(sizeof(T), alignof(T));
  if (!mem) return nullptr;
  return ::new (mem) T(std::forward<Args>(args)...);
}

// Allocate a value-initialized array of T in the arena. Returns nullptr on OOM.
template <typename T>
T* arenaNewArray(Arena& a, size_t count) {
  if (count == 0) return nullptr;
  void* mem = a.alloc(sizeof(T) * count, alignof(T));
  if (!mem) return nullptr;
  return ::new (mem) T[count]();
}
