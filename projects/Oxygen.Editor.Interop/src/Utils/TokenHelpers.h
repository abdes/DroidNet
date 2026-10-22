//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Oxygen::Interop {

// Token storage for the asynchronous surface operations (register, unregister,
// resize): one token per operation, mapped to a pointer to a GCHandle (via
// IntPtr.ToPointer()). The GCHandle keeps the operation's TaskCompletionSource
// alive until the engine module processes the operation and the token resolves.
// Operations on one surface overlap (a resize staged while its removal is
// pending), so a token never names the surface.
using TokenId = std::uint64_t;

inline std::unordered_map<TokenId, void*> tokens_map;
inline std::mutex tokens_mutex;
inline std::atomic<TokenId> next_token_id { 1 };

//! Resolves an operation's token with its outcome and forgets it.
void ResolveToken(TokenId token, bool ok);

//! A native callback that resolves the given token when invoked.
auto MakeResolveCallback(TokenId token) -> std::function<void(bool)>;

} // namespace Oxygen::Interop

#pragma managed(pop)

namespace Oxygen::Interop {

//! Keeps an operation's completion alive under a new token, for its callback.
auto StoreToken(
  System::Threading::Tasks::TaskCompletionSource<bool>^ completion)
  -> TokenId;

} // namespace Oxygen::Interop
