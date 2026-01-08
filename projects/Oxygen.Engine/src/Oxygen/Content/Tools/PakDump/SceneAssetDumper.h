//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps scene asset descriptors.
class SceneAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    using oxygen::data::ComponentType;
    using oxygen::data::pak::DirectionalLightRecord;
    using oxygen::data::pak::EnvironmentComponentType;
    using oxygen::data::pak::NodeRecord;
    using oxygen::data::pak::OrthographicCameraRecord;
    using oxygen::data::pak::PerspectiveCameraRecord;
    using oxygen::data::pak::PointLightRecord;
    using oxygen::data::pak::PostProcessVolumeEnvironmentRecord;
    using oxygen::data::pak::RenderableRecord;
    using oxygen::data::pak::SceneEnvironmentBlockHeader;
    using oxygen::data::pak::SceneEnvironmentSystemRecordHeader;
    using oxygen::data::pak::SkyAtmosphereEnvironmentRecord;
    using oxygen::data::pak::SkyLightEnvironmentRecord;
    using oxygen::data::pak::SkySphereEnvironmentRecord;
    using oxygen::data::pak::SpotLightRecord;
    using oxygen::data::pak::VolumetricCloudsEnvironmentRecord;

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    // Optional raw bytes preview (useful for debugging), but the parsed view
    // below is sourced from the engine's validated loader.
    if (ctx.show_asset_descriptors) {
      if (const auto data
        = asset_dump_helpers::ReadDescriptorBytes(pak, entry)) {
        asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
      }
    }

    const auto scene
      = co_await asset_loader.LoadAssetAsync<oxygen::data::SceneAsset>(
        entry.asset_key);
    if (!scene) {
      std::cout << "    Failed to load SceneAsset via AssetLoader\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetHeaderFields(scene->GetHeader(), 4);

    const auto nodes_span = scene->GetNodes();
    const uint32_t node_count = static_cast<uint32_t>(nodes_span.size());

    std::vector<NodeRecord> nodes;
    nodes.resize(node_count);
    for (uint32_t i = 0; i < node_count; ++i) {
      std::memcpy(&nodes[i], nodes_span.data() + i, sizeof(NodeRecord));
    }

    const uint32_t node_limit
      = ctx.verbose ? node_count : (std::min)(node_count, 16u);

    if (node_count > 0) {
      std::cout << "    Nodes (" << node_count << "):\n";
      for (uint32_t i = 0; i < node_limit; ++i) {
        const auto name = scene->GetNodeName(nodes[i]);
        std::cout << "      [" << i << "] "
                  << (name.empty() ? "(unnamed)" : std::string(name))
                  << " (parent=" << nodes[i].parent_index << ")\n";

        if (ctx.verbose) {
          oxygen::data::AssetKey node_id {};
          std::memcpy(&node_id,
            reinterpret_cast<const std::byte*>(&nodes[i])
              + offsetof(NodeRecord, node_id),
            sizeof(node_id));
          PrintUtils::Field("Node ID", oxygen::data::to_string(node_id), 10);
          PrintUtils::Field(
            "Flags", asset_dump_helpers::ToHexString(nodes[i].node_flags), 10);
          PrintUtils::Field(
            "T", asset_dump_helpers::FormatVec3(nodes[i].translation), 10);
          PrintUtils::Field(
            "R", asset_dump_helpers::FormatQuat(nodes[i].rotation), 10);
          PrintUtils::Field(
            "S", asset_dump_helpers::FormatVec3(nodes[i].scale), 10);
        }
      }

      if (node_count > node_limit) {
        std::cout << "      ... (" << (node_count - node_limit)
                  << " more nodes)\n";
      }

      std::cout << "\n";
      std::cout << "    Node Hierarchy:\n";

      std::vector<std::vector<uint32_t>> children;
      children.resize(node_count);

      std::vector<uint32_t> roots;
      roots.reserve(node_count);

      for (uint32_t i = 0; i < node_count; ++i) {
        const auto parent = nodes[i].parent_index;
        if (i == 0 || parent == i || parent >= node_count) {
          roots.push_back(i);
          continue;
        }
        children[parent].push_back(i);
      }

      for (auto& c : children) {
        std::sort(c.begin(), c.end());
      }
      std::sort(roots.begin(), roots.end());

      std::vector<bool> visited;
      visited.resize(node_count, false);

      const auto PrintSubtree = [&](auto&& self, const uint32_t node_index,
                                  const uint32_t depth) -> void {
        if (node_index >= nodes.size()) {
          return;
        }

        const auto name = scene->GetNodeName(nodes[node_index]);
        const auto indent = static_cast<int>(10 + depth * 2);
        const auto pretty_name = name.empty() ? "(unnamed)" : std::string(name);

        if (visited[node_index]) {
          fmt::print(
            "{:>{}}[{}] {} (cycle)\n", "", indent, node_index, pretty_name);
          return;
        }
        visited[node_index] = true;

        fmt::print("{:>{}}[{}] {}\n", "", indent, node_index, pretty_name);
        for (const auto child_index : children[node_index]) {
          self(self, child_index, depth + 1);
        }
      };

      for (const auto root_index : roots) {
        PrintSubtree(PrintSubtree, root_index, 0);
      }

      std::cout << "\n";
    }

    const auto DumpComponentTableHeader
      = [&](const ComponentType type, const size_t count) -> void {
      std::cout << "      " << ::nostd::to_string(type) << ": " << count
                << "\n";
    };

    std::cout << "    Component Tables:\n";
    DumpComponentTableHeader(ComponentType::kRenderable,
      scene->GetComponents<RenderableRecord>().size());
    DumpComponentTableHeader(ComponentType::kPerspectiveCamera,
      scene->GetComponents<PerspectiveCameraRecord>().size());
    DumpComponentTableHeader(ComponentType::kOrthographicCamera,
      scene->GetComponents<OrthographicCameraRecord>().size());
    DumpComponentTableHeader(ComponentType::kDirectionalLight,
      scene->GetComponents<DirectionalLightRecord>().size());
    DumpComponentTableHeader(ComponentType::kPointLight,
      scene->GetComponents<PointLightRecord>().size());
    DumpComponentTableHeader(ComponentType::kSpotLight,
      scene->GetComponents<SpotLightRecord>().size());
    std::cout << "\n";

    const auto DumpDirectionalLights = [&]() -> void {
      const auto lights = scene->GetComponents<DirectionalLightRecord>();
      if (lights.empty()) {
        return;
      }

      std::cout << "    Directional Lights (" << lights.size() << "):";
      std::cout << "\n";

      for (size_t i = 0; i < lights.size(); ++i) {
        DirectionalLightRecord rec {};
        std::memcpy(&rec, lights.data() + i, sizeof(DirectionalLightRecord));

        std::cout << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field("IsSunLight", rec.is_sun_light != 0U, 10);
        PrintUtils::Field(
          "Environment Contrib", rec.environment_contribution != 0U, 10);
      }

      std::cout << "\n";
    };

    const auto DumpPointLights = [&]() -> void {
      const auto lights = scene->GetComponents<PointLightRecord>();
      if (lights.empty()) {
        return;
      }

      std::cout << "    Point Lights (" << lights.size() << "):";
      std::cout << "\n";

      for (size_t i = 0; i < lights.size(); ++i) {
        PointLightRecord rec {};
        std::memcpy(&rec, lights.data() + i, sizeof(PointLightRecord));

        std::cout << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field("Range", rec.range, 10);
        PrintUtils::Field(
          "Attenuation Model", static_cast<int>(rec.attenuation_model), 10);
        PrintUtils::Field("Decay Exponent", rec.decay_exponent, 10);
        PrintUtils::Field("Source Radius", rec.source_radius, 10);
      }

      std::cout << "\n";
    };

    const auto DumpSpotLights = [&]() -> void {
      const auto lights = scene->GetComponents<SpotLightRecord>();
      if (lights.empty()) {
        return;
      }

      std::cout << "    Spot Lights (" << lights.size() << "):";
      std::cout << "\n";

      for (size_t i = 0; i < lights.size(); ++i) {
        SpotLightRecord rec {};
        std::memcpy(&rec, lights.data() + i, sizeof(SpotLightRecord));

        std::cout << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field("Range", rec.range, 10);
        PrintUtils::Field(
          "Attenuation Model", static_cast<int>(rec.attenuation_model), 10);
        PrintUtils::Field("Decay Exponent", rec.decay_exponent, 10);
        PrintUtils::Field("Inner Cone (rad)", rec.inner_cone_angle_radians, 10);
        PrintUtils::Field("Outer Cone (rad)", rec.outer_cone_angle_radians, 10);
        PrintUtils::Field("Source Radius", rec.source_radius, 10);
      }

      std::cout << "\n";
    };

    DumpDirectionalLights();
    DumpPointLights();
    DumpSpotLights();

    // v3+ scenes: validated trailing SceneEnvironment block.
    if (!scene->HasEnvironmentBlock()) {
      std::cout << "    SceneEnvironment Block: (not present)\n\n";
      co_return;
    }

    SceneEnvironmentBlockHeader env_header {};
    std::memcpy(&env_header, scene->GetEnvironmentBlockHeader(),
      sizeof(SceneEnvironmentBlockHeader));

    std::cout << "    SceneEnvironment Block:\n";
    PrintUtils::Field("Byte Size", env_header.byte_size, 8);
    PrintUtils::Field("Systems Count", env_header.systems_count, 8);
    std::cout << "\n";

    const auto EnvironmentTypeName
      = [](const EnvironmentComponentType type) -> const char* {
      switch (type) {
      case EnvironmentComponentType::kSkyAtmosphere:
        return "SkyAtmosphere";
      case EnvironmentComponentType::kVolumetricClouds:
        return "VolumetricClouds";
      case EnvironmentComponentType::kFog:
        return "Fog";
      case EnvironmentComponentType::kSkyLight:
        return "SkyLight";
      case EnvironmentComponentType::kSkySphere:
        return "SkySphere";
      case EnvironmentComponentType::kPostProcessVolume:
        return "PostProcessVolume";
      default:
        return "Unknown";
      }
    };

    const auto records = scene->GetEnvironmentSystemRecords();
    for (size_t i = 0; i < records.size(); ++i) {
      const auto& record = records[i];
      const auto type
        = static_cast<EnvironmentComponentType>(record.header.system_type);

      fmt::print("      [{}] {} (size {})\n", i, EnvironmentTypeName(type),
        record.header.record_size);

      const auto TryReadSkyAtmosphere
        = [&](const std::span<const std::byte> bytes)
        -> std::optional<SkyAtmosphereEnvironmentRecord> {
        if (bytes.size() != sizeof(SkyAtmosphereEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        SkyAtmosphereEnvironmentRecord decoded {};
        const auto res = reader.ReadInto(decoded);
        if (!res) {
          return std::nullopt;
        }
        return decoded;
      };

      const auto TryReadVolumetricClouds
        = [&](const std::span<const std::byte> bytes)
        -> std::optional<VolumetricCloudsEnvironmentRecord> {
        if (bytes.size() != sizeof(VolumetricCloudsEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        VolumetricCloudsEnvironmentRecord decoded {};
        const auto res = reader.ReadInto(decoded);
        if (!res) {
          return std::nullopt;
        }
        return decoded;
      };

      const auto TryReadSkyLight = [&](const std::span<const std::byte> bytes)
        -> std::optional<SkyLightEnvironmentRecord> {
        if (bytes.size() != sizeof(SkyLightEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        SkyLightEnvironmentRecord decoded {};
        const auto res = reader.ReadInto(decoded);
        if (!res) {
          return std::nullopt;
        }
        return decoded;
      };

      const auto TryReadSkySphere = [&](const std::span<const std::byte> bytes)
        -> std::optional<SkySphereEnvironmentRecord> {
        if (bytes.size() != sizeof(SkySphereEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        SkySphereEnvironmentRecord decoded {};
        const auto res = reader.ReadInto(decoded);
        if (!res) {
          return std::nullopt;
        }
        return decoded;
      };

      const auto TryReadPostProcess
        = [&](const std::span<const std::byte> bytes)
        -> std::optional<PostProcessVolumeEnvironmentRecord> {
        if (bytes.size() != sizeof(PostProcessVolumeEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        PostProcessVolumeEnvironmentRecord decoded {};
        const auto res = reader.ReadInto(decoded);
        if (!res) {
          return std::nullopt;
        }
        return decoded;
      };

      switch (type) {
      case EnvironmentComponentType::kSkyAtmosphere: {
        const auto rec = TryReadSkyAtmosphere(record.bytes);
        if (!rec) {
          fmt::print("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field("Planet Radius (m)", rec->planet_radius_m, 10);
        PrintUtils::Field(
          "Atmosphere Height (m)", rec->atmosphere_height_m, 10);
        PrintUtils::Field("Ground Albedo",
          asset_dump_helpers::FormatVec3(rec->ground_albedo_rgb), 10);
        PrintUtils::Field("Rayleigh Scattering",
          asset_dump_helpers::FormatVec3(rec->rayleigh_scattering_rgb), 10);
        PrintUtils::Field(
          "Rayleigh Scale Height (m)", rec->rayleigh_scale_height_m, 10);
        PrintUtils::Field("Mie Scattering",
          asset_dump_helpers::FormatVec3(rec->mie_scattering_rgb), 10);
        PrintUtils::Field("Mie Scale Height (m)", rec->mie_scale_height_m, 10);
        PrintUtils::Field("Mie g", rec->mie_g, 10);
        PrintUtils::Field("Absorption",
          asset_dump_helpers::FormatVec3(rec->absorption_rgb), 10);
        PrintUtils::Field(
          "Absorption Scale Height (m)", rec->absorption_scale_height_m, 10);
        PrintUtils::Field(
          "Multi Scattering Factor", rec->multi_scattering_factor, 10);
        PrintUtils::Field(
          "Sun Disk Enabled", static_cast<int>(rec->sun_disk_enabled), 10);
        PrintUtils::Field("Sun Disk Angular Radius (rad)",
          rec->sun_disk_angular_radius_radians, 10);
        PrintUtils::Field("Aerial Perspective Distance Scale",
          rec->aerial_perspective_distance_scale, 10);
        break;
      }
      case EnvironmentComponentType::kVolumetricClouds: {
        const auto rec = TryReadVolumetricClouds(record.bytes);
        if (!rec) {
          fmt::print("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field("Base Altitude (m)", rec->base_altitude_m, 10);
        PrintUtils::Field("Layer Thickness (m)", rec->layer_thickness_m, 10);
        PrintUtils::Field("Coverage", rec->coverage, 10);
        PrintUtils::Field("Density", rec->density, 10);
        PrintUtils::Field(
          "Albedo", asset_dump_helpers::FormatVec3(rec->albedo_rgb), 10);
        PrintUtils::Field("Extinction Scale", rec->extinction_scale, 10);
        PrintUtils::Field("Phase g", rec->phase_g, 10);
        PrintUtils::Field("Wind Dir (ws)",
          asset_dump_helpers::FormatVec3(rec->wind_dir_ws), 10);
        PrintUtils::Field("Wind Speed (m/s)", rec->wind_speed_mps, 10);
        PrintUtils::Field("Shadow Strength", rec->shadow_strength, 10);
        break;
      }
      case EnvironmentComponentType::kSkyLight: {
        const auto rec = TryReadSkyLight(record.bytes);
        if (!rec) {
          fmt::print("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field("Source", static_cast<int>(rec->source), 10);
        PrintUtils::Field(
          "Cubemap Asset", oxygen::data::to_string(rec->cubemap_asset), 10);
        PrintUtils::Field("Intensity", rec->intensity, 10);
        PrintUtils::Field(
          "Tint", asset_dump_helpers::FormatVec3(rec->tint_rgb), 10);
        PrintUtils::Field("Diffuse Intensity", rec->diffuse_intensity, 10);
        PrintUtils::Field("Specular Intensity", rec->specular_intensity, 10);
        break;
      }
      case EnvironmentComponentType::kSkySphere: {
        const auto rec = TryReadSkySphere(record.bytes);
        if (!rec) {
          fmt::print("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field("Source", static_cast<int>(rec->source), 10);
        PrintUtils::Field(
          "Cubemap Asset", oxygen::data::to_string(rec->cubemap_asset), 10);
        PrintUtils::Field("Solid Color",
          asset_dump_helpers::FormatVec3(rec->solid_color_rgb), 10);
        PrintUtils::Field("Intensity", rec->intensity, 10);
        PrintUtils::Field("Rotation (rad)", rec->rotation_radians, 10);
        PrintUtils::Field(
          "Tint", asset_dump_helpers::FormatVec3(rec->tint_rgb), 10);
        break;
      }
      case EnvironmentComponentType::kPostProcessVolume: {
        const auto rec = TryReadPostProcess(record.bytes);
        if (!rec) {
          fmt::print("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(
          "Tone Mapper", static_cast<int>(rec->tone_mapper), 10);
        PrintUtils::Field(
          "Exposure Mode", static_cast<int>(rec->exposure_mode), 10);
        PrintUtils::Field(
          "Exposure Compensation (EV)", rec->exposure_compensation_ev, 10);
        PrintUtils::Field(
          "Auto Exposure Min (EV)", rec->auto_exposure_min_ev, 10);
        PrintUtils::Field(
          "Auto Exposure Max (EV)", rec->auto_exposure_max_ev, 10);
        PrintUtils::Field(
          "Auto Exposure Speed Up", rec->auto_exposure_speed_up, 10);
        PrintUtils::Field(
          "Auto Exposure Speed Down", rec->auto_exposure_speed_down, 10);
        PrintUtils::Field("Bloom Intensity", rec->bloom_intensity, 10);
        PrintUtils::Field("Bloom Threshold", rec->bloom_threshold, 10);
        PrintUtils::Field("Saturation", rec->saturation, 10);
        PrintUtils::Field("Contrast", rec->contrast, 10);
        PrintUtils::Field("Vignette Intensity", rec->vignette_intensity, 10);
        break;
      }
      default:
        fmt::print("        (no decoder)\n");
        break;
      }
    }

    std::cout << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
