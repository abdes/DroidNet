//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::internal {

using oxygen::base::ComputeFileSha256;
using oxygen::base::IsAllZero;
using oxygen::base::Sha256Digest;

//! Asset location within a PAK file.
struct PakAssetLocator {
  data::pak::AssetDirectoryEntry entry {};
};

//! Asset location within a loose cooked root.
struct LooseCookedAssetLocator {
  std::filesystem::path descriptor_path;
};

//! Type-erased locator for an asset descriptor.
using AssetLocator = std::variant<PakAssetLocator, LooseCookedAssetLocator>;

//! Minimal runtime-facing abstraction over a source of cooked bytes.
/*!
 A ContentSource provides cooked descriptor bytes and cooked resource bytes.

 This is an internal runtime abstraction used by the loader pipeline to treat
 different storage forms uniformly (e.g. `.pak` vs loose cooked directories).

 It is not an editor mount-point abstraction.
*/
class IContentSource : public oxygen::Object {
public:
  ~IContentSource() override = default;

  IContentSource() = default;

  IContentSource(const IContentSource&) = delete;
  IContentSource(IContentSource&&) = delete;
  auto operator=(const IContentSource&) -> IContentSource& = delete;
  auto operator=(IContentSource&&) -> IContentSource& = delete;

  [[nodiscard]] virtual auto DebugName() const noexcept -> std::string_view = 0;

  [[nodiscard]] virtual auto GetSourceKey() const noexcept -> data::SourceKey
    = 0;

  [[nodiscard]] virtual auto FindAsset(const data::AssetKey& key) const noexcept
    -> std::optional<AssetLocator>
    = 0;
  [[nodiscard]] virtual auto GetAssetCount() const noexcept -> size_t = 0;
  [[nodiscard]] virtual auto GetAssetKeyByIndex(uint32_t index) const noexcept
    -> std::optional<data::AssetKey>
    = 0;

  [[nodiscard]] virtual auto CreateAssetDescriptorReader(
    const AssetLocator& locator) const -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreatePhysicsTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* = 0;

  [[nodiscard]] virtual auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* = 0;

  [[nodiscard]] virtual auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* = 0;

  [[nodiscard]] virtual auto GetPhysicsTable() const noexcept
    -> const ResourceTable<data::PhysicsResource>* = 0;

  [[nodiscard]] virtual auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreatePhysicsDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto ScriptSlotCount() const noexcept -> uint32_t = 0;

  [[nodiscard]] virtual auto ReadScriptSlotRecords(uint32_t start_index,
    uint32_t count) const -> std::vector<data::pak::ScriptSlotRecord>
    = 0;

  [[nodiscard]] virtual auto ReadScriptParamRecords(
    data::pak::OffsetT absolute_offset, uint32_t count) const
    -> std::vector<data::pak::ScriptParamRecord>
    = 0;
};

} // namespace oxygen::content::internal
