//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemProto.h>

using oxygen::vortex::sceneprep::RenderItemProto;
namespace scn = oxygen::scene;
namespace scn_detail = oxygen::scene::detail;

namespace {

//! Test helper: a SceneNodeImpl that also has a RenderableComponent.
class NodeWithRenderable final : public scn::SceneNodeImpl {
public:
  using GeometryAsset = oxygen::data::GeometryAsset;

  explicit NodeWithRenderable(const std::string& name,
    std::shared_ptr<const GeometryAsset> geometry = nullptr)
    : SceneNodeImpl(name)
  {
    // Add a RenderableComponent (safe with nullptr geometry for tests).
    AddComponent<scn_detail::RenderableComponent>(std::move(geometry));
  }
};

//! Constructing with a node missing Renderable must throw a ComponentError.
/*!
 RenderItemProto requires the node to own a RenderableComponent. Passing a node
 without that component is an error in the precondition and should result in a
 ComponentError being thrown.
*/
NOLINT_TEST(RenderItemProtoTest, ConstructorWithoutRenderable_Throws)
{
  // Arrange
  const scn::SceneNodeImpl node("NoRenderable");

  // Act + Assert
  NOLINT_ASSERT_THROW(RenderItemProto unused(node), oxygen::ComponentError);
}

//! Constructing with a node that has Renderable succeeds and facades work.
/*!
 When the node contains a RenderableComponent, RenderItemProto construction must
 succeed. The test validates accessible facades on the proto and the default LOD
 policy flags. It verifies that the transform facade exposes the node-owned
 matrix.
*/
NOLINT_TEST(RenderItemProtoTest, ConstructorWithRenderable_Succeeds)
{
  // Arrange
  NodeWithRenderable node("WithRenderable");
  node.GetComponent<scn_detail::TransformComponent>()
    .UpdateWorldTransformAsRoot();

  // Act
  RenderItemProto proto(node);

  // Assert Renderable facade is usable; default policy is not distance/SSE.
  EXPECT_FALSE(proto.Renderable().UsesDistancePolicy());
  EXPECT_FALSE(proto.Renderable().UsesScreenSpaceErrorPolicy());

  // Both facades must refer to the same node-owned transform.
  EXPECT_EQ(&proto.Transform().GetWorldMatrix(),
    &node.GetComponent<scn_detail::TransformComponent>().GetWorldMatrix());
}

//! Visible submeshes roundtrip through SetVisibleSubmeshes/VisibleSubmeshes.
/*!
 This test ensures that when visible submesh indices are set on the proto they
 are returned unchanged by VisibleSubmeshes(). Uses ASSERT to ensure roundtrip
 length equality before element-wise checks.
*/
NOLINT_TEST(RenderItemProtoTest, VisibleSubmeshes_Roundtrip)
{
  // Arrange
  const NodeWithRenderable node("WithRenderable");
  RenderItemProto proto(node);
  const std::vector visible {
    2U,
    5U,
    7U,
  };

  // Act
  proto.SetVisibleSubmeshes(visible);

  // Assert
  const auto span = proto.VisibleSubmeshes();
  ASSERT_EQ(span.size(), visible.size());
  EXPECT_THAT(span, ::testing::ElementsAreArray(visible));
}

//! ResolvedMeshIndex uses default 0 then reflects the last resolved LOD.
/*!
 By default a newly-constructed proto reports ResolvedMeshIndex() == 0 and no
 resolved mesh pointer. After ResolveMesh is called the index must reflect the
 last resolution. The test exercises resolving to a null mesh pointer which is a
 permitted proto state.
*/
NOLINT_TEST(RenderItemProtoTest, ResolvedMeshIndex_DefaultAndUpdated)
{
  // Arrange
  const NodeWithRenderable node("WithRenderable");
  RenderItemProto proto(node);

  // Assert default
  EXPECT_EQ(proto.ResolvedMeshIndex(), 0U);
  EXPECT_FALSE(static_cast<bool>(proto.ResolvedMesh()));

  // Act: set a new lod with a null mesh pointer (allowed for proto state)
  proto.ResolveMesh(std::shared_ptr<const oxygen::data::Mesh> {}, 3U);

  // Assert updated
  EXPECT_EQ(proto.ResolvedMeshIndex(), 3U);
  EXPECT_FALSE(static_cast<bool>(proto.ResolvedMesh()));
}

//! Dropped flag toggles via MarkDropped/IsDropped.
/*!
 Simple state toggle test: the proto is initially not dropped and MarkDropped()
 sets the dropped state. No other side-effects are assumed.
*/
NOLINT_TEST(RenderItemProtoTest, DropFlag_Toggles)
{
  // Arrange
  const NodeWithRenderable node("WithRenderable");
  RenderItemProto proto(node);

  // Act + Assert
  EXPECT_FALSE(proto.IsDropped());
  proto.MarkDropped();
  EXPECT_TRUE(proto.IsDropped());
}

} // namespace
