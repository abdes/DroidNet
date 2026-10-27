// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Native references owned by one indexed asset descriptor.</summary>
/// <param name="AssetKey">The owning native asset identity.</param>
/// <param name="AssetType">The native type.</param>
/// <param name="VirtualPath">The indexed virtual path.</param>
/// <param name="Dependencies">Direct asset-target key references only.</param>
/// <param name="Complete">Whether every asset dependency was decoded.</param>
/// <param name="Diagnostic">The explanation when decoding is incomplete.</param>
public sealed record CookedAssetDependencies(string AssetKey, byte AssetType, string VirtualPath, ImmutableArray<string> Dependencies, bool Complete, string? Diagnostic)
{
    /// <summary>Gets all native key references, including non-asset targets.</summary>
    public ImmutableArray<CookedKeyReference> KeyReferences { get; init; } = [];

    /// <summary>Gets opaque native resource-binding metadata.</summary>
    public ImmutableArray<CookedResourceBinding> ResourceBindings { get; init; } = [];
}
