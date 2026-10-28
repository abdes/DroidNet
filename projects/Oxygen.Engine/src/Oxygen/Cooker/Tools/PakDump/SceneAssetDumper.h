//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::pakdump {

//! Dumps scene asset descriptors.
class SceneAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    using oxygen::data::ComponentType;
    using oxygen::data::pak::scripting::ScriptingComponentRecord;
    using oxygen::data::pak::world::BackgroundEnvironmentRecord;
    using oxygen::data::pak::world::DirectionalLightRecord;
    using oxygen::data::pak::world::EnvironmentComponentType;
    using oxygen::data::pak::world::FogEnvironmentRecord;
    using oxygen::data::pak::world::LocalFogVolumeRecord;
    using oxygen::data::pak::world::NodeRecord;
    using oxygen::data::pak::world::OrthographicCameraRecord;
    using oxygen::data::pak::world::OverrideRecord;
    using oxygen::data::pak::world::PerspectiveCameraRecord;
    using oxygen::data::pak::world::PointLightRecord;
    using oxygen::data::pak::world::RenderableRecord;
    using oxygen::data::pak::world::SceneEnvironmentBlockHeader;
    using oxygen::data::pak::world::SceneEnvironmentSystemRecordHeader;
    using oxygen::data::pak::world::SkyAtmosphereEnvironmentRecord;
    using oxygen::data::pak::world::SkyLightEnvironmentRecord;
    using oxygen::data::pak::world::SkySphereEnvironmentRecord;
    using oxygen::data::pak::world::SpotLightRecord;
    using oxygen::data::pak::world::VolumetricCloudsEnvironmentRecord;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

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
      ctx.Out() << "    Failed to load SceneAsset via AssetLoader\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetHeaderFields(
      ctx.Out(), scene->GetHeader(), scene->GetAssetName(), 4);

    const auto nodes_span = scene->GetNodes();
    const auto nodes
      = std::vector<NodeRecord>(nodes_span.begin(), nodes_span.end());
    const auto node_count = static_cast<uint32_t>(nodes.size());

    const uint32_t node_limit
      = ctx.verbose ? node_count : (std::min)(node_count, 16U);

    if (node_count > 0) {
      ctx.Out() << "    Nodes (" << node_count << "):\n";
      for (uint32_t i = 0; i < node_limit; ++i) {
        const auto name = scene->GetNodeName(nodes.at(i));
        ctx.Out() << "      [" << i << "] "
                  << (name.empty() ? "(unnamed)" : std::string(name))
                  << " (parent=" << nodes.at(i).parent_index << ")\n";

        if (ctx.verbose) {
          PrintUtils::Field(ctx.Out(), "Node ID",
            oxygen::data::to_string(nodes.at(i).node_id), 10);
          PrintUtils::Field(ctx.Out(), "Flags",
            asset_dump_helpers::ToHexString(nodes.at(i).node_flags), 10);
          PrintUtils::Field(ctx.Out(), "Inherited Flags",
            asset_dump_helpers::ToHexString(nodes.at(i).inherited_flags), 10);
          PrintUtils::Field(ctx.Out(), "T",
            asset_dump_helpers::FormatVec3(nodes.at(i).translation), 10);
          PrintUtils::Field(ctx.Out(), "R",
            asset_dump_helpers::FormatQuat(nodes.at(i).rotation), 10);
          PrintUtils::Field(ctx.Out(), "S",
            asset_dump_helpers::FormatVec3(nodes.at(i).scale), 10);
        }
      }

      if (node_count > node_limit) {
        ctx.Out() << "      ... (" << (node_count - node_limit)
                  << " more nodes)\n";
      }

      ctx.Out() << "\n";
      ctx.Out() << "    Node Hierarchy:\n";

      std::vector<std::vector<uint32_t>> children;
      children.resize(node_count);

      std::vector<uint32_t> roots;
      roots.reserve(node_count);

      for (uint32_t i = 0; i < node_count; ++i) {
        const auto parent = nodes.at(i).parent_index;
        if (i == 0 || parent == i || parent >= node_count) {
          roots.push_back(i);
          continue;
        }
        children.at(parent).push_back(i);
      }

      for (auto& c : children) {
        std::ranges::sort(c);
      }
      std::ranges::sort(roots);

      std::vector<bool> visited;
      visited.resize(node_count, false);

      struct TreeVisit {
        uint32_t node_index;
        uint32_t depth;
      };
      const auto PrintSubtree
        = [&](auto&& self, const TreeVisit visit) -> void {
        const auto [node_index, depth] = visit;
        if (node_index >= nodes.size()) {
          return;
        }

        const auto name = scene->GetNodeName(nodes.at(node_index));
        const auto indent = static_cast<int>(10 + (depth * 2));
        const auto pretty_name = name.empty() ? "(unnamed)" : std::string(name);

        if (visited.at(node_index)) {
          ctx.Out() << fmt::format(
            "{:>{}}[{}] {} (cycle)\n", "", indent, node_index, pretty_name);
          return;
        }
        visited.at(node_index) = true;

        ctx.Out() << fmt::format(
          "{:>{}}[{}] {}\n", "", indent, node_index, pretty_name);
        for (const auto child_index : children.at(node_index)) {
          self(
            self, TreeVisit { .node_index = child_index, .depth = depth + 1 });
        }
      };

      for (const auto root_index : roots) {
        PrintSubtree(
          PrintSubtree, TreeVisit { .node_index = root_index, .depth = 0 });
      }

      ctx.Out() << "\n";
    }

    const auto DumpComponentTableHeader
      = [&](const ComponentType type, const size_t count) -> void {
      ctx.Out() << "      " << ::nostd::to_string(type) << ": " << count
                << "\n";
    };

    ctx.Out() << "    Component Tables:\n";
    DumpComponentTableHeader(ComponentType::kRenderable,
      scene->GetComponents<RenderableRecord>().size());
    DumpComponentTableHeader(
      ComponentType::kOverride, scene->GetComponents<OverrideRecord>().size());
    DumpComponentTableHeader(ComponentType::kLocalFogVolume,
      scene->GetComponents<LocalFogVolumeRecord>().size());
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
    DumpComponentTableHeader(ComponentType::kScripting,
      scene->GetComponents<ScriptingComponentRecord>().size());
    ctx.Out() << "\n";

    // Each override as its property key, part and value, decoded by the value
    // type the schema gives its id; the scene validated them when it loaded.
    const auto DumpOverrides
      = [&](const oxygen::data::pak::world::SceneNodeIndexT node) -> void {
      const auto policies
        = scene->GetComponents<oxygen::data::pak::geometry::LodPolicyRecord>();
      for (const auto& record : scene->GetComponents<OverrideRecord>()) {
        if (record.target.node_index != node) {
          continue;
        }
        const auto* property
          = oxygen::data::schema::FindOverrideProperty(record.override_id);
        DCHECK_NOTNULL_F(property);
        auto label = std::string { "Override " } + std::string(property->key);
        if (!record.part_id.IsNil()) {
          label += " @" + oxygen::data::to_string(record.part_id);
        }
        using oxygen::data::schema::ValueType;
        switch (property->type) {
        case ValueType::kBool:
          PrintUtils::Field(ctx.Out(), label, record.value.as_bool != 0U, 10);
          break;
        case ValueType::kAsset:
          PrintUtils::Field(ctx.Out(), label,
            oxygen::data::to_string(record.value.as_asset), 10);
          break;
        case ValueType::kLodPolicy: {
          const auto& policy = oxygen::base::CheckedAt(
            policies, record.value.as_record_index.get());
          PrintUtils::Field(ctx.Out(), label,
            "type=" + std::to_string(static_cast<int>(policy.type))
              + " boundaries=" + std::to_string(policy.boundary_count),
            10);
          break;
        }
        case ValueType::kInt:
        case ValueType::kFloat:
        case ValueType::kVec3:
        case ValueType::kEnum:
        case ValueType::kString:
          // No override property has these types yet.
          PrintUtils::Field(
            ctx.Out(), label, std::string { "(not shown)" }, 10);
          break;
        }
      }
    };

    const auto DumpRenderables = [&] -> void {
      const auto renderables = scene->GetComponents<RenderableRecord>();
      if (renderables.empty()) {
        return;
      }

      ctx.Out() << "    Renderables (" << renderables.size() << "):\n";
      for (size_t i = 0; i < renderables.size(); ++i) {
        const auto& rec = oxygen::base::CheckedAt(renderables, i);
        ctx.Out() << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field(ctx.Out(), "Geometry Key",
          oxygen::data::to_string(rec.geometry_key), 10);
        DumpOverrides(rec.node_index);
      }
      ctx.Out() << "\n";
    };

    const auto DumpDirectionalLights = [&] -> void {
      const auto lights = scene->GetComponents<DirectionalLightRecord>();
      if (lights.empty()) {
        return;
      }

      ctx.Out() << "    Directional Lights (" << lights.size() << "):";
      ctx.Out() << "\n";

      for (size_t i = 0; i < lights.size(); ++i) {
        const auto& rec = oxygen::base::CheckedAt(lights, i);

        ctx.Out() << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field(ctx.Out(), "Atmosphere Slot",
          static_cast<int>(rec.atmosphere_light_slot), 10);
        PrintUtils::Field(ctx.Out(), "Per-pixel Transmittance",
          rec.use_per_pixel_atmosphere_transmittance != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Disk Luminance Scale",
          asset_dump_helpers::FormatVec3(
            rec.atmosphere_disk_luminance_scale_rgb),
          10);
        PrintUtils::Field(ctx.Out(), "Cascade Count", rec.cascade_count, 10);
        PrintUtils::Field(
          ctx.Out(), "Split Mode", static_cast<uint32_t>(rec.split_mode), 10);
        PrintUtils::Field(
          ctx.Out(), "Max Shadow Distance", rec.max_shadow_distance, 10);
        PrintUtils::Field(
          ctx.Out(), "Distribution Exponent", rec.distribution_exponent, 10);
        PrintUtils::Field(
          ctx.Out(), "Transition Fraction", rec.transition_fraction, 10);
        PrintUtils::Field(
          ctx.Out(), "Fadeout Fraction", rec.distance_fadeout_fraction, 10);
        PrintUtils::Field(
          ctx.Out(), "Cascade Distance[0]", rec.cascade_distances[0], 10);
        PrintUtils::Field(
          ctx.Out(), "Cascade Distance[1]", rec.cascade_distances[1], 10);
        PrintUtils::Field(
          ctx.Out(), "Cascade Distance[2]", rec.cascade_distances[2], 10);
        PrintUtils::Field(
          ctx.Out(), "Cascade Distance[3]", rec.cascade_distances[3], 10);
      }

      ctx.Out() << "\n";
    };

    const auto DumpPointLights = [&] -> void {
      const auto lights = scene->GetComponents<PointLightRecord>();
      if (lights.empty()) {
        return;
      }

      ctx.Out() << "    Point Lights (" << lights.size() << "):";
      ctx.Out() << "\n";

      for (size_t i = 0; i < lights.size(); ++i) {
        const auto& rec = oxygen::base::CheckedAt(lights, i);

        ctx.Out() << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field(ctx.Out(), "Range", rec.range, 10);
        PrintUtils::Field(ctx.Out(), "Source Radius", rec.source_radius, 10);
      }

      ctx.Out() << "\n";
    };

    const auto DumpSpotLights = [&] -> void {
      const auto lights = scene->GetComponents<SpotLightRecord>();
      if (lights.empty()) {
        return;
      }

      ctx.Out() << "    Spot Lights (" << lights.size() << "):";
      ctx.Out() << "\n";

      for (size_t i = 0; i < lights.size(); ++i) {
        const auto& rec = oxygen::base::CheckedAt(lights, i);

        ctx.Out() << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field(ctx.Out(), "Range", rec.range, 10);
        PrintUtils::Field(
          ctx.Out(), "Inner Cone (rad)", rec.inner_cone_angle_radians, 10);
        PrintUtils::Field(
          ctx.Out(), "Outer Cone (rad)", rec.outer_cone_angle_radians, 10);
        PrintUtils::Field(ctx.Out(), "Source Radius", rec.source_radius, 10);
      }

      ctx.Out() << "\n";
    };

    const auto DumpLocalFogVolumes = [&] -> void {
      const auto volumes = scene->GetComponents<LocalFogVolumeRecord>();
      if (volumes.empty()) {
        return;
      }

      ctx.Out() << "    Local Fog Volumes (" << volumes.size() << "):\n";
      for (size_t i = 0; i < volumes.size(); ++i) {
        const auto& rec = oxygen::base::CheckedAt(volumes, i);
        ctx.Out() << "      [" << i << "] node=" << rec.node_index << "\n";
        PrintUtils::Field(ctx.Out(), "Enabled", rec.enabled != 0U, 10);
        PrintUtils::Field(
          ctx.Out(), "Radial Extinction", rec.radial_fog_extinction, 10);
        PrintUtils::Field(
          ctx.Out(), "Height Extinction", rec.height_fog_extinction, 10);
        PrintUtils::Field(
          ctx.Out(), "Height Falloff", rec.height_fog_falloff, 10);
        PrintUtils::Field(
          ctx.Out(), "Height Offset", rec.height_fog_offset, 10);
        PrintUtils::Field(ctx.Out(), "Phase g", rec.fog_phase_g, 10);
        PrintUtils::Field(ctx.Out(), "Albedo",
          asset_dump_helpers::FormatVec3(rec.fog_albedo), 10);
        PrintUtils::Field(ctx.Out(), "Emissive",
          asset_dump_helpers::FormatVec3(rec.fog_emissive), 10);
        PrintUtils::Field(ctx.Out(), "Sort Priority", rec.sort_priority, 10);
      }
      ctx.Out() << "\n";
    };

    DumpRenderables();
    DumpDirectionalLights();
    DumpPointLights();
    DumpSpotLights();
    DumpLocalFogVolumes();

    // v3+ scenes: validated trailing SceneEnvironment block.
    if (!scene->HasEnvironmentBlock()) {
      ctx.Out() << "    SceneEnvironment Block: (not present)\n\n";
      co_return;
    }

    SceneEnvironmentBlockHeader env_header {};
    std::memcpy(&env_header, scene->GetEnvironmentBlockHeader(),
      sizeof(SceneEnvironmentBlockHeader));

    ctx.Out() << "    SceneEnvironment Block:\n";
    PrintUtils::Field(ctx.Out(), "Byte Size", env_header.byte_size, 8);
    PrintUtils::Field(ctx.Out(), "Systems Count", env_header.systems_count, 8);
    ctx.Out() << "\n";

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
      case EnvironmentComponentType::kBackground:
        return "Background";
      default:
        return "Unknown";
      }
    };

    const auto records = scene->GetEnvironmentSystemRecords();
    for (size_t i = 0; i < records.size(); ++i) {
      const auto& record = oxygen::base::CheckedAt(records, i);
      const auto type
        = static_cast<EnvironmentComponentType>(record.header.system_type);

      ctx.Out() << fmt::format("      [{}] {} (size {})\n", i,
        EnvironmentTypeName(type), record.header.record_size);

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

      const auto TryReadFog = [&](const std::span<const std::byte> bytes)
        -> std::optional<FogEnvironmentRecord> {
        if (bytes.size() != sizeof(FogEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        FogEnvironmentRecord decoded {};
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

      const auto TryReadBackground = [&](const std::span<const std::byte> bytes)
        -> std::optional<BackgroundEnvironmentRecord> {
        if (bytes.size() != sizeof(BackgroundEnvironmentRecord)) {
          return std::nullopt;
        }
        std::vector<std::byte> buffer;
        buffer.assign(bytes.begin(), bytes.end());
        oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
        oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
        auto packed = reader.ScopedAlignment(1);
        BackgroundEnvironmentRecord decoded {};
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
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(
          ctx.Out(), "Planet Radius (m)", rec->planet_radius_m, 10);
        PrintUtils::Field(
          ctx.Out(), "Atmosphere Height (m)", rec->atmosphere_height_m, 10);
        PrintUtils::Field(ctx.Out(), "Ground Albedo",
          asset_dump_helpers::FormatVec3(rec->ground_albedo_rgb), 10);
        PrintUtils::Field(ctx.Out(), "Rayleigh Scattering",
          asset_dump_helpers::FormatVec3(rec->rayleigh_scattering_rgb), 10);
        PrintUtils::Field(ctx.Out(), "Rayleigh Scale Height (m)",
          rec->rayleigh_scale_height_m, 10);
        PrintUtils::Field(ctx.Out(), "Mie Scattering",
          asset_dump_helpers::FormatVec3(rec->mie_scattering_rgb), 10);
        PrintUtils::Field(ctx.Out(), "Mie Absorption",
          asset_dump_helpers::FormatVec3(rec->mie_absorption_rgb), 10);
        PrintUtils::Field(
          ctx.Out(), "Mie Scale Height (m)", rec->mie_scale_height_m, 10);
        PrintUtils::Field(ctx.Out(), "Mie g", rec->mie_g, 10);
        PrintUtils::Field(ctx.Out(), "Ozone Absorption",
          asset_dump_helpers::FormatVec3(rec->absorption_rgb), 10);
        PrintUtils::Field(ctx.Out(), "Ozone Density Profile",
          asset_dump_helpers::FormatVec3(rec->ozone_density_profile), 10);
        PrintUtils::Field(ctx.Out(), "Multi Scattering Factor",
          rec->multi_scattering_factor, 10);
        PrintUtils::Field(ctx.Out(), "Sky Luminance",
          asset_dump_helpers::FormatVec3(rec->sky_luminance_factor_rgb), 10);
        PrintUtils::Field(ctx.Out(), "Sky+Aerial Luminance",
          asset_dump_helpers::FormatVec3(
            rec->sky_and_aerial_perspective_luminance_factor_rgb),
          10);
        PrintUtils::Field(ctx.Out(), "Sun Disk Enabled",
          static_cast<int>(rec->sun_disk_enabled), 10);
        PrintUtils::Field(ctx.Out(), "Aerial Perspective Distance Scale",
          rec->aerial_perspective_distance_scale, 10);
        PrintUtils::Field(ctx.Out(), "Aerial Scattering Strength",
          rec->aerial_scattering_strength, 10);
        PrintUtils::Field(ctx.Out(), "Aerial Start Depth (m)",
          rec->aerial_perspective_start_depth_m, 10);
        PrintUtils::Field(ctx.Out(), "Height Fog Contribution",
          rec->height_fog_contribution, 10);
        PrintUtils::Field(ctx.Out(), "Trace Sample Count Scale",
          rec->trace_sample_count_scale, 10);
        PrintUtils::Field(ctx.Out(), "Min Light Elevation (deg)",
          rec->transmittance_min_light_elevation_deg, 10);
        PrintUtils::Field(ctx.Out(), "Holdout", rec->holdout != 0U, 10);
        PrintUtils::Field(
          ctx.Out(), "Render In Main Pass", rec->render_in_main_pass != 0U, 10);
        break;
      }
      case EnvironmentComponentType::kVolumetricClouds: {
        const auto rec = TryReadVolumetricClouds(record.bytes);
        if (!rec) {
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(
          ctx.Out(), "Base Altitude (m)", rec->base_altitude_m, 10);
        PrintUtils::Field(
          ctx.Out(), "Layer Thickness (m)", rec->layer_thickness_m, 10);
        PrintUtils::Field(ctx.Out(), "Coverage", rec->coverage, 10);
        PrintUtils::Field(
          ctx.Out(), "Extinction σt (1/m)", rec->extinction_sigma_t_per_m, 10);
        PrintUtils::Field(ctx.Out(), "Single-Scattering Albedo",
          asset_dump_helpers::FormatVec3(rec->single_scattering_albedo_rgb),
          10);
        PrintUtils::Field(ctx.Out(), "Phase g", rec->phase_g, 10);
        PrintUtils::Field(ctx.Out(), "Wind Dir (ws)",
          asset_dump_helpers::FormatVec3(rec->wind_dir_ws), 10);
        PrintUtils::Field(
          ctx.Out(), "Wind Speed (m/s)", rec->wind_speed_mps, 10);
        PrintUtils::Field(
          ctx.Out(), "Shadow Strength", rec->shadow_strength, 10);
        break;
      }
      case EnvironmentComponentType::kFog: {
        const auto rec = TryReadFog(record.bytes);
        if (!rec) {
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(ctx.Out(), "Enabled", rec->enabled != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Model", rec->model, 10);
        PrintUtils::Field(
          ctx.Out(), "Extinction σt (1/m)", rec->extinction_sigma_t_per_m, 10);
        PrintUtils::Field(
          ctx.Out(), "Height Falloff (1/m)", rec->height_falloff_per_m, 10);
        PrintUtils::Field(
          ctx.Out(), "Height Offset (m)", rec->height_offset_m, 10);
        PrintUtils::Field(
          ctx.Out(), "Start Distance (m)", rec->start_distance_m, 10);
        PrintUtils::Field(ctx.Out(), "Max Opacity", rec->max_opacity, 10);
        PrintUtils::Field(ctx.Out(), "Albedo",
          asset_dump_helpers::FormatVec3(rec->single_scattering_albedo_rgb),
          10);
        PrintUtils::Field(ctx.Out(), "Anisotropy g", rec->anisotropy_g, 10);
        PrintUtils::Field(
          ctx.Out(), "Enable Height Fog", rec->enable_height_fog != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Enable Volumetric Fog",
          rec->enable_volumetric_fog != 0U, 10);
        PrintUtils::Field(
          ctx.Out(), "Second Fog Density", rec->second_fog_density, 10);
        PrintUtils::Field(
          ctx.Out(), "Second Fog Falloff", rec->second_fog_height_falloff, 10);
        PrintUtils::Field(
          ctx.Out(), "Second Fog Offset", rec->second_fog_height_offset, 10);
        PrintUtils::Field(ctx.Out(), "Inscattering Luminance",
          asset_dump_helpers::FormatVec3(rec->fog_inscattering_luminance), 10);
        PrintUtils::Field(ctx.Out(), "Ambient Contribution",
          asset_dump_helpers::FormatVec3(
            rec->sky_atmosphere_ambient_contribution_color_scale),
          10);
        PrintUtils::Field(ctx.Out(), "Inscattering Cubemap Texture Reference",
          rec->inscattering_color_cubemap.get(), 10);
        PrintUtils::Field(ctx.Out(), "Cubemap Angle",
          rec->inscattering_color_cubemap_angle, 10);
        PrintUtils::Field(ctx.Out(), "Texture Tint",
          asset_dump_helpers::FormatVec3(rec->inscattering_texture_tint), 10);
        PrintUtils::Field(ctx.Out(), "Full Directional Distance",
          rec->fully_directional_inscattering_color_distance, 10);
        PrintUtils::Field(ctx.Out(), "Non-Directional Distance",
          rec->non_directional_inscattering_color_distance, 10);
        PrintUtils::Field(ctx.Out(), "Directional Luminance",
          asset_dump_helpers::FormatVec3(
            rec->directional_inscattering_luminance),
          10);
        PrintUtils::Field(ctx.Out(), "Directional Exponent",
          rec->directional_inscattering_exponent, 10);
        PrintUtils::Field(ctx.Out(), "Directional Start Distance",
          rec->directional_inscattering_start_distance, 10);
        PrintUtils::Field(
          ctx.Out(), "End Distance (m)", rec->end_distance_m, 10);
        PrintUtils::Field(
          ctx.Out(), "Fog Cutoff Distance (m)", rec->fog_cutoff_distance_m, 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Distribution",
          rec->volumetric_fog_scattering_distribution, 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Albedo",
          asset_dump_helpers::FormatVec3(rec->volumetric_fog_albedo), 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Emissive",
          asset_dump_helpers::FormatVec3(rec->volumetric_fog_emissive), 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Extinction Scale",
          rec->volumetric_fog_extinction_scale, 10);
        PrintUtils::Field(
          ctx.Out(), "Volumetric Distance", rec->volumetric_fog_distance, 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Start Distance",
          rec->volumetric_fog_start_distance, 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Near Fade",
          rec->volumetric_fog_near_fade_in_distance, 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Static Lighting",
          rec->volumetric_fog_static_lighting_scattering_intensity, 10);
        PrintUtils::Field(ctx.Out(), "Override Light Colors",
          rec->override_light_colors_with_fog_inscattering_colors != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Holdout", rec->holdout != 0U, 10);
        PrintUtils::Field(
          ctx.Out(), "Render In Main Pass", rec->render_in_main_pass != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Visible In Reflection Captures",
          rec->visible_in_reflection_captures != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Visible In Real-Time Sky Captures",
          rec->visible_in_real_time_sky_captures != 0U, 10);
        break;
      }
      case EnvironmentComponentType::kSkyLight: {
        const auto rec = TryReadSkyLight(record.bytes);
        if (!rec) {
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(
          ctx.Out(), "Source", static_cast<int>(rec->source), 10);
        PrintUtils::Field(
          ctx.Out(), "Cubemap Texture Reference", rec->cubemap.get(), 10);
        PrintUtils::Field(ctx.Out(), "Intensity", rec->intensity, 10);
        PrintUtils::Field(
          ctx.Out(), "Tint", asset_dump_helpers::FormatVec3(rec->tint_rgb), 10);
        PrintUtils::Field(
          ctx.Out(), "Diffuse Intensity", rec->diffuse_intensity, 10);
        PrintUtils::Field(
          ctx.Out(), "Specular Intensity", rec->specular_intensity, 10);

        PrintUtils::Field(ctx.Out(), "Source Cubemap Angle",
          rec->source_cubemap_angle_radians, 10);
        PrintUtils::Field(ctx.Out(), "Lower Hemisphere Color",
          asset_dump_helpers::FormatVec3(rec->lower_hemisphere_color), 10);
        PrintUtils::Field(ctx.Out(), "Lower Hemisphere Solid",
          rec->lower_hemisphere_is_solid_color != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Lower Hemisphere Blend",
          rec->lower_hemisphere_blend_alpha, 10);
        PrintUtils::Field(ctx.Out(), "Volumetric Scattering",
          rec->volumetric_scattering_intensity, 10);
        PrintUtils::Field(
          ctx.Out(), "Affect Reflections", rec->affect_reflections != 0U, 10);
        break;
      }
      case EnvironmentComponentType::kSkySphere: {
        const auto rec = TryReadSkySphere(record.bytes);
        if (!rec) {
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(
          ctx.Out(), "Source", static_cast<int>(rec->source), 10);
        PrintUtils::Field(
          ctx.Out(), "Cubemap Texture Reference", rec->cubemap.get(), 10);
        PrintUtils::Field(ctx.Out(), "Solid Color",
          asset_dump_helpers::FormatVec3(rec->solid_color_rgb), 10);
        PrintUtils::Field(ctx.Out(), "Intensity", rec->intensity, 10);
        PrintUtils::Field(
          ctx.Out(), "Rotation (rad)", rec->rotation_radians, 10);
        PrintUtils::Field(
          ctx.Out(), "Tint", asset_dump_helpers::FormatVec3(rec->tint_rgb), 10);
        break;
      }
      case EnvironmentComponentType::kBackground: {
        const auto rec = TryReadBackground(record.bytes);
        if (!rec) {
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }
        PrintUtils::Field(ctx.Out(), "Linear SDR Color",
          asset_dump_helpers::FormatVec3(rec->color_rgb), 10);
        break;
      }
      case EnvironmentComponentType::kPostProcessVolume: {
        const auto rec = scene->TryGetPostProcessVolumeEnvironment();
        if (!rec) {
          ctx.Out() << fmt::format("        (failed to decode)\n");
          break;
        }

        PrintUtils::Field(
          ctx.Out(), "Tone Mapper", static_cast<int>(rec->tone_mapper), 10);
        PrintUtils::Field(
          ctx.Out(), "Exposure Mode", static_cast<int>(rec->exposure_mode), 10);
        PrintUtils::Field(ctx.Out(), "Exposure Compensation (EV)",
          rec->exposure_compensation_ev, 10);
        PrintUtils::Field(
          ctx.Out(), "Auto Exposure Min (EV)", rec->auto_exposure_min_ev, 10);
        PrintUtils::Field(
          ctx.Out(), "Auto Exposure Max (EV)", rec->auto_exposure_max_ev, 10);
        PrintUtils::Field(
          ctx.Out(), "Auto Exposure Speed Up", rec->auto_exposure_speed_up, 10);
        PrintUtils::Field(ctx.Out(), "Auto Exposure Speed Down",
          rec->auto_exposure_speed_down, 10);
        PrintUtils::Field(
          ctx.Out(), "Bloom Intensity", rec->bloom_intensity, 10);
        PrintUtils::Field(
          ctx.Out(), "Bloom Threshold", rec->bloom_threshold, 10);
        PrintUtils::Field(ctx.Out(), "Saturation", rec->saturation, 10);
        PrintUtils::Field(ctx.Out(), "Contrast", rec->contrast, 10);
        PrintUtils::Field(
          ctx.Out(), "Vignette Intensity", rec->vignette_intensity, 10);
        PrintUtils::Field(
          ctx.Out(), "Exposure Enabled", rec->exposure_enabled != 0U, 10);
        PrintUtils::Field(ctx.Out(), "Exposure Key", rec->exposure_key, 10);
        PrintUtils::Field(
          ctx.Out(), "Manual Exposure (EV)", rec->manual_exposure_ev, 10);
        PrintUtils::Field(ctx.Out(), "Metering Mode",
          static_cast<int>(rec->auto_exposure_metering_mode), 10);
        PrintUtils::Field(
          ctx.Out(), "Low Percentile", rec->auto_exposure_low_percentile, 10);
        PrintUtils::Field(
          ctx.Out(), "High Percentile", rec->auto_exposure_high_percentile, 10);
        PrintUtils::Field(ctx.Out(), "Min Log Luminance",
          rec->auto_exposure_min_log_luminance, 10);
        PrintUtils::Field(ctx.Out(), "Log Luminance Range",
          rec->auto_exposure_log_luminance_range, 10);
        PrintUtils::Field(ctx.Out(), "Target Luminance",
          rec->auto_exposure_target_luminance, 10);
        PrintUtils::Field(ctx.Out(), "Spot Meter Radius",
          rec->auto_exposure_spot_meter_radius, 10);
        PrintUtils::Field(ctx.Out(), "Display Gamma", rec->display_gamma, 10);
        PrintUtils::Field(
          ctx.Out(), "Black Influence", rec->auto_exposure_black_influence, 10);
        PrintUtils::Field(ctx.Out(), "Transition Distance (EV)",
          rec->auto_exposure_transition_distance_ev, 10);
        PrintUtils::Field(ctx.Out(), "Metering Mask Texture Index",
          rec->auto_exposure_metering_mask.get(), 10);
        const auto curve = scene->GetPostProcessCompensationCurve();
        PrintUtils::Field(
          ctx.Out(), "Compensation Curve Keys", curve.size(), 10);
        for (const auto& key : curve) {
          ctx.Out() << fmt::format("          EV {}: compensation {} EV\n",
            key.metered_ev, key.compensation_ev);
        }

        break;
      }
      default:
        ctx.Out() << fmt::format("        (no decoder)\n");
        break;
      }
    }

    ctx.Out() << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
