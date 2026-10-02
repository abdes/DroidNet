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

/// <summary>A native key reference; only asset-target references are cook dependencies.</summary>
/// <param name="AssetKey">The referenced native key.</param>
/// <param name="TargetKind">The native target classification.</param>
/// <param name="ExpectedAssetType">The expected asset type, or zero when not applicable.</param>
public sealed record CookedKeyReference(string AssetKey, CookedKeyReferenceTargetKind TargetKind, byte ExpectedAssetType);

/// <summary>Stable key-reference target kinds emitted by the native Inspector.</summary>
public enum CookedKeyReferenceTargetKind : byte
{
    Asset = 1,
    PhysicsResource = 2,
    Logical = 3,
}

/// <summary>A descriptor-local native resource binding, not a cooked asset dependency.</summary>
/// <param name="Kind">The stable native resource kind.</param>
/// <param name="Index">The descriptor-local binding index.</param>
/// <param name="State">The native fallback/error classification when applicable.</param>
public sealed record CookedResourceBinding(CookedResourceKind Kind, uint Index, CookedResourceBindingState State);

/// <summary>Stable resource kinds emitted by the native Inspector.</summary>
public enum CookedResourceKind : byte
{
    Buffer = 1,
    Texture = 2,
    Script = 3,
    Physics = 4,
}

/// <summary>Native classification of an indexed resource binding.</summary>
public enum CookedResourceBindingState
{
    Resource,
    Error,
    Fallback,
}
