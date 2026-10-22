//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch_clr.h"

#pragma managed(push, off)
#include <atomic>
#include <functional>
#include <mutex>
#pragma managed(pop)

#pragma managed

#include "Utils/TokenHelpers.h"

namespace Oxygen::Interop {

  void ResolveToken(const TokenId token, const bool ok) {
    std::lock_guard<std::mutex> lg(tokens_mutex);
    auto it = tokens_map.find(token);
    if (it == tokens_map.end()) {
      return;
    }

    void* hv = it->second;
    if (hv != nullptr) {
      try {
        System::IntPtr ip(hv);
        auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(ip);
        auto tcs =
          safe_cast<System::Threading::Tasks::TaskCompletionSource<bool>^>(
            gh.Target);
        if (tcs != nullptr) {
          tcs->TrySetResult(ok);
        }
        gh.Free();
      }
      catch (...) {
        /* swallow */
      }
    }

    tokens_map.erase(it);
  }

  // Return a native callback that resolves the token when invoked.
  auto MakeResolveCallback(const TokenId token) -> std::function<void(bool)> {
    // Capture the token by value and call ResolveToken when the callback is
    // invoked. Keeping the lambda simple and noexcept-friendly avoids
    // unnecessary exceptions crossing native/managed boundaries.
    return [token](bool ok) noexcept { ResolveToken(token, ok); };
  }

  auto StoreToken(
    System::Threading::Tasks::TaskCompletionSource<bool>^ completion)
    -> TokenId {
    auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
      completion, System::Runtime::InteropServices::GCHandleType::Normal);
    auto ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
    const auto token = next_token_id.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lg(tokens_mutex);
    tokens_map[token] = ip.ToPointer();
    return token;
  }

} // namespace Oxygen::Interop
