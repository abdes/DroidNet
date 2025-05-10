//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen {

//! Helper for `static_assert` to fail dependent on template parameter.
/*!
 This template is used when you need to create a compile-time assertion that
 depends on a template parameter. Since it always evaluates to false regardless
 of the template parameter, it can be used in contexts where you need to fail
 compilation for specific template instantiations.

 **Example usage:**
 ```cpp
 template <typename T>
 void process(const T& value) {
     if constexpr (std::is_integral_v<T>) {
         // Handle integer types
     } else if constexpr (std::is_floating_point_v<T>) {
         // Handle floating point types
     } else {
         static_assert(always_false_v<T>, "Unsupported type!");
     }
 }
*/
template <typename>
inline constexpr bool always_false_v = false;

//! Overloads pattern, used to create a lambda that can handle multiple
//! types within a `variant`, in a type-safe manner.
/*!
 It elegantly combines multiple lambdas into a single visitor object, and
 inherits function call operators from each lambda. Each type is handled with
 type-specific code, and a 'catch-all' lambda provides helpful runtime errors
 for unexpected types.

 **Example usage:**
 ```cpp
 using BarrierDesc = std::variant<
    BufferBarrierDesc,
    TextureBarrierDesc,
    MemoryBarrierDesc
 >;

 auto GetStateAfter() -> ResourceStates {
    return std::visit(
        overloads {
            [](const BufferBarrierDesc& desc) { return desc.after; },
            [](const TextureBarrierDesc& desc) { return desc.after; },
            // Catch-all for unexpected types
            [](const auto&) -> ResourceStates {
                ABORT_F("Unexpected barrier descriptor type");
            },
        },
        descriptor_);
 }
 ```
*/
template <class... Ts>
struct Overloads : Ts... {
    using Ts::operator()...;
};

} // namespace oxygen
