//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once
#if defined(_MSC_VER) && defined(_DEBUG)
#  include <atomic>
#  include <cassert>
#  include <cstdint>

#  include <crtdbg.h>
namespace oxygen::vortex::testing {
//! Diagnostic allocation counts across the shared Debug CRT, on the calling
//! render thread. Includes checked-iterator allocations; not Release heap
//! totals.
class CpuAllocationCounter final {
public:
  CpuAllocationCounter()
    : previous_(_CrtGetAllocHook())
  {
    assert(current_ == nullptr);
    previous_hook_.store(previous_);
    _CrtSetAllocHook(Hook);
    current_ = this;
  }
  ~CpuAllocationCounter() { Stop(); }
  CpuAllocationCounter(const CpuAllocationCounter&) = delete;
  auto operator=(const CpuAllocationCounter&) -> CpuAllocationCounter& = delete;
  auto Stop() noexcept -> void
  {
    if (current_ == this) {
      current_ = nullptr;
      _CrtSetAllocHook(previous_);
    }
  }
  std::uint64_t allocations { 0 };
  std::uint64_t requested_bytes { 0 };

private:
  static auto __cdecl Hook(int operation, void* data, size_t size, int block,
    long request, const unsigned char* file, int line) -> int
  {
    if (current_ && (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)) {
      ++current_->allocations;
      current_->requested_bytes += size;
    }
    const auto previous = previous_hook_.load();
    return previous
      ? previous(operation, data, size, block, request, file, line)
      : 1;
  }
  inline static thread_local CpuAllocationCounter* current_ = nullptr;
  inline static std::atomic<_CRT_ALLOC_HOOK> previous_hook_ { nullptr };
  _CRT_ALLOC_HOOK previous_;
};
}
#endif
