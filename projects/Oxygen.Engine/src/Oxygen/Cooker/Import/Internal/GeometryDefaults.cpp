//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Data/LodPolicyValidation.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Scene/Schema/Generated.SceneComponents.h>

namespace oxygen::content::import::internal {
namespace {
  using data::pak::geometry::LodPolicyRecord;
  using data::pak::geometry::LodPolicyType;
  using nlohmann::json;
  namespace surface_schema = scene::schema::geometry::surface;

  //! Copies a schema-valid threshold list into `out`; returns its length.
  auto ReadBoundaries(const json& values, std::span<float> out) -> std::size_t
  {
    const auto count = std::min(values.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
      out[i] = values.at(i).get<float>();
    }
    return values.size();
  }

  auto WriteBoundaries(std::span<const float> values) -> json
  {
    auto out = json::array();
    for (const auto value : values) {
      out.push_back(value);
    }
    return out;
  }
} // namespace

auto MakeSurfaceTable(const std::span<const data::SurfaceId> declared,
  const std::span<const AuthoredSurfaceDefaults> authored)
  -> std::vector<data::pak::geometry::SurfaceDesc>
{
  auto table = std::vector<data::pak::geometry::SurfaceDesc> {};
  table.reserve(declared.size());
  for (const auto& id : declared) {
    const auto found
      = std::ranges::find(authored, id, &AuthoredSurfaceDefaults::surface_id);
    const auto entry
      = found == authored.end() ? AuthoredSurfaceDefaults {} : *found;
    auto flags = uint32_t { 0 };
    if (entry.visible.value_or(surface_schema::kVisibilityDefault)) {
      flags |= data::pak::geometry::kSurfaceFlag_Visible;
    }
    if (entry.casts_shadows.value_or(surface_schema::kCastsShadowsDefault)) {
      flags |= data::pak::geometry::kSurfaceFlag_CastsShadows;
    }
    table.push_back({ .surface_id = id, .flags = flags });
  }
  return table;
}

auto MakeDefaultLodPolicy(const std::optional<LodPolicyRecord>& authored)
  -> LodPolicyRecord
{
  return authored.value_or(scene::schema::geometry::kLodPolicyDefault);
}

auto ReadLodPolicy(const json& document) -> std::optional<LodPolicyRecord>
{
  auto record = LodPolicyRecord {};
  const auto type = document.at("type").get<std::string>();
  if (type == "fixed") {
    record.type = LodPolicyType::kFixed;
    record.lod_index = document.at("lod_index").get<uint8_t>();
  } else if (type == "distance") {
    record.type = LodPolicyType::kDistance;
    const auto count = ReadBoundaries(
      document.at("thresholds"), std::span<float> { record.thresholds });
    if (count > data::pak::geometry::kMaxLodBoundaryCount) {
      return std::nullopt;
    }
    record.boundary_count = static_cast<uint8_t>(count);
    record.hysteresis_ratio = document.at("hysteresis_ratio").get<float>();
  } else if (type == "screen_space_error") {
    record.type = LodPolicyType::kScreenSpaceError;
    const auto enter = ReadBoundaries(document.at("enter_finer_sse"),
      std::span<float> { record.enter_finer_sse });
    const auto exit = ReadBoundaries(document.at("exit_coarser_sse"),
      std::span<float> { record.exit_coarser_sse });
    if (enter != exit || enter > data::pak::geometry::kMaxLodBoundaryCount) {
      return std::nullopt;
    }
    record.boundary_count = static_cast<uint8_t>(enter);
  } else {
    return std::nullopt;
  }
  if (!data::IsValidLodPolicy(record)) {
    return std::nullopt;
  }
  return record;
}

auto WriteLodPolicy(const LodPolicyRecord& record) -> json
{
  const auto count = std::size_t { record.boundary_count };
  switch (record.type) {
  case LodPolicyType::kFixed:
    return json { { "type", "fixed" }, { "lod_index", record.lod_index } };
  case LodPolicyType::kDistance:
    return json {
      { "type", "distance" },
      {
        "thresholds",
        WriteBoundaries(
          std::span<const float> { record.thresholds }.first(count)),
      },
      { "hysteresis_ratio", record.hysteresis_ratio },
    };
  case LodPolicyType::kScreenSpaceError:
    return json {
      { "type", "screen_space_error" },
      {
        "enter_finer_sse",
        WriteBoundaries(
          std::span<const float> { record.enter_finer_sse }.first(count)),
      },
      {
        "exit_coarser_sse",
        WriteBoundaries(
          std::span<const float> { record.exit_coarser_sse }.first(count)),
      },
    };
  }
  return json {};
}

} // namespace oxygen::content::import::internal
