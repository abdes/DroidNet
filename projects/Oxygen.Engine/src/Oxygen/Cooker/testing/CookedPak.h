//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::cooker::testing {

//! A pak of one mount a test describes: its loose cooked roots, the paks it
//! depends on and, for a patch, the paks it patches.
/*!
 `Encode` packs it with the product's planner and writer, in memory, so the
 kit decides nothing about the pak format: the pak lists, requires and
 deletes what the product derives from the description. A pak that references
 `/Engine/Default.omat` and depends on a pak of `Engine` requires that asset
 of `Engine`; a patch deletes every asset of its bases its roots no longer
 hold.

 ```cpp
 const auto engine = CookedPak { engine_root };
 const auto bytes = CookedPak { game }.DependsOn(engine).Encode();
 ```

 Encoding throws, with every diagnostic, when the product rejects the
 description.
*/
class CookedPak final {
public:
  explicit CookedPak(LooseCookedRoot root);
  explicit CookedPak(std::vector<LooseCookedRoot> roots);

  //! Makes `dependency` a pak this pak references without containing.
  auto DependsOn(const CookedPak& dependency) -> CookedPak&;

  //! Makes this pak a patch of `base`; patch `PatchOf` several bases, lowest
  //! first.
  auto PatchOf(const CookedPak& base) -> CookedPak&;

  //! The content version of the pak; 1 by default.
  auto ContentVersion(uint16_t version) -> CookedPak&;

  //! The identity of the pak; by default one derived from its mount and its
  //! number of bases, so a patch never shares its base's identity.
  auto Identity(const data::SourceKey& key) -> CookedPak&;

  //! The bytes of the pak.
  [[nodiscard]] auto Encode() const -> std::vector<std::byte>;

  //! The pak, opened from its bytes.
  [[nodiscard]] auto Open() const -> PackedPak;

  //! The catalog of the pak.
  [[nodiscard]] auto Catalog() const -> data::PakCatalog;

private:
  [[nodiscard]] auto DefaultIdentity() const -> data::SourceKey;

  std::vector<LooseCookedRoot> roots_;
  std::vector<content::pak::PakDependency> dependencies_;
  std::vector<data::PakCatalog> bases_;
  //! The textures of each of `bases_`, in its order.
  std::vector<content::pak::PakTextures> base_textures_;
  uint16_t content_version_ = 1U;
  std::optional<data::SourceKey> identity_;
};

//! The bytes of the browse index `browse` describes, encoded on its own by
//! the product's encoder, for tests of browse decoding.
[[nodiscard]] auto EncodeBrowseIndex(
  const content::pak::PakBrowseIndexPlan& browse) -> std::vector<std::byte>;

} // namespace oxygen::cooker::testing
