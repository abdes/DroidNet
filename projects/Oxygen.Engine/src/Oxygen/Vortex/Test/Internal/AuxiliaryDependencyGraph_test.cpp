//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ShaderPassConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ToneMapPassConfig.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::observer_ptr;
using oxygen::ViewId;
using oxygen::vortex::CompositionView;
using oxygen::vortex::internal::CompositionViewImpl;
using oxygen::vortex::internal::FramePlanBuilder;
using oxygen::vortex::internal::access::ViewLifecycleTagFactory;
using oxygen::vortex::testing::FakeGraphics;

auto MakeView() -> oxygen::View
{
  auto view = oxygen::View {};
  view.viewport = {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 128.0F,
    .height = 72.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  return view;
}

auto MakeInputs() -> FramePlanBuilder::Inputs
{
  static const oxygen::vortex::ToneMapPassConfig tone_map_config {};
  static const oxygen::vortex::ShaderPassConfig shader_pass_config {};
  return FramePlanBuilder::Inputs {
    .frame_settings = {},
    .pending_auto_exposure_reset = std::nullopt,
    .tone_map_pass_config = observer_ptr { &tone_map_config },
    .shader_pass_config = observer_ptr { &shader_pass_config },
    .resolve_published_view_id = [](const ViewId id) {
      return ViewId { id.get() + 1000U };
    },
  };
}

void PrepareView(CompositionViewImpl& view_impl, const CompositionView& desc,
  FakeGraphics& graphics)
{
  view_impl.PrepareForRender(
    desc, 0U, graphics, ViewLifecycleTagFactory::Get());
}

auto MakeSceneView(const ViewId id, const std::string_view name)
  -> CompositionView
{
  auto view = CompositionView::ForScene(id, MakeView(), oxygen::scene::SceneNode {});
  view.name = name;
  return view;
}

void BuildPackets(FramePlanBuilder& builder,
  std::span<CompositionViewImpl* const> views)
{
  builder.BuildFrameViewPackets(
    observer_ptr<oxygen::scene::Scene> {}, views, MakeInputs());
}

TEST(AuxiliaryDependencyGraphTest, MissingRequiredProducerFailsBeforeGpuWork)
{
  auto graphics = std::make_shared<FakeGraphics>();
  auto consumer = MakeSceneView(ViewId { 1U }, "Consumer");
  consumer.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 7U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .required = true,
  });

  CompositionViewImpl consumer_impl;
  PrepareView(consumer_impl, consumer, *graphics);

  FramePlanBuilder builder;
  std::array views { &consumer_impl };
  EXPECT_THROW(BuildPackets(builder,
                 std::span<CompositionViewImpl* const> {
                   views.data(), views.size() }),
    std::runtime_error);
}

TEST(AuxiliaryDependencyGraphTest, DuplicateProducerFailsBeforeGpuWork)
{
  auto graphics = std::make_shared<FakeGraphics>();
  auto first = MakeSceneView(ViewId { 2U }, "FirstProducer");
  first.produced_aux_outputs.push_back(CompositionView::AuxOutputDesc {
    .id = CompositionView::AuxOutputId { 9U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .debug_name = "First.Color",
  });
  auto second = MakeSceneView(ViewId { 3U }, "SecondProducer");
  second.produced_aux_outputs = first.produced_aux_outputs;

  CompositionViewImpl first_impl;
  CompositionViewImpl second_impl;
  PrepareView(first_impl, first, *graphics);
  PrepareView(second_impl, second, *graphics);

  FramePlanBuilder builder;
  std::array views { &first_impl, &second_impl };
  EXPECT_THROW(BuildPackets(builder,
                 std::span<CompositionViewImpl* const> {
                   views.data(), views.size() }),
    std::runtime_error);
}

TEST(AuxiliaryDependencyGraphTest, OptionalMissingProducerPublishesTypedInvalid)
{
  auto graphics = std::make_shared<FakeGraphics>();
  auto consumer = MakeSceneView(ViewId { 4U }, "OptionalConsumer");
  consumer.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 11U },
    .kind = CompositionView::AuxOutputKind::kDepthTexture,
    .required = false,
  });

  CompositionViewImpl consumer_impl;
  PrepareView(consumer_impl, consumer, *graphics);

  FramePlanBuilder builder;
  std::array views { &consumer_impl };
  BuildPackets(builder,
    std::span<CompositionViewImpl* const> { views.data(), views.size() });

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 1U);
  const auto& resolved = builder.GetFrameViewPackets()[0].ResolvedAuxInputs();
  ASSERT_EQ(resolved.size(), 1U);
  EXPECT_FALSE(resolved[0].valid);
  EXPECT_EQ(resolved[0].kind, CompositionView::AuxOutputKind::kDepthTexture);
  EXPECT_EQ(resolved[0].producer_view_id, oxygen::kInvalidViewId);
}

TEST(AuxiliaryDependencyGraphTest, ProducerOrdersBeforeConsumer)
{
  auto graphics = std::make_shared<FakeGraphics>();
  auto consumer = MakeSceneView(ViewId { 5U }, "Consumer");
  consumer.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 13U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .required = true,
  });
  auto producer = MakeSceneView(ViewId { 6U }, "Producer");
  producer.view_kind = CompositionView::ViewKind::kAuxiliary;
  producer.produced_aux_outputs.push_back(CompositionView::AuxOutputDesc {
    .id = CompositionView::AuxOutputId { 13U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .debug_name = "Producer.Color",
  });

  CompositionViewImpl consumer_impl;
  CompositionViewImpl producer_impl;
  PrepareView(consumer_impl, consumer, *graphics);
  PrepareView(producer_impl, producer, *graphics);

  FramePlanBuilder builder;
  std::array views { &consumer_impl, &producer_impl };
  BuildPackets(builder,
    std::span<CompositionViewImpl* const> { views.data(), views.size() });

  ASSERT_EQ(builder.GetFrameViewPackets().size(), 2U);
  EXPECT_EQ(builder.GetFrameViewPackets()[0].PublishedViewId(),
    ViewId { 1006U });
  EXPECT_EQ(builder.GetFrameViewPackets()[1].PublishedViewId(),
    ViewId { 1005U });
  const auto& resolved = builder.GetFrameViewPackets()[1].ResolvedAuxInputs();
  ASSERT_EQ(resolved.size(), 1U);
  EXPECT_TRUE(resolved[0].valid);
  EXPECT_EQ(resolved[0].producer_view_id, ViewId { 1006U });
}

TEST(AuxiliaryDependencyGraphTest, KindMismatchFailsBeforeGpuWork)
{
  auto graphics = std::make_shared<FakeGraphics>();
  auto producer = MakeSceneView(ViewId { 7U }, "Producer");
  producer.produced_aux_outputs.push_back(CompositionView::AuxOutputDesc {
    .id = CompositionView::AuxOutputId { 15U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .debug_name = "Producer.Color",
  });
  auto consumer = MakeSceneView(ViewId { 8U }, "Consumer");
  consumer.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 15U },
    .kind = CompositionView::AuxOutputKind::kDepthTexture,
    .required = true,
  });

  CompositionViewImpl producer_impl;
  CompositionViewImpl consumer_impl;
  PrepareView(producer_impl, producer, *graphics);
  PrepareView(consumer_impl, consumer, *graphics);

  FramePlanBuilder builder;
  std::array views { &producer_impl, &consumer_impl };
  EXPECT_THROW(BuildPackets(builder,
                 std::span<CompositionViewImpl* const> {
                   views.data(), views.size() }),
    std::runtime_error);
}

TEST(AuxiliaryDependencyGraphTest, CycleIsRejected)
{
  auto graphics = std::make_shared<FakeGraphics>();
  auto first = MakeSceneView(ViewId { 9U }, "First");
  first.produced_aux_outputs.push_back(CompositionView::AuxOutputDesc {
    .id = CompositionView::AuxOutputId { 17U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .debug_name = "First.Color",
  });
  first.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 19U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .required = true,
  });

  auto second = MakeSceneView(ViewId { 10U }, "Second");
  second.produced_aux_outputs.push_back(CompositionView::AuxOutputDesc {
    .id = CompositionView::AuxOutputId { 19U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .debug_name = "Second.Color",
  });
  second.consumed_aux_outputs.push_back(CompositionView::AuxInputDesc {
    .id = CompositionView::AuxOutputId { 17U },
    .kind = CompositionView::AuxOutputKind::kColorTexture,
    .required = true,
  });

  CompositionViewImpl first_impl;
  CompositionViewImpl second_impl;
  PrepareView(first_impl, first, *graphics);
  PrepareView(second_impl, second, *graphics);

  FramePlanBuilder builder;
  std::array views { &first_impl, &second_impl };
  EXPECT_THROW(BuildPackets(builder,
                 std::span<CompositionViewImpl* const> {
                   views.data(), views.size() }),
    std::runtime_error);
}

} // namespace
