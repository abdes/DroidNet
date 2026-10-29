// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One source mount of the native import manifest: a project folder whose files are addressed under the mount's name.
/// </summary>
/// <param name="Name">The mount name, for example <c>Content</c>.</param>
/// <param name="Path">The mount folder, relative to the project root and with forward slashes.</param>
public sealed record ContentImportMount(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("path")] string Path);
