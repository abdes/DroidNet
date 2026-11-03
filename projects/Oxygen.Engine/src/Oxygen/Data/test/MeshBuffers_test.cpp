//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Data/GeometryAsset.cpp

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::data::BufferResource;
using oxygen::data::CookedDefect;
using oxygen::data::CookedFormatError;
using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshBuilder;
using oxygen::data::Vertex;
using oxygen::data::pak::geometry::MeshViewDesc;

constexpr auto kLast = std::numeric_limits<uint32_t>::max();

//! A buffer that holds `payload`, with elements of `stride` bytes or of
//! `format`.
[[nodiscard]] auto Buffer(const std::span<const std::byte> payload,
  const BufferResource::UsageFlags usage, const uint32_t stride,
  const Format format = Format::kUnknown) -> std::shared_ptr<BufferResource>
{
  return std::make_shared<BufferResource>(
    oxygen::data::pak::core::BufferResourceDesc {
      .data_offset = 0,
      .size_bytes = static_cast<uint32_t>(payload.size()),
      .usage_flags = static_cast<uint32_t>(usage),
      .element_stride = stride,
      .element_format = static_cast<uint8_t>(format),
    },
    oxygen::serio::testing::OwnedBytes(payload));
}

//! A vertex buffer of `count` vertices.
[[nodiscard]] auto Vertices(const std::size_t count)
  -> std::shared_ptr<BufferResource>
{
  const auto vertices = std::vector<Vertex>(count);
  return Buffer(std::as_bytes(std::span(vertices)),
    BufferResource::UsageFlags::kVertexBuffer, sizeof(Vertex));
}

//! An index buffer of `indices`, 16-bit or 32-bit as `Index` is.
template <typename Index>
[[nodiscard]] auto Indices(const std::vector<Index>& indices)
  -> std::shared_ptr<BufferResource>
{
  return Buffer(std::as_bytes(std::span(indices)),
    BufferResource::UsageFlags::kIndexBuffer, 0U,
    sizeof(Index) == 2 ? Format::kR16UInt : Format::kR32UInt);
}

//! The defect `bind` refuses with, none when it accepts.
template <typename Bind>
[[nodiscard]] auto Refusal(const Bind& bind) -> std::optional<CookedDefect>
{
  try {
    bind();
  } catch (const CookedFormatError& refused) {
    return refused.Defect();
  }
  return std::nullopt;
}

//! A loaded mesh draws a view of the buffers bound to it after it loads.
class MeshBuffersTest : public testing::Test {
protected:
  //! The view of three vertices from vertex 2 and three indices from
  //! index 1.
  static constexpr auto kView = MeshViewDesc {
    .first_index = 1,
    .index_count = 3,
    .first_vertex = 2,
    .vertex_count = 3,
  };

  //! A mesh read from its descriptor, drawing `view` of buffers it does not
  //! hold yet.
  [[nodiscard]] static auto LoadedMesh(const MeshViewDesc& view = kView)
    -> std::unique_ptr<Mesh>
  {
    auto descriptor = oxygen::data::pak::geometry::MeshDesc {};
    descriptor.mesh_type
      = static_cast<uint8_t>(oxygen::data::MeshType::kStandard);
    return MeshBuilder {}
      .WithDescriptor(descriptor)
      .WithBufferResources(nullptr, nullptr)
      .BeginSubMesh("Surface", MaterialAsset::CreateDefault())
      .WithMeshView(view)
      .EndSubMesh()
      .Build();
  }

  [[nodiscard]] static auto ViewOf(const Mesh& mesh) -> oxygen::data::MeshView
  {
    return mesh.SubMeshes().front().MeshViews().front();
  }
};

NOLINT_TEST_F(MeshBuffersTest, DrawsTheViewOfTheBuffersBoundToIt)
{
  const auto mesh = LoadedMesh();

  mesh->SetBufferResources(Vertices(5), Indices<uint32_t>({ 9, 0, 1, 2 }));

  EXPECT_TRUE(mesh->HasBuffers());
  EXPECT_EQ(ViewOf(*mesh).Vertices().size(), 3U);
  EXPECT_EQ(
    ViewOf(*mesh).Vertices().data(), mesh->Vertices().subspan(2).data());
  EXPECT_THAT(
    ViewOf(*mesh).IndexBuffer().Widened(), testing::ElementsAre(0U, 1U, 2U));
}

NOLINT_TEST_F(MeshBuffersTest, ShowsNoVerticesBeforeItsBuffersAreBound)
{
  const auto mesh = LoadedMesh();

  EXPECT_FALSE(mesh->HasBuffers());
  EXPECT_TRUE(ViewOf(*mesh).Vertices().empty());
  EXPECT_TRUE(ViewOf(*mesh).IndexBuffer().Empty());
}

//! A refused binding leaves the mesh without buffers.
NOLINT_TEST_F(MeshBuffersTest, RefusesBuffersShorterThanItsViews)
{
  const auto mesh = LoadedMesh();

  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(Vertices(4), Indices<uint32_t>({ 9, 0, 1, 2 }));
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(Vertices(5), Indices<uint32_t>({ 9, 0, 1 }));
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_FALSE(mesh->HasBuffers());
}

//! A range is checked without adding its start and count, which could wrap.
NOLINT_TEST_F(MeshBuffersTest, RefusesViewRangesThatWrapAround)
{
  auto vertices_wrap = kView;
  vertices_wrap.first_vertex = kLast;
  auto indices_wrap = kView;
  indices_wrap.first_index = kLast;

  for (const auto& view : { vertices_wrap, indices_wrap }) {
    const auto mesh = LoadedMesh(view);
    EXPECT_EQ(Refusal([&mesh] -> void {
      mesh->SetBufferResources(Vertices(5), Indices<uint32_t>({ 9, 0, 1, 2 }));
    }),
      CookedDefect::kOutOfBounds);
  }
}

//! A view's indices count from its first vertex, the draw's base vertex.
NOLINT_TEST_F(MeshBuffersTest, RefusesIndicesOutsideTheirView)
{
  const auto mesh = LoadedMesh();

  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(Vertices(6), Indices<uint32_t>({ 0, 0, 1, 3 }));
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(Vertices(6), Indices<uint16_t>({ 0, 3, 1, 2 }));
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(Vertices(6), Indices<uint16_t>({ 3, 2, 1, 0 }));
  }),
    std::nullopt);
}

NOLINT_TEST_F(MeshBuffersTest, RefusesBuffersOfAnotherShape)
{
  const auto mesh = LoadedMesh();
  const auto bytes = std::vector<std::byte>((5 * sizeof(Vertex)) + 2);
  const auto partial_vertex
    = Buffer(bytes, BufferResource::UsageFlags::kVertexBuffer, 1U);
  const auto byte_indices = Buffer(std::span(bytes).first(4),
    BufferResource::UsageFlags::kIndexBuffer, 0U, Format::kR8UInt);
  const auto raw_indices = Buffer(
    std::span(bytes).first(16), BufferResource::UsageFlags::kIndexBuffer, 1U);

  EXPECT_EQ(Refusal([&] -> void {
    mesh->SetBufferResources(partial_vertex, Indices<uint32_t>({ 0, 0, 1, 2 }));
  }),
    CookedDefect::kWrongRowSize);
  for (const auto& indices : { byte_indices, raw_indices }) {
    EXPECT_EQ(
      Refusal([&] -> void { mesh->SetBufferResources(Vertices(5), indices); }),
      CookedDefect::kInvalidValue);
  }
}

NOLINT_TEST_F(MeshBuffersTest, RefusesAMissingBuffer)
{
  const auto mesh = LoadedMesh();

  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(nullptr, Indices<uint32_t>({ 0, 0, 1, 2 }));
  }),
    CookedDefect::kMissingPart);
  EXPECT_EQ(Refusal([&mesh] -> void {
    mesh->SetBufferResources(Vertices(5), nullptr);
  }),
    CookedDefect::kMissingPart);
}

//! A mesh built with its buffers holds them to the same rules.
NOLINT_TEST_F(MeshBuffersTest, BuildingRefusesBuffersThatDoNotHoldItsViews)
{
  const auto build = [](std::shared_ptr<BufferResource> vertices) -> void {
    static_cast<void>(MeshBuilder {}
        .WithBufferResources(
          std::move(vertices), Indices<uint32_t>({ 9, 0, 1, 2 }))
        .BeginSubMesh("Surface", MaterialAsset::CreateDefault())
        .WithMeshView(kView)
        .EndSubMesh()
        .Build());
  };
  const auto owned = [](std::vector<uint32_t> indices) -> void {
    static_cast<void>(MeshBuilder {}
        .WithVertices(std::vector<Vertex>(5))
        .WithIndices(indices)
        .BeginSubMesh("Surface", MaterialAsset::CreateDefault())
        .WithMeshView(kView)
        .EndSubMesh()
        .Build());
  };

  EXPECT_EQ(Refusal([&build] -> void { build(Vertices(4)); }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(Refusal([&owned] -> void { owned({ 9, 0, 1, 3 }); }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(Refusal([&owned] -> void { owned({ 9, 0, 1, 2 }); }), std::nullopt);
}

} // namespace
