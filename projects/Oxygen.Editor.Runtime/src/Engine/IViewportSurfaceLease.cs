// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Represents a tracked association between a WinUI <see cref="SwapChainPanel"/> and an engine composition surface.
/// </summary>
public interface IViewportSurfaceLease : IAsyncDisposable
{
    /// <summary>
    /// Gets the logical identifier of the leased composition surface.
    /// </summary>
    public ViewportSurfaceKey Key { get; }

    /// <summary>
    /// Gets a value indicating whether the lease is currently attached to a <see cref="SwapChainPanel"/>.
    /// </summary>
    public bool IsAttached { get; }

    /// <summary>
    /// Ensures the surface is connected to the supplied panel on the UI thread.
    /// </summary>
    /// <param name="panel">The panel that will host the swap chain.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes when the attach operation is done.</returns>
    public ValueTask AttachAsync(SwapChainPanel panel, CancellationToken cancellationToken = default);

    /// <summary>
    /// Requests a resize of the native composition surface backing this lease.
    /// </summary>
    /// <param name="pixelWidth">The panel width in physical pixels.</param>
    /// <param name="pixelHeight">The panel height in physical pixels.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes when the resize request has been queued.</returns>
    public ValueTask ResizeAsync(uint pixelWidth, uint pixelHeight, CancellationToken cancellationToken = default);
}
