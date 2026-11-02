//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::cooker::testing {

namespace {

  //! The defect `read` refuses with, none when it accepts.
  template <typename Read>
  auto RefusalOf(const std::string_view reader, const Read& read)
    -> std::optional<data::CookedDefect>
  {
    try {
      read();
    } catch (const data::CookedOpenError& refused) {
      return refused.Defect();
    } catch (const std::exception& failed) {
      throw std::logic_error(
        std::string(reader) + " refuses without a defect: " + failed.what());
    }
    return std::nullopt;
  }

} // namespace

auto SceneRefusal(const std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>
{
  const auto by_reader = RefusalOf("the scene reader", [bytes] -> void {
    static_cast<void>(data::SceneAsset(data::AssetKey {}, bytes));
  });
  const auto by_loader = RefusalOf("the scene loader", [bytes] -> void {
    auto stream = serio::ReadOnlyMemoryStream { bytes };
    auto reader = serio::Reader<serio::ReadOnlyMemoryStream> { stream };
    static_cast<void>(content::loaders::LoadSceneAsset(content::LoaderContext {
      .desc_reader = &reader,
      .work_offline = true,
      .parse_only = true,
    }));
  });
  if (by_reader != by_loader) {
    throw std::logic_error(
      "the scene reader and the scene loader refuse the scene differently");
  }
  return by_reader;
}

auto SidecarRefusal(const std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>
{
  const auto by_reader = RefusalOf("the sidecar reader", [bytes] -> void {
    static_cast<void>(data::PhysicsSceneAsset(data::AssetKey {}, bytes));
  });
  const auto by_loader = RefusalOf("the sidecar loader", [bytes] -> void {
    auto stream = serio::ReadOnlyMemoryStream { bytes };
    auto reader = serio::Reader<serio::ReadOnlyMemoryStream> { stream };
    static_cast<void>(
      content::loaders::LoadPhysicsSceneAsset(content::LoaderContext {
        .desc_reader = &reader,
        .work_offline = true,
        .parse_only = true,
      }));
  });
  if (by_reader != by_loader) {
    throw std::logic_error("the sidecar reader and the sidecar loader refuse "
                           "the sidecar differently");
  }
  return by_reader;
}

} // namespace oxygen::cooker::testing
