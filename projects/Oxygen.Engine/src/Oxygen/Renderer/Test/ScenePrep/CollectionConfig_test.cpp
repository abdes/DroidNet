//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>

namespace {

//! Basic compile-time checks for CollectionConfig wiring.
NOLINT_TEST(CollectionConfig, CollectionConfig_BasicFactory_StaticAsserts)
{
  // ReSharper disable CppIdenticalOperandsInBinaryExpression
  using namespace oxygen::engine::sceneprep;

  // Arrange
  [[maybe_unused]] auto cfg = CreateBasicCollectionConfig();

  // Act & Assert
  static_assert(decltype(cfg)::has_pre_filter);
  static_assert(decltype(cfg)::has_mesh_resolver);
  static_assert(decltype(cfg)::has_visibility_filter);
  static_assert(decltype(cfg)::has_producer);

  // Ensure callables satisfy the extractor concept
  static_assert(RenderItemDataExtractor<std::decay_t<decltype(*cfg.pre_filter)>>
    || RenderItemDataExtractor<std::decay_t<decltype(cfg.pre_filter)>>);
  static_assert(
    RenderItemDataExtractor<std::decay_t<decltype(*cfg.mesh_resolver)>>
    || RenderItemDataExtractor<std::decay_t<decltype(cfg.mesh_resolver)>>);
  static_assert(
    RenderItemDataExtractor<std::decay_t<decltype(*cfg.visibility_filter)>>
    || RenderItemDataExtractor<std::decay_t<decltype(cfg.visibility_filter)>>);
  static_assert(RenderItemDataExtractor<std::decay_t<decltype(*cfg.producer)>>
    || RenderItemDataExtractor<std::decay_t<decltype(cfg.producer)>>);
  // ReSharper restore CppIdenticalOperandsInBinaryExpression
}

} // namespace
