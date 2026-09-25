//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#  include <atomic>
#  include <cassert>
#  include <memory>

#  include <crtdbg.h>

namespace oxygen::graphics::testing {
//! Short, non-overlapping test scopes; reject this thread's shared-CRT
//! allocations.
class HeapAllocationFailure final {
public:
  enum class DebugProxies { kInclude, kExcludeBySize };
  explicit HeapAllocationFailure(int allocations_before_failure = 0,
    DebugProxies proxies = DebugProxies::kInclude)
    : previous_(_CrtGetAllocHook())
  {
    assert(remaining_ == -1);
    previous_hook_.store(previous_);
    _CrtSetAllocHook(Hook);
    remaining_ = allocations_before_failure;
    exclude_proxy_size_ = proxies == DebugProxies::kExcludeBySize;
  }
  ~HeapAllocationFailure()
  {
    remaining_ = -1;
    exclude_proxy_size_ = false;
    _CrtSetAllocHook(previous_);
  }
  HeapAllocationFailure(const HeapAllocationFailure&) = delete;
  auto operator=(const HeapAllocationFailure&)
    -> HeapAllocationFailure& = delete;
  static auto RejectedCount() noexcept -> unsigned { return rejected_; }
  static auto ExcludedCount() noexcept -> unsigned { return excluded_; }

private:
  static auto __cdecl Hook(int operation, void* data, size_t size, int block,
    long request, const unsigned char* file, int line) -> int
  {
    if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC) {
      // Accepted qualification limit: MSVC's noexcept _Hash_vec constructor
      // allocates this checked-iterator proxy. Size-based exclusion necessarily
      // also excludes unrelated allocations of that size. Never enable it for
      // retirement or prepared-action commit tests.
      if (exclude_proxy_size_ && size == sizeof(std::_Container_proxy)) {
        ++excluded_;
        const auto previous = previous_hook_.load();
        return previous
          ? previous(operation, data, size, block, request, file, line)
          : 1;
      }
      if (remaining_ == 0) {
        ++rejected_;
        return 0;
      }
      if (remaining_ > 0) {
        --remaining_;
      }
    }
    const auto previous = previous_hook_.load();
    return previous
      ? previous(operation, data, size, block, request, file, line)
      : 1;
  }
  inline static thread_local int remaining_ = -1;
  inline static thread_local unsigned rejected_ = 0;
  inline static thread_local unsigned excluded_ = 0;
  inline static thread_local bool exclude_proxy_size_ = false;
  inline static std::atomic<_CRT_ALLOC_HOOK> previous_hook_ { nullptr };
  _CRT_ALLOC_HOOK previous_;
};
} // namespace oxygen::graphics::testing
#endif
