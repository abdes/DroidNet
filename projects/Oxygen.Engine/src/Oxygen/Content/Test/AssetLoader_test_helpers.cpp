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
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>

#include "./AssetLoader_test.h"

using oxygen::content::testing::AssetLoaderLoadingTest;

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

  // Generate PAK file using pakgen CLI (replaces legacy generate_pak.py).
  // Prefer a deterministic build for reproducible tests. The pakgen editable
  // install is configured by CMake (pakgen_editable_install target). Fallback:
  // if pakgen is not on PATH, attempt invoking via python -m.
  std::string command;
  {
    // Primary invocation
    command = "pakgen build \"" + spec_path.string() + "\" \""
      + output_path.string() + "\" --deterministic";
    // If that fails we will retry with python -m pakgen.cli below.
  }

  auto run_command
    = [&](const std::string& cmd) -> int { return std::system(cmd.c_str()); };

  int result = run_command(command);
  if (result != 0) {
    // Retry using explicit module invocation (handles virtual env edge cases).
    const std::string module_cmd = "python -m pakgen.cli build \""
      + spec_path.string() + "\" \"" + output_path.string()
      + "\" --deterministic";
    result = run_command(module_cmd);
  }

  if (result != 0) {
    throw std::runtime_error(
      "Failed to generate PAK file with pakgen for spec: " + spec_name);
  }

  // Verify the PAK file was created
  if (!std::filesystem::exists(output_path)) {
    throw std::runtime_error(
      "PAK file was not created: " + output_path.string());
  }

  // Track generated file for cleanup
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
