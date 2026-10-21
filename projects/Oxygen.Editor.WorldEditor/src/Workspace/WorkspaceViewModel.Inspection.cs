// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Routes the Content Browser's requests to show assets and to inspect cooked output to their owners.</summary>
public partial class WorkspaceViewModel
{
    private IAssetReveal? assetReveal;
    private ICookedInspections? cookedInspections;

    private async Task<bool> ShowInspectionAssetAsync(ShowAssetRequestMessage request)
        => this.projectContextService.ActiveProject is { } project
            && request.Project.ProjectId == project.ProjectId
            && request.Project.ProjectRoot == project.ProjectRoot
            && this.assetReveal is { } reveal
            && await reveal.RevealAsync(request.Assets).ConfigureAwait(true);

    // The window's Content Browser shows the assets that parts ask to reveal.
    private void RegisterAssetReveal(IContainer childContainer)
    {
        childContainer.Register<ContentBrowserAssetReveal>(Reuse.Singleton);
        childContainer.RegisterMapping<IAssetReveal, ContentBrowserAssetReveal>();
        this.assetReveal = childContainer.Resolve<IAssetReveal>();
    }

    // An inspection request sets the panel's scope and shows the panel; a request for the shown scope inspects it again.
    private Task<bool> OpenInspectionAsync(OpenCookedInspectionRequestMessage request)
        => ReferenceEquals(request.Project, this.projectContextService.ActiveProject) && this.cookedInspections is { } inspections
            ? inspections.InspectAsync(new CookedInspectionRequest(request.Scope, request.Validate)
            {
                Asset = request.Asset,
                CookedSource = request.CookedSource,
            })
            : Task.FromResult(false);
}
