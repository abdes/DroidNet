//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

using oxygen::engine::sceneprep::RenderItemProto;
namespace scn = oxygen::scene;
namespace scn_detail = oxygen::scene::detail;

namespace {

//! Test helper: a SceneNodeImpl that also has a RenderableComponent.
class NodeWithRenderable final : public scn::SceneNodeImpl {
public:
  using GeometryAsset = oxygen::data::GeometryAsset;

  explicit NodeWithRenderable(
    std::string name, std::shared_ptr<const GeometryAsset> geometry = nullptr)
    : scn::SceneNodeImpl(name)
  {
    // Add a RenderableComponent (safe with nullptr geometry for tests).
    AddComponent<scn_detail::RenderableComponent>(std::move(geometry));
  }
};

//! Constructing with a node missing Renderable must throw.
NOLINT_TEST(RenderItemProtoTest, ConstructorWithoutRenderable_Throws)
{
  // Arrange
  scn::SceneNodeImpl node("NoRenderable");

  // Act + Assert
  NOLINT_ASSERT_THROW(RenderItemProto unused(node), oxygen::ComponentError);
}

//! Constructing with a node that has Renderable succeeds and facades work.
NOLINT_TEST(RenderItemProtoTest, ConstructorWithRenderable_Succeeds)
{
  // Arrange
  NodeWithRenderable node("WithRenderable");

  // Act
  RenderItemProto proto(node);

  // Assert
  // Renderable facade is usable; default policy is not distance/SSE.
  EXPECT_FALSE(proto.Renderable().UsesDistancePolicy());
  EXPECT_FALSE(proto.Renderable().UsesScreenSpaceErrorPolicy());

  // Transform facade exists (don’t dereference world matrix in tests).
  auto* tf = &proto.Transform();
  EXPECT_NE(tf, nullptr);
}

//! Visible submeshes roundtrip through SetVisibleSubmeshes/VisibleSubmeshes.
NOLINT_TEST(RenderItemProtoTest, VisibleSubmeshes_Roundtrip)
{
  // Arrange
  NodeWithRenderable node("WithRenderable");
  RenderItemProto proto(node);
  const std::vector<uint32_t> visible { 2u, 5u, 7u };

  // Act
  proto.SetVisibleSubmeshes(visible);

  // Assert
  const auto span = proto.VisibleSubmeshes();
  ASSERT_EQ(span.size(), visible.size());
  for (size_t i = 0; i < visible.size(); ++i) {
    EXPECT_EQ(span[i], visible[i]);
  }
}

//! ResolvedMeshIndex uses default 0 then reflects the last resolved LOD.
NOLINT_TEST(RenderItemProtoTest, ResolvedMeshIndex_DefaultAndUpdated)
{
  // Arrange
  NodeWithRenderable node("WithRenderable");
  RenderItemProto proto(node);

  // Assert default
  EXPECT_EQ(proto.ResolvedMeshIndex(), 0u);
  EXPECT_FALSE(static_cast<bool>(proto.ResolvedMesh()));

  // Act: set a new lod with a null mesh pointer (allowed for proto state)
  proto.ResolveMesh(std::shared_ptr<const oxygen::data::Mesh> {}, 3u);

  // Assert updated
  EXPECT_EQ(proto.ResolvedMeshIndex(), 3u);
  EXPECT_FALSE(static_cast<bool>(proto.ResolvedMesh()));
}

//! Dropped flag toggles via MarkDropped/IsDropped.
NOLINT_TEST(RenderItemProtoTest, DropFlag_Toggles)
{
  // Arrange
  NodeWithRenderable node("WithRenderable");
  RenderItemProto proto(node);

  // Act + Assert
  EXPECT_FALSE(proto.IsDropped());
  proto.MarkDropped();
  EXPECT_TRUE(proto.IsDropped());
}

} // namespace
