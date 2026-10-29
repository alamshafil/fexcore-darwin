// SPDX-License-Identifier: MIT
#pragma once
#include <FEXCore/Utils/AllocatorHooks.h>

#include <memory>
#include <new>
#include <cstdlib>

namespace fextl {
/**
 * @brief C++ allocator class interface in to FEXCore::Allocator for memory allocations.
 */
template<typename T>
class FEXAlloc : public std::allocator<T> {
public:
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;

  FEXAlloc() noexcept {}
  template<class U>
  FEXAlloc(const FEXAlloc<U>&) noexcept {}

  inline value_type* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    auto ptr = reinterpret_cast<value_type*>(::FEXCore::Allocator::aligned_alloc(alignof(value_type), n * sizeof(value_type)));
    if (!ptr) {
#if __cpp_exceptions
      throw std::bad_alloc();
#else
      std::abort();
#endif
    }
    return ptr;
  }

  inline void deallocate(value_type* p, size_t) noexcept {
    if (p) {
      ::FEXCore::Allocator::aligned_free(p);
    }
  }

  inline bool operator==(const FEXAlloc&) const {
    return true;
  }
};
} // namespace fextl
