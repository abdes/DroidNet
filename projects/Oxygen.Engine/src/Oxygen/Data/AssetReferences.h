//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Index into one descriptor's binding table, never a container resource index.
using ResourceReferenceIndex = NamedType<uint32_t,
  struct ResourceReferenceIndexTag, Comparable, Printable>;
inline constexpr ResourceReferenceIndex kNoResourceReference {
  std::numeric_limits<uint32_t>::max()
};

//! Stable wire values, independent of runtime TypeId and typelist order.
enum class ResourceKind : uint8_t {
  kBuffer = 1,
  kTexture = 2,
  kScript = 3,
  kPhysics = 4,
};

struct ResourceBinding final {
  ResourceKind kind = ResourceKind::kBuffer;
  ResourceIndexT index { 0U };

  auto operator==(const ResourceBinding&) const -> bool = default;
};

//! A typed descriptor field's use of a local binding slot.
struct ResourceReferenceUse final {
  ResourceReferenceIndex reference { kNoResourceReference };
  ResourceKind kind = ResourceKind::kBuffer;
};

//! Counts include the reserved entry at index zero; zero means no table.
struct ResourceTableCounts final {
  uint64_t buffers { 0U };
  uint64_t textures { 0U };
  uint64_t scripts { 0U };
  uint64_t physics { 0U };
};

enum class KeyReferenceKind : uint8_t {
  kAsset = 1,
  kPhysicsResource = 2,
  kLogical = 3,
};

struct KeyReference final {
  AssetKey key;
  KeyReferenceKind kind = KeyReferenceKind::kAsset;
  AssetType expected_type = AssetType::kUnknown;

  auto operator==(const KeyReference&) const -> bool = default;
};

//! Immutable reference inventory carried beside an opaque asset descriptor.
//! Source owners validate physical table bounds and key targets separately.
class AssetReferences final {
public:
  static constexpr size_t kResourceBindingSize
    = sizeof(uint8_t) + sizeof(uint32_t);
  static constexpr size_t kKeyReferenceSize
    = AssetKey::kSizeBytes + (2U * sizeof(uint8_t));

  AssetReferences() = default;

  auto operator==(const AssetReferences&) const -> bool = default;

  [[nodiscard]] static constexpr auto EncodedSize(
    uint32_t resource_count, uint32_t key_count) noexcept -> uint64_t
  {
    return (uint64_t { resource_count } * kResourceBindingSize)
      + (uint64_t { key_count } * kKeyReferenceSize);
  }

  //! Validate bindings and canonicalize key order. Distinct local slots may
  //! target the same resource after relocation; their positions stay stable.
  OXGN_DATA_NDAPI static auto Create(std::vector<ResourceBinding> resources,
    std::vector<KeyReference> keys) -> Result<AssetReferences, std::string>;

  //! Decode exactly the declared packed arrays, checking sizes before
  //! allocation.
  OXGN_DATA_NDAPI static auto Decode(std::span<const std::byte> bytes,
    uint32_t resource_count, uint32_t key_count)
    -> Result<AssetReferences, std::string>;

  OXGN_DATA_NDAPI auto Encode() const
    -> Result<std::vector<std::byte>, std::string>;

  [[nodiscard]] auto Resources() const noexcept
    -> std::span<const ResourceBinding>
  {
    return resources_;
  }

  [[nodiscard]] auto Keys() const noexcept -> std::span<const KeyReference>
  {
    return keys_;
  }

  //! Absence returns nullopt. Texture bindings preserve the fallback and error
  //! markers; source owners do not look up a container payload for the latter.
  OXGN_DATA_NDAPI auto ResolveResource(
    ResourceReferenceIndex reference, ResourceKind expected_kind) const
    -> Result<std::optional<ResourceIndexT>, std::string>;

  //! Check exact agreement with fields observed by the typed decoder.
  //! Repeated uses and relocated aliases are valid; unused slots, undeclared
  //! keys and conflicting kinds/types are not. Nil optional keys are omitted.
  OXGN_DATA_NDAPI auto ValidateUsage(
    std::span<const ResourceReferenceUse> resources,
    std::span<const KeyReference> keys) const -> Result<void, std::string>;

  OXGN_DATA_NDAPI auto ValidateResourceBounds(
    const ResourceTableCounts& counts) const -> Result<void, std::string>;

private:
  std::vector<ResourceBinding> resources_;
  std::vector<KeyReference> keys_;
};

} // namespace oxygen::data
