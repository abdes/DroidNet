// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Slots;

/// <summary>Identifies an existing native surface and the inventory observed by its editor.</summary>
/// <param name="Geometry">The geometry whose native inventory owns the surface.</param>
/// <param name="SurfaceId">The opaque native surface identity.</param>
/// <param name="LayoutRevision">The native inventory revision observed for this assignment.</param>
public sealed record SurfaceTarget(AssetPath Geometry, Guid SurfaceId, string LayoutRevision);
