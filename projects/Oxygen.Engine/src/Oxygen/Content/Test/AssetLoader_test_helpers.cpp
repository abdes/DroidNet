//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/PakFormat_core.h>

#include "./AssetLoader_test.h"

using oxygen::content::testing::AssetLoaderLoadingTest;

namespace {

constexpr auto kCrcInitialState = uint32_t { 0xFFFFFFFFU };
constexpr auto kCrcFinalXor = uint32_t { 0xFFFFFFFFU };
constexpr auto kCrcReflectedPolynomial = uint32_t { 0xEDB88320U };
constexpr auto kByteBitCount = uint32_t { 8U };

auto UpdateCrc32Ieee(
  const uint32_t state, const std::span<const std::byte> bytes) -> uint32_t
{
  auto crc = state;
  for (const auto byte : bytes) {
    crc ^= static_cast<uint32_t>(std::to_integer<uint8_t>(byte));
    for (auto bit = uint32_t { 0U }; bit < kByteBitCount; ++bit) {
      const auto lsb = (crc & 1U) != 0U;
      crc >>= 1U;
      if (lsb) {
        crc ^= kCrcReflectedPolynomial;
      }
    }
  }
  return crc;
}

auto ComputeFileCrc32SkippingRange(const std::span<const std::byte> bytes,
  const size_t skip_offset, const size_t skip_size) -> uint32_t
{
  auto state = kCrcInitialState;
  if (skip_offset > bytes.size()) {
    throw std::runtime_error("PAK CRC skip offset exceeds file size");
  }

  state = UpdateCrc32Ieee(state, bytes.first(skip_offset));

  auto resume_offset = skip_offset;
  if (skip_size <= (bytes.size() - skip_offset)) {
    resume_offset += skip_size;
  } else {
    resume_offset = bytes.size();
  }
  state = UpdateCrc32Ieee(
    state, bytes.subspan(resume_offset, bytes.size() - resume_offset));
  return state ^ kCrcFinalXor;
}

auto MakeDeterministicSourceIdentity(std::string_view seed)
  -> std::array<uint8_t, 16>
{
  const auto seed_bytes = std::as_bytes(std::span(seed.data(), seed.size()));
  const auto digest = oxygen::base::ComputeSha256(seed_bytes);

  auto bytes = std::array<uint8_t, 16> {};
  std::ranges::copy_n(digest.begin(), bytes.size(), bytes.begin());
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x70U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);

  if (std::ranges::all_of(
        bytes, [](const uint8_t value) noexcept { return value == 0U; })) {
    bytes.back() = 1U;
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x70U);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
  }

  return bytes;
}

auto NormalizePakSourceIdentity(
  const std::filesystem::path& pak_path, std::string_view seed) -> void
{
  using PakFooter = oxygen::data::pak::core::PakFooter;
  using PakHeader = oxygen::data::pak::core::PakHeader;

  std::ifstream in(pak_path, std::ios::binary);
  if (!in) {
    throw std::runtime_error(
      "Failed to open generated PAK for reading: " + pak_path.string());
  }

  const auto file_size = std::filesystem::file_size(pak_path);
  if (file_size < (sizeof(PakHeader) + sizeof(PakFooter))) {
    throw std::runtime_error(
      "Generated PAK is too small to normalize source identity: "
      + pak_path.string());
  }

  auto bytes = std::vector<std::byte>(static_cast<size_t>(file_size));
  in.read(reinterpret_cast<char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    throw std::runtime_error(
      "Failed to read generated PAK bytes: " + pak_path.string());
  }

  auto header = PakHeader {};
  std::memcpy(&header, bytes.data(), sizeof(header));
  header.source_identity = MakeDeterministicSourceIdentity(seed);
  std::memcpy(bytes.data(), &header, sizeof(header));

  const auto footer_offset = bytes.size() - sizeof(PakFooter);
  auto footer = PakFooter {};
  std::memcpy(&footer, bytes.data() + footer_offset, sizeof(footer));
  if (footer.pak_crc32 != 0U) {
    const auto crc_offset = footer_offset + offsetof(PakFooter, pak_crc32);
    footer.pak_crc32 = ComputeFileCrc32SkippingRange(
      bytes, crc_offset, sizeof(footer.pak_crc32));
    std::memcpy(bytes.data() + footer_offset, &footer, sizeof(footer));
  }

  std::ofstream out(pak_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error(
      "Failed to open generated PAK for writing: " + pak_path.string());
  }

  out.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    throw std::runtime_error(
      "Failed to rewrite generated PAK bytes: " + pak_path.string());
  }
}

} // namespace

auto AssetLoaderLoadingTest::GetTestDataDir() -> std::filesystem::path
{
  // Path to test data directory containing YAML specs
  return std::filesystem::path(__FILE__).parent_path() / "TestData";
}

auto AssetLoaderLoadingTest::GeneratePakFile(const std::string& spec_name)
  -> std::filesystem::path
{
  const auto test_data_dir = GetTestDataDir();
  const auto spec_path = test_data_dir / (spec_name + ".yaml");
  auto output_path = temp_dir_ / (spec_name + ".pak");
  std::filesystem::create_directories(output_path.parent_path());

  // Check if YAML spec exists
  if (!std::filesystem::exists(spec_path)) {
    throw std::runtime_error("Test spec not found: " + spec_path.string());
  }

  // Generate a deterministic PAK file from the YAML spec.
  std::string command;
  {
    command = "pakgen build \"" + spec_path.string() + "\" \""
      + output_path.string() + "\" --deterministic";
  }

  auto run_command
    = [&](const std::string& cmd) -> int { return std::system(cmd.c_str()); };

  int result = run_command(command);
  if (result != 0) {
    const std::string module_cmd = "python -m pakgen.cli build \""
      + spec_path.string() + "\" \"" + output_path.string()
      + "\" --deterministic";
    result = run_command(module_cmd);
  }

  if (result != 0) {
    throw std::runtime_error(
      "Failed to generate PAK file for spec: " + spec_name);
  }

  if (!std::filesystem::exists(output_path)) {
    throw std::runtime_error(
      "PAK file was not created: " + output_path.string());
  }

  NormalizePakSourceIdentity(output_path, spec_name);
  generated_paks_.push_back(output_path);

  return output_path;
}

auto AssetLoaderLoadingTest::CreateTestAssetKey(const std::string& name)
  -> oxygen::data::AssetKey
{
  using data::AssetKey;
  const auto make_key = [](std::initializer_list<std::uint8_t> bytes) {
    auto key_bytes = std::array<std::uint8_t, AssetKey::kSizeBytes> {};
    std::copy_n(bytes.begin(), std::min(bytes.size(), key_bytes.size()),
      key_bytes.begin());
    return AssetKey::FromBytes(key_bytes);
  };

  if (name == "test_material") {
    return make_key({ 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
      0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef });
  }
  if (name == "test_geometry") {
    return make_key({ 0xaa, 0xaa, 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd,
      0xdd, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee });
  }
  if (name == "textured_material") {
    return make_key({ 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12,
      0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef });
  }
  if (name == "buffered_geometry") {
    return make_key({ 0xff, 0xff, 0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd, 0xcc,
      0xcc, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb });
  }
  if (name == "complex_geometry" || name == "SpaceshipGeometry") {
    return make_key({ 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe, 0xde,
      0xad, 0xfe, 0xed, 0xde, 0xad, 0xbe, 0xef });
  }
  if (name == "test_scene") {
    return make_key({ 0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55,
      0x55, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66 });
  }
  if (name == "test_scene_no_renderables") {
    return make_key({ 0x33, 0x33, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66,
      0x66, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77 });
  }
  if (name == "test_scene_duplicate_renderables") {
    return make_key({ 0x44, 0x44, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77,
      0x77, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 });
  }
  if (name == "test_scene_two_geometries") {
    return make_key({ 0x55, 0x55, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77, 0x88,
      0x88, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99 });
  }
  if (name == "test_scene_lights_env") {
    return make_key({ 0x77, 0x77, 0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0xaa,
      0xaa, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb });
  }
  if (name == "test_input_action_accelerate") {
    return make_key({ 0x88, 0x88, 0x88, 0x88, 0x11, 0x11, 0x22, 0x22, 0x33,
      0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 });
  }
  if (name == "test_input_action_decelerate") {
    return make_key({ 0x99, 0x99, 0x99, 0x99, 0x11, 0x11, 0x22, 0x22, 0x33,
      0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 });
  }
  if (name == "test_input_mapping_context") {
    return make_key({ 0xaa, 0xaa, 0xaa, 0xaa, 0x11, 0x11, 0x22, 0x22, 0x33,
      0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 });
  }
  if (name == "test_scene_with_input_context") {
    return make_key({ 0xbb, 0xbb, 0xbb, 0xbb, 0x11, 0x11, 0x22, 0x22, 0x33,
      0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 });
  }
  if (name == "test_scene_invalid_unknown_geometry") {
    return make_key({ 0x66, 0x66, 0x66, 0x66, 0x77, 0x77, 0x88, 0x88, 0x99,
      0x99, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa });
  }
  if (name == "test_scene_with_physics") {
    return make_key({ 0xcc, 0xcc, 0xcc, 0xcc, 0x11, 0x11, 0x22, 0x22, 0x33,
      0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 });
  }
  if (name == "test_scene_with_physics_sidecar") {
    return make_key({ 0xff, 0xff, 0xff, 0xff, 0x11, 0x11, 0x22, 0x22, 0x33,
      0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 });
  }
  if (name == "test_script") {
    return make_key({ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 });
  }
  if (name == "test_scene_with_scripting") {
    return make_key({ 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 });
  }
  if (name == "duplicate_shared_material") {
    return make_key({ 0xab, 0xcd, 0xab, 0xcd, 0xab, 0xcd, 0xab, 0xcd, 0xab,
      0xcd, 0xab, 0xcd, 0xab, 0xcd, 0xab, 0xcd });
  }
  if (name == "duplicate_source_a_geometry") {
    return make_key({ 0xee, 0xee, 0xee, 0xee, 0xdd, 0xdd, 0xcc, 0xcc, 0xbb,
      0xbb, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa });
  }

  // Fallback: create a deterministic key from hash for unknown names.
  constexpr std::hash<std::string> hasher;
  const auto hash = hasher(name);
  auto key_bytes = std::array<std::uint8_t, AssetKey::kSizeBytes> {};
  const auto hash_bytes
    = std::bit_cast<std::array<std::uint8_t, sizeof(hash)>>(hash);
  std::copy_n(hash_bytes.begin(), std::min(hash_bytes.size(), key_bytes.size()),
    key_bytes.begin());
  return AssetKey::FromBytes(key_bytes);
}
