// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Native material descriptor JSON consumed by Oxygen.Cooker material-descriptor jobs.
/// </summary>
internal sealed record NativeMaterialDescriptor(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("domain")] string Domain,
    [property: JsonPropertyName("alpha_mode")] string AlphaMode,
    [property: JsonPropertyName("parameters")] NativeMaterialParameters Parameters);
