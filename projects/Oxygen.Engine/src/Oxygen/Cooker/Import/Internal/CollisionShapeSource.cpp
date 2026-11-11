//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedNumbers.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Transforms/Decompose.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::import::internal {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;
  using JsonPointer = json::json_pointer;
  namespace phys = data::pak::physics;

  auto GetCollisionShapeDescriptorValidator() -> json_validator&
  {
    static auto validator
      = MakeJsonSchemaValidator(json::parse(kCollisionShapeDescriptorSchema));
    return validator;
  }

  //! A shape type's name and the members its parameters take.
  struct TypeTraits final {
    std::string_view name;
    phys::ShapeType type = phys::ShapeType::kInvalid;
    std::span<const std::string_view> members;
    //! Whether the runtime builds the type with any per-axis scale.
    bool takes_non_uniform_scale = false;
    //! Whether the runtime builds the type without its scale.
    bool ignores_scale = false;
  };

  constexpr auto kSphereMembers = std::array<std::string_view, 1> {
    "radius",
  };
  constexpr auto kAxialMembers = std::array<std::string_view, 2> {
    "radius",
    "half_height",
  };
  constexpr auto kBoxMembers = std::array<std::string_view, 1> {
    "half_extents",
  };
  constexpr auto kPlaneMembers = std::array<std::string_view, 2> {
    "normal",
    "distance",
  };
  constexpr auto kWorldBoundaryMembers = std::array<std::string_view, 3> {
    "boundary_mode",
    "limits_min",
    "limits_max",
  };
  constexpr auto kConvexHullMembers = std::array<std::string_view, 3> {
    "geometry_ref",
    "convex_radius",
    "hull_tolerance",
  };
  constexpr auto kTriangleMeshMembers = std::array<std::string_view, 1> {
    "geometry_ref",
  };
  constexpr auto kHeightFieldMembers = std::array<std::string_view, 5> {
    "sample_count",
    "heights",
    "sample_spacing",
    "block_size",
    "bits_per_sample",
  };
  constexpr auto kCompoundMembers = std::array<std::string_view, 1> {
    "children",
  };

  constexpr auto kTypeTraits = std::array {
    TypeTraits {
      .name = "sphere",
      .type = phys::ShapeType::kSphere,
      .members = kSphereMembers,
    },
    TypeTraits {
      .name = "capsule",
      .type = phys::ShapeType::kCapsule,
      .members = kAxialMembers,
    },
    TypeTraits {
      .name = "box",
      .type = phys::ShapeType::kBox,
      .members = kBoxMembers,
      .takes_non_uniform_scale = true,
    },
    TypeTraits {
      .name = "cylinder",
      .type = phys::ShapeType::kCylinder,
      .members = kAxialMembers,
    },
    TypeTraits {
      .name = "cone",
      .type = phys::ShapeType::kCone,
      .members = kAxialMembers,
    },
    TypeTraits {
      .name = "convex_hull",
      .type = phys::ShapeType::kConvexHull,
      .members = kConvexHullMembers,
    },
    TypeTraits {
      .name = "triangle_mesh",
      .type = phys::ShapeType::kTriangleMesh,
      .members = kTriangleMeshMembers,
      .takes_non_uniform_scale = true,
    },
    TypeTraits {
      .name = "height_field",
      .type = phys::ShapeType::kHeightField,
      .members = kHeightFieldMembers,
      .takes_non_uniform_scale = true,
    },
    TypeTraits {
      .name = "plane",
      .type = phys::ShapeType::kPlane,
      .members = kPlaneMembers,
      .ignores_scale = true,
    },
    TypeTraits {
      .name = "world_boundary",
      .type = phys::ShapeType::kWorldBoundary,
      .members = kWorldBoundaryMembers,
      .ignores_scale = true,
    },
    TypeTraits {
      .name = "compound",
      .type = phys::ShapeType::kCompound,
      .members = kCompoundMembers,
      .takes_non_uniform_scale = true,
    },
  };

  //! The members every part takes, whatever its type.
  constexpr auto kPartMembers = std::array<std::string_view, 4> {
    "shape_type",
    "local_position",
    "local_rotation",
    "local_scale",
  };

  //! The members only the descriptor takes, not a compound child.
  constexpr auto kDescriptorMembers = std::array<std::string_view, 7> {
    "$schema",
    "name",
    "material_ref",
    "is_sensor",
    "collision_own_layer",
    "collision_target_layers",
    "children",
  };

  //! The cooked world boundary mode `name` names; `kInvalid` for none.
  [[nodiscard]] auto ToBoundaryMode(const std::string_view name)
    -> phys::WorldBoundaryMode
  {
    if (name == "aabb_clamp") {
      return phys::WorldBoundaryMode::kAabbClamp;
    }
    if (name == "plane_set") {
      return phys::WorldBoundaryMode::kPlaneSet;
    }
    return phys::WorldBoundaryMode::kInvalid;
  }

  [[nodiscard]] auto FindTraits(const std::string_view name)
    -> const TypeTraits*
  {
    const auto found = std::ranges::find(kTypeTraits, name, &TypeTraits::name);
    return found == kTypeTraits.end() ? nullptr : &*found;
  }

  //! Thrown when a schema-valid document does not have what the schema
  //! promised; reported as a contract mismatch.
  class ContractMismatch final : public std::exception {
  public:
    explicit ContractMismatch(std::string what)
      : what_(std::move(what))
    {
    }
    [[nodiscard]] auto what() const noexcept -> const char* override
    {
      return what_.c_str();
    }

  private:
    std::string what_;
  };

  //! Reads a schema-valid descriptor into its typed form, reporting every
  //! value the cook or the runtime would refuse.
  class ShapeReader final {
  public:
    ShapeReader(const std::filesystem::path& source_path,
      std::vector<ImportDiagnostic>& diagnostics)
      : source_path_(observer_ptr { &source_path })
      , diagnostics_(observer_ptr { &diagnostics })
    {
    }

    [[nodiscard]] auto Failed() const noexcept -> bool { return failed_; }

    auto Error(const std::string_view code, std::string message,
      const JsonPointer& pointer) -> void
    {
      failed_ = true;
      diagnostics_->push_back(ImportDiagnostic {
        .severity = ImportSeverity::kError,
        .code = std::string(code),
        .message = std::move(message),
        .source_path = source_path_->string(),
        .json_pointer = pointer.to_string(),
      });
    }

    //! Refuses each member of `doc` that is neither in `common` nor in the
    //! members of `traits`.
    auto CheckMembers(const json& doc, const JsonPointer& path,
      const TypeTraits& traits, const std::span<const std::string_view> common)
      -> void
    {
      for (const auto& [member, value] : doc.items()) {
        static_cast<void>(value);
        const auto in
          = [&](const std::span<const std::string_view> names) -> bool {
          return std::ranges::find(names, member) != names.end();
        };
        if (in(kPartMembers) || in(common) || in(traits.members)) {
          continue;
        }
        Error(import_codes::kPhysicsShapeMemberNotApplicable,
          "'" + member + "' does not apply to a " + std::string(traits.name)
            + " shape",
          path / member);
      }
    }

    //! A number narrowed to float; nothing, reported, when a float cannot
    //! hold it.
    [[nodiscard]] auto TryFloat(const json& value, const JsonPointer& pointer)
      -> std::optional<float>
    {
      if (!value.is_number()) {
        throw ContractMismatch(pointer.to_string() + " is not a number");
      }
      const auto cooked = ToCookedFloat(value.get<double>());
      if (!cooked.has_value()) {
        Error(import_codes::kPhysicsShapeValueOutOfRange,
          pointer.to_string() + " = " + value.dump()
            + " does not fit a 32-bit float",
          pointer);
      }
      return cooked;
    }

    //! A number narrowed to float; zero, reported, when a float cannot hold
    //! it.
    [[nodiscard]] auto Float(const json& value, const JsonPointer& pointer)
      -> float
    {
      return TryFloat(value, pointer).value_or(0.0F);
    }

    //! A float that must be positive once narrowed: a tiny authored value
    //! that becomes zero is refused.
    [[nodiscard]] auto Positive(const json& value, const JsonPointer& pointer)
      -> float
    {
      const auto cooked = TryFloat(value, pointer);
      if (cooked.has_value() && !(*cooked > 0.0F)) {
        Error(import_codes::kPhysicsShapeValueOutOfRange,
          pointer.to_string() + " = " + value.dump()
            + " is not positive as a 32-bit float",
          pointer);
      }
      return cooked.value_or(0.0F);
    }

    //! A float that must not be negative.
    [[nodiscard]] auto NonNegative(
      const json& value, const JsonPointer& pointer) -> float
    {
      const auto cooked = TryFloat(value, pointer);
      if (cooked.has_value() && *cooked < 0.0F) {
        Error(import_codes::kPhysicsShapeValueOutOfRange,
          pointer.to_string() + " = " + value.dump() + " is negative", pointer);
      }
      return cooked.value_or(0.0F);
    }

    template <size_t N, typename ReadOne>
    [[nodiscard]] auto Array(const json& value, const JsonPointer& pointer,
      const ReadOne& read_one) -> std::array<float, N>
    {
      if (!value.is_array() || value.size() != N) {
        throw ContractMismatch(pointer.to_string() + " is not an array of "
          + std::to_string(N) + " numbers");
      }
      auto out = std::array<float, N> {};
      for (size_t i = 0; i < N; ++i) {
        out.at(i) = read_one(value.at(i), pointer / i);
      }
      return out;
    }

    template <size_t N>
    [[nodiscard]] auto Floats(const json& value, const JsonPointer& pointer)
      -> std::array<float, N>
    {
      return Array<N>(
        value, pointer, [this](const json& v, const JsonPointer& p) -> float {
          return Float(v, p);
        });
    }

    template <size_t N>
    [[nodiscard]] auto PositiveFloats(
      const json& value, const JsonPointer& pointer) -> std::array<float, N>
    {
      return Array<N>(
        value, pointer, [this](const json& v, const JsonPointer& p) -> float {
          return Positive(v, p);
        });
    }

    [[nodiscard]] static auto Member(
      const json& doc, const std::string_view name) -> const json&
    {
      const auto found = doc.find(name);
      if (found == doc.end()) {
        throw ContractMismatch(
          "required member '" + std::string(name) + "' is missing");
      }
      return *found;
    }

    [[nodiscard]] static auto Unsigned(
      const json& value, const JsonPointer& pointer) -> uint64_t
    {
      if (!value.is_number_unsigned()) {
        throw ContractMismatch(
          pointer.to_string() + " is not an unsigned integer");
      }
      return value.get<uint64_t>();
    }

    //! A layer value: an integer of 64 bits; a negative one is reported.
    [[nodiscard]] auto Layers(const json& value, const JsonPointer& pointer)
      -> uint64_t
    {
      if (value.is_number_unsigned()) {
        return value.get<uint64_t>();
      }
      if (!value.is_number_integer()) {
        throw ContractMismatch(pointer.to_string() + " is not an integer");
      }
      Error(import_codes::kPhysicsShapeValueOutOfRange,
        pointer.to_string() + " = " + value.dump() + " is negative", pointer);
      return 0U;
    }

    //! Reads the transform and parameters of one part.
    auto ReadPart(const json& doc, const JsonPointer& path,
      const TypeTraits& traits, ShapePart& out) -> void
    {
      out.type = traits.type;
      out.json_pointer = path.to_string();
      if (doc.contains("local_position")) {
        out.local_position
          = Floats<3>(doc.at("local_position"), path / "local_position");
      }
      if (doc.contains("local_rotation")) {
        const auto pointer = path / "local_rotation";
        out.local_rotation = Floats<4>(doc.at("local_rotation"), pointer);
        if (!IsUnitQuaternion(out.local_rotation)) {
          Error(import_codes::kPhysicsShapeValueOutOfRange,
            "local_rotation has squared length "
              + std::to_string(LengthSquared(out.local_rotation))
              + "; it must be a unit quaternion",
            pointer);
        }
      }
      if (doc.contains("local_scale")) {
        out.local_scale
          = PositiveFloats<3>(doc.at("local_scale"), path / "local_scale");
      }
      const auto& scale = out.local_scale;
      const auto uniform = transforms::IsUniformScale(
        glm::vec3 { scale[0], scale[1], scale[2] });
      if (!traits.ignores_scale && !traits.takes_non_uniform_scale
        && !uniform) {
        Error(import_codes::kPhysicsShapeScaleNotUniform,
          "a " + std::string(traits.name)
            + " shape takes a uniform local_scale; it is ["
            + std::to_string(scale[0]) + ", " + std::to_string(scale[1]) + ", "
            + std::to_string(scale[2]) + "]",
          path / "local_scale");
      }
      out.params = ReadParams(doc, path, traits);
    }

    [[nodiscard]] auto ReadParams(const json& doc, const JsonPointer& path,
      const TypeTraits& traits) -> ShapeParams
    {
      using enum phys::ShapeType;
      switch (traits.type) {
      case kSphere:
        return SphereParams {
          .radius = Positive(Member(doc, "radius"), path / "radius"),
        };
      case kCapsule:
        return AxialParams {
          .radius = Positive(Member(doc, "radius"), path / "radius"),
          .half_height
          = NonNegative(Member(doc, "half_height"), path / "half_height"),
        };
      case kCylinder:
      case kCone:
        return AxialParams {
          .radius = Positive(Member(doc, "radius"), path / "radius"),
          .half_height
          = Positive(Member(doc, "half_height"), path / "half_height"),
        };
      case kBox:
        return BoxParams {
          .half_extents = PositiveFloats<3>(
            Member(doc, "half_extents"), path / "half_extents"),
        };
      case kPlane:
        return ReadPlane(doc, path);
      case kWorldBoundary:
        return ReadWorldBoundary(doc, path);
      case kConvexHull:
        return ReadConvexHull(doc, path);
      case kTriangleMesh:
        return TriangleMeshParams {
          .geometry_ref = Member(doc, "geometry_ref").get<std::string>(),
        };
      case kHeightField:
        return ReadHeightField(doc, path);
      case kCompound:
        return CompoundParams {};
      case kInvalid:
        break;
      }
      throw ContractMismatch("unsupported shape type");
    }

    [[nodiscard]] auto ReadPlane(const json& doc, const JsonPointer& path)
      -> PlaneParams
    {
      auto out = PlaneParams {
        .normal = Floats<3>(Member(doc, "normal"), path / "normal"),
        .distance = Float(Member(doc, "distance"), path / "distance"),
      };
      const auto& n = out.normal;
      const auto length_squared = (n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]);
      if (!(length_squared > 0.0F) || !std::isfinite(length_squared)) {
        Error(import_codes::kPhysicsShapeValueOutOfRange,
          "normal must have a nonzero finite length", path / "normal");
      }
      return out;
    }

    [[nodiscard]] auto ReadWorldBoundary(
      const json& doc, const JsonPointer& path) -> WorldBoundaryParams
    {
      const auto mode = Member(doc, "boundary_mode").get<std::string>();
      auto out = WorldBoundaryParams {
        .mode = ToBoundaryMode(mode),
        .limits_min = Floats<3>(Member(doc, "limits_min"), path / "limits_min"),
        .limits_max = Floats<3>(Member(doc, "limits_max"), path / "limits_max"),
      };
      if (out.mode == phys::WorldBoundaryMode::kInvalid) {
        throw ContractMismatch("unsupported boundary_mode: " + mode);
      }
      for (size_t axis = 0; axis < out.limits_min.size(); ++axis) {
        if (!(out.limits_max.at(axis) > out.limits_min.at(axis))) {
          Error(import_codes::kPhysicsShapeWorldBoundaryLimitsInvalid,
            "limits_max[" + std::to_string(axis)
              + "] = " + std::to_string(out.limits_max.at(axis))
              + " must be greater than limits_min[" + std::to_string(axis)
              + "] = " + std::to_string(out.limits_min.at(axis)),
            path / "limits_max" / axis);
        }
      }
      return out;
    }

    [[nodiscard]] auto ReadConvexHull(const json& doc, const JsonPointer& path)
      -> ConvexHullParams
    {
      auto out = ConvexHullParams {
        .geometry_ref = Member(doc, "geometry_ref").get<std::string>(),
      };
      if (doc.contains("convex_radius")) {
        out.convex_radius
          = NonNegative(doc.at("convex_radius"), path / "convex_radius");
      }
      if (doc.contains("hull_tolerance")) {
        out.hull_tolerance
          = Positive(doc.at("hull_tolerance"), path / "hull_tolerance");
      }
      return out;
    }

    [[nodiscard]] auto ReadHeightField(const json& doc, const JsonPointer& path)
      -> HeightFieldParams
    {
      auto out = HeightFieldParams {};
      const auto count
        = Unsigned(Member(doc, "sample_count"), path / "sample_count");
      // The schema bounds the count, so its square holds in 32 bits.
      out.sample_count = static_cast<uint32_t>(count);
      const auto& heights = Member(doc, "heights");
      const auto expected = static_cast<size_t>(count) * count;
      if (heights.size() != expected) {
        Error(import_codes::kPhysicsShapeHeightFieldInvalid,
          "heights has " + std::to_string(heights.size())
            + " samples; sample_count " + std::to_string(count) + " needs "
            + std::to_string(expected),
          path / "heights");
        return out;
      }
      out.heights.reserve(expected);
      auto solid = size_t { 0 };
      for (size_t i = 0; i < heights.size(); ++i) {
        const auto& sample = heights.at(i);
        if (sample.is_null()) {
          out.heights.emplace_back(std::nullopt);
          continue;
        }
        out.heights.emplace_back(Float(sample, path / "heights" / i));
        ++solid;
      }
      if (solid == 0U) {
        Error(import_codes::kPhysicsShapeHeightFieldInvalid,
          "every height is a hole; a height field needs a solid sample",
          path / "heights");
      }
      if (doc.contains("sample_spacing")) {
        out.spacing = PositiveFloats<2>(
          doc.at("sample_spacing"), path / "sample_spacing");
      }
      if (doc.contains("block_size")) {
        out.block_size = static_cast<uint32_t>(
          Unsigned(doc.at("block_size"), path / "block_size"));
      }
      if (doc.contains("bits_per_sample")) {
        out.bits_per_sample = static_cast<uint32_t>(
          Unsigned(doc.at("bits_per_sample"), path / "bits_per_sample"));
      }
      return out;
    }

    //! Reads the descriptor members that are not part of its shape.
    auto ReadDescriptor(const json& doc, CollisionShapeSource& out) -> void
    {
      out.name = doc.value("name", std::string {});
      out.material_ref = Member(doc, "material_ref").get<std::string>();
      out.is_sensor = doc.value("is_sensor", false);
      if (doc.contains("collision_own_layer")) {
        const auto pointer = JsonPointer {} / "collision_own_layer";
        out.collision_own_layer
          = Layers(doc.at("collision_own_layer"), pointer);
        const auto read = doc.at("collision_own_layer").is_number_unsigned();
        if (read && !std::has_single_bit(out.collision_own_layer)) {
          Error(import_codes::kPhysicsShapeOwnLayerInvalid,
            "collision_own_layer = " + std::to_string(out.collision_own_layer)
              + " is not a single layer bit",
            pointer);
        }
      }
      if (doc.contains("collision_target_layers")) {
        out.collision_target_layers = Layers(doc.at("collision_target_layers"),
          JsonPointer {} / "collision_target_layers");
      }
    }

  private:
    observer_ptr<const std::filesystem::path> source_path_;
    observer_ptr<std::vector<ImportDiagnostic>> diagnostics_;
    bool failed_ = false;
  };

  [[nodiscard]] auto TraitsOf(const json& doc) -> const TypeTraits&
  {
    const auto& type = ShapeReader::Member(doc, "shape_type");
    const auto* const traits = FindTraits(type.get<std::string>());
    if (traits == nullptr) {
      throw ContractMismatch("unsupported shape_type: " + type.dump());
    }
    return *traits;
  }

  auto ReadShape(const json& doc, ShapeReader& reader) -> CollisionShapeSource
  {
    auto out = CollisionShapeSource {};
    const auto& traits = TraitsOf(doc);
    const auto root = JsonPointer {};
    reader.CheckMembers(doc, root, traits, kDescriptorMembers);
    reader.ReadDescriptor(doc, out);
    reader.ReadPart(doc, root, traits, out.shape);
    if (traits.type != phys::ShapeType::kCompound) {
      return out;
    }
    const auto& children = ShapeReader::Member(doc, "children");
    out.children.resize(children.size());
    for (size_t i = 0; i < children.size(); ++i) {
      const auto path = JsonPointer {} / "children" / i;
      const auto& child_doc = children.at(i);
      const auto& child_traits = TraitsOf(child_doc);
      if (child_traits.type == phys::ShapeType::kCompound) {
        throw ContractMismatch("a compound child cannot be a compound");
      }
      reader.CheckMembers(child_doc, path, child_traits, {});
      reader.ReadPart(child_doc, path, child_traits, out.children.at(i));
    }
    return out;
  }

  auto AddError(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, const std::string_view code,
    std::string message) -> void
  {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::string(code),
      .message = std::move(message),
      .source_path = source_path.string(),
    });
  }

  auto ValidateDescriptorSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, const json& descriptor_doc)
    -> bool
  {
    const auto config = JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code
      = import_codes::kPhysicsShapeSchemaValidationFailed,
      .validation_failed_prefix
      = "Collision shape descriptor validation failed: ",
      .validator_failure_code
      = import_codes::kPhysicsShapeSchemaValidatorFailure,
      .validator_failure_prefix
      = "Collision shape descriptor schema validator failed: ",
    };
    return ValidateJsonSchemaWithDiagnostics(
      GetCollisionShapeDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& json_pointer) -> void {
        diagnostics.push_back(ImportDiagnostic {
          .severity = ImportSeverity::kError,
          .code = std::string(code),
          .message = message,
          .source_path = source_path.string(),
          .json_pointer = json_pointer,
        });
      });
  }

} // namespace

auto GeometryReferencesOf(const CollisionShapeSource& source)
  -> std::vector<ShapeGeometryReference>
{
  auto out = std::vector<ShapeGeometryReference> {};
  const auto add = [&](const ShapePart& part) -> void {
    const auto* const reference = std::visit(
      [](const auto& params) -> const std::string* {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, ConvexHullParams>
          || std::is_same_v<T, TriangleMeshParams>) {
          return &params.geometry_ref;
        } else {
          return nullptr;
        }
      },
      part.params);
    if (reference != nullptr) {
      out.push_back({
        .reference = *reference,
        .json_pointer = part.json_pointer + "/geometry_ref",
      });
    }
  };
  add(source.shape);
  std::ranges::for_each(source.children, add);
  return out;
}

auto PrepareCollisionShapeDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<CollisionShapeSource>
{
  auto document = json {};
  try {
    document = json::parse(bytes);
  } catch (const json::exception& failure) {
    AddError(diagnostics, source_path,
      import_codes::kPhysicsShapeRequestInvalidJson,
      std::string("Descriptor payload is invalid JSON: ") + failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddError(diagnostics, source_path,
      import_codes::kPhysicsShapeRequestInvalidJson,
      "Descriptor payload must be a JSON object");
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(diagnostics, source_path, document)) {
    return std::nullopt;
  }

  auto reader = ShapeReader { source_path, diagnostics };
  try {
    auto source = ReadShape(document, reader);
    if (reader.Failed()) {
      return std::nullopt;
    }
    return source;
  } catch (const std::exception& ex) {
    AddError(diagnostics, source_path,
      import_codes::kPhysicsShapeSchemaContractMismatch,
      std::string("Schema validated descriptor failed extraction: ")
        + ex.what());
    return std::nullopt;
  }
}

} // namespace oxygen::content::import::internal
