//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::cooker::testing {

CookedPak::CookedPak(LooseCookedRoot root)
  : CookedPak(std::vector { std::move(root) })
{
}

CookedPak::CookedPak(std::vector<LooseCookedRoot> roots)
  : roots_(std::move(roots))
{
  if (roots_.empty()) {
    throw std::invalid_argument("a pak holds at least one loose cooked root");
  }
}

auto CookedPak::DependsOn(const CookedPak& dependency) -> CookedPak&
{
  const auto pak = dependency.Open();
  dependencies_.push_back({
    .catalog = pak.Pak().Catalog(),
    .textures = content::pak::ReadPakTextures(pak.Pak()),
  });
  return *this;
}

auto CookedPak::PatchOf(const CookedPak& base) -> CookedPak&
{
  const auto pak = base.Open();
  bases_.push_back(pak.Pak().Catalog());
  base_textures_.push_back(content::pak::ReadPakTextures(pak.Pak()));
  return *this;
}

auto CookedPak::ContentVersion(const uint16_t version) -> CookedPak&
{
  content_version_ = version;
  return *this;
}

auto CookedPak::Identity(const data::SourceKey& key) -> CookedPak&
{
  identity_ = key;
  return *this;
}

auto CookedPak::Encode() const -> std::vector<std::byte>
{
  auto workspace = PakWorkspace {};
  auto sources = std::vector<data::CookedSource> {};
  for (const auto& root : roots_) {
    sources.push_back(workspace.Add(root));
  }
  auto request = bases_.empty()
    ? workspace.FullRequest(std::move(sources))
    : workspace.PatchRequest(std::move(sources), bases_);
  request.content_version = content_version_;
  request.source_key = identity_.value_or(DefaultIdentity());
  request.dependencies = dependencies_;
  request.base_textures = base_textures_;
  const auto pak = workspace.PackOrThrow(request);
  const auto bytes = pak.Bytes();
  return { bytes.begin(), bytes.end() };
}

auto CookedPak::Open() const -> PackedPak { return PackedPak { Encode() }; }

auto CookedPak::Catalog() const -> data::PakCatalog
{
  return Open().Pak().Catalog();
}

auto CookedPak::DefaultIdentity() const -> data::SourceKey
{
  constexpr auto kFirstPakSeed = uint8_t { 0x70U };
  auto seed = static_cast<uint8_t>(kFirstPakSeed + bases_.size());
  for (const auto c : roots_.front().Name()) {
    seed = static_cast<uint8_t>(seed + static_cast<uint8_t>(c));
  }
  return SourceKeyOf(seed);
}

auto EncodeBrowseIndex(const content::pak::PakBrowseIndexPlan& browse)
  -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte> {};
  if (!content::pak::StoreBrowseIndexPayload(browse, bytes)) {
    throw std::invalid_argument("the product cannot encode the browse index");
  }
  return bytes;
}

} // namespace oxygen::cooker::testing
