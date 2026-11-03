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

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/ByteBuffer.h>
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
    } catch (const data::CookedFormatError& refused) {
      return refused.Defect();
    } catch (const content::ContentLoadError& refused) {
      throw std::logic_error(std::string(reader)
        + " refuses for a rule of the engine, not of the format: "
        + refused.what());
    } catch (const std::exception& failed) {
      throw std::logic_error(
        std::string(reader) + " refuses without a defect: " + failed.what());
    }
    return std::nullopt;
  }

  //! The engine rule `read` refuses with, none when it accepts.
  template <typename Read>
  auto RuleRefusalOf(const std::string_view reader, const Read& read)
    -> std::optional<content::ContentLoadFailure>
  {
    try {
      read();
    } catch (const content::ContentLoadError& refused) {
      return refused.Failure();
    } catch (const std::exception& failed) {
      throw std::logic_error(std::string(reader)
        + " refuses for no rule of the engine: " + failed.what());
    }
    return std::nullopt;
  }

  //! Reads the geometry descriptor `bytes` as a mount's geometry loader does.
  auto ReadGeometry(const std::span<const std::byte> bytes) -> void
  {
    auto stream = serio::ReadOnlyMemoryStream { bytes };
    auto reader = serio::Reader<serio::ReadOnlyMemoryStream> { stream };
    static_cast<void>(
      content::loaders::LoadGeometryAsset(content::LoaderContext {
        .desc_reader = &reader,
        .work_offline = true,
        .parse_only = true,
      }));
  }

  //! The defect `load` refuses the resource row `desc` and its `payload`
  //! with, read as a mount reads them: the row from the descriptor stream,
  //! the payload already read into its buffer.
  template <typename Desc, typename Load>
  auto ResourceRefusal(const std::string_view loader, const Desc& desc,
    const std::span<const std::byte> payload, const Load& load)
    -> std::optional<data::CookedDefect>
  {
    auto buffer = serio::ByteBuffer::CopyOf(payload).value();
    return RefusalOf(loader, [&desc, &buffer, &load] -> void {
      auto stream = serio::ReadOnlyMemoryStream {
        std::as_bytes(std::span { &desc, 1U }),
      };
      auto reader = serio::Reader<serio::ReadOnlyMemoryStream> { stream };
      load(content::LoaderContext {
        .desc_reader = &reader,
        .payload = observer_ptr<serio::ByteBuffer> { &buffer },
        .work_offline = true,
        .parse_only = true,
      });
    });
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

auto GeometryRefusal(const std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>
{
  return RefusalOf(
    "the geometry loader", [bytes] -> void { ReadGeometry(bytes); });
}

auto GeometryRuleRefusal(const std::span<const std::byte> bytes)
  -> std::optional<content::ContentLoadFailure>
{
  return RuleRefusalOf(
    "the geometry loader", [bytes] -> void { ReadGeometry(bytes); });
}

auto MaterialRefusal(const std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>
{
  return RefusalOf("the material loader", [bytes] -> void {
    auto stream = serio::ReadOnlyMemoryStream { bytes };
    auto reader = serio::Reader<serio::ReadOnlyMemoryStream> { stream };
    static_cast<void>(
      content::loaders::LoadMaterialAsset(content::LoaderContext {
        .desc_reader = &reader,
        .work_offline = true,
        .parse_only = true,
      }));
  });
}

auto TextureRefusal(const data::pak::core::TextureResourceDesc& desc,
  const std::span<const std::byte> payload) -> std::optional<data::CookedDefect>
{
  return ResourceRefusal("the texture loader", desc, payload,
    [](const content::LoaderContext& context) -> void {
      static_cast<void>(content::loaders::LoadTextureResource(context));
    });
}

auto BufferRefusal(const data::pak::core::BufferResourceDesc& desc,
  const std::span<const std::byte> payload) -> std::optional<data::CookedDefect>
{
  return ResourceRefusal("the buffer loader", desc, payload,
    [](const content::LoaderContext& context) -> void {
      static_cast<void>(content::loaders::LoadBufferResource(context));
    });
}

} // namespace oxygen::cooker::testing
