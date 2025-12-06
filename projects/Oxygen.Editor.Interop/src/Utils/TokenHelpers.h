//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <functional>
#include <mutex>
#include <unordered_map>

#include "EditorModule/SurfaceRegistry.h"

namespace Oxygen::Interop {

// Token storage: native map keyed by the registry GuidKey, storing a pointer
// to a GCHandle (via IntPtr.ToPointer()). The GCHandle holds the
// TaskCompletionSource kept alive until the engine module processes the
// pending destruction/resize and we resolve the token.
using TokenKey = oxygen::interop::module::SurfaceRegistry::GuidKey;

// SurfaceRegistry::GuidHasher is a private helper; provide an internal
// TokenHasher for use in this compilation unit.
struct TokenHasher {
  inline auto operator()(const TokenKey &key) const noexcept -> std::size_t {
    std::size_t hash = 1469598103934665603ULL;
    for (auto byte : key) {
      hash ^= static_cast<std::size_t>(byte);
      hash *= 1099511628211ULL;
    }
    return hash;
  }
};
inline std::unordered_map<TokenKey, void*, TokenHasher> tokens_map;
inline std::mutex tokens_mutex;

void ResolveToken(const TokenKey &nativeKey, bool ok);

// Helper that returns a native callback which resolves the given token.
std::function<void(bool)> MakeResolveCallback(const TokenKey &k);

} // namespace Oxygen::Interop

#pragma managed(pop)
