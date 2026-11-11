//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp,
//   Import/Internal/PhysicsSidecarSource.cpp

#include <Jolt/Jolt.h> // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <ios>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Jolt/Core/Memory.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/ConstraintPart/SwingTwistConstraintPart.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::FindPhysicsResource;
  using oxygen::cooker::test::HasCookedFile;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::PhysicsSidecar;
  namespace phys = data::pak::physics;

  constexpr auto kScenePath
    = std::string_view { "/Cooked/Scenes/Joints.oscene" };
  constexpr auto kSidecarPath
    = std::string_view { "/Cooked/Scenes/Joints.opscene" };
  constexpr auto kMaterialPath
    = std::string_view { "/Cooked/Physics/Materials/Ground.opmat" };
  constexpr auto kShapePath
    = std::string_view { "/Cooked/Physics/Shapes/Box.ocshape" };
  constexpr auto kSidecarIdentity
    = std::string_view { "/Content/Scenes/Joints.physics-sidecar.json" };

  //! A 90 degree turn about +Z, [x, y, z, w]: frame +X becomes +Y.
  constexpr auto kQuarterTurnZ
    = std::array { 0.0F, 0.0F, 0.70710678F, 0.70710678F };

  //! The bytes Jolt writes for `settings`: what the cook must emit for a
  //! joint authored to mean exactly these settings.
  auto JoltBinaryState(const JPH::ConstraintSettings& settings)
    -> std::vector<std::byte>
  {
    auto stream = std::ostringstream(std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    const auto text = stream.str();
    const auto bytes = std::as_bytes(std::span { text });
    return { bytes.begin(), bytes.end() };
  }

  auto Rotation(const std::array<float, 4>& q) -> JPH::Quat
  {
    return JPH::Quat(q[0], q[1], q[2], q[3]).Normalized();
  }

  class PhysicsSidecarBindingsTest : public ImportServiceTest {
  protected:
    static void SetUpTestSuite()
    {
      static std::once_flag once {};
      std::call_once(once, [] -> void { JPH::RegisterDefaultAllocator(); });
    }

    //! Cooks a scene of three nodes, a physics material and a box shape.
    void SetUp() override
    {
      auto scene = oxygen::cooker::test::SceneDescriptor {
        std::string { kScenePath },
      };
      for (const auto* const id : {
             "00000000-0000-4000-8000-000000000002",
             "00000000-0000-4000-8000-000000000003",
           }) {
        scene.Append(
          "/nodes", json { { "id", id }, { "name", id }, { "parent", 0U } });
      }
      auto scene_request = ImportRequest {};
      scene_request.source_path = "inline://Joints.scene.json";
      scene_request.cooked_root = CookedRoot();
      scene_request.asset_virtual_path = std::string { kScenePath };
      scene_request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(scene_request.source_path);
      scene_request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .descriptor_json = scene.Text(),
      };
      ExpectCooks(std::move(scene_request));

      auto material_request = ImportRequest {};
      material_request.source_path = "inline://Ground.opmat.json";
      material_request.cooked_root = CookedRoot();
      material_request.asset_virtual_path = std::string { kMaterialPath };
      material_request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(material_request.source_path);
      material_request.physics_material_descriptor
        = ImportRequest::PhysicsMaterialDescriptorPayload {
            .descriptor_json = oxygen::cooker::test::PhysicsMaterialDescriptor {
              std::string { kMaterialPath },
            }
                                 .Text(),
          };
      ExpectCooks(std::move(material_request));

      auto shape_request = ImportRequest {};
      shape_request.source_path = "inline://Box.ocshape.json";
      shape_request.cooked_root = CookedRoot();
      shape_request.asset_virtual_path = std::string { kShapePath };
      shape_request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(shape_request.source_path);
      shape_request.collision_shape_descriptor
        = ImportRequest::CollisionShapeDescriptorPayload {
            .descriptor_json = oxygen::cooker::test::CollisionShapeDescriptor {
              std::string { kShapePath },
              kMaterialPath,
            }
                                 .Text(),
          };
      ExpectCooks(std::move(shape_request));
    }

    void ExpectCooks(ImportRequest request)
    {
      const auto report = Import(std::move(request));
      ASSERT_HAS_VALUE(report);
      ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    }

    [[nodiscard]] static auto Sidecar() -> PhysicsSidecar
    {
      return PhysicsSidecar { std::string { kSidecarIdentity } };
    }

    [[nodiscard]] auto CookSidecar(const PhysicsSidecar& sidecar,
      const core::meta::physics::PhysicsBackend backend
      = core::meta::physics::PhysicsBackend::kJolt) -> ImportReport
    {
      auto request = ImportRequest {};
      request.source_path = "inline://Joints.physics-sidecar.json";
      request.cooked_root = CookedRoot();
      request.options.physics.backend = backend;
      request.physics = PhysicsImportSettings {
        .target_scene_virtual_path = std::string { kScenePath },
        .inline_bindings_json = sidecar.Text(),
      };
      auto report = Import(std::move(request));
      if (!report.has_value()) {
        ADD_FAILURE() << "The sidecar import produced no report";
        return {};
      }
      return std::move(*report);
    }

    //! The cooked constraint resource of the sidecar's only joint.
    [[nodiscard]] auto JointResource() const
      -> std::optional<oxygen::cooker::test::CookedPhysicsResource>
    {
      const auto content = LoadInspection(CookedRoot());
      const auto key = data::AssetKey::FromVirtualPath(kSidecarPath);
      const auto bytes = content.Descriptor(key);
      const auto sidecar = data::PhysicsSceneAsset(key, bytes);
      const auto joints = sidecar.GetBindings<phys::JointBindingRecord>();
      if (joints.size() != 1U) {
        return std::nullopt;
      }
      return FindPhysicsResource(content, joints.front().constraint_asset_key);
    }

    //! Cooks `joint` and checks its constraint is exactly `expected`.
    void ExpectJointCooksTo(json joint, const JPH::ConstraintSettings& expected)
    {
      const auto report
        = CookSidecar(Sidecar().AppendBinding("joints", std::move(joint)));
      ASSERT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
      const auto resource = JointResource();
      ASSERT_HAS_VALUE(resource);
      EXPECT_EQ(
        resource->format, phys::PhysicsResourceFormat::kJoltConstraintBinary);
      EXPECT_EQ(resource->payload, JoltBinaryState(expected));
    }

    //! The sidecar is refused with `code` at `pointer` and publishes nothing.
    void ExpectRefused(const ImportReport& report, const std::string_view code,
      const std::string_view pointer) const
    {
      EXPECT_FALSE(report.success);
      EXPECT_TRUE(std::ranges::any_of(report.diagnostics,
        [&](const ImportDiagnostic& diagnostic) -> bool {
          return diagnostic.code == code && diagnostic.json_pointer == pointer;
        }))
        << DiagnosticSummary(report.diagnostics);
      EXPECT_FALSE(HasCookedFile(CookedRoot(), "Scenes/Joints.opscene"));
    }
  };

  //! physics.md, joints: frame +X is the hinge axis and +Y the normal; the
  //! hinge reads limits, spring and motor on rx; priority and the Jolt solver
  //! overrides apply to every constraint.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, HingeCooksEveryAuthoredSetting)
  {
    auto expected = JPH::HingeConstraintSettings {};
    expected.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
    expected.mPoint1 = JPH::RVec3(0.1F, 0.2F, 0.3F);
    expected.mHingeAxis1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisX();
    expected.mNormalAxis1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisY();
    expected.mPoint2 = JPH::RVec3(-0.4F, 0.5F, -0.6F);
    expected.mHingeAxis2 = JPH::Vec3::sAxisX();
    expected.mNormalAxis2 = JPH::Vec3::sAxisY();
    expected.mLimitsMin = -0.5F;
    expected.mLimitsMax = 0.8F;
    expected.mLimitsSpringSettings = JPH::SpringSettings(
      JPH::ESpringMode::StiffnessAndDamping, 120.0F, 4.0F);
    expected.mMotorSettings.mSpringSettings
      = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, 2.0F, 0.7F);
    expected.mMotorSettings.SetTorqueLimit(55.0F);
    expected.mConstraintPriority = 9U;
    expected.mNumVelocityStepsOverride = 3U;
    expected.mNumPositionStepsOverride = 4U;

    ExpectJointCooksTo(
      json {
        { "node_index_a", 1U },
        { "node_index_b", 2U },
        { "constraint_type", "hinge" },
        { "constraint_space", "local" },
        { "local_frame_a_position", { 0.1F, 0.2F, 0.3F } },
        { "local_frame_a_rotation", kQuarterTurnZ },
        { "local_frame_b_position", { -0.4F, 0.5F, -0.6F } },
        { "limits_lower", { 0.0F, 0.0F, 0.0F, -0.5F, 0.0F, 0.0F } },
        { "limits_upper", { 0.0F, 0.0F, 0.0F, 0.8F, 0.0F, 0.0F } },
        { "spring_stiffnesses", { 0.0F, 0.0F, 0.0F, 120.0F, 0.0F, 0.0F } },
        { "spring_dampings", { 0.0F, 0.0F, 0.0F, 4.0F, 0.0F, 0.0F } },
        { "motor_drive_frequencies", { 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F } },
        { "motor_damping_ratios", { 0.0F, 0.0F, 0.0F, 0.7F, 0.0F, 0.0F } },
        { "motor_max_torques", { 0.0F, 0.0F, 0.0F, 55.0F, 0.0F, 0.0F } },
        { "priority", 9U },
        {
          "backend",
          {
            { "target", "jolt" },
            { "num_velocity_steps_override", 3U },
            { "num_position_steps_override", 4U },
          },
        },
      },
      expected);
  }

  //! The slider reads limits, spring and motor on tx, the slide axis.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, SliderCooksEveryAuthoredSetting)
  {
    auto expected = JPH::SliderConstraintSettings {};
    expected.mAutoDetectPoint = false;
    expected.mPoint1 = JPH::RVec3(1.0F, 0.0F, 0.0F);
    expected.mSliderAxis1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisX();
    expected.mNormalAxis1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisY();
    expected.mSliderAxis2 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisX();
    expected.mNormalAxis2 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisY();
    expected.mLimitsMin = -0.25F;
    expected.mLimitsMax = 1.5F;
    expected.mLimitsSpringSettings = JPH::SpringSettings(
      JPH::ESpringMode::StiffnessAndDamping, 300.0F, 8.0F);
    expected.mMotorSettings.SetForceLimit(75.0F);

    ExpectJointCooksTo(
      json {
        { "node_index_a", 1U },
        { "node_index_b", 2U },
        { "constraint_type", "slider" },
        { "local_frame_a_position", { 1.0F, 0.0F, 0.0F } },
        { "local_frame_a_rotation", kQuarterTurnZ },
        { "local_frame_b_rotation", kQuarterTurnZ },
        { "limits_lower", { -0.25F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "limits_upper", { 1.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "spring_stiffnesses", { 300.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "spring_dampings", { 8.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "motor_max_forces", { 75.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
      },
      expected);
  }

  //! The six-DOF joint reads each axis's mode, its limits, the translation
  //! springs and every motor; an asymmetric swing is a pyramid.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, SixDofCooksEveryAuthoredSetting)
  {
    using EAxis = JPH::SixDOFConstraintSettings::EAxis;
    auto expected = JPH::SixDOFConstraintSettings {};
    expected.mAxisX1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisX();
    expected.mAxisY1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisY();
    expected.MakeFixedAxis(EAxis::TranslationX);
    expected.SetLimitedAxis(EAxis::TranslationY, -0.1F, 0.3F);
    expected.MakeFreeAxis(EAxis::TranslationZ);
    expected.SetLimitedAxis(EAxis::RotationX, -1.0F, 1.0F);
    expected.SetLimitedAxis(EAxis::RotationY, -0.2F, 0.4F);
    expected.MakeFreeAxis(EAxis::RotationZ);
    expected.mSwingType = JPH::ESwingType::Pyramid;
    expected.mLimitsSpringSettings[EAxis::TranslationY]
      = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, 50.0F, 2.0F);
    expected.mMotorSettings[EAxis::TranslationZ].SetForceLimit(40.0F);
    expected.mMotorSettings[EAxis::RotationZ].SetTorqueLimit(30.0F);

    ExpectJointCooksTo(
      json {
        { "node_index_a", 1U },
        { "node_index_b", 2U },
        { "constraint_type", "six_dof" },
        { "local_frame_a_rotation", kQuarterTurnZ },
        {
          "axis_modes",
          { "locked", "limited", "free", "limited", "limited", "free" },
        },
        { "limits_lower", { 0.0F, -0.1F, 0.0F, -1.0F, -0.2F, 0.0F } },
        { "limits_upper", { 0.0F, 0.3F, 0.0F, 1.0F, 0.4F, 0.0F } },
        { "spring_stiffnesses", { 0.0F, 50.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "spring_dampings", { 0.0F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "motor_max_forces", { 0.0F, 0.0F, 40.0F, 0.0F, 0.0F, 0.0F } },
        { "motor_max_torques", { 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 30.0F } },
      },
      expected);
  }

  //! The cone is symmetric: its half angle is the swing limit about Y and Z,
  //! about the twist axis +X.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, ConeCooksItsHalfAngle)
  {
    auto expected = JPH::ConeConstraintSettings {};
    expected.mTwistAxis1 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisX();
    expected.mHalfConeAngle = 0.6F;

    ExpectJointCooksTo(
      json {
        { "node_index_a", 1U },
        { "node_index_b", 2U },
        { "constraint_type", "cone" },
        { "local_frame_a_rotation", kQuarterTurnZ },
        { "limits_lower", { 0.0F, 0.0F, 0.0F, 0.0F, -0.6F, -0.6F } },
        { "limits_upper", { 0.0F, 0.0F, 0.0F, 0.0F, 0.6F, 0.6F } },
      },
      expected);
  }

  //! The distance joint reads its range and spring on tx.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, DistanceCooksItsRangeAndSpring)
  {
    auto expected = JPH::DistanceConstraintSettings {};
    expected.mPoint2 = JPH::RVec3(0.0F, 0.0F, 2.0F);
    expected.mMinDistance = 0.5F;
    expected.mMaxDistance = 2.5F;
    expected.mLimitsSpringSettings
      = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, 10.0F, 1.0F);

    ExpectJointCooksTo(
      json {
        { "node_index_a", 1U },
        { "node_index_b", 2U },
        { "constraint_type", "distance" },
        { "local_frame_b_position", { 0.0F, 0.0F, 2.0F } },
        { "limits_lower", { 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "limits_upper", { 2.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "spring_stiffnesses", { 10.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
        { "spring_dampings", { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F } },
      },
      expected);
  }

  //! A fixed joint without a type keeps both frames' orientation.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, FixedCooksItsFrames)
  {
    auto expected = JPH::FixedConstraintSettings {};
    expected.mAutoDetectPoint = false;
    expected.mAxisX2 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisX();
    expected.mAxisY2 = Rotation(kQuarterTurnZ) * JPH::Vec3::sAxisY();

    ExpectJointCooksTo(
      json {
        { "node_index_a", 1U },
        { "node_index_b", 2U },
        { "local_frame_b_rotation", kQuarterTurnZ },
      },
      expected);
  }

  //! A binding's backend scalars hold the arm its backend.target names, so a
  //! target other than the import's is refused before any of them is read.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, RefusesAnotherBackendsSettings)
  {
    const auto physx = json { { "target", "physx" } };
    const auto rigid_body = json {
      { "node_index", 1U },
      { "shape_ref", "/Cooked/Physics/Shapes/Box.ocshape" },
      { "material_ref", "/Cooked/Physics/Materials/Ground.opmat" },
      { "backend", physx },
    };
    const auto character = json {
      { "node_index", 2U },
      { "shape_ref", "/Cooked/Physics/Shapes/Box.ocshape" },
      { "backend", physx },
    };
    const auto joint = json {
      { "node_index_a", 1U },
      { "node_index_b", 2U },
      { "backend", physx },
    };

    const auto report = CookSidecar(Sidecar()
        .AppendBinding("rigid_bodies", rigid_body)
        .AppendBinding("characters", character)
        .AppendBinding("joints", joint));

    ExpectRefused(report, import_codes::kPhysicsSidecarBackendMismatch,
      "/bindings/rigid_bodies/0/backend/target");
    ExpectRefused(report, import_codes::kPhysicsSidecarBackendMismatch,
      "/bindings/characters/0/backend/target");
    ExpectRefused(report, import_codes::kPhysicsSidecarBackendMismatch,
      "/bindings/joints/0/backend/target");
  }

  //! Only Jolt has a runtime backend, so a PhysX cook is refused.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, RefusesAPhysxCook)
  {
    const auto report
      = CookSidecar(Sidecar(), core::meta::physics::PhysicsBackend::kPhysX);

    ExpectRefused(report, import_codes::kPhysicsSidecarBackendUnsupported, "");
  }

  //! A node index past the scene is refused where it is authored.
  NOLINT_TEST_F(PhysicsSidecarBindingsTest, RefusesANodeOutsideTheScene)
  {
    const auto report = CookSidecar(Sidecar().AppendBinding(
      "joints", json { { "node_index_a", 3U }, { "node_index_b", "world" } }));

    ExpectRefused(report, import_codes::kPhysicsSidecarNodeRefOutOfBounds,
      "/bindings/joints/0/node_index_a");
  }

} // namespace

} // namespace oxygen::content::import::test
