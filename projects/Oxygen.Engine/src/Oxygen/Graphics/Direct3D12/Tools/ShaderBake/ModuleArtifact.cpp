//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Serio/MemoryStream.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  using oxygen::graphics::serio_utils::CheckedSizeT;
  using oxygen::graphics::serio_utils::CheckedU16Size;
  using oxygen::graphics::serio_utils::PackedGuard;
  using oxygen::graphics::serio_utils::ReadUtf8String16;
  using oxygen::graphics::serio_utils::ThrowOnError;
  using oxygen::graphics::serio_utils::ValueOrThrow;
  using oxygen::graphics::serio_utils::WriteUtf8String16;

  auto ValidateDependencyPath(std::string_view path) -> void
  {
    if (path.empty()) {
      throw std::runtime_error("module artifact dependency path is empty");
    }

    std::filesystem::path dependency_path(path);
    if (dependency_path.is_absolute()) {
      throw std::runtime_error(
        "module artifact dependency path must be relative");
    }

    if (dependency_path.lexically_normal().generic_string() != path) {
      throw std::runtime_error(
        "module artifact dependency path must be normalized");
    }
  }

  auto ValidateModuleArtifact(const ModuleArtifact& artifact) -> ModuleArtifact
  {
    auto canonical_request
      = oxygen::graphics::CanonicalizeShaderRequest(ShaderRequest {
        artifact.request,
      });
    if (canonical_request != artifact.request) {
      throw std::runtime_error(
        "module artifact request is not stored in canonical form");
    }

    if (artifact.request.stage == ShaderType::kUnknown
      || artifact.request.stage > ShaderType::kMaxShaderType) {
      throw std::runtime_error("module artifact contains invalid shader stage");
    }

    if (const auto expected_request_key
      = oxygen::graphics::ComputeShaderRequestKey(canonical_request);
      expected_request_key != artifact.request_key) {
      throw std::runtime_error(fmt::format(
        "module artifact request key mismatch: expected {:016x}, got {:016x}",
        expected_request_key, artifact.request_key));
    }

    for (const auto& dependency : artifact.dependencies) {
      ValidateDependencyPath(dependency.path);
    }

    return ModuleArtifact {
      .request_key = artifact.request_key,
      .action_key = artifact.action_key,
      .toolchain_hash = artifact.toolchain_hash,
      .request = std::move(canonical_request),
      .primary_hash = artifact.primary_hash,
      .dependencies = artifact.dependencies,
      .dxil = artifact.dxil,
      .reflection = artifact.reflection,
    };
  }

  auto ReadBinaryFile(const std::filesystem::path& file)
    -> std::vector<std::byte>
  {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
      throw std::runtime_error(
        "failed to open module artifact `" + ToUtf8PathString(file) + "`");
    }

    std::vector<char> bytes {
      std::istreambuf_iterator<char>(in),
      std::istreambuf_iterator<char>(),
    };
    return std::vector<std::byte>(
      reinterpret_cast<const std::byte*>(bytes.data()),
      reinterpret_cast<const std::byte*>(bytes.data()) + bytes.size());
  }

  auto WriteReservedBytes(serio::AnyWriter& writer) -> void
  {
    constexpr std::array<std::byte, 7> kReserved {};
    ThrowOnError(writer.WriteBlob(kReserved), "write reserved bytes");
  }

  auto ReadReservedBytes(serio::AnyReader& reader) -> void
  {
    std::array<std::byte, 7> reserved {};
    ThrowOnError(reader.ReadBlobInto(reserved), "read reserved bytes");
  }

  auto WriteOptionalString16(
    serio::AnyWriter& writer, const std::optional<std::string>& value) -> void
  {
    WriteUtf8String16(writer, value.value_or(""));
  }

  auto ReadOptionalString16(serio::AnyReader& reader)
    -> std::optional<std::string>
  {
    auto value = ReadUtf8String16(reader);
    if (value.empty()) {
      return std::nullopt;
    }
    return value;
  }

} // namespace

auto WriteModuleArtifactFile(const std::filesystem::path& artifact_path,
  const ModuleArtifact& artifact) -> void
{
  const auto validated_artifact = ValidateModuleArtifact(artifact);

  serio::MemoryStream stream;
  serio::Writer<serio::MemoryStream> writer(stream);
  const auto packed = PackedGuard(writer);
  (void)packed;

  ThrowOnError(writer.Write<uint32_t>(kOxsmMagic), "write oxsm magic");
  ThrowOnError(writer.Write<uint32_t>(kOxsmVersion), "write oxsm version");
  ThrowOnError(writer.Write<uint64_t>(validated_artifact.request_key),
    "write request key");
  ThrowOnError(
    writer.Write<uint64_t>(validated_artifact.action_key), "write action key");
  ThrowOnError(writer.Write<uint64_t>(validated_artifact.toolchain_hash),
    "write toolchain hash");
  ThrowOnError(writer.Write<uint8_t>(
                 static_cast<uint8_t>(validated_artifact.request.stage)),
    "write shader stage");
  WriteReservedBytes(writer);
  WriteUtf8String16(writer, validated_artifact.request.source_path);
  WriteUtf8String16(writer, validated_artifact.request.entry_point);
  ThrowOnError(writer.Write<uint16_t>(CheckedU16Size(
                 validated_artifact.request.defines.size(), "define list")),
    "write define count");
  for (const auto& define : validated_artifact.request.defines) {
    WriteUtf8String16(writer, define.name);
    WriteOptionalString16(writer, define.value);
  }

  ThrowOnError(writer.Write<uint64_t>(validated_artifact.primary_hash),
    "write primary hash");
  ThrowOnError(writer.Write<uint32_t>(
                 static_cast<uint32_t>(validated_artifact.dependencies.size())),
    "write dependency count");
  ThrowOnError(
    writer.Write<uint64_t>(validated_artifact.dxil.size()), "write dxil size");
  ThrowOnError(writer.Write<uint64_t>(validated_artifact.reflection.size()),
    "write reflection size");

  for (const auto& dependency : validated_artifact.dependencies) {
    WriteUtf8String16(writer, dependency.path);
    ThrowOnError(
      writer.Write<uint64_t>(dependency.content_hash), "write dependency hash");
    ThrowOnError(
      writer.Write<uint64_t>(dependency.size_bytes), "write dependency size");
    ThrowOnError(writer.Write<int64_t>(dependency.write_time_utc),
      "write dependency write time");
  }

  if (!validated_artifact.dxil.empty()) {
    ThrowOnError(
      writer.WriteBlob(validated_artifact.dxil), "write dxil payload");
  }
  if (!validated_artifact.reflection.empty()) {
    ThrowOnError(writer.WriteBlob(validated_artifact.reflection),
      "write reflection payload");
  }
  ThrowOnError(writer.Flush(), "flush oxsm stream");

  WriteBinaryFileAtomically(artifact_path, stream.Data());
}

auto ReadModuleArtifactFile(const std::filesystem::path& artifact_path)
  -> ModuleArtifact
{
  const auto bytes = ReadBinaryFile(artifact_path);

  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader<serio::ReadOnlyMemoryStream> reader(stream);
  const auto packed = PackedGuard(reader);
  (void)packed;

  const auto magic = ValueOrThrow(reader.Read<uint32_t>(), "read oxsm magic");
  if (magic != kOxsmMagic) {
    throw std::runtime_error(fmt::format(
      "invalid oxsm magic in `{}`", ToUtf8PathString(artifact_path)));
  }

  const auto version
    = ValueOrThrow(reader.Read<uint32_t>(), "read oxsm version");
  if (version != kOxsmVersion) {
    throw std::runtime_error(fmt::format("unsupported oxsm version {} in `{}`",
      version, ToUtf8PathString(artifact_path)));
  }

  ModuleArtifact artifact;
  artifact.request_key
    = ValueOrThrow(reader.Read<uint64_t>(), "read request key");
  artifact.action_key
    = ValueOrThrow(reader.Read<uint64_t>(), "read action key");
  artifact.toolchain_hash
    = ValueOrThrow(reader.Read<uint64_t>(), "read toolchain hash");

  const auto stage = ValueOrThrow(reader.Read<uint8_t>(), "read shader stage");
  artifact.request.stage = static_cast<ShaderType>(stage);
  ReadReservedBytes(reader);

  artifact.request.source_path = ReadUtf8String16(reader);
  artifact.request.entry_point = ReadUtf8String16(reader);

  const auto define_count
    = ValueOrThrow(reader.Read<uint16_t>(), "read define count");
  artifact.request.defines.reserve(define_count);
  for (uint16_t index = 0; index < define_count; ++index) {
    artifact.request.defines.push_back(ShaderDefine {
      .name = ReadUtf8String16(reader),
      .value = ReadOptionalString16(reader),
    });
  }

  artifact.primary_hash
    = ValueOrThrow(reader.Read<uint64_t>(), "read primary hash");
  const auto dependency_count
    = ValueOrThrow(reader.Read<uint32_t>(), "read dependency count");
  const auto dxil_size = CheckedSizeT(
    ValueOrThrow(reader.Read<uint64_t>(), "read dxil size"), "dxil payload");
  const auto reflection_size = CheckedSizeT(
    ValueOrThrow(reader.Read<uint64_t>(), "read reflection size"),
    "reflection payload");

  artifact.dependencies.reserve(dependency_count);
  for (uint32_t index = 0; index < dependency_count; ++index) {
    artifact.dependencies.push_back(DependencyFingerprint {
      .path = ReadUtf8String16(reader),
      .content_hash
      = ValueOrThrow(reader.Read<uint64_t>(), "read dependency hash"),
      .size_bytes
      = ValueOrThrow(reader.Read<uint64_t>(), "read dependency size"),
      .write_time_utc
      = ValueOrThrow(reader.Read<int64_t>(), "read dependency write time"),
    });
  }

  artifact.dxil = ValueOrThrow(reader.ReadBlob(dxil_size), "read dxil payload");
  artifact.reflection
    = ValueOrThrow(reader.ReadBlob(reflection_size), "read reflection payload");

  const auto final_pos = ValueOrThrow(reader.Position(), "read final position");
  const auto total_size = ValueOrThrow(stream.Size(), "read oxsm size");
  if (final_pos != total_size) {
    throw std::runtime_error(
      fmt::format("module artifact `{}` has trailing bytes",
        ToUtf8PathString(artifact_path)));
  }

  return ValidateModuleArtifact(artifact);
}

auto TryReadModuleArtifactFile(const std::filesystem::path& artifact_path)
  -> std::optional<ModuleArtifact>
{
  try {
    if (!std::filesystem::exists(artifact_path)) {
      return std::nullopt;
    }

    return ReadModuleArtifactFile(artifact_path);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
