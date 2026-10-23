// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The surface a part override addresses, with the layout revision it was resolved against.</summary>
/// <param name="SurfaceId">The opaque native surface identity.</param>
/// <param name="LayoutRevision">The canonical SHA-256 text of the geometry layout revision.</param>
public sealed record RuntimeOverridePart(Guid SurfaceId, string LayoutRevision);
