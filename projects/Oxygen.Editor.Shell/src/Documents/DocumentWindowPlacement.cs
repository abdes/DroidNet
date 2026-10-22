// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>Placement in a document window: documents go to its document area, and it has no panels to show.</summary>
/// <param name="area">The window's document area.</param>
public sealed class DocumentWindowPlacement(IDocumentArea area) : IPlacementService
{
    /// <inheritdoc/>
    public Task<DocumentPlacement> OpenDocumentAsync(AssetPath asset, WindowPlacement placement = WindowPlacement.Current, CancellationToken cancellationToken = default)
        => area.OpenAsync(asset, cancellationToken);

    /// <inheritdoc/>
    /// <remarks>Panels are the workspace window's: a document window shows none.</remarks>
    public Task<bool> ShowPanelAsync(string panelId, WindowPlacement placement = WindowPlacement.Current)
    {
        ArgumentException.ThrowIfNullOrEmpty(panelId);
        return Task.FromResult(false);
    }

    /// <inheritdoc/>
    /// <remarks>Panels are the workspace window's: a document window shows none.</remarks>
    public Task<bool> ShowPanelsAsync(IReadOnlyList<string> panelIds, WindowPlacement placement = WindowPlacement.Current)
    {
        ArgumentNullException.ThrowIfNull(panelIds);
        return Task.FromResult(panelIds.Count == 0);
    }

    /// <inheritdoc/>
    /// <remarks>A document window has no panel tabs to title.</remarks>
    public void SetPanelTitle(string panelId, string title) => ArgumentException.ThrowIfNullOrEmpty(panelId);
}
