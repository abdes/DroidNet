// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Describes a request to attach the engine to a WinUI <c>SwapChainPanel</c> within a document viewport.
/// </summary>
public sealed record class ViewportSurfaceRequest
{
    /// <summary>
    ///     Gets the owner of the viewport.
    /// </summary>
    public required SurfaceOwnerId Owner { get; init; }

    /// <summary>
    ///     Gets the stable identifier of the viewport instance inside its owner.
    /// </summary>
    public required Guid ViewportId { get; init; }

    /// <summary>
    ///     Gets the zero-based index of the viewport within the current layout.
    /// </summary>
    public required int ViewportIndex { get; init; }

    /// <summary>
    ///     Gets a value indicating whether gets a flag indicating whether the viewport should be
    ///     treated as its owner's primary surface.
    /// </summary>
    public bool IsPrimary { get; init; }

    /// <summary>
    ///     Gets an optional tag used strictly for diagnostics and logging.
    /// </summary>
    public string? Tag { get; init; }

    /// <summary>
    ///     Creates the <see cref="ViewportSurfaceKey"/> associated with this request.
    /// </summary>
    /// <returns>The derived surface key.</returns>
    public ViewportSurfaceKey ToKey() => new(this.Owner, this.ViewportId);
}
