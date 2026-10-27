//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <EditorModule/ICommandFactory.h>
#include <EngineContext.h>
#include <World/EnvironmentStateManaged.h>
#include <World/SkyEnvironmentManaged.h>

#pragma managed(push, on)

namespace Oxygen::Interop::World {

using namespace System;
using namespace System::Numerics;

//! The value type of a property-pipeline field, numbered as the engine's
//! `FieldType`.
public
enum class FieldType : System::Byte {
  Bool = 0,
  Int32 = 1,
  Float = 2,
};

//! Wire-format entry for the property-pipeline transport (§5.3).
/*!
 The (ComponentId, FieldId) pair is the property-pipeline §5.3 wire
 address. ComponentId is a stable engine-wide enum (see
 `oxygen::interop::module::ComponentId` in PropertyKeys.h);
 FieldId is a component-local enumerator owned by the matching
 `IComponentPropertyApplier`. Adding a new component or property
 never requires changing this struct.

 `Type` names the member that holds the value; the others stay at their
 defaults. It must be the field's schema value type: SetProperties refuses
 an entry that is not. Vectors and colors go as one float field per axis.
*/
public
value struct PropertyValueEntry {
  System::UInt16 ComponentId;
  System::UInt16 FieldId;
  FieldType Type;
  bool BoolValue;
  System::Int32 Int32Value;
  float FloatValue;
};

  //! A LOD policy as the scene schema spells it. `Type` is the native
  //! `LodPolicyType`: 0 fixed (`LodIndex`), 1 distance (`Thresholds`,
  //! `HysteresisRatio`), 2 screen-space error (`EnterFinerSse`,
  //! `ExitCoarserSse`). Members the type does not use are null or zero.
  public value struct LodPolicyManaged {
    System::Byte Type;
    System::Byte LodIndex;
    array<float>^ Thresholds;
    float HysteresisRatio;
    array<float>^ EnterFinerSse;
    array<float>^ ExitCoarserSse;
  };

  //! One override of a geometry property. The property its `OverrideId` names
  //! decides which value member holds the value: `Flag` for a bool,
  //! `AssetPath` for an asset, `LodPolicy` for a LOD policy. The other members
  //! stay at their defaults.
  public value struct OverrideEntryManaged {
    System::UInt16 OverrideId;
    bool Flag;
    System::String^ AssetPath;
    LodPolicyManaged LodPolicy;
  };

  //! The member of `InstanceChangeManaged` that holds a change's value.
  public enum class InstanceValueKind : System::Byte {
    Bool = 0,
    Int32 = 1,
    Float = 2,
    Vector3 = 3,
    Asset = 4,
    LodPolicy = 5,
  };

  //! A property change of a node an instance brings in, its value in the
  //! schema's units: an enum is its member's wire value, a material the
  //! virtual path of its cooked asset, a LOD policy its record's values.
  //! `Kind` names the member that holds the value; the others stay at their
  //! defaults.
  public value struct InstanceChangeManaged {
    //! One node ID per nesting level below the instance root, shallowest
    //! first.
    array<System::Guid>^ Path;
    System::UInt16 OverrideId;
    //! The surface of a surface property; empty otherwise.
    System::Guid PartId;
    InstanceValueKind Kind;
    bool BoolValue;
    System::Int32 Int32Value;
    float FloatValue;
    System::Numerics::Vector3 Vector3Value;
    System::String^ AssetPath;
    LodPolicyManaged LodPolicy;
  };

  //! How the engine reads a cooked content source.
  public enum class CookedRootKindManaged : System::Byte {
    //! A loose cooked root directory.
    LooseFolder,
    //! A pak file.
    PakFile,
  };

  //! One cooked content source of a mount set.
  public value struct CookedRootManaged {
    System::String^ Path;
    CookedRootKindManaged Kind;
  };

  //! Native scene background values observed at the mutation boundary.
  //! Stored component values and resolved LOD-zero assets for a native node.
  public value struct NodeStateManaged {
    bool Exists;
    bool IsPrimarySun;
    array<PropertyValueEntry>^ Properties;
    System::String^ GeometryKey;
    System::String^ GeometryName;
    System::UInt64 VertexCount;
    System::UInt64 IndexCount;
    array<System::String^>^ MaterialKeys;
    array<System::Numerics::Vector4>^ MaterialBaseColors;
  };

  //! A node of a mounted scene's hierarchy: one of the scene's own nodes, or
  //! one an instance of it brings in.
  public value struct SceneHierarchyNodeManaged {
    //! The node IDs from the scene read down to the node, shallowest first.
    array<System::Guid>^ Path;
    System::String^ Name;
    //! The parent's index in the hierarchy's nodes; -1 for a root of the
    //! scene read.
    int Parent;
    //! The asset key of the scene that defines the node.
    System::Guid SceneKey;
    //! The asset key of the scene the node instances; empty when the node is
    //! no instance root.
    System::Guid InstanceKey;
    //! The virtual path of the geometry the node renders: null when it
    //! renders nothing, empty when no mounted source provides it.
    System::String^ GeometryPath;
    //! Whether the node has a camera, perspective or orthographic.
    bool HasCamera;
  };

  //! Why part of a scene's hierarchy could not be read.
  public enum class SceneHierarchyIssueCodeManaged : System::Byte {
    SceneUnavailable = 0,
    Cycle = 1,
  };

  //! A part of a scene's hierarchy that could not be read.
  public value struct SceneHierarchyIssueManaged {
    SceneHierarchyIssueCodeManaged Code;
    //! The instance root's index in the hierarchy's nodes; -1 for none.
    int Node;
    System::Guid SceneKey;
  };

  //! The hierarchy of a mounted scene with every scene it instances.
  public value struct SceneHierarchyManaged {
    //! Whether the virtual path names a mounted, readable scene.
    bool Exists;
    System::Guid SceneKey;
    array<SceneHierarchyNodeManaged>^ Nodes;
    array<SceneHierarchyIssueManaged>^ Issues;
  };

  //! A property of a mounted scene's node with its effective value, in the
  //! schema's units: an enum is its member's wire value, a rotation the
  //! editor's Y-X-Z Euler angles in degrees, an asset the virtual path of its
  //! cooked asset (empty when no mounted source provides it), a LOD policy its
  //! record's values. `Kind` names the member that holds the value; the
  //! others stay at their defaults.
  public value struct SceneNodePropertyManaged {
    System::UInt16 OverrideId;
    //! The surface of a surface property; empty otherwise.
    System::Guid PartId;
    //! The geometry layout revision of a surface property, as lowercase
    //! hexadecimal; null otherwise.
    System::String^ LayoutRevision;
    InstanceValueKind Kind;
    bool BoolValue;
    System::Int32 Int32Value;
    float FloatValue;
    System::Numerics::Vector3 Vector3Value;
    System::String^ AssetPath;
    LodPolicyManaged LodPolicy;
  };

  //! The effective values of a node of a mounted scene's composition.
  public value struct SceneNodeValuesManaged {
    //! Whether the virtual path names a mounted, readable scene with the node.
    bool Exists;
    System::String^ Name;
    //! The virtual path of the geometry the node renders: null when it
    //! renders none, empty when no mounted source provides it.
    System::String^ GeometryPath;
    array<SceneNodePropertyManaged>^ Properties;
  };

  public
  ref class OxygenWorld {
  public:
    OxygenWorld(EngineContext^ context);
    OxygenWorld(EngineContext^ context, ICommandFactory^ commandFactory);

    // Scene management
    // Scenes are identified by name on the native engine side; keep
    // CreateScene(String) as before.
    void CreateScene(String^ name);
    // Create scene and return a Task that completes when native creation finishes
    System::Threading::Tasks::Task<bool>^ CreateSceneAsync(String^ name);
    // Destroy the currently active scene on the engine thread. This will
    // enqueue a scene-destruction command so teardown happens safely on the
    // engine thread and does not race with frame traversal.
    void DestroyScene();

    // Node management (GUID-based): all node APIs accept node or parent GUIDs
    // Creates a node and invokes the callback with the new node GUID on the
    // engine thread.
    void CreateSceneNode(String^ name, System::Guid nodeId,
      Nullable<System::Guid> parentGuid,
      Action<System::Guid>^ onCreated);
    void CreateSceneNode(String^ name, System::Guid nodeId,
      Nullable<System::Guid> parentGuid,
      Action<System::Guid>^ onCreated,
      bool initializeWorldAsRoot);
    void RemoveSceneNode(System::Guid nodeId);
    void RenameSceneNode(System::Guid nodeId, String^ newName);

    // Transform management
    void SetLocalTransform(System::Guid nodeId,
      System::Numerics::Vector3 position,
      System::Numerics::Quaternion rotation,
      System::Numerics::Vector3 scale);

    //! Property pipeline §5.3 — enqueue a generic SetProperties command.
    void SetProperties(System::Guid nodeId,
      array<PropertyValueEntry>^ entries);

    // Geometry management
    //! Queues geometry with a generation-correlated asynchronous failure sink.
    void SetGeometry(System::Guid nodeId, String^ assetUri,
      Action<System::UInt64, String^>^ onFailure);
    void SetGeometry(System::Guid nodeId, String^ assetUri,
      Action<System::UInt64, String^>^ onFailure, Action<System::UInt64>^ onSuccess);
    //! Sets the whole override state of one part of a node's geometry: the
    //! surface `surfaceId` resolved at `layoutRevision`, or with an empty
    //! `surfaceId` and a null `layoutRevision` the geometry component itself.
    //! A property of the part with no entry takes the geometry's asset value.
    void SetOverrides(System::Guid nodeId, String^ geometryUri,
      System::Guid surfaceId, String^ layoutRevision,
      array<OverrideEntryManaged>^ entries, System::Byte intent,
      Action<System::UInt64, String^>^ onFailure,
      Action<System::UInt64>^ onSuccess);

    // Scene instances
    //! Instances the cooked scene at `sourcePath` under the node, with
    //! `changes`; replaces an instance the node has. Every outcome of the
    //! instance, its changes included, reaches `onFailure` or `onSuccess` with
    //! a generation that grows; mounting content again builds it again.
    void InstantiateScene(System::Guid nodeId, String^ sourcePath,
      array<InstanceChangeManaged>^ changes,
      Action<System::UInt64, String^>^ onFailure,
      Action<System::UInt64>^ onSuccess);
    //! Sets one change of the node's instance.
    void SetInstanceChange(System::Guid nodeId, InstanceChangeManaged change);
    //! Clears one change of the node's instance: the node below it takes the
    //! next level's value, or its source's.
    void ClearInstanceChange(System::Guid nodeId, array<System::Guid>^ path,
      System::UInt16 overrideId, System::Guid partId);
    //! Removes the node's instance; the node and its owned children stay.
    void DetachInstance(System::Guid nodeId);

    //! Reads native atmosphere and post-process values after preceding mutations.
    System::Threading::Tasks::Task<EnvironmentStateManaged>^
      ObserveEnvironmentAsync();

    //! Reads node properties and currently resolved assets after earlier mutations.
    System::Threading::Tasks::Task<NodeStateManaged>^
      ObserveNodeAsync(System::Guid nodeId);

    //! Reads the hierarchy of the mounted scene at `virtualPath`, with every
    //! scene it instances, from the scene descriptors alone.
    System::Threading::Tasks::Task<SceneHierarchyManaged>^
      ReadSceneHierarchyAsync(String^ virtualPath);

    //! Reads the effective values of the node `path` names in the composition
    //! of the mounted scene at `virtualPath`, from the scene descriptors
    //! alone: one node ID per nesting level, the first a node of the scene.
    System::Threading::Tasks::Task<SceneNodeValuesManaged>^
      ReadSceneNodeAsync(String^ virtualPath, array<System::Guid>^ path);

    void SetEnvironment(bool atmosphereEnabled, bool sunDiskEnabled,
      float planetRadiusMeters, float atmosphereHeightMeters,
      System::Numerics::Vector3 groundAlbedoRgb,
      float rayleighScaleHeightMeters, float mieScaleHeightMeters,
      float mieAnisotropy, System::Numerics::Vector3 skyLuminanceFactorRgb,
      float aerialPerspectiveDistanceScale, float aerialScatteringStrength,
      float aerialPerspectiveStartDepthMeters, float heightFogContribution,
      int exposureMode, bool exposureEnabled, float exposureKey,
      float manualExposureEv, float exposureCompensation, int toneMapping,
      int autoExposureMeteringMode, float autoExposureMinEv,
      float autoExposureMaxEv, float autoExposureSpeedUp,
      float autoExposureSpeedDown, float autoExposureLowPercentile,
      float autoExposureHighPercentile, float autoExposureMinLogLuminance,
      float autoExposureLogLuminanceRange, float autoExposureTargetLuminance,
      float autoExposureSpotMeterRadius, float autoExposureBlackInfluence,
      float autoExposureTransitionDistanceEv,
      cli::array<ExposureCompensationKeyManaged>
        ^ autoExposureCompensationCurve,
      String ^ exposureMaskPath, float bloomIntensity,
      float bloomThreshold, float saturation, float contrast,
      float vignetteIntensity, float displayGamma, FogEnvironmentManaged fog,
      SkySphereEnvironmentManaged skySphere,
      SkyLightEnvironmentManaged skyLight,
      BackgroundEnvironmentManaged background,
      Action<System::UInt64, String ^> ^ onFailure,
      Action<System::UInt64> ^ onSuccess);
    void DetachGeometry(System::Guid nodeId);
    void AttachPerspectiveCamera(System::Guid nodeId,
      float fieldOfViewYRadians, float aspectRatio, float nearPlane,
      float farPlane, System::Byte aspectMode);
    void AttachOrthographicCamera(System::Guid nodeId,
      float orthographicSize, float aspectRatio, float nearPlane,
      float farPlane, System::Byte aspectMode);
    void DetachCamera(System::Guid nodeId);
    void AttachDirectionalLight(System::Guid nodeId,
    float intensityLux, float angularSizeRadians, System::Numerics::Vector3 color,
    bool affectsWorld, int mobility, bool castsShadows, float shadowBias,
    float shadowNormalBias, bool contactShadows, int shadowResolutionHint,
    float exposureCompensation, int atmosphereLightSlot, bool usePerPixelAtmosphereTransmittance,
    System::Numerics::Vector3 atmosphereDiskLuminanceScaleRgb,
    int cascadeCount, int splitMode, float maxShadowDistance,
    System::Numerics::Vector4 cascadeDistances, float distributionExponent,
    float transitionFraction, float distanceFadeoutFraction);
    //! Attaches or replaces the local fog volume on a node.
    void AttachLocalFogVolume(System::Guid nodeId, bool enabled,
      float radialFogExtinction, float heightFogExtinction,
      float heightFogFalloff, float heightFogOffset, float fogPhaseG,
      System::Numerics::Vector3 fogAlbedo,
      System::Numerics::Vector3 fogEmissive, int sortPriority);

    //! Removes the local fog volume from a node.
    void DetachLocalFogVolume(System::Guid nodeId);

    void AttachPointLight(System::Guid nodeId, float luminousFluxLumens,
    float range, float sourceRadius, System::Numerics::Vector3 color,
    bool affectsWorld, bool castsShadows, float shadowBias, float shadowNormalBias,
    bool contactShadows, int shadowResolutionHint, float exposureCompensation);
    void AttachSpotLight(System::Guid nodeId, float luminousFluxLumens,
    float range, float sourceRadius, float innerConeAngleRadians, float outerConeAngleRadians,
    System::Numerics::Vector3 color, bool affectsWorld, bool castsShadows,
    float shadowBias, float shadowNormalBias, bool contactShadows,
    int shadowResolutionHint, float exposureCompensation);
    void DetachLight(System::Guid nodeId);

    // Selection (Editor-side state)
    void SelectNode(System::Guid nodeId);
    void DeselectNode(System::Guid nodeId);

    // Reparent a node to a new parent (or make root when parent is null)
    void ReparentSceneNode(System::Guid child, Nullable<System::Guid> parent,
      bool preserveWorldTransform);
    void ReparentSceneNodes(array<System::Guid>^ children,
      Nullable<System::Guid> parent,
      bool preserveWorldTransform);
    void UpdateTransformsForNodes(array<System::Guid>^ nodes);
    void RemoveSceneNodes(array<System::Guid>^ nodes);

    // Asset management
    System::Threading::Tasks::Task
      ^ ReplaceCookedRootsAsync(array<CookedRootManaged> ^ roots);
    System::Threading::Tasks::Task^ SetCookedContentPausedAsync(bool paused);

    // (All APIs are GUID-based; no scene names are accepted)

  private:
    EngineContext^ context_;
    ICommandFactory^ commandFactory_;
  };

  } // namespace Oxygen::Interop::World

#pragma managed(pop)
