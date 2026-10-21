// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Places documents and panels in the windows of the session, for parts that never name a window.</summary>
public interface IPlacementService
{
    /// <summary>Opens an asset's document and shows it.</summary>
    /// <param name="asset">The asset to edit.</param>
    /// <param name="placement">Where to show it; a document that is open shows where it is.</param>
    /// <param name="cancellationToken">Cancels the open before the document is shown.</param>
    /// <returns>Where the document was placed, or why it was not.</returns>
    public Task<DocumentPlacement> OpenDocumentAsync(AssetPath asset, WindowPlacement placement = WindowPlacement.Current, CancellationToken cancellationToken = default);

    /// <summary>Shows a panel and brings it forward.</summary>
    /// <param name="panelId">The panel's identity.</param>
    /// <param name="placement">Where to show it.</param>
    /// <returns><see langword="true"/> when the panel is shown.</returns>
    public Task<bool> ShowPanelAsync(string panelId, WindowPlacement placement = WindowPlacement.Current);

    /// <summary>Shows panels so that each one is visible: a panel that shares a dock with a later one moves to a dock of its own.</summary>
    /// <param name="panelIds">The panels' identities, in the order they are shown; the last is in front where panels still share a dock.</param>
    /// <param name="placement">Where to show them.</param>
    /// <returns><see langword="true"/> when every panel is shown.</returns>
    public Task<bool> ShowPanelsAsync(IReadOnlyList<string> panelIds, WindowPlacement placement = WindowPlacement.Current);

    /// <summary>Sets the title a panel's tab shows, for a panel whose title follows its state, such as its problem counts.</summary>
    /// <remarks>A panel that is not shown yet shows the title when it appears.</remarks>
    /// <param name="panelId">The panel's identity.</param>
    /// <param name="title">The title.</param>
    public void SetPanelTitle(string panelId, string title);
}
