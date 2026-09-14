// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Loose cooked layout fields serialized to the native manifest schema.
/// </summary>
/// <param name="VirtualMountRoot">The native virtual mount root, for example <c>/Content</c>.</param>
public sealed record ContentImportLayout(
    [property: JsonPropertyName("virtual_mount_root")] string VirtualMountRoot)
{
    /// <summary>Gets the source-owned relative descriptor folder, also reflected in native virtual paths.</summary>
    [JsonPropertyName("descriptors_dir")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? DescriptorsDirectory { get; init; }
}
