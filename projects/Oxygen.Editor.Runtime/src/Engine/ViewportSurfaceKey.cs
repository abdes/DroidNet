// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Identifies a leased composition surface by its owner and viewport.
/// </summary>
/// <param name="Owner">The owner of the viewport.</param>
/// <param name="ViewportId">A unique identifier for the viewport instance.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct ViewportSurfaceKey(SurfaceOwnerId Owner, Guid ViewportId)
{
    /// <summary>
    /// Gets a friendly name used in logs.
    /// </summary>
    public string DisplayName => $"owner:{this.Owner.Value:N}/vp:{this.ViewportId:N}";
}
