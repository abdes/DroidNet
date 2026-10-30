// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One source mount the native import manifest declares: a folder whose files are addressed under the mount's name.
/// </summary>
/// <param name="Name">The mount name, for example <c>Content</c>.</param>
/// <param name="Path">The mount folder, relative to the project root and with forward slashes; null for a placeholder,
/// which the cook's mounts file binds.</param>
public sealed record ContentImportMount(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("path"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Path);
