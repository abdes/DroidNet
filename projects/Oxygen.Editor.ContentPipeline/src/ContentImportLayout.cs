// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Loose cooked layout fields serialized to the native manifest schema. Every job names the identity it cooks, and a
/// container (an input source or a model) names its products under it, so the layout only anchors virtual paths.
/// </summary>
/// <param name="VirtualMountRoot">The native virtual mount root, for example <c>/Content</c>.</param>
public sealed record ContentImportLayout(
    [property: JsonPropertyName("virtual_mount_root")] string VirtualMountRoot);
