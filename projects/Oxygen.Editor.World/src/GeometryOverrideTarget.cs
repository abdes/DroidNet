// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.Core;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World;

/// <summary>
/// Represents a targeted override for specific LODs and/or submeshes within a geometry asset.
/// </summary>
/// <remarks>
/// <para>
/// This class allows fine-grained control over rendering properties for specific parts of geometry.
/// The <see cref="LodIndex"/> and <see cref="SubmeshIndex"/> properties determine the target scope:
/// </para>
/// <list type="bullet">
/// <item><description>(-1, -1) = All LODs, all submeshes</description></item>
/// <item><description>(N, -1) = LOD N, all submeshes</description></item>
/// <item><description>(-1, M) = All LODs, submesh M (uncommon)</description></item>
/// <item><description>(N, M) = LOD N, submesh M</description></item>
/// </list>
/// </remarks>
public partial class GeometryOverrideTarget : ScopedObservableObject
{
    private int lodIndex = -1;
    private int submeshIndex = -1;

    /// <summary>
    /// Gets or sets the LOD index to target.
    /// </summary>
    /// <value>
    /// The zero-based LOD index, or -1 to target all LODs. Default is -1.
    /// </value>
    public int LodIndex
    {
        get => this.lodIndex;
        set => this.SetProperty(ref this.lodIndex, value);
    }

    /// <summary>
    /// Gets or sets the submesh index to target.
    /// </summary>
    /// <value>
    /// The zero-based submesh index within the LOD, or -1 to target all submeshes. Default is -1.
    /// </value>
    public int SubmeshIndex
    {
        get => this.submeshIndex;
        set => this.SetProperty(ref this.submeshIndex, value);
    }

    /// <summary>
    /// Gets the collection of override slots applied to this target.
    /// </summary>
    /// <remarks>
    /// Slots in this collection override properties for the specified LOD/submesh combination.
    /// For example, a <see cref="MaterialsSlot"/> here would override the material for the targeted submesh.
    /// </remarks>
    public ObservableCollection<OverrideSlot> OverrideSlots { get; } = [];
}
