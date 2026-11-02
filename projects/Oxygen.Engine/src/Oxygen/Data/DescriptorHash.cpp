//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::data {

namespace {
  using pak::core::AssetHeader;
  using pak::core::ContentHashDigest;

  constexpr auto kHashOffset = offsetof(AssetHeader, content_hash);
  constexpr auto kHashSize = sizeof(AssetHeader::content_hash);

  //! The bytes of `descriptor` that hold its header's content hash.
  template <typename Byte>
  auto HashField(const std::span<Byte> descriptor) -> std::span<Byte, kHashSize>
  {
    if (descriptor.size() < sizeof(AssetHeader)) {
      throw CookedOpenError(CookedDefect::kTooSmall,
        "asset descriptor is smaller than its asset header");
    }
    return descriptor.template subspan<kHashOffset, kHashSize>();
  }
} // namespace

auto ComputeDescriptorHash(const std::span<const std::byte> descriptor)
  -> ContentHashDigest
{
  static_cast<void>(HashField(descriptor));
  constexpr auto kZeroHash = std::array<std::byte, kHashSize> {};
  auto hasher = base::Sha256 {};
  hasher.Update(descriptor.first(kHashOffset));
  hasher.Update(kZeroHash);
  hasher.Update(descriptor.subspan(kHashOffset + kHashSize));
  return hasher.Finalize();
}

auto StampDescriptorHash(const std::span<std::byte> descriptor) -> void
{
  const auto hash = ComputeDescriptorHash(descriptor);
  std::ranges::copy(
    std::as_bytes(std::span(hash)), HashField(descriptor).begin());
}

auto CheckDescriptorHash(const std::span<const std::byte> descriptor) -> void
{
  const auto recorded = HashField(descriptor);
  if (std::ranges::all_of(recorded,
        [](const std::byte value) -> bool { return value == std::byte {}; })) {
    throw CookedOpenError(
      CookedDefect::kMissingDigest, "asset descriptor records no content hash");
  }
  const auto expected = ComputeDescriptorHash(descriptor);
  if (!std::ranges::equal(recorded, std::as_bytes(std::span(expected)))) {
    throw CookedOpenError(CookedDefect::kDigestMismatch,
      "asset descriptor content hash does not match its bytes");
  }
}

} // namespace oxygen::data
