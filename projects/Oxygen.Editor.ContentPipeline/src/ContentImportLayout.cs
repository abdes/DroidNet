// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Loose cooked layout fields serialized to the native manifest schema: where a model's products go by type. A source
/// that cooks one asset, or an input source whose products are named under it, names its identity on its job instead.
/// </summary>
/// <param name="VirtualMountRoot">The native virtual mount root, for example <c>/Content</c>.</param>
public sealed record ContentImportLayout(
    [property: JsonPropertyName("virtual_mount_root")] string VirtualMountRoot)
{
    /// <summary>Gets the source-owned relative descriptor folder, also reflected in native virtual paths.</summary>
    [JsonPropertyName("descriptors_dir")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? DescriptorsDirectory { get; init; }

    /// <summary>Gets the mount-relative folder for a model's materials.</summary>
    [JsonPropertyName("materials_subdir")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? MaterialsDirectory { get; init; }

    /// <summary>Gets the mount-relative folder for a model's geometry.</summary>
    [JsonPropertyName("geometry_subdir")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? GeometryDirectory { get; init; }

    /// <summary>Gets the mount-relative folder for a model's scene.</summary>
    [JsonPropertyName("scenes_subdir")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ScenesDirectory { get; init; }
}
