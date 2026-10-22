// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>A surface of a geometry, as chosen from the geometry's surface inventory.</summary>
/// <param name="Geometry">The geometry whose inventory lists the surface.</param>
/// <param name="SurfaceId">The engine-owned surface identity.</param>
/// <param name="LayoutRevision">The revision of the inventory the surface was chosen from.</param>
public sealed record SurfaceTarget(AssetPath Geometry, Guid SurfaceId, string LayoutRevision)
{
    /// <summary>Gets the surface as a part of a geometry component.</summary>
    public PartId Part => new(SceneSchema.Geometry.Surface.Kind, this.SurfaceId);
}
