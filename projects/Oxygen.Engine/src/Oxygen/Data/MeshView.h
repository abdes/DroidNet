//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::data {

//! Immutable, non-owning view of a mesh's geometry data.
/*!
 MeshView provides a lightweight, immutable, value-type view into a subset of
 mesh data owned by a MeshAsset. It is designed for efficient, read-only access
 to vertex and index data, supporting scene, rendering, and asset management
 systems. MeshView does not own any memory and is always externally validated.

 @see MeshAsset, Vertex
*/
class MeshView {
public:
  //! Default constructor creates an empty, invalid MeshView (required for
  //! collection types).
  MeshView() = default;

  //! Only MeshAsset can construct MeshView instances.
  MeshView(const std::string_view name, const std::span<const Vertex> vertices,
    const std::span<const std::uint32_t> indices) noexcept
    : name_(name)
    , vertices_(vertices)
    , indices_(indices)
  {
  }

  ~MeshView() = default;

  OXYGEN_DEFAULT_COPYABLE(MeshView)
  OXYGEN_DEFAULT_MOVABLE(MeshView)

  //! Returns the name of the mesh view (for debugging/identification).
  [[nodiscard]] constexpr auto Name() const noexcept -> std::string_view
  {
    return name_;
  }

  //! Returns a span of vertices for this mesh view.
  [[nodiscard]] constexpr auto Vertices() const noexcept
    -> std::span<const Vertex>
  {
    return vertices_;
  }

  //! Returns a span of indices for this mesh view.
  [[nodiscard]] constexpr auto Indices() const noexcept
    -> std::span<const std::uint32_t>
  {
    return indices_;
  }

  //! Returns the number of vertices in this mesh view.
  [[nodiscard]] constexpr auto VertexCount() const noexcept -> std::size_t
  {
    return vertices_.size();
  }

  //! Returns the number of indices in this mesh view.
  [[nodiscard]] constexpr auto IndexCount() const noexcept -> std::size_t
  {
    return indices_.size();
  }

  // Manual equality and inequality operators (defaulted comparison not
  // supported for std::span)
  friend auto operator==(const MeshView& lhs, const MeshView& rhs) noexcept
    -> bool
  {
    return lhs.name_ == rhs.name_
      && lhs.vertices_.data() == rhs.vertices_.data()
      && lhs.vertices_.size() == rhs.vertices_.size()
      && lhs.indices_.data() == rhs.indices_.data()
      && lhs.indices_.size() == rhs.indices_.size();
  }

  friend auto operator!=(const MeshView& lhs, const MeshView& rhs) noexcept
    -> bool
  {
    return !(lhs == rhs);
  }

private:
  std::string_view name_ { "__Invalid__" };
  std::span<const Vertex> vertices_ {};
  std::span<const std::uint32_t> indices_ {};
};

} // namespace oxygen::data
