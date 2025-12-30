//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <functional>
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
  // Return predefined asset keys that match the YAML test specifications
  oxygen::data::AssetKey key {};

  if (name == "test_material") {
    // Matches simple_material.yaml: "01234567-89ab-cdef-0123-456789abcdef"
    const auto bytes = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
      0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_geometry") {
    // Matches simple_geometry.yaml: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    const auto bytes = { 0xaa, 0xaa, 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd,
      0xdd, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "textured_material") {
    // Matches material_with_textures.yaml:
    // "12345678-90ab-cdef-1234-567890abcdef"
    const auto bytes = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12,
      0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "buffered_geometry") {
    // Matches geometry_with_buffers.yaml:
    // "ffffffff-eeee-dddd-cccc-bbbbbbbbbbbb"
    const auto bytes = { 0xff, 0xff, 0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd, 0xcc,
      0xcc, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "complex_geometry" || name == "SpaceshipGeometry") {
    // Matches complex_geometry.yaml SpaceshipGeometry:
    // "deadbeef-cafe-babe-dead-feeddeadbeef"
    const auto bytes = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe, 0xde,
      0xad, 0xfe, 0xed, 0xde, 0xad, 0xbe, 0xef };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_scene") {
    // Matches scene_with_renderable.yaml:
    // "22222222-3333-4444-5555-666666666666"
    const auto bytes = { 0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55,
      0x55, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66 };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_scene_no_renderables") {
    // Matches scene_no_renderables.yaml:
    // "33333333-4444-5555-6666-777777777777"
    const auto bytes = { 0x33, 0x33, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66,
      0x66, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77 };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_scene_duplicate_renderables") {
    // Matches scene_duplicate_renderables.yaml:
    // "44444444-5555-6666-7777-888888888888"
    const auto bytes = { 0x44, 0x44, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77,
      0x77, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_scene_two_geometries") {
    // Matches scene_two_geometries.yaml:
    // "55555555-6666-7777-8888-999999999999"
    const auto bytes = { 0x55, 0x55, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77, 0x88,
      0x88, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99 };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_scene_invalid_unknown_geometry") {
    // Matches scene_invalid_unknown_geometry.yaml:
    // "66666666-7777-8888-9999-aaaaaaaaaaaa"
    const auto bytes = { 0x66, 0x66, 0x66, 0x66, 0x77, 0x77, 0x88, 0x88, 0x99,
      0x99, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa };
    std::ranges::copy(bytes, key.guid.begin());
  } else {
    // Fallback: create a deterministic key from hash for unknown names
    constexpr std::hash<std::string> hasher;
    const auto hash = hasher(name);
    auto hash_bytes = std::bit_cast<std::array<uint8_t, sizeof(hash)>>(hash);
    auto guid_span = std::span { key.guid };
    auto hash_span = std::span { hash_bytes };
    std::copy_n(hash_span.begin(), std::min(hash_span.size(), guid_span.size()),
      guid_span.begin());
  }

  return key;
}
