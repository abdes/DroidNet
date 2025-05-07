//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>

#include <Oxygen/Graphics/Common/NativeObject.h>

namespace oxygen::graphics {

class Buffer;
class Texture;

// Concepts to identify resource types
template <typename T>
concept IsBuffer = std::derived_from<T, Buffer>;

template <typename T>
concept IsTexture = std::derived_from<T, Texture>;

// C++20 concept to identify resources that can have barriers
template <typename T>
concept Trackable = HoldsNativeResource<T> && (IsBuffer<T> || IsTexture<T>);

} // namespace oxygen::graphics
