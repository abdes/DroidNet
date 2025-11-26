// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Engine;

/// <summary>
/// Coordinates the lifetime of the native engine and arbitrates access to composition surfaces across all viewports.
/// </summary>
public interface IEngineService : IAsyncDisposable
{
    /// <summary>
    /// Gets the current lifecycle state of the service.
    /// </summary>
    public EngineServiceState State { get; }

    /// <summary>
    /// Gets the number of active composition surfaces managed by the service.
    /// </summary>
    public int ActiveSurfaceCount { get; }

    /// <summary>
    /// Ensures the underlying engine is initialized in its dormant headless state.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes once the engine is ready for surface allocation.</returns>
    public ValueTask InitializeAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Attaches the engine to a WinUI panel and returns a handle that can be used for subsequent resize and disposal operations.
    /// </summary>
    /// <param name="request">The logical viewport request.</param>
    /// <param name="panel">The swap chain panel that will host the engine output.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that yields the active lease.</returns>
    /// <remarks>
    /// Implementations must enforce <see cref="EngineSurfaceLimits"/> and throw <see cref="InvalidOperationException"/>
    /// when the request would exceed them.
    /// </remarks>
    public ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default);

    /// <summary>
    /// Releases every surface leased by the specified document, allowing the slots to be reused by other files.
    /// </summary>
    /// <param name="documentId">The owning document.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes when the releases have finished.</returns>
    public ValueTask ReleaseDocumentSurfacesAsync(Guid documentId, CancellationToken cancellationToken = default);
}
