// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>One opaque semantic surface and its exact surface bindings.</summary>
/// <param name="SurfaceId">The native identity; labels and ordering do not establish identity.</param>
/// <param name="DisplayName">The native presentation label.</param>
/// <param name="Visible">The asset's default Visibility of the surface, which applies where a scene does not override it.</param>
/// <param name="CastsShadows">The asset's default Cast Shadows of the surface, which applies where a scene does not override it.</param>
/// <param name="Bindings">All LOD/submesh bindings owned by this surface.</param>
public sealed record GeometrySurface(Guid SurfaceId, string DisplayName, bool Visible, bool CastsShadows, ImmutableArray<GeometrySurfaceBinding> Bindings);
