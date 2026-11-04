//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <span>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MeshBufferRules.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::data::CheckJointIndices;
using oxygen::data::CheckMeshViewRange;
using oxygen::data::CheckSkinBufferRows;
using oxygen::data::CheckVertexBufferRow;
using oxygen::data::CookedDefect;
using oxygen::data::CookedFormatError;
using oxygen::data::IndexTypeOf;
using oxygen::data::MeshViewRange;
using oxygen::data::SkinBufferRows;
using oxygen::data::Vertex;
using oxygen::data::detail::IndexBufferView;
using oxygen::data::detail::IndexType;
using oxygen::data::pak::core::BufferResourceDesc;

constexpr uint32_t kVertexSize = sizeof(Vertex);

//! The defect `rule` throws, which it must.
template <typename Rule> auto DefectOf(const Rule& rule) -> CookedDefect
{
  try {
    rule();
  } catch (const CookedFormatError& error) {
    return error.Defect();
  }
  ADD_FAILURE() << "The rule accepted what it must refuse";
  return CookedDefect::kAbsent;
}

auto Row(const uint32_t size, const uint32_t stride) -> BufferResourceDesc
{
  return BufferResourceDesc {
    .size_bytes = size,
    .element_stride = stride,
  };
}

//! A stride says what an element is: only a whole vertex lets views count
//! vertices.
NOLINT_TEST(MeshBufferRulesTest, VertexRowHoldsWholeVertices)
{
  EXPECT_NO_THROW(CheckVertexBufferRow(Row(3U * kVertexSize, kVertexSize)));

  EXPECT_EQ(
    DefectOf([] -> void { CheckVertexBufferRow(Row(3U * kVertexSize, 1U)); }),
    CookedDefect::kWrongRowSize);
  EXPECT_EQ(DefectOf([] -> void {
    CheckVertexBufferRow(Row((3U * kVertexSize) + 1U, kVertexSize));
  }),
    CookedDefect::kWrongRowSize);
  EXPECT_EQ(
    DefectOf([] -> void { CheckVertexBufferRow(Row(0U, kVertexSize)); }),
    CookedDefect::kWrongRowSize);
}

//! A format names the width; a buffer without one has it from its stride.
NOLINT_TEST(MeshBufferRulesTest, IndexTypeComesFromFormatOrStride)
{
  EXPECT_EQ(IndexTypeOf(Format::kR16UInt, 0U), IndexType::kUInt16);
  EXPECT_EQ(IndexTypeOf(Format::kR32UInt, 0U), IndexType::kUInt32);
  EXPECT_EQ(IndexTypeOf(Format::kUnknown, 2U), IndexType::kUInt16);
  EXPECT_EQ(IndexTypeOf(Format::kUnknown, 4U), IndexType::kUInt32);

  EXPECT_EQ(DefectOf([] -> void {
    static_cast<void>(IndexTypeOf(Format::kUnknown, 8U));
  }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(DefectOf([] -> void {
    static_cast<void>(IndexTypeOf(Format::kR32Float, 0U));
  }),
    CookedDefect::kInvalidValue);
}

//! Ranges at the 32-bit limit are refused, not wrapped into the buffer.
NOLINT_TEST(MeshBufferRulesTest, ViewStaysInItsBuffers)
{
  constexpr auto kIndices = std::array<uint32_t, 3> { 0U, 1U, 2U };
  const auto indices = IndexBufferView {
    .bytes = std::as_bytes(std::span(kIndices)),
    .type = IndexType::kUInt32,
  };

  EXPECT_NO_THROW(CheckMeshViewRange(
    MeshViewRange {
      .first_vertex = 0U,
      .vertex_count = 3U,
      .first_index = 0U,
      .index_count = 3U,
    },
    3U, indices));

  const auto refused = [&indices](const MeshViewRange& view) -> CookedDefect {
    return DefectOf([&] -> void { CheckMeshViewRange(view, 3U, indices); });
  };
  EXPECT_EQ(refused({
              .first_vertex = 1U,
              .vertex_count = 3U,
              .first_index = 0U,
              .index_count = 3U,
            }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(refused({
              .first_vertex = 0xFFFFFFFFU,
              .vertex_count = 2U,
              .first_index = 0U,
              .index_count = 3U,
            }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(refused({
              .first_vertex = 0U,
              .vertex_count = 3U,
              .first_index = 0xFFFFFFFEU,
              .index_count = 3U,
            }),
    CookedDefect::kOutOfBounds);
}

//! Indices count from the view's first vertex, so the check is relative.
NOLINT_TEST(MeshBufferRulesTest, IndexStaysInItsView)
{
  constexpr auto kIndices = std::array<uint16_t, 3> { 0U, 1U, 2U };
  const auto indices = IndexBufferView {
    .bytes = std::as_bytes(std::span(kIndices)),
    .type = IndexType::kUInt16,
  };

  EXPECT_NO_THROW(CheckMeshViewRange(
    MeshViewRange {
      .first_vertex = 7U,
      .vertex_count = 3U,
      .first_index = 0U,
      .index_count = 3U,
    },
    10U, indices));
  EXPECT_EQ(DefectOf([&indices] -> void {
    CheckMeshViewRange(
      MeshViewRange {
        .first_vertex = 0U,
        .vertex_count = 2U,
        .first_index = 0U,
        .index_count = 3U,
      },
      10U, indices);
  }),
    CookedDefect::kOutOfBounds);
}

auto BindableSkin() -> SkinBufferRows
{
  return SkinBufferRows {
    .joint_indices = Row(48U, 16U),
    .joint_weights = Row(48U, 16U),
    .inverse_bind_matrices = Row(192U, 64U),
    .joint_remap = Row(12U, 4U),
    .vertex_count = 3U,
    .joint_count = 3U,
    .influences_per_vertex = 4U,
  };
}

NOLINT_TEST(MeshBufferRulesTest, SkinStreamsServeTheMesh)
{
  EXPECT_NO_THROW(CheckSkinBufferRows(BindableSkin()));

  // A format's element counts as a stride's, as the model cook writes them.
  auto formatted = BindableSkin();
  formatted.joint_indices = BufferResourceDesc {
    .size_bytes = 48U,
    .element_format = static_cast<uint8_t>(Format::kRGBA32UInt),
  };
  EXPECT_NO_THROW(CheckSkinBufferRows(formatted));

  // More matrices and remap entries than joints are fine.
  auto spare = BindableSkin();
  spare.inverse_bind_matrices = Row(256U, 64U);
  spare.joint_remap = Row(16U, 4U);
  EXPECT_NO_THROW(CheckSkinBufferRows(spare));
}

NOLINT_TEST(MeshBufferRulesTest, SkinStreamsMustServeEveryVertexAndJoint)
{
  auto wrong_stride = BindableSkin();
  wrong_stride.joint_weights = Row(48U, 8U);
  EXPECT_EQ(DefectOf([&] -> void { CheckSkinBufferRows(wrong_stride); }),
    CookedDefect::kWrongRowSize);

  auto too_few_indices = BindableSkin();
  too_few_indices.joint_indices = Row(32U, 16U);
  EXPECT_EQ(DefectOf([&] -> void { CheckSkinBufferRows(too_few_indices); }),
    CookedDefect::kSizeMismatch);

  auto too_many_weights = BindableSkin();
  too_many_weights.joint_weights = Row(64U, 16U);
  EXPECT_EQ(DefectOf([&] -> void { CheckSkinBufferRows(too_many_weights); }),
    CookedDefect::kSizeMismatch);

  auto too_few_matrices = BindableSkin();
  too_few_matrices.inverse_bind_matrices = Row(128U, 64U);
  EXPECT_EQ(DefectOf([&] -> void { CheckSkinBufferRows(too_few_matrices); }),
    CookedDefect::kSizeMismatch);

  auto too_few_remaps = BindableSkin();
  too_few_remaps.joint_remap = Row(8U, 4U);
  EXPECT_EQ(DefectOf([&] -> void { CheckSkinBufferRows(too_few_remaps); }),
    CookedDefect::kSizeMismatch);
}

NOLINT_TEST(MeshBufferRulesTest, JointIndicesNameTheMeshJoints)
{
  constexpr auto kInRange = std::array<uint32_t, 4> { 0U, 1U, 2U, 2U };
  EXPECT_NO_THROW(CheckJointIndices(std::as_bytes(std::span(kInRange)), 3U));

  constexpr auto kPast = std::array<uint32_t, 4> { 0U, 1U, 2U, 3U };
  EXPECT_EQ(DefectOf([&] -> void {
    CheckJointIndices(std::as_bytes(std::span(kPast)), 3U);
  }),
    CookedDefect::kOutOfBounds);

  EXPECT_EQ(DefectOf([&] -> void {
    CheckJointIndices(std::as_bytes(std::span(kInRange)).first(5U), 3U);
  }),
    CookedDefect::kSizeMismatch);
}

} // namespace
