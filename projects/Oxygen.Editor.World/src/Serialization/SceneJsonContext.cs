// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// JSON serialization context for scene data using source generators.
/// </summary>
[JsonSerializable(typeof(SceneData))]
[JsonSerializable(typeof(SceneEnvironmentData))]
[JsonSerializable(typeof(SceneReferencesData))]
[JsonSerializable(typeof(StartingInputContextData))]
[JsonSerializable(typeof(SkyAtmosphereEnvironmentData))]
[JsonSerializable(typeof(PostProcessEnvironmentData))]
[JsonSerializable(typeof(FogEnvironmentData))]
[JsonSerializable(typeof(SkySphereEnvironmentData))]
[JsonSerializable(typeof(SkyLightEnvironmentData))]
[JsonSerializable(typeof(BackgroundEnvironmentData))]
[JsonSerializable(typeof(ExposureCompensationKeyData))]
[JsonSerializable(typeof(SceneNodeData))]
[JsonSerializable(typeof(ComponentData))]
[JsonSerializable(typeof(MetadataData))]
[JsonSerializable(typeof(HierarchyData))]
[JsonSerializable(typeof(NodeFlagsData))]
[JsonSerializable(typeof(PerspectiveCameraData))]
[JsonSerializable(typeof(OrthographicCameraData))]
[JsonSerializable(typeof(CameraComponentData))]
[JsonSerializable(typeof(LightComponentData))]
[JsonSerializable(typeof(DirectionalLightData))]
[JsonSerializable(typeof(PointLightData))]
[JsonSerializable(typeof(LocalFogVolumeData))]
[JsonSerializable(typeof(ScriptingData))]
[JsonSerializable(typeof(ScriptSlotData))]
[JsonSerializable(typeof(ScriptValueData))]
[JsonSerializable(typeof(bool))]
[JsonSerializable(typeof(int))]
[JsonSerializable(typeof(float))]
[JsonSerializable(typeof(float[]))]
[JsonSerializable(typeof(string))]
[JsonSerializable(typeof(SpotLightData))]
[JsonSerializable(typeof(TransformData))]
[JsonSerializable(typeof(SourceOverridesData))]
[JsonSerializable(typeof(PartOverridesData))]
[JsonSerializable(typeof(SceneInstanceComponentData))]
[JsonSerializable(typeof(InstancedNodeChangesData))]

// The value types of overridable properties (SceneSchema.Overridable).
[JsonSerializable(typeof(AssetPath))]
[JsonSerializable(typeof(Vector3))]
[JsonSerializable(typeof(LodPolicy))]
[JsonSerializable(typeof(CameraAspectMode))]
[JsonSerializable(typeof(VisibilitySource))]
[JsonSerializable(typeof(ShadowSource))]
[JsonSerializable(typeof(AtmosphereLightSlot))]
[JsonSerializable(typeof(DirectionalCsmSplitMode))]
[JsonSerializable(typeof(ShadowResolutionHint))]

// A LOD policy's "type" may follow its fields, as in the engine's reports, whose JSON objects sort their keys.
[JsonSourceGenerationOptions(
    AllowOutOfOrderMetadataProperties = true,
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    Converters = new[] { typeof(Vector3JsonConverter), typeof(Vector4JsonConverter), typeof(QuaternionJsonConverter) })]
public partial class SceneJsonContext : JsonSerializerContext;
