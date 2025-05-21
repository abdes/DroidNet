//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <type_traits>

// TODO: Replace this include when the header is moved to Base
#include <Oxygen/Composition/TypeSystem.h>

namespace oxygen::graphics {

enum class ResourceViewType: std::uint8_t;
enum class DescriptorVisibility: std::uint8_t;
class Texture;
class Buffer;
class Sampler;

struct RegisteredResource { };

template <typename T>
concept TextureResource = std::is_base_of_v<Texture, std::remove_cvref_t<T>>;

template <typename T>
concept BufferResource = std::is_base_of_v<Buffer, std::remove_cvref_t<T>>;

template <typename T>
concept SamplerResource = std::is_base_of_v<Sampler, std::remove_cvref_t<T>>;

template <typename T>
concept AnyResource = std::is_base_of_v<RegisteredResource, std::remove_cvref_t<T>>;

template <typename T>
concept ViewDescription = std::equality_comparable<T>
    && requires(T vk) {
           { std::hash<T> {}(vk) } -> std::convertible_to<std::size_t>;
           { vk.view_type } -> std::convertible_to<ResourceViewType>;
           { vk.visibility } -> std::convertible_to<DescriptorVisibility>;
       };

template <typename T>
concept SupportedResource
    = (TextureResource<T>
          || BufferResource<T>
          || SamplerResource<T>
          || AnyResource<T>)
    && requires { { T::ClassTypeId() } -> std::convertible_to<TypeId>; };

template <typename T>
concept ResourceWithViews = SupportedResource<T>
    && requires { typename T::ViewDescriptionT; }
    && ViewDescription<typename T::ViewDescriptionT>;

} // namespace oxygen::graphics
