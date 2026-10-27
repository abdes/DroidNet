// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Models;

/// <summary>The model import settings a person chooses, with the importer's defaults and the manifest schema's values
/// and ranges.</summary>
/// <remarks>
/// The rest of a job's vocabulary is the importer's rules or another surface's: content flags, content hashing, texture
/// formats and mips, per-texture overrides and per-geometry defaults are not chosen here, and settings keep them as they
/// are.
/// </remarks>
public static class ModelImportSettingCatalog
{
    /// <summary>Gets how source units become metres: <c>normalize</c> converts to metres, <c>preserve</c> keeps file
    /// units, <c>custom</c> applies <see cref="UnitScale"/>.</summary>
    public static ModelImportSetting UnitPolicy { get; } = new()
    {
        Key = "unit_policy",
        Kind = ModelImportSettingKind.Choice,
        Default = "normalize",
        Choices = ["normalize", "preserve", "custom"],
    };

    /// <summary>Gets the factor of a <c>custom</c> unit policy.</summary>
    public static ModelImportSetting UnitScale { get; } = new()
    {
        Key = "unit_scale",
        Kind = ModelImportSettingKind.Number,
        Default = 1d,
        Minimum = 0.0001,
    };

    /// <summary>Gets the up axis an FBX source is read in.</summary>
    public static ModelImportSetting UpAxis { get; } = new()
    {
        Key = "up_axis",
        Kind = ModelImportSettingKind.Choice,
        Default = "from-file",
        Choices = ["from-file", "y", "z"],
        Formats = ModelSourceFormats.Fbx,
    };

    /// <summary>Gets how vertex normals are made.</summary>
    public static ModelImportSetting NormalsPolicy { get; } = new()
    {
        Key = "normals_policy",
        Kind = ModelImportSettingKind.Choice,
        Default = "generate",
        Choices = ["none", "preserve", "generate", "recalculate"],
    };

    /// <summary>Gets the largest angle between two faces that share a smoothed normal.</summary>
    public static ModelImportSetting SmoothingAngle { get; } = new()
    {
        Key = "normals_smoothing_angle_deg",
        Kind = ModelImportSettingKind.Number,
        Default = 60d,
        Minimum = 0,
        Maximum = 180,
    };

    /// <summary>Gets how vertex tangents are made.</summary>
    public static ModelImportSetting TangentsPolicy { get; } = new()
    {
        Key = "tangents_policy",
        Kind = ModelImportSettingKind.Choice,
        Default = "generate",
        Choices = ["none", "preserve", "generate", "recalculate"],
    };

    /// <summary>Gets whether node transforms are baked into the meshes; the panel shows its inverse, Keep node pivots.</summary>
    public static ModelImportSetting BakeTransforms { get; } = new()
    {
        Key = "bake_transforms",
        Kind = ModelImportSettingKind.Toggle,
        Default = false,
    };

    /// <summary>Gets whether empty nodes are dropped.</summary>
    public static ModelImportSetting NodePruning { get; } = new()
    {
        Key = "node_pruning",
        Kind = ModelImportSettingKind.Choice,
        Default = "keep",
        Choices = ["keep", "drop-empty"],
    };

    /// <summary>Gets the glTF scene imported as the model scene: its selector, <c>#N</c> for an unnamed one.</summary>
    public static ModelImportSetting GltfScene { get; } = new()
    {
        Key = "gltf_scene",
        Kind = ModelImportSettingKind.Text,
        Formats = ModelSourceFormats.Gltf,
    };

    /// <summary>Gets the largest width or height of a stored texture; unset keeps the source size.</summary>
    public static ModelImportSetting MaxTextureSize { get; } = new()
    {
        Key = "max_size",
        Kind = ModelImportSettingKind.WholeNumber,
        Minimum = 1,
    };

    /// <summary>Gets the BC7 encoding quality.</summary>
    public static ModelImportSetting Bc7Quality { get; } = new()
    {
        Key = "bc7_quality",
        Kind = ModelImportSettingKind.Choice,
        Default = "default",
        Choices = ["none", "fast", "default", "high"],
    };

    /// <summary>Gets whether normal maps flip their green channel, from the DirectX convention.</summary>
    public static ModelImportSetting FlipNormalGreen { get; } = new()
    {
        Key = "flip_normal_green",
        Kind = ModelImportSettingKind.Toggle,
        Default = false,
    };

    /// <summary>Gets whether the file's lights are imported.</summary>
    public static ModelImportSetting ImportLights { get; } = new()
    {
        Key = "import_lights",
        Kind = ModelImportSettingKind.Toggle,
        Default = true,
    };

    /// <summary>Gets the factor every imported light's intensity is multiplied by.</summary>
    public static ModelImportSetting LightIntensityScale { get; } = new()
    {
        Key = "light_intensity_scale",
        Kind = ModelImportSettingKind.Number,
        Default = 1d,
        Minimum = 0,
    };

    /// <summary>Gets the range, in metres, of a glTF light without one.</summary>
    public static ModelImportSetting OmittedLightRange { get; } = new()
    {
        Key = "gltf_omitted_light_range_m",
        Kind = ModelImportSettingKind.Number,
        Default = 4096d,
        Minimum = 0,
        IsMinimumExclusive = true,
        Formats = ModelSourceFormats.Gltf,
    };

    /// <summary>Gets whether the file's cameras are imported.</summary>
    public static ModelImportSetting ImportCameras { get; } = new()
    {
        Key = "import_cameras",
        Kind = ModelImportSettingKind.Toggle,
        Default = true,
    };

    /// <summary>Gets the key of the source materials replaced by project materials: a material's name in the import to
    /// the virtual path of the material its surfaces use instead.</summary>
    public static string MaterialRemapKey => "material_remap";

    /// <summary>Gets the key of the import's name, shown for its cook job.</summary>
    public static string NameKey => "name";

    /// <summary>Gets every setting of this catalog.</summary>
    public static ImmutableArray<ModelImportSetting> All { get; } =
    [
        UnitPolicy, UnitScale, UpAxis, NormalsPolicy, SmoothingAngle, TangentsPolicy, BakeTransforms, NodePruning,
        GltfScene, MaxTextureSize, Bc7Quality, FlipNormalGreen, ImportLights, LightIntensityScale, OmittedLightRange,
        ImportCameras,
    ];
}
