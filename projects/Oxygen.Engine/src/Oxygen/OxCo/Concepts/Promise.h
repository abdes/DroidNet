//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Concepts/Awaitable.h"

namespace oxygen::co {

// Concept to define Promise type requirements with return type Ret
template <typename P, typename Ret>
concept PromiseType = requires(P promise) {
    { promise.get_return_object() };
    { promise.initial_suspend() } -> DirectAwaitable;
    { promise.final_suspend() } -> DirectAwaitable;
    { promise.unhandled_exception() } -> std::same_as<void>;
} && ((std::same_as<Ret, void> && requires(P promise) {
    { promise.return_void() } -> std::same_as<void>;
}) || (!std::same_as<Ret, void> && requires(P promise) {
    { promise.return_value(std::declval<Ret>()) } -> std::same_as<void>;
}));

}; // namespace oxygen::co
