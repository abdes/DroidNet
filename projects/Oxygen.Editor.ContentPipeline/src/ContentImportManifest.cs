// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Editor representation of an Oxygen import manifest.
/// </summary>
/// <param name="Version">The native manifest version.</param>
/// <param name="Output">The physical cooked output root.</param>
/// <param name="Layout">The loose cooked layout.</param>
/// <param name="Jobs">The manifest jobs.</param>
public sealed record ContentImportManifest(
    [property: JsonPropertyName("version")] int Version,
    [property: JsonPropertyName("output")] string Output,
    [property: JsonPropertyName("layout")] ContentImportLayout Layout,
    [property: JsonPropertyName("jobs")] IReadOnlyList<ContentImportJob> Jobs);
