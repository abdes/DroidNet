//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "pch.h"

#include "Utils/TokenHelpers.h"

namespace Oxygen::Interop {

  void Oxygen::Interop::ResolveToken(const TokenKey& nativeKey, bool ok) {

#if 0
    using oxygen::engine::interop::LogInfoMessage;
    // Helpful diagnostic message for interop troubleshooting: include a
    // short hex representation of the token key and whether resolution
    // succeeded.
    auto tokenToHex = [](const TokenKey& k) -> std::string {
      std::string out;
      out.reserve(k.size() * 3);
      for (size_t i = 0; i < k.size(); ++i) {
        out += fmt::format("{:02x}", static_cast<unsigned int>(
          static_cast<unsigned char>(k[i])));
        if (i + 1 < k.size() && (i % 4 == 3))
          out.push_back('-');
      }
      return out;
      };
    try {
      auto msg = fmt::format("ResolveToken: key={} ok={}", tokenToHex(nativeKey),
        ok ? "true" : "false");
      LogInfoMessage(msg.c_str());
    }
    catch (...) { /* keep resolving even if logging fails */
    }
#endif // 0

    std::lock_guard<std::mutex> lg(tokens_mutex);
    auto it = tokens_map.find(nativeKey);
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
  std::function<void(bool)> MakeResolveCallback(const TokenKey &k) {
    // Capture the key by value and call ResolveToken when the callback is
    // invoked. Keeping the lambda simple and noexcept-friendly avoids
    // unnecessary exceptions crossing native/managed boundaries.
    return [k](bool ok) noexcept { ResolveToken(k, ok); };
  }

} // namespace Oxygen::Interop
