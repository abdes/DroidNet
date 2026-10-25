//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/NodeReferenceJson.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneChangeValues.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/SceneRecordFields.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::content::import::internal {
namespace {
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  struct SourceContext final {
    observer_ptr<const std::filesystem::path> source_path;
    observer_ptr<std::vector<ImportDiagnostic>> diagnostics;
  };

  auto AddDiagnostic(SourceContext& context, const ImportSeverity severity,
    std::string code, std::string message, std::string object_path = {}) -> void
  {
    context.diagnostics->push_back({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = context.source_path->string(),
      .object_path = std::move(object_path),
    });
  }

  auto GetSceneDescriptorValidator() -> json_validator&
  {
    static auto validator
      = MakeJsonSchemaValidator(nlohmann::json::parse(kSceneDescriptorSchema));
    return validator;
  }

  auto ValidateDescriptorSchema(
    SourceContext& context, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "scene.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Scene descriptor validation failed: ",
      .validation_overflow_prefix = "Scene descriptor validation emitted ",
      .validator_failure_code = "scene.descriptor.schema_validator_failure",
      .validator_failure_prefix = "Scene descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetSceneDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) -> void {
        AddDiagnostic(context, ImportSeverity::kError, std::string(code),
          message, object_path);
      });
  }

  auto ValidateDescriptorVersion(
    SourceContext& context, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto version_it = descriptor_doc.find("version");
    const auto invalid_version = version_it == descriptor_doc.end()
      || !version_it->is_number_unsigned()
      || version_it->get<uint64_t>() != kSceneDescriptorVersion;
    if (!invalid_version) {
      return true;
    }

    AddDiagnostic(context, ImportSeverity::kError,
      "scene.descriptor.recook_required",
      "Scene descriptor version " + std::to_string(kSceneDescriptorVersion)
        + " is required; update the authored descriptor and re-cook the scene "
          "content.");
    return false;
  }

  using data::schema::ComponentId;
  namespace world = data::pak::world;

  //! Reads the properties of one authored object into its cooked record, as
  //! the scene schema declares them.
  /*!
   Each value goes to its field through the record's field writer
   (`SceneRecordFields.h`), as a scene change does. An omitted property keeps
   the value the record holds, which is the schema's default. A present one
   must be a finite value inside the generated range of its field, or a
   declared member of its enum. A violation is reported as
   `kScenePropertyOutOfRangeCode` at the property's path and keeps the
   record's value; the shared validity flag then reads false.
  */
  class PropertyReader final {
  public:
    PropertyReader(SourceContext& context, const json& document,
      std::string object_path, bool& valid)
      : context_(&context)
      , document_(&document)
      , path_(std::move(object_path))
      , valid_(&valid)
    {
    }

    //! The reader of the object `key` names, empty when it is omitted.
    [[nodiscard]] auto Nested(const std::string_view key) const
      -> PropertyReader
    {
      static const auto kEmptyObject = json::object();
      const auto found = document_->find(key);
      return { *context_, found == document_->end() ? kEmptyObject : *found,
        PathOf(key), *valid_ };
    }

    //! Whether the object holds `key`.
    [[nodiscard]] auto Contains(const std::string_view key) const -> bool
    {
      return document_->contains(key);
    }

    //! Writes the value of `key` into `field` of `record`, a field of
    //! `component`.
    template <typename Record, typename Field>
    auto Read(const std::string_view key, Record& record,
      const ComponentId component, const Field field) const -> void
    {
      const auto found = document_->find(key);
      if (found == document_->end()) {
        return;
      }
      const auto value
        = ToFieldValue(*found, component, static_cast<std::uint16_t>(field));
      if (!value.has_value() || !world::WriteField(record, field, *value)) {
        Reject(key, PathOf(key));
      }
    }

    //! Writes the member of an enum the descriptor spells by its key into
    //! `field` of `record`; `members` holds the wire values and `keys` the key
    //! of each, in the same order.
    template <typename Record, typename Field>
    auto ReadNamed(const std::string_view key, Record& record,
      const Field field, const std::span<const uint8_t> members,
      const std::span<const std::string_view> keys) const -> void
    {
      DCHECK_EQ_F(members.size(), keys.size());
      const auto found = document_->find(key);
      if (found == document_->end()) {
        return;
      }
      const auto member
        = std::ranges::find(keys, found->get_ref<const std::string&>());
      if (member == keys.end()
        || !world::WriteField(record, field,
          static_cast<std::int32_t>(
            members[static_cast<size_t>(member - keys.begin())]))) {
        Reject(key, PathOf(key));
      }
    }

    //! Writes an array of numbers into `fields` of `record`, one element
    //! each; unchanged unless every element is valid.
    template <typename Record, typename Field, size_t N>
    auto ReadArray(const std::string_view key, Record& record,
      const ComponentId component, const std::array<Field, N>& fields) const
      -> void
    {
      const auto found = document_->find(key);
      if (found == document_->end()) {
        return;
      }
      // The descriptor schema fixes the length of each array.
      DCHECK_EQ_F(found->size(), N);
      auto values = std::array<world::FieldValue, N> {};
      auto elements_valid = true;
      for (size_t i = 0; i < N; ++i) {
        const auto value = ToFieldValue(
          found->at(i), component, static_cast<std::uint16_t>(fields.at(i)));
        if (!value.has_value()) {
          Reject(key, PathOf(key) + "[" + std::to_string(i) + "]");
          elements_valid = false;
          continue;
        }
        values.at(i) = *value;
      }
      if (!elements_valid) {
        return;
      }
      for (size_t i = 0; i < N; ++i) {
        [[maybe_unused]] const auto written
          = world::WriteField(record, fields.at(i), values.at(i));
        DCHECK_F(written, "the record holds every field of '{}'", key);
      }
    }

    //! An array of finite numbers with no field of its own, such as a
    //! quaternion; unchanged unless every element is finite.
    auto ReadFloats(
      const std::string_view key, const std::span<float> out) const -> void
    {
      const auto found = document_->find(key);
      if (found == document_->end()) {
        return;
      }
      DCHECK_EQ_F(found->size(), out.size());
      auto elements_valid = true;
      for (size_t i = 0; i < out.size(); ++i) {
        if (!ToFloat(found->at(i).get<double>()).has_value()) {
          Reject(key, PathOf(key) + "[" + std::to_string(i) + "]");
          elements_valid = false;
        }
      }
      if (!elements_valid) {
        return;
      }
      for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<float>(found->at(i).get<double>());
      }
    }

  private:
    //! A value narrowed to float, or nothing for one a float cannot hold.
    [[nodiscard]] static auto ToFloat(const double value)
      -> std::optional<float>
    {
      if (!std::isfinite(value)
        || std::abs(value) > std::numeric_limits<float>::max()) {
        return std::nullopt;
      }
      return static_cast<float>(value);
    }

    //! The value of `field` of `component` that `document` holds, by the
    //! field's value type, or nothing when the field would not hold it.
    [[nodiscard]] static auto ToFieldValue(const json& document,
      const ComponentId component, const std::uint16_t field)
      -> std::optional<world::FieldValue>
    {
      const auto type = data::schema::FindFieldType(component, field);
      DCHECK_F(type.has_value(), "field {} is not one of its component", field);
      if (!type.has_value()) {
        return std::nullopt;
      }
      switch (*type) {
      case data::schema::FieldType::kBool:
        return document.get<bool>();
      case data::schema::FieldType::kInt32: {
        const auto value = document.get<std::int64_t>();
        if (!std::in_range<std::int32_t>(value)
          || !data::schema::IsValidFieldValue(
            component, field, static_cast<std::int32_t>(value))) {
          return std::nullopt;
        }
        return static_cast<std::int32_t>(value);
      }
      case data::schema::FieldType::kFloat: {
        const auto value = ToFloat(document.get<double>());
        if (!value.has_value()
          || !data::schema::IsValidFieldValue(component, field, *value)) {
          return std::nullopt;
        }
        return *value;
      }
      }
      return std::nullopt;
    }

    [[nodiscard]] auto PathOf(const std::string_view key) const -> std::string
    {
      return path_ + "." + std::string(key);
    }

    auto Reject(const std::string_view key, std::string path) const -> void
    {
      AddDiagnostic(*context_, ImportSeverity::kError,
        std::string(kScenePropertyOutOfRangeCode),
        "Property '" + std::string(key)
          + "' is not a finite value in the range, or a member, that the scene "
            "schema declares",
        std::move(path));
      *valid_ = false;
    }

    observer_ptr<SourceContext> context_;
    observer_ptr<const json> document_;
    std::string path_;
    observer_ptr<bool> valid_;
  };

  //! Reads one authored override value as its property's value type.
  auto ReadOverrideValue(SourceContext& context,
    const data::schema::OverrideProperty& property, const json& document,
    const std::string& object_path) -> std::optional<SceneSource::OverrideValue>
  {
    using data::schema::ValueType;
    switch (property.type) {
    case ValueType::kBool:
      return document.get<bool>();
    case ValueType::kAsset:
      return SceneSource::AssetReference {
        .virtual_path = document.get<std::string>(),
      };
    case ValueType::kLodPolicy:
      if (auto policy = ReadLodPolicy(document)) {
        return *policy;
      }
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.lod_policy_invalid",
        "LOD policy thresholds must be finite, distances must not decrease and "
        "screen-space errors must not increase",
        object_path);
      return std::nullopt;
    case ValueType::kInt:
    case ValueType::kFloat:
    case ValueType::kVec3:
    case ValueType::kEnum:
    case ValueType::kString:
      // No geometry override property has these types yet; the descriptor
      // schema admits none, and each needs its reader when the schema
      // declares one.
      break;
    }
    AddDiagnostic(context, ImportSeverity::kError,
      "scene.descriptor.override_unsupported",
      "The override value type is not supported", object_path);
    return std::nullopt;
  }

  //! Reads the `values` of one override owner into `overrides`.
  auto ReadOverrideValues(SourceContext& context, const json& values,
    const data::schema::PartKind part_kind, const data::SurfaceId part_id,
    const std::string& object_path,
    std::vector<SceneSource::Override>& overrides) -> bool
  {
    for (const auto& [key, document] : values.items()) {
      auto value_path = object_path;
      value_path.append("[\"").append(key).append("\"]");
      const auto* property
        = data::schema::FindOverrideProperty("geometry", part_kind, key);
      if (property == nullptr) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.override_unknown",
          "The scene schema declares no override with this key", value_path);
        return false;
      }
      auto value = ReadOverrideValue(context, *property, document, value_path);
      if (!value) {
        return false;
      }
      overrides.push_back({
        .property = property,
        .part_id = part_id,
        .value = std::move(*value),
        .object_path = value_path,
      });
    }
    return true;
  }

  //! Reads a renderable's `overrides`; each surface is named once.
  auto ReadOverrides(SourceContext& context, const json& document,
    const std::string& object_path,
    std::vector<SceneSource::Override>& overrides) -> bool
  {
    if (document.contains("values")
      && !ReadOverrideValues(context, document.at("values"),
        data::schema::PartKind::kNone, data::SurfaceId {},
        object_path + ".values", overrides)) {
      return false;
    }
    if (!document.contains("parts")) {
      return true;
    }
    auto surfaces = std::unordered_set<data::SurfaceId> {};
    const auto& parts = document.at("parts");
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts.at(i);
      const auto part_path = object_path + ".parts[" + std::to_string(i) + "]";
      // The descriptor schema admits surfaces only.
      DCHECK_F(part.at("kind").get<std::string>() == "surface");
      const auto surface
        = data::SurfaceId::FromString(part.at("id").get<std::string>());
      if (!surface || surface->IsNil() || !surfaces.insert(*surface).second) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.surface_id_invalid",
          "A surface override requires a non-nil surface identity, named once",
          part_path + ".id");
        return false;
      }
      if (!ReadOverrideValues(context, part.at("values"),
            data::schema::PartKind::kSurface, *surface, part_path + ".values",
            overrides)) {
        return false;
      }
    }
    return true;
  }

  //! Reads a node path, reporting the first element that is not a non-nil
  //! node ID.
  auto ReadNodePath(SourceContext& context, const json& document,
    const std::string& object_path) -> std::optional<std::vector<data::NodeId>>
  {
    auto path = ParseNodePath(document);
    if (!path.has_value()) {
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.node_path_invalid",
        "A node path element must be a non-nil node ID",
        object_path + "[" + std::to_string(path.error()) + "]");
      return std::nullopt;
    }
    return std::move(*path);
  }

  //! Reads the value of a change of a property its node's records hold, in
  //! schema units, and encodes it as the property's change.
  auto ReadChangeValue(SourceContext& context,
    const data::schema::OverrideProperty& property, const json& document,
    const std::string& object_path) -> std::optional<SceneSource::OverrideValue>
  {
    using data::schema::ValueType;
    auto value = std::optional<data::schema::SchemaValue> {};
    switch (property.type) {
    case ValueType::kBool:
      if (document.is_boolean()) {
        value = document.get<bool>();
      }
      break;
    case ValueType::kInt:
    case ValueType::kEnum:
      // An enum value is its member's wire value.
      if (document.is_number_integer()
        && std::in_range<std::int32_t>(document.get<std::int64_t>())) {
        value = static_cast<std::int32_t>(document.get<std::int64_t>());
      }
      break;
    case ValueType::kFloat:
      if (document.is_number() && std::isfinite(document.get<double>())) {
        value = document.get<float>();
      }
      break;
    case ValueType::kVec3:
      if (document.is_array() && document.size() == 3
        && std::ranges::all_of(document, [](const json& axis) -> bool {
             return axis.is_number() && std::isfinite(axis.get<double>());
           })) {
        value = std::array {
          document.at(0).get<float>(),
          document.at(1).get<float>(),
          document.at(2).get<float>(),
        };
      }
      break;
    case ValueType::kAsset:
    case ValueType::kString:
    case ValueType::kLodPolicy:
      break;
    }
    const auto encoded = value.has_value()
      ? data::schema::EncodeChange(property.id, *value)
      : std::nullopt;
    if (!encoded.has_value()) {
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.instance_override_invalid",
        "The value is not a valid value of the property the scene schema "
        "declares",
        object_path);
      return std::nullopt;
    }
    return *encoded;
  }

  //! Reads one component's changes of an instanced node.
  auto ReadComponentChanges(SourceContext& context,
    const std::string_view component_kind, const json& document,
    const std::string& object_path, const std::vector<data::NodeId>& path,
    std::vector<SceneSource::InstanceChange>& changes) -> bool
  {
    const auto read_values
      = [&](const json& values, const data::schema::PartKind part_kind,
          const data::SurfaceId part_id,
          const base::Sha256Digest& layout_revision,
          const std::string& values_path) -> bool {
      for (const auto& [key, value] : values.items()) {
        auto value_path = values_path;
        value_path.append("[\"").append(key).append("\"]");
        const auto* property
          = data::schema::FindOverrideProperty(component_kind, part_kind, key);
        if (property == nullptr) {
          AddDiagnostic(context, ImportSeverity::kError,
            "scene.descriptor.override_unknown",
            "The scene schema declares no changeable property with this "
            "component and key",
            value_path);
          return false;
        }
        auto read = property->asset_sourced
          ? ReadOverrideValue(context, *property, value, value_path)
          : ReadChangeValue(context, *property, value, value_path);
        if (!read.has_value()) {
          return false;
        }
        changes.push_back({
          .path = path,
          .change = {
            .property = property,
            .part_id = part_id,
            .layout_revision = layout_revision,
            .value = std::move(*read),
            .object_path = std::move(value_path),
          },
        });
      }
      return true;
    };

    if (document.contains("values")
      && !read_values(document.at("values"), data::schema::PartKind::kNone,
        data::SurfaceId {}, base::Sha256Digest {}, object_path + ".values")) {
      return false;
    }
    if (!document.contains("parts")) {
      return true;
    }
    const auto& parts = document.at("parts");
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts.at(i);
      const auto part_path = object_path + ".parts[" + std::to_string(i) + "]";
      // Surfaces are the only part kind.
      if (part.at("kind").get<std::string>() != "surface") {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.part_kind_unknown",
          "The scene schema declares no part of this kind",
          part_path + ".kind");
        return false;
      }
      const auto surface
        = data::SurfaceId::FromString(part.at("id").get<std::string>());
      const auto revision = base::Sha256DigestFromHex(
        part.at("layout_revision").get<std::string>());
      if (!surface.has_value() || surface.value().IsNil()) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.surface_id_invalid",
          "A surface change requires a non-nil surface identity",
          part_path + ".id");
        return false;
      }
      if (!revision.has_value()) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.layout_revision_invalid",
          "A surface change requires the layout revision it was made for",
          part_path + ".layout_revision");
        return false;
      }
      if (!read_values(part.at("values"), data::schema::PartKind::kSurface,
            surface.value(), *revision, part_path + ".values")) {
        return false;
      }
    }
    return true;
  }

  //! Reads an instance root's `overrides`, the changes of the nodes the
  //! instance brings in.
  auto ReadInstanceChanges(SourceContext& context, const json& document,
    const std::string& object_path,
    std::vector<SceneSource::InstanceChange>& changes) -> bool
  {
    for (size_t i = 0; i < document.size(); ++i) {
      const auto& entry = document.at(i);
      const auto entry_path = object_path + "[" + std::to_string(i) + "]";
      const auto path
        = ReadNodePath(context, entry.at("path"), entry_path + ".path");
      if (!path.has_value()) {
        return false;
      }
      for (const auto& [kind, component] : entry.at("components").items()) {
        auto component_path = entry_path;
        component_path.append(".components.").append(kind);
        if (!ReadComponentChanges(
              context, kind, component, component_path, *path, changes)) {
          return false;
        }
      }
    }
    // A scene holds one change per node, property and part.
    auto seen = std::set<std::tuple<std::vector<data::NodeId>,
      data::schema::OverrideId, data::SurfaceId>> {};
    for (const auto& change : changes) {
      if (!seen
            .emplace(
              change.path, change.change.property->id, change.change.part_id)
            .second) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.instance_override_duplicate",
          "Another change of this instance names the same node, property and "
          "part",
          change.change.object_path);
        return false;
      }
    }
    return true;
  }

  //! Copies the leading elements of a JSON number array into a fixed field.
  auto CopyFloatArray(const json& source, const std::span<float> dest) -> void
  {
    for (size_t i = 0; auto& value : dest) {
      value = source.at(i++).get<float>();
    }
  }

  auto BuildSkyAtmosphereSystemRecord(const json& source)
    -> data::pak::world::SkyAtmosphereEnvironmentRecord
  {
    auto record = data::pak::world::SkyAtmosphereEnvironmentRecord {};
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.planet_radius_m
      = source.value("planet_radius_m", record.planet_radius_m);
    record.atmosphere_height_m
      = source.value("atmosphere_height_m", record.atmosphere_height_m);
    CopyFloatArray(source.at("ground_albedo_rgb"), record.ground_albedo_rgb);
    CopyFloatArray(
      source.at("rayleigh_scattering_rgb"), record.rayleigh_scattering_rgb);
    record.rayleigh_scale_height_m
      = source.value("rayleigh_scale_height_m", record.rayleigh_scale_height_m);
    CopyFloatArray(source.at("mie_scattering_rgb"), record.mie_scattering_rgb);
    CopyFloatArray(source.at("mie_absorption_rgb"), record.mie_absorption_rgb);
    record.mie_scale_height_m
      = source.value("mie_scale_height_m", record.mie_scale_height_m);
    record.mie_g = source.value("mie_anisotropy", record.mie_g);
    CopyFloatArray(source.at("ozone_absorption_rgb"), record.absorption_rgb);
    CopyFloatArray(
      source.at("ozone_density_profile"), record.ozone_density_profile);
    record.multi_scattering_factor
      = source.value("multi_scattering_factor", record.multi_scattering_factor);
    CopyFloatArray(
      source.at("sky_luminance_factor_rgb"), record.sky_luminance_factor_rgb);
    CopyFloatArray(source.at("sky_and_aerial_perspective_luminance_factor_rgb"),
      record.sky_and_aerial_perspective_luminance_factor_rgb);
    record.aerial_perspective_distance_scale
      = source.value("aerial_perspective_distance_scale",
        record.aerial_perspective_distance_scale);
    record.aerial_scattering_strength = source.value(
      "aerial_scattering_strength", record.aerial_scattering_strength);
    record.aerial_perspective_start_depth_m
      = source.value("aerial_perspective_start_depth_m",
        record.aerial_perspective_start_depth_m);
    record.height_fog_contribution
      = source.value("height_fog_contribution", record.height_fog_contribution);
    record.trace_sample_count_scale = source.value(
      "trace_sample_count_scale", record.trace_sample_count_scale);
    record.transmittance_min_light_elevation_deg
      = source.value("transmittance_min_light_elevation_deg",
        record.transmittance_min_light_elevation_deg);
    record.sun_disk_enabled = source.value("sun_disk_enabled", true) ? 1U : 0U;
    record.holdout = source.value("holdout", false) ? 1U : 0U;
    record.render_in_main_pass
      = source.value("render_in_main_pass", true) ? 1U : 0U;

    return record;
  }

  auto BuildFogSystemRecord(const json& source) -> SceneSource::Fog
  {
    auto prepared = SceneSource::Fog {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.model = source.value("model", record.model);
    record.extinction_sigma_t_per_m = source.value(
      "extinction_sigma_t_per_m", record.extinction_sigma_t_per_m);
    record.height_falloff_per_m
      = source.value("height_falloff_per_m", record.height_falloff_per_m);
    record.height_offset_m
      = source.value("height_offset_m", record.height_offset_m);
    record.start_distance_m
      = source.value("start_distance_m", record.start_distance_m);
    record.max_opacity = source.value("max_opacity", record.max_opacity);
    CopyFloatArray(source.at("single_scattering_albedo_rgb"),
      record.single_scattering_albedo_rgb);
    record.anisotropy_g = source.value("anisotropy_g", record.anisotropy_g);
    record.enable_height_fog
      = source.value("enable_height_fog", true) ? 1U : 0U;
    record.enable_volumetric_fog
      = source.value("enable_volumetric_fog", false) ? 1U : 0U;
    record.second_fog_density
      = source.value("second_fog_density", record.second_fog_density);
    record.second_fog_height_falloff = source.value(
      "second_fog_height_falloff", record.second_fog_height_falloff);
    record.second_fog_height_offset = source.value(
      "second_fog_height_offset", record.second_fog_height_offset);
    CopyFloatArray(source.at("fog_inscattering_luminance"),
      record.fog_inscattering_luminance);
    CopyFloatArray(source.at("sky_atmosphere_ambient_contribution_color_scale"),
      record.sky_atmosphere_ambient_contribution_color_scale);
    if (source.contains("inscattering_color_cubemap_ref")) {
      prepared.cubemap
        = source.at("inscattering_color_cubemap_ref").get<std::string>();
    }
    record.inscattering_color_cubemap_angle
      = source.value("inscattering_color_cubemap_angle",
        record.inscattering_color_cubemap_angle);
    CopyFloatArray(
      source.at("inscattering_texture_tint"), record.inscattering_texture_tint);
    record.fully_directional_inscattering_color_distance
      = source.value("fully_directional_inscattering_color_distance",
        record.fully_directional_inscattering_color_distance);
    record.non_directional_inscattering_color_distance
      = source.value("non_directional_inscattering_color_distance",
        record.non_directional_inscattering_color_distance);
    CopyFloatArray(source.at("directional_inscattering_luminance"),
      record.directional_inscattering_luminance);
    record.directional_inscattering_exponent
      = source.value("directional_inscattering_exponent",
        record.directional_inscattering_exponent);
    record.directional_inscattering_start_distance
      = source.value("directional_inscattering_start_distance",
        record.directional_inscattering_start_distance);
    record.end_distance_m
      = source.value("end_distance_m", record.end_distance_m);
    record.fog_cutoff_distance_m
      = source.value("fog_cutoff_distance_m", record.fog_cutoff_distance_m);
    record.volumetric_fog_scattering_distribution
      = source.value("volumetric_fog_scattering_distribution",
        record.volumetric_fog_scattering_distribution);
    CopyFloatArray(
      source.at("volumetric_fog_albedo"), record.volumetric_fog_albedo);
    CopyFloatArray(
      source.at("volumetric_fog_emissive"), record.volumetric_fog_emissive);
    record.volumetric_fog_extinction_scale
      = source.value("volumetric_fog_extinction_scale",
        record.volumetric_fog_extinction_scale);
    record.volumetric_fog_distance
      = source.value("volumetric_fog_distance", record.volumetric_fog_distance);
    record.volumetric_fog_start_distance = source.value(
      "volumetric_fog_start_distance", record.volumetric_fog_start_distance);
    record.volumetric_fog_near_fade_in_distance
      = source.value("volumetric_fog_near_fade_in_distance",
        record.volumetric_fog_near_fade_in_distance);
    record.volumetric_fog_static_lighting_scattering_intensity
      = source.value("volumetric_fog_static_lighting_scattering_intensity",
        record.volumetric_fog_static_lighting_scattering_intensity);
    record.override_light_colors_with_fog_inscattering_colors
      = source.value(
          "override_light_colors_with_fog_inscattering_colors", false)
      ? 1U
      : 0U;
    record.holdout = source.value("holdout", false) ? 1U : 0U;
    record.render_in_main_pass
      = source.value("render_in_main_pass", true) ? 1U : 0U;
    record.visible_in_reflection_captures
      = source.value("visible_in_reflection_captures", true) ? 1U : 0U;
    record.visible_in_real_time_sky_captures
      = source.value("visible_in_real_time_sky_captures", true) ? 1U : 0U;

    return prepared;
  }

  auto BuildSkyLightSystemRecord(const json& source) -> SceneSource::SkyLight
  {
    auto prepared = SceneSource::SkyLight {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.source = source.value("source", record.source);
    if (source.contains("cubemap_ref")) {
      prepared.cubemap = source.at("cubemap_ref").get<std::string>();
    }
    record.illuminance_lux
      = source.value("illuminance_lux", record.illuminance_lux);
    record.intensity = source.value("intensity", record.intensity);
    CopyFloatArray(source.at("tint_rgb"), record.tint_rgb);
    record.diffuse_intensity
      = source.value("diffuse_intensity", record.diffuse_intensity);
    record.specular_intensity
      = source.value("specular_intensity", record.specular_intensity);

    record.source_cubemap_angle_radians = source.value(
      "source_cubemap_angle_radians", record.source_cubemap_angle_radians);
    CopyFloatArray(
      source.at("lower_hemisphere_color"), record.lower_hemisphere_color);
    record.lower_hemisphere_is_solid_color
      = source.value("lower_hemisphere_is_solid_color", true) ? 1U : 0U;
    record.lower_hemisphere_blend_alpha = source.value(
      "lower_hemisphere_blend_alpha", record.lower_hemisphere_blend_alpha);
    record.volumetric_scattering_intensity
      = source.value("volumetric_scattering_intensity",
        record.volumetric_scattering_intensity);
    record.affect_reflections
      = source.value("affect_reflections", true) ? 1U : 0U;

    return prepared;
  }

  auto BuildSkySphereSystemRecord(const json& source) -> SceneSource::SkySphere
  {
    auto prepared = SceneSource::SkySphere {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.source = source.value("source", record.source);
    if (source.contains("cubemap_ref")) {
      prepared.cubemap = source.at("cubemap_ref").get<std::string>();
    }
    if (source.contains("solid_color_rgb")) {
      CopyFloatArray(source.at("solid_color_rgb"), record.solid_color_rgb);
    }
    record.illuminance_lux
      = source.value("illuminance_lux", record.illuminance_lux);
    record.intensity = source.value("intensity", record.intensity);
    record.rotation_radians
      = source.value("rotation_radians", record.rotation_radians);
    if (source.contains("tint_rgb")) {
      CopyFloatArray(source.at("tint_rgb"), record.tint_rgb);
    }
    return prepared;
  }

  auto BuildPostProcessSystemRecord(SourceContext& context, const json& source)
    -> std::optional<SceneSource::PostProcess>
  {
    auto prepared = SceneSource::PostProcess {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.tone_mapper = static_cast<engine::ToneMapper>(
      source.value("tone_mapper", static_cast<uint32_t>(record.tone_mapper)));
    record.exposure_mode = static_cast<engine::ExposureMode>(source.value(
      "exposure_mode", static_cast<uint32_t>(record.exposure_mode)));
    record.exposure_enabled
      = source.value("exposure_enabled", record.exposure_enabled != 0U) ? 1U
                                                                        : 0U;
    record.exposure_compensation_ev = source.value(
      "exposure_compensation_ev", record.exposure_compensation_ev);
    record.exposure_key = source.value("exposure_key", record.exposure_key);
    record.manual_exposure_ev
      = source.value("manual_exposure_ev", record.manual_exposure_ev);
    record.auto_exposure_min_ev
      = source.value("auto_exposure_min_ev", record.auto_exposure_min_ev);
    record.auto_exposure_max_ev
      = source.value("auto_exposure_max_ev", record.auto_exposure_max_ev);
    record.auto_exposure_speed_up
      = source.value("auto_exposure_speed_up", record.auto_exposure_speed_up);
    record.auto_exposure_speed_down = source.value(
      "auto_exposure_speed_down", record.auto_exposure_speed_down);
    record.auto_exposure_metering_mode = static_cast<engine::MeteringMode>(
      source.value("auto_exposure_metering_mode",
        static_cast<uint32_t>(record.auto_exposure_metering_mode)));
    record.auto_exposure_low_percentile = source.value(
      "auto_exposure_low_percentile", record.auto_exposure_low_percentile);
    record.auto_exposure_high_percentile = source.value(
      "auto_exposure_high_percentile", record.auto_exposure_high_percentile);
    record.auto_exposure_min_log_luminance
      = source.value("auto_exposure_min_log_luminance",
        record.auto_exposure_min_log_luminance);
    record.auto_exposure_log_luminance_range
      = source.value("auto_exposure_log_luminance_range",
        record.auto_exposure_log_luminance_range);
    record.auto_exposure_target_luminance = source.value(
      "auto_exposure_target_luminance", record.auto_exposure_target_luminance);
    record.auto_exposure_spot_meter_radius
      = source.value("auto_exposure_spot_meter_radius",
        record.auto_exposure_spot_meter_radius);
    record.bloom_intensity
      = source.value("bloom_intensity", record.bloom_intensity);
    record.bloom_threshold
      = source.value("bloom_threshold", record.bloom_threshold);
    record.saturation = source.value("saturation", record.saturation);
    record.contrast = source.value("contrast", record.contrast);
    record.vignette_intensity
      = source.value("vignette_intensity", record.vignette_intensity);
    record.display_gamma = source.value("display_gamma", record.display_gamma);
    record.auto_exposure_black_influence = source.value(
      "auto_exposure_black_influence", record.auto_exposure_black_influence);
    record.auto_exposure_transition_distance_ev
      = source.value("auto_exposure_transition_distance_ev",
        record.auto_exposure_transition_distance_ev);
    if (source.contains("auto_exposure_metering_mask")) {
      prepared.metering_mask
        = source.at("auto_exposure_metering_mask").get<std::string>();
    }
    auto settings = scene::ExposureSettings {
      .enabled = record.exposure_enabled != 0U,
      .mode = record.exposure_mode,
      .manual_ev = record.manual_exposure_ev,
      .compensation_ev = record.exposure_compensation_ev,
      .key = record.exposure_key,
      .min_ev = record.auto_exposure_min_ev,
      .max_ev = record.auto_exposure_max_ev,
      .speed_up = record.auto_exposure_speed_up,
      .speed_down = record.auto_exposure_speed_down,
      .metering_mode = record.auto_exposure_metering_mode,
      .low_percentile = record.auto_exposure_low_percentile,
      .high_percentile = record.auto_exposure_high_percentile,
      .min_log_luminance = record.auto_exposure_min_log_luminance,
      .log_luminance_range = record.auto_exposure_log_luminance_range,
      .target_luminance = record.auto_exposure_target_luminance,
      .spot_meter_radius = record.auto_exposure_spot_meter_radius,
      .black_influence = record.auto_exposure_black_influence,
      .transition_distance = record.auto_exposure_transition_distance_ev,
    };
    if (const auto curve = source.find("auto_exposure_compensation_curve");
      curve != source.end()) {
      for (const auto& key : *curve) {
        settings.compensation_curve.push_back({
          .metered_ev = key.at("metered_ev").get<float>(),
          .compensation_ev = key.at("compensation_ev").get<float>(),
        });
      }
    }
    const auto resolved = scene::ResolveExposureSettings(settings);
    // The authored record has no selected view/camera. Validate gain again
    // with the actual camera EV when the runtime activates ManualCamera.
    if (!resolved
      && resolved.error() != scene::ExposureSettingsError::kMissingCameraEv) {
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.exposure_invalid",
        "Exposure settings rejected: "
          + std::string(scene::to_string(resolved.error())),
        "environment.post_process_volume");
      return std::nullopt;
    }
    record.curve_key_count
      = static_cast<uint32_t>(settings.compensation_curve.size());
    record.header.record_size = static_cast<uint32_t>(sizeof(record)
      + (settings.compensation_curve.size()
        * sizeof(data::pak::world::ExposureCompensationKeyRecord)));
    prepared.curve.reserve(settings.compensation_curve.size());
    for (const auto& key : settings.compensation_curve) {
      prepared.curve.push_back({
        .metered_ev = key.metered_ev,
        .compensation_ev = key.compensation_ev,
      });
    }
    return prepared;
  }

  auto BuildBackgroundSystemRecord(const json& source)
    -> data::pak::world::BackgroundEnvironmentRecord
  {
    auto record = data::pak::world::BackgroundEnvironmentRecord {};
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    if (source.contains("color_rgb")) {
      CopyFloatArray(source.at("color_rgb"), record.color_rgb);
    }
    return record;
  }

  auto ValidateNodeIndex(SourceContext& context, const uint32_t node_index,
    const uint32_t node_count, std::string code, std::string message,
    std::string object_path) -> bool
  {
    if (node_index < node_count) {
      return true;
    }
    AddDiagnostic(context, ImportSeverity::kError, std::move(code),
      std::move(message), std::move(object_path));
    return false;
  }

  //! Reads a light's `common` object into `record`; `Field` is the light's
  //! field set, whose shared fields have the same names in every light kind.
  template <typename Field, typename Record>
  auto ReadLightCommon(const PropertyReader& light, Record& record,
    const ComponentId component) -> void
  {
    const auto reader = light.Nested("common");
    reader.Read("affects_world", record, component, Field::kAffectsWorld);
    reader.ReadArray("color_rgb", record, component,
      std::array { Field::kColorR, Field::kColorG, Field::kColorB });
    reader.Read("casts_shadows", record, component, Field::kCastsShadows);
    reader.Read("exposure_compensation_ev", record, component,
      Field::kExposureCompensation);

    const auto shadow = reader.Nested("shadow");
    shadow.Read("bias", record, component, Field::kShadowBias);
    shadow.Read("normal_bias", record, component, Field::kShadowNormalBias);
    shadow.Read("contact_shadows", record, component, Field::kContactShadows);
    shadow.Read(
      "resolution_hint", record, component, Field::kShadowResolutionHint);
  }

  //! Reads a node's `flags`: an omitted flag takes the schema's default for a
  //! root or for a child.
  auto ReadNodeFlags(const PropertyReader& node, const bool is_root,
    data::pak::world::NodeRecord& record) -> void
  {
    namespace flags_schema = data::schema::node_flags;
    using data::schema::NodeField;
    const auto write_default = [&record](const NodeField field,
                                 const world::FieldValue& value) -> void {
      [[maybe_unused]] const auto written
        = world::WriteField(record, field, value);
      DCHECK_F(written, "the node record holds every node flag");
    };
    write_default(NodeField::kVisible,
      static_cast<std::int32_t>(
        std::to_underlying(is_root ? flags_schema::kVisibleRootDefault
                                   : flags_schema::kVisibleDefault)));
    write_default(NodeField::kCastsShadows,
      static_cast<std::int32_t>(
        std::to_underlying(is_root ? flags_schema::kCastsShadowsRootDefault
                                   : flags_schema::kCastsShadowsDefault)));
    write_default(NodeField::kReceivesShadows,
      static_cast<std::int32_t>(
        std::to_underlying(is_root ? flags_schema::kReceivesShadowsRootDefault
                                   : flags_schema::kReceivesShadowsDefault)));
    write_default(NodeField::kStatic, flags_schema::kStaticDefault);
    write_default(NodeField::kRayCastingSelectable,
      flags_schema::kRayCastingSelectableDefault);
    write_default(NodeField::kIgnoreParentTransform,
      flags_schema::kIgnoreParentTransformDefault);

    const auto flags = node.Nested("flags");
    flags.ReadNamed("visible", record, NodeField::kVisible,
      flags_schema::kVisibleValues, data::schema::kVisibilitySourceMemberKeys);
    flags.ReadNamed("casts_shadows", record, NodeField::kCastsShadows,
      flags_schema::kCastsShadowsValues, data::schema::kShadowSourceMemberKeys);
    flags.ReadNamed("receives_shadows", record, NodeField::kReceivesShadows,
      flags_schema::kReceivesShadowsValues,
      data::schema::kShadowSourceMemberKeys);
    flags.Read("static", record, ComponentId::kNode, NodeField::kStatic);
    flags.Read("ray_cast_selectable", record, ComponentId::kNode,
      NodeField::kRayCastingSelectable);
    flags.Read("ignore_parent_transform", record, ComponentId::kNode,
      NodeField::kIgnoreParentTransform);
  }

  //! Reads a node's `transform` over the defaults `record` holds. The
  //! rotation is a quaternion, which the scene schema states no range for
  //! beyond being finite.
  auto ReadNodeTransform(
    const PropertyReader& node, data::pak::world::NodeRecord& record) -> void
  {
    using data::schema::TransformField;
    const auto transform = node.Nested("transform");
    transform.ReadArray("translation", record, ComponentId::kTransform,
      std::array {
        TransformField::kPositionX,
        TransformField::kPositionY,
        TransformField::kPositionZ,
      });
    auto rotation = world::Quaternion {};
    std::ranges::copy(record.rotation, rotation.begin());
    transform.ReadFloats("rotation", rotation);
    world::WriteRotation(record, rotation);
    transform.ReadArray("scale", record, ComponentId::kTransform,
      std::array {
        TransformField::kScaleX,
        TransformField::kScaleY,
        TransformField::kScaleZ,
      });
  }

  auto PrepareSceneSource(SourceContext& context, const json& descriptor_doc)
    -> std::optional<SceneSource>
  {
    auto prepared = SceneSource {};
    prepared.name = descriptor_doc.at("name").get<std::string>();
    if (descriptor_doc.contains("content_hashing")) {
      prepared.content_hashing
        = descriptor_doc.at("content_hashing").get<bool>();
    }
    const auto& nodes_doc = descriptor_doc.at("nodes");
    const auto node_count = static_cast<uint32_t>(nodes_doc.size());

    auto& string_table = prepared.build.strings;
    prepared.build.nodes.reserve(nodes_doc.size());
    auto node_ids = std::unordered_set<data::NodeId> {};

    for (size_t node_i = 0; node_i < nodes_doc.size(); ++node_i) {
      const auto& node_doc = nodes_doc.at(node_i);
      const auto node_path
        = std::string { "nodes[" } + std::to_string(node_i) + "]";

      auto node_record = data::pak::world::NodeRecord {};
      const auto node_id = ParseNonNilId<data::NodeId>(node_doc.at("id"));
      if (!node_id.has_value()) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.node_id_invalid",
          "A node ID must be a non-nil UUID", node_path + ".id");
        return std::nullopt;
      }
      if (!node_ids.insert(*node_id).second) {
        AddDiagnostic(context, ImportSeverity::kError,
          "scene.descriptor.node_id_duplicate",
          "Another node of the scene has this ID", node_path + ".id");
        return std::nullopt;
      }
      node_record.node_id = *node_id;
      if (node_doc.contains("name")) {
        node_record.scene_name_offset
          = string_table.Add(node_doc.at("name").get<std::string>());
      }
      node_record.parent_index = static_cast<uint32_t>(node_i);
      if (node_doc.contains("parent")) {
        const auto parent_index = node_doc.at("parent").get<uint32_t>();
        if (parent_index >= node_count) {
          AddDiagnostic(context, ImportSeverity::kError,
            "scene.descriptor.node_parent_out_of_range",
            "Node parent index is out of range", node_path + ".parent");
          return std::nullopt;
        }
        node_record.parent_index = parent_index;
      }

      auto node_valid = true;
      const auto node_reader
        = PropertyReader(context, node_doc, node_path, node_valid);
      ReadNodeFlags(node_reader,
        node_record.parent_index == static_cast<uint32_t>(node_i), node_record);
      ReadNodeTransform(node_reader, node_record);
      if (!node_valid) {
        return std::nullopt;
      }

      if (node_doc.contains("instance")) {
        const auto& instance_doc = node_doc.at("instance");
        auto instance = SceneSource::Instance {
          .node_index = static_cast<uint32_t>(node_i),
          .scene = instance_doc.at("scene_ref").get<std::string>(),
          .object_path = node_path + ".instance.scene_ref",
          .changes = {},
        };
        if (instance_doc.contains("overrides")
          && !ReadInstanceChanges(context, instance_doc.at("overrides"),
            node_path + ".instance.overrides", instance.changes)) {
          return std::nullopt;
        }
        prepared.instances.push_back(std::move(instance));
      }

      prepared.build.nodes.push_back(node_record);
    }

    if (descriptor_doc.contains("starting_camera")) {
      const auto& camera_doc = descriptor_doc.at("starting_camera");
      const auto camera = camera_doc.at("node").get<uint32_t>();
      if (!ValidateNodeIndex(context, camera, node_count,
            "scene.descriptor.starting_camera_out_of_range",
            "Starting camera node index is out of range",
            "starting_camera.node")) {
        return std::nullopt;
      }
      prepared.build.starting_camera.node_index = camera;
      if (camera_doc.contains("path")) {
        auto path = ReadNodePath(
          context, camera_doc.at("path"), "starting_camera.path");
        if (!path.has_value()) {
          return std::nullopt;
        }
        prepared.starting_camera_path = std::move(*path);
      }
    }
    if (descriptor_doc.contains("starting_input_contexts")) {
      const auto& values = descriptor_doc.at("starting_input_contexts");
      prepared.starting_input_contexts.reserve(values.size());
      for (size_t i = 0; i < values.size(); ++i) {
        const auto& document = values.at(i);
        prepared.starting_input_contexts.push_back({
          .context = document.at("context_ref").get<std::string>(),
          .priority = document.value("priority", int32_t { 0 }),
          .object_path
          = "starting_input_contexts[" + std::to_string(i) + "].context_ref",
        });
      }
    }

    if (descriptor_doc.contains("renderables")) {
      const auto& values = descriptor_doc.at("renderables");
      prepared.renderables.reserve(values.size());
      for (size_t i = 0; i < values.size(); ++i) {
        const auto& document = values.at(i);
        const auto object_path = "renderables[" + std::to_string(i) + "]";
        auto renderable = SceneSource::Renderable {
          .node_index = document.at("node").get<uint32_t>(),
          .geometry = document.at("geometry_ref").get<std::string>(),
          .overrides = {},
        };
        if (!ValidateNodeIndex(context, renderable.node_index, node_count,
              "scene.descriptor.renderable_node_index_out_of_range",
              "Renderable node index is out of range", object_path + ".node")) {
          return std::nullopt;
        }
        if (document.contains("overrides")
          && !ReadOverrides(context, document.at("overrides"),
            object_path + ".overrides", renderable.overrides)) {
          return std::nullopt;
        }
        prepared.renderables.push_back(std::move(renderable));
      }
    }

    if (descriptor_doc.contains("cameras")) {
      const auto& cameras_doc = descriptor_doc.at("cameras");
      if (cameras_doc.contains("perspective")) {
        const auto& perspective_doc = cameras_doc.at("perspective");
        prepared.build.perspective_cameras.reserve(perspective_doc.size());
        for (size_t i = 0; i < perspective_doc.size(); ++i) {
          const auto& camera_doc = perspective_doc.at(i);
          const auto object_path
            = std::string { "cameras.perspective[" } + std::to_string(i) + "]";
          const auto node_index = camera_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.camera_node_index_out_of_range",
                "Perspective camera node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          using data::schema::PerspectiveCameraField;
          auto camera
            = world::MakeDefaultRecord<world::PerspectiveCameraRecord>();
          camera.node_index = node_index;
          auto camera_valid = true;
          const auto reader
            = PropertyReader(context, camera_doc, object_path, camera_valid);
          constexpr auto kCamera = ComponentId::kPerspectiveCamera;
          reader.ReadNamed("aspect_mode", camera,
            PerspectiveCameraField::kAspectMode,
            data::schema::kCameraAspectModeMembers,
            data::schema::kCameraAspectModeMemberKeys);
          reader.Read("fov_y", camera, kCamera,
            PerspectiveCameraField::kFieldOfViewYRadians);
          reader.Read("aspect_ratio", camera, kCamera,
            PerspectiveCameraField::kAspectRatio);
          reader.Read(
            "near_plane", camera, kCamera, PerspectiveCameraField::kNearPlane);
          reader.Read(
            "far_plane", camera, kCamera, PerspectiveCameraField::kFarPlane);
          reader.Read(
            "aperture_f", camera, kCamera, PerspectiveCameraField::kApertureF);
          reader.Read("shutter_rate", camera, kCamera,
            PerspectiveCameraField::kShutterRate);
          reader.Read("iso", camera, kCamera, PerspectiveCameraField::kIso);
          if (!camera_valid) {
            return std::nullopt;
          }
          prepared.build.perspective_cameras.push_back(camera);
        }
      }

      if (cameras_doc.contains("orthographic")) {
        const auto& orthographic_doc = cameras_doc.at("orthographic");
        prepared.build.orthographic_cameras.reserve(orthographic_doc.size());
        for (size_t i = 0; i < orthographic_doc.size(); ++i) {
          const auto& camera_doc = orthographic_doc.at(i);
          const auto object_path
            = std::string { "cameras.orthographic[" } + std::to_string(i) + "]";
          const auto node_index = camera_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.camera_node_index_out_of_range",
                "Orthographic camera node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          using data::schema::OrthographicCameraField;
          auto camera
            = world::MakeDefaultRecord<world::OrthographicCameraRecord>();
          camera.node_index = node_index;
          auto camera_valid = true;
          const auto reader
            = PropertyReader(context, camera_doc, object_path, camera_valid);
          constexpr auto kCamera = ComponentId::kOrthographicCamera;
          reader.ReadNamed("aspect_mode", camera,
            OrthographicCameraField::kAspectMode,
            data::schema::kCameraAspectModeMembers,
            data::schema::kCameraAspectModeMemberKeys);
          // The volume is the authored half height and aspect ratio.
          reader.Read("orthographic_size", camera, kCamera,
            OrthographicCameraField::kOrthographicSize);
          reader.Read("aspect_ratio", camera, kCamera,
            OrthographicCameraField::kAspectRatio);
          reader.Read(
            "near_plane", camera, kCamera, OrthographicCameraField::kNearPlane);
          reader.Read(
            "far_plane", camera, kCamera, OrthographicCameraField::kFarPlane);
          reader.Read(
            "aperture_f", camera, kCamera, OrthographicCameraField::kApertureF);
          reader.Read("shutter_rate", camera, kCamera,
            OrthographicCameraField::kShutterRate);
          reader.Read("iso", camera, kCamera, OrthographicCameraField::kIso);
          if (!camera_valid) {
            return std::nullopt;
          }
          prepared.build.orthographic_cameras.push_back(camera);
        }
      }
    }

    if (descriptor_doc.contains("lights")) {
      const auto& lights_doc = descriptor_doc.at("lights");
      if (lights_doc.contains("directional")) {
        const auto& directional_doc = lights_doc.at("directional");
        prepared.build.directional_lights.reserve(directional_doc.size());
        for (size_t i = 0; i < directional_doc.size(); ++i) {
          const auto& light_doc = directional_doc.at(i);
          const auto object_path
            = std::string { "lights.directional[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Directional light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          using data::schema::DirectionalLightField;
          auto light
            = world::MakeDefaultRecord<world::DirectionalLightRecord>();
          light.node_index = node_index;
          auto light_valid = true;
          const auto reader
            = PropertyReader(context, light_doc, object_path, light_valid);
          constexpr auto kLight = ComponentId::kDirectionalLight;
          ReadLightCommon<DirectionalLightField>(reader, light, kLight);
          reader.Read("angular_size_radians", light, kLight,
            DirectionalLightField::kAngularSizeRadians);
          reader.Read("atmosphere_light_slot", light, kLight,
            DirectionalLightField::kAtmosphereLightSlot);
          reader.Read("use_per_pixel_atmosphere_transmittance", light, kLight,
            DirectionalLightField::kUsePerPixelAtmosphereTransmittance);
          reader.ReadArray("atmosphere_disk_luminance_scale_rgb", light, kLight,
            std::array {
              DirectionalLightField::kDiskScaleR,
              DirectionalLightField::kDiskScaleG,
              DirectionalLightField::kDiskScaleB,
            });
          reader.Read("cascade_count", light, kLight,
            DirectionalLightField::kCascadeCount);
          reader.Read(
            "split_mode", light, kLight, DirectionalLightField::kSplitMode);
          reader.Read("max_shadow_distance", light, kLight,
            DirectionalLightField::kMaxShadowDistance);
          reader.ReadArray("cascade_distances", light, kLight,
            std::array {
              DirectionalLightField::kCascadeDistance0,
              DirectionalLightField::kCascadeDistance1,
              DirectionalLightField::kCascadeDistance2,
              DirectionalLightField::kCascadeDistance3,
            });
          // Authored distances without a split mode or a shadow distance are
          // the manual distances, ending at the last one.
          if (reader.Contains("cascade_distances")) {
            if (!reader.Contains("split_mode")) {
              light.split_mode = std::to_underlying(
                data::schema::DirectionalCsmSplitMode::kManualDistances);
            }
            if (!reader.Contains("max_shadow_distance")) {
              light.max_shadow_distance = light.cascade_distances[3];
            }
          }
          reader.Read("distribution_exponent", light, kLight,
            DirectionalLightField::kDistributionExponent);
          reader.Read("transition_fraction", light, kLight,
            DirectionalLightField::kTransitionFraction);
          reader.Read("distance_fadeout_fraction", light, kLight,
            DirectionalLightField::kDistanceFadeoutFraction);
          reader.Read("intensity_lux", light, kLight,
            DirectionalLightField::kIntensityLux);
          if (!light_valid) {
            return std::nullopt;
          }
          prepared.build.directional_lights.push_back(light);
        }
      }

      if (lights_doc.contains("point")) {
        const auto& point_doc = lights_doc.at("point");
        prepared.build.point_lights.reserve(point_doc.size());
        for (size_t i = 0; i < point_doc.size(); ++i) {
          const auto& light_doc = point_doc.at(i);
          const auto object_path
            = std::string { "lights.point[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Point light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          using data::schema::LocalLightField;
          auto light = world::MakeDefaultRecord<world::PointLightRecord>();
          light.node_index = node_index;
          auto light_valid = true;
          const auto reader
            = PropertyReader(context, light_doc, object_path, light_valid);
          constexpr auto kLight = ComponentId::kPointLight;
          ReadLightCommon<LocalLightField>(reader, light, kLight);
          reader.Read("range", light, kLight, LocalLightField::kRange);
          reader.Read(
            "source_radius", light, kLight, LocalLightField::kSourceRadius);
          reader.Read("luminous_flux_lm", light, kLight,
            LocalLightField::kLuminousFluxLm);
          if (!light_valid) {
            return std::nullopt;
          }
          prepared.build.point_lights.push_back(light);
        }
      }

      if (lights_doc.contains("spot")) {
        const auto& spot_doc = lights_doc.at("spot");
        prepared.build.spot_lights.reserve(spot_doc.size());
        for (size_t i = 0; i < spot_doc.size(); ++i) {
          const auto& light_doc = spot_doc.at(i);
          const auto object_path
            = std::string { "lights.spot[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Spot light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          using data::schema::LocalLightField;
          auto light = world::MakeDefaultRecord<world::SpotLightRecord>();
          light.node_index = node_index;
          auto light_valid = true;
          const auto reader
            = PropertyReader(context, light_doc, object_path, light_valid);
          constexpr auto kLight = ComponentId::kSpotLight;
          ReadLightCommon<LocalLightField>(reader, light, kLight);
          reader.Read("range", light, kLight, LocalLightField::kRange);
          reader.Read("inner_cone_angle_radians", light, kLight,
            LocalLightField::kInnerConeAngleRadians);
          reader.Read("outer_cone_angle_radians", light, kLight,
            LocalLightField::kOuterConeAngleRadians);
          reader.Read(
            "source_radius", light, kLight, LocalLightField::kSourceRadius);
          reader.Read("luminous_flux_lm", light, kLight,
            LocalLightField::kLuminousFluxLm);
          if (!light_valid) {
            return std::nullopt;
          }
          prepared.build.spot_lights.push_back(light);
        }
      }
    }

    if (descriptor_doc.contains("environment")) {
      const auto& environment = descriptor_doc.at("environment");
      if (environment.contains("post_process_volume")) {
        prepared.post_process = BuildPostProcessSystemRecord(
          context, environment.at("post_process_volume"));
        if (!prepared.post_process.has_value()) {
          return std::nullopt;
        }
      }
      if (environment.contains("background")) {
        prepared.background
          = BuildBackgroundSystemRecord(environment.at("background"));
      }
      if (environment.contains("sky_atmosphere")) {
        prepared.atmosphere
          = BuildSkyAtmosphereSystemRecord(environment.at("sky_atmosphere"));
      }
      if (environment.contains("fog")) {
        prepared.fog = BuildFogSystemRecord(environment.at("fog"));
      }
      if (environment.contains("sky_light")) {
        prepared.sky_light
          = BuildSkyLightSystemRecord(environment.at("sky_light"));
      }
      if (environment.contains("sky_sphere")) {
        prepared.sky_sphere
          = BuildSkySphereSystemRecord(environment.at("sky_sphere"));
      }
    }

    if (descriptor_doc.contains("local_fog_volumes")) {
      const auto& local_fog_doc = descriptor_doc.at("local_fog_volumes");
      prepared.build.local_fog_volumes.reserve(local_fog_doc.size());
      for (size_t i = 0; i < local_fog_doc.size(); ++i) {
        const auto& volume_doc = local_fog_doc.at(i);
        const auto object_path
          = std::string { "local_fog_volumes[" } + std::to_string(i) + "]";
        const auto node_index = volume_doc.at("node").get<uint32_t>();
        if (!ValidateNodeIndex(context, node_index, node_count,
              "scene.descriptor.local_fog_node_index_out_of_range",
              "Local fog volume node index is out of range",
              object_path + ".node")) {
          return std::nullopt;
        }

        using data::schema::LocalFogVolumeField;
        auto record = world::MakeDefaultRecord<world::LocalFogVolumeRecord>();
        record.node_index = node_index;
        auto fog_valid = true;
        const auto reader
          = PropertyReader(context, volume_doc, object_path, fog_valid);
        constexpr auto kFog = ComponentId::kLocalFogVolume;
        reader.Read("enabled", record, kFog, LocalFogVolumeField::kEnabled);
        reader.Read("radial_fog_extinction", record, kFog,
          LocalFogVolumeField::kRadialFogExtinction);
        reader.Read("height_fog_extinction", record, kFog,
          LocalFogVolumeField::kHeightFogExtinction);
        reader.Read("height_fog_falloff", record, kFog,
          LocalFogVolumeField::kHeightFogFalloff);
        reader.Read("height_fog_offset", record, kFog,
          LocalFogVolumeField::kHeightFogOffset);
        reader.Read(
          "fog_phase_g", record, kFog, LocalFogVolumeField::kFogPhaseG);
        reader.ReadArray("fog_albedo", record, kFog,
          std::array {
            LocalFogVolumeField::kFogAlbedoR,
            LocalFogVolumeField::kFogAlbedoG,
            LocalFogVolumeField::kFogAlbedoB,
          });
        reader.ReadArray("fog_emissive", record, kFog,
          std::array {
            LocalFogVolumeField::kFogEmissiveR,
            LocalFogVolumeField::kFogEmissiveG,
            LocalFogVolumeField::kFogEmissiveB,
          });
        reader.Read(
          "sort_priority", record, kFog, LocalFogVolumeField::kSortPriority);
        if (!fog_valid) {
          return std::nullopt;
        }
        prepared.build.local_fog_volumes.push_back(record);
      }
    }

    if (descriptor_doc.contains("references")) {
      const auto& refs_doc = descriptor_doc.at("references");

      const auto add_references
        = [&](const std::string_view key,
            const std::optional<data::AssetType> type) -> void {
        if (!refs_doc.contains(key)) {
          return;
        }
        const auto& values = refs_doc.at(key);
        for (size_t i = 0; i < values.size(); ++i) {
          prepared.references.push_back({
            .virtual_path = values.at(i).get<std::string>(),
            .type = type,
            .object_path
            = "references." + std::string(key) + "[" + std::to_string(i) + "]",
          });
        }
      };
      add_references("materials", data::AssetType::kMaterial);
      add_references("scripts", data::AssetType::kScript);
      add_references("input_actions", data::AssetType::kInputAction);
      add_references(
        "input_mapping_contexts", data::AssetType::kInputMappingContext);
      add_references("physics_sidecars", data::AssetType::kPhysicsScene);
      add_references("extra_assets", std::nullopt);
    }

    return prepared;
  }

} // namespace

auto SceneSource::FromDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<SceneSource>
{
  auto context = SourceContext {
    .source_path = observer_ptr { &source_path },
    .diagnostics = observer_ptr { &diagnostics },
  };
  try {
    const auto document = json::parse(bytes);
    if (!document.is_object()) {
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.request_invalid_json",
        "Scene descriptor must be a JSON object");
      return std::nullopt;
    }
    if (!ValidateDescriptorVersion(context, document)
      || !ValidateDescriptorSchema(context, document)) {
      return std::nullopt;
    }
    return PrepareSceneSource(context, document);
  } catch (const json::exception& error) {
    AddDiagnostic(context, ImportSeverity::kError,
      "scene.descriptor.request_invalid_json", error.what());
    return std::nullopt;
  }
}

} // namespace oxygen::content::import::internal
