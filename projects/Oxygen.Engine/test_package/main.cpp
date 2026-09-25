// Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>

#include <fmt/format.h>

#include <Oxygen/Base/Uuid.h>

#if defined(OXYGEN_TEST_COMPOSITION)
#  include <Oxygen/Composition/TypeSystem.h>
#endif

#if defined(OXYGEN_TEST_OXCO)
#  include <Oxygen/OxCo/Co.h>
#  include <Oxygen/OxCo/Run.h>
#  include <Oxygen/OxCo/asio.h>
#  if OXYGEN_TEST_CHECKER != defined(OXCO_AWAITER_STATE_DEBUG)
#    error Package awaiter checker usage requirements do not match its options
#  endif

auto Load(asio::io_context& io) -> oxygen::co::Co<int>
{
  co_await oxygen::co::SleepFor(io, std::chrono::milliseconds(1));
  co_return 42;
}
#endif

#if defined(OXYGEN_TEST_SERIO)
#  include <Oxygen/Serio/MemoryStream.h>
#endif
#if defined(OXYGEN_TEST_TEXTWRAP)
#  include <Oxygen/TextWrap/TextWrap.h>
#endif
#if defined(OXYGEN_TEST_CLAP)
#  include <Oxygen/Clap/Cli.h>
#  include <Oxygen/Clap/Fluent/DSL.h>
#endif
#if defined(OXYGEN_TEST_CORE)
#  include <glm/geometric.hpp>
#  include <glm/vec3.hpp>

#  include <Oxygen/Core/Version.h>
#endif
#if defined(OXYGEN_TEST_SCENE)
#  include <Oxygen/Scene/Scene.h>
#endif

auto main() -> int
{
  const auto id = oxygen::Uuid::Generate();
  if (!id.IsValidV7() || id.ToString().size() != 36) {
    return 1;
  }
#if defined(OXYGEN_TEST_COMPOSITION)
  auto& registry = oxygen::TypeRegistry::Get();
  const auto type = registry.RegisterType("OxygenPackageConsumer");
  if (registry.GetTypeId("OxygenPackageConsumer") != type) {
    return 7;
  }
#endif
#if defined(OXYGEN_TEST_OXCO)
  asio::io_context io;
  if (oxygen::co::Run(io, Load(io)) != 42) {
    return 2;
  }
#endif
#if defined(OXYGEN_TEST_SERIO)
  oxygen::serio::MemoryStream stream;
  const std::byte value { 42 };
  if (!stream.Write(&value, 1) || stream.Size().value() != 1) {
    return 3;
  }
#endif
#if defined(OXYGEN_TEST_TEXTWRAP)
  const oxygen::wrap::TextWrapper wrapper
    = oxygen::wrap::MakeWrapper().Width(12);
  if (!wrapper.Wrap("Oxygen package consumer")) {
    return 4;
  }
#endif
#if defined(OXYGEN_TEST_CLAP)
  const std::unique_ptr<oxygen::clap::Cli> cli
    = oxygen::clap::CliBuilder()
        .ProgramName("oxygen-consumer")
        .WithHelpCommand();
  if (!cli) {
    return 5;
  }
#endif
#if defined(OXYGEN_TEST_CORE)
  const glm::vec3 position { 1.0F, 2.0F, 3.0F };
  if (oxygen::version::NameVersion().empty()
    || glm::dot(position, position) != 14.0F) {
    return 6;
  }
#endif
#if defined(OXYGEN_TEST_SCENE)
  auto scene = std::make_shared<oxygen::scene::Scene>("Package world", 8);
  auto parent = scene->CreateNode("Player");
  auto child = scene->CreateChildNode(parent, "Camera");
  if (!child || !parent.GetTransform().SetLocalPosition({ 2.0F, 0.0F, 0.0F })
    || !child->GetTransform().SetLocalPosition({ 0.0F, 3.0F, 0.0F })) {
    return 8;
  }
  scene->Update();
  const auto world_position = child->GetTransform().GetWorldPosition();
  if (!world_position || std::abs(world_position->x - 2.0F) > 0.001F
    || std::abs(world_position->y - 3.0F) > 0.001F) {
    return 9;
  }
#endif
  std::cout << fmt::format(
    "Oxygen package consumer passed: {}\n", id.ToString());
  return 0;
}
