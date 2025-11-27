// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Identifies a leased composition surface by document and viewport identifiers.
/// </summary>
/// <param name="DocumentId">The scene document that owns the viewport.</param>
/// <param name="ViewportId">A unique identifier for the viewport instance.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct ViewportSurfaceKey(Guid DocumentId, Guid ViewportId)
{
    /// <summary>
    /// Gets a friendly name used in logs.
    /// </summary>
    public string DisplayName => $"doc:{this.DocumentId:N}/vp:{this.ViewportId:N}";
}
