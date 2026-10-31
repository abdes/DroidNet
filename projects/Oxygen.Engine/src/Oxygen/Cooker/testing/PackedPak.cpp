//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/MemoryFile.h>

namespace oxygen::cooker::testing {

namespace {

  auto OpenPak(const content::cooked::CookerStorageReader& storage,
    const std::filesystem::path& path) -> std::unique_ptr<content::PakFile>
  {
    auto device = storage.Open(path);
    if (!device) {
      throw std::system_error(device.error(), "open '" + path.string() + "'");
    }
    return std::make_unique<content::PakFile>(std::move(device).value(), path);
  }

  auto OpenPak(const std::span<const std::byte> bytes)
    -> std::unique_ptr<content::PakFile>
  {
    auto file = serio::MemoryFile::FromBytes(bytes);
    if (!file) {
      throw std::system_error(file.error(), "hold the pak in memory");
    }
    return std::make_unique<content::PakFile>(
      serio::AnyDevice(std::move(file).value()), "memory.pak");
  }

} // namespace

PackedPak::PackedPak(const content::cooked::CookerStorageReader& storage,
  const std::filesystem::path& path)
  : pak_(OpenPak(storage, path))
{
  pak_->ValidateCrc32Integrity();
}

PackedPak::PackedPak(const std::span<const std::byte> bytes)
  : pak_(OpenPak(bytes))
{
  pak_->ValidateCrc32Integrity();
}

auto PackedPak::Descriptor(const data::AssetKey& key) const
  -> std::span<const std::byte>
{
  const auto entry = pak_->FindEntry(key);
  if (!entry) {
    throw std::out_of_range("the pak does not list the asset");
  }
  return pak_->DescriptorBytes(*entry);
}

auto PackedPak::Resolve(const data::AssetKey& key,
  const data::ResourceReferenceIndex reference,
  const data::ResourceKind kind) const -> std::optional<ResourceIndexT>
{
  const auto resolved
    = pak_->ReadAssetReferences(key).ResolveResource(reference, kind);
  if (!resolved) {
    throw std::runtime_error(resolved.error());
  }
  return *resolved;
}

auto PackedPak::Bytes() const -> std::span<const std::byte>
{
  const auto bytes = pak_->Device().Contiguous();
  if (!bytes) {
    throw std::logic_error("a pak is read from a contiguous device");
  }
  return *bytes;
}

} // namespace oxygen::cooker::testing
