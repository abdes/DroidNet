// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>A supported procedural name, its alias group, and cook contribution.</summary>
/// <param name="AssetUri">The authored built-in identity.</param>
/// <param name="Name">The supported name or alias.</param>
/// <param name="CanonicalName">The shared generator name used to group aliases.</param>
/// <param name="Contribution">The native descriptor and output mapping.</param>
public sealed record BuiltinGeometryDefinition(Uri AssetUri, string Name, string CanonicalName, BuiltinDescriptorContribution Contribution);
