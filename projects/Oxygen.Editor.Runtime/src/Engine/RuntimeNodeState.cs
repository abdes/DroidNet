// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Stored node properties and currently resolved LOD-zero assets.</summary>
/// <param name="Exists">Whether the native node still exists.</param>
/// <param name="IsPrimarySun">Whether the native light resolver selects this node as the sun.</param>
/// <param name="Properties">Scalar fields for the components present on the node.</param>
/// <param name="GeometryKey">The resolved geometry key, or an empty string when absent.</param>
/// <param name="GeometryName">The resolved geometry name.</param>
/// <param name="VertexCount">The first LOD's vertex count.</param>
/// <param name="IndexCount">The first LOD's index count.</param>
/// <param name="MaterialKeys">Resolved material keys in first-LOD slot order.</param>
public sealed record RuntimeNodeState(
    bool Exists,
    bool IsPrimarySun,
    ImmutableArray<RuntimePropertyValue> Properties,
    string GeometryKey,
    string GeometryName,
    ulong VertexCount,
    ulong IndexCount,
    ImmutableArray<string> MaterialKeys)
{
    /// <summary>Gets the currently bound linear base colours in first-LOD slot order.</summary>
    public ImmutableArray<System.Numerics.Vector4> MaterialBaseColors { get; init; } = [];
}
