// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Layout;

/// <summary>Placement in a docking workspace window: documents go to its document area, panels are its dockables.</summary>
/// <param name="documents">The window's document service.</param>
/// <param name="docker">The window's docker.</param>
/// <param name="contributions">The window's panels, which keep the titles their features set.</param>
public sealed class DockingPlacementService(IDocumentService documents, IDocker docker, ContributionRegistry contributions) : IPlacementService
{
    /// <inheritdoc/>
    public Task<DocumentPlacement> OpenDocumentAsync(AssetPath asset, WindowPlacement placement = WindowPlacement.Current, CancellationToken cancellationToken = default)
        => documents.OpenAsync(asset, cancellationToken);

    /// <inheritdoc/>
    /// <remarks>A minimized panel's dock is pinned, and the panel becomes the active one of its dock.</remarks>
    public Task<bool> ShowPanelAsync(string panelId, WindowPlacement placement = WindowPlacement.Current)
    {
        ArgumentException.ThrowIfNullOrEmpty(panelId);
        if (Dockable.FromId(panelId) is not { Owner: { } dock } dockable)
        {
            return Task.FromResult(false);
        }

        docker.PinDock(dock);
        dockable.IsActive = true;
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public async Task<bool> ShowPanelsAsync(IReadOnlyList<string> panelIds, WindowPlacement placement = WindowPlacement.Current)
    {
        ArgumentNullException.ThrowIfNull(panelIds);
        var dockables = panelIds.Select(Dockable.FromId).ToArray();
        for (var index = 0; index < dockables.Length - 1; index++)
        {
            if (dockables[index] is { } earlier && dockables.Skip(index + 1).Any(later => later is not null && ReferenceEquals(later.Owner, earlier.Owner)))
            {
                this.MoveToOwnDock(earlier);
            }
        }

        var shown = true;
        foreach (var panelId in panelIds)
        {
            shown &= await this.ShowPanelAsync(panelId, placement).ConfigureAwait(true);
        }

        return shown;
    }

    /// <inheritdoc/>
    public void SetPanelTitle(string panelId, string title)
    {
        ArgumentException.ThrowIfNullOrEmpty(panelId);
        contributions.SetTitle(panelId, title);
        if (Dockable.FromId(panelId) is { } dockable)
        {
            dockable.Title = title;
        }
    }

    private void MoveToOwnDock(Dockable dockable)
    {
        if (dockable.Owner is not { } shared)
        {
            return;
        }

        shared.DisownDockable(dockable);
        var dock = ToolDock.New();
        dock.AdoptDockable(dockable);
        var anchor = new Anchor(AnchorPosition.Right);
        try
        {
            docker.Dock(dock, anchor);
            anchor = null;
        }
        finally
        {
            anchor?.Dispose();
        }
    }
}
