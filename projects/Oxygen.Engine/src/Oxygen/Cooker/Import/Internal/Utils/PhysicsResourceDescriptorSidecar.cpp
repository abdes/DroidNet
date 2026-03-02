//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <span>
#include <type_traits>

#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

#include <Oxygen/Cooker/Import/Internal/Utils/PhysicsResourceDescriptorSidecar.h>

namespace oxygen::content::import::internal {

namespace {

  constexpr uint16_t kPhysicsResourceSidecarVersion = 1;

#pragma pack(push, 1)
  struct PhysicsResourceSidecarFile final {
    char magic[4] = { 'O', 'P', 'R', 'S' };
    uint16_t version = kPhysicsResourceSidecarVersion;
    uint16_t reserved = 0;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::physics::PhysicsResourceDesc descriptor {};
  };
#pragma pack(pop)

  static_assert(std::is_trivially_copyable_v<PhysicsResourceSidecarFile>);
  static_assert(std::is_standard_layout_v<PhysicsResourceSidecarFile>);
  static_assert(
    std::has_unique_object_representations_v<PhysicsResourceSidecarFile>);

  auto Store(serio::AnyWriter& writer, const PhysicsResourceSidecarFile& value)
    -> oxygen::Result<void>
  {
    [[maybe_unused]] const auto pack = writer.ScopedAlignment(1);
    return writer.WriteBlob(
      std::as_bytes(std::span<const PhysicsResourceSidecarFile, 1>(&value, 1)));
  }

  auto Load(serio::AnyReader& reader, PhysicsResourceSidecarFile& value)
    -> oxygen::Result<void>
  {
    [[maybe_unused]] const auto pack = reader.ScopedAlignment(1);
    return reader.ReadBlobInto(std::as_writable_bytes(
      std::span<PhysicsResourceSidecarFile, 1>(&value, 1)));
  }

} // namespace

auto SerializePhysicsResourceDescriptorSidecar(
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::physics::PhysicsResourceDesc& descriptor)
  -> std::vector<std::byte>
{
  const auto file = PhysicsResourceSidecarFile {
    .magic = { 'O', 'P', 'R', 'S' },
    .version = kPhysicsResourceSidecarVersion,
    .reserved = 0,
    .resource_index = resource_index,
    .descriptor = descriptor,
  };

  auto stream = serio::MemoryStream {};
  auto writer = serio::Writer(stream);
  [[maybe_unused]] const auto write = writer.Write(file);
  DCHECK_F(write.has_value(), "Physics resource sidecar serialization failed");
  const auto bytes = stream.Data();
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

auto ParsePhysicsResourceDescriptorSidecar(
  const std::span<const std::byte> bytes,
  ParsedPhysicsResourceDescriptorSidecar& out, std::string& error_message)
  -> bool
{
  error_message.clear();
  out = ParsedPhysicsResourceDescriptorSidecar {};

  if (bytes.size() < sizeof(PhysicsResourceSidecarFile)) {
    error_message = "Physics resource descriptor file is truncated";
    return false;
  }

  auto stream = serio::ReadOnlyMemoryStream(bytes);
  auto reader = serio::Reader(stream);
  auto read_result = reader.Read<PhysicsResourceSidecarFile>();
  if (!read_result.has_value()) {
    error_message = "Physics resource descriptor parse failed";
    return false;
  }

  const auto& file = read_result.value();
  if (std::memcmp(file.magic, "OPRS", 4) != 0) {
    error_message = "Physics resource descriptor has invalid magic";
    return false;
  }
  if (file.version != kPhysicsResourceSidecarVersion) {
    error_message = "Physics resource descriptor has unsupported version";
    return false;
  }

  out.resource_index = file.resource_index;
  out.descriptor = file.descriptor;
  return true;
}

} // namespace oxygen::content::import::internal
