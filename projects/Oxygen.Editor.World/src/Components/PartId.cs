// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Identifies a part of a component: a piece the asset that defines it identifies, such as a surface of a geometry.
/// </summary>
/// <remarks>
/// The ID is owned by the asset, never derived from a name or an index, so it survives reimport and reordering.
/// </remarks>
/// <param name="Kind">The part kind the schema declares, such as <c>surface</c>.</param>
/// <param name="Id">The asset-owned identity of the part.</param>
public readonly record struct PartId(string Kind, Guid Id)
{
    /// <inheritdoc/>
    public override string ToString() => $"{this.Kind}:{this.Id}";
}
