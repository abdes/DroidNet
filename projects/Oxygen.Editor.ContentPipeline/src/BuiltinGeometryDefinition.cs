// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>A canonical procedural identity, authoring category and cook contribution.</summary>
/// <param name="AssetUri">The authored built-in identity.</param>
/// <param name="Name">The supported canonical name.</param>
/// <param name="CanonicalName">The native generator name.</param>
/// <param name="Contribution">The native descriptor and output mapping.</param>
/// <param name="AuthoringCategory">The engine-owned authoring availability.</param>
public sealed record BuiltinGeometryDefinition(Uri AssetUri, string Name, string CanonicalName, BuiltinDescriptorContribution Contribution, GeneratedAssetCategory AuthoringCategory);
