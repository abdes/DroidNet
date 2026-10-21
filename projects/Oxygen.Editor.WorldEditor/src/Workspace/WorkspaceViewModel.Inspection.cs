// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World.Inspection;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Routes the Content Browser's requests to show assets and to inspect cooked output to their owners.</summary>
public partial class WorkspaceViewModel
{
    /// <summary>The dock id of the Cooked Inspector panel.</summary>
    internal const string InspectorDockId = CookedInspections.PanelId;

    private IAssetReveal? assetReveal;
    private ICookedInspections? cookedInspections;

    private async Task<bool> ShowInspectionAssetAsync(ShowAssetRequestMessage request)
        => this.projectContextService.ActiveProject is { } project
            && request.Project.ProjectId == project.ProjectId
            && request.Project.ProjectRoot == project.ProjectRoot
            && this.assetReveal is { } reveal
            && await reveal.RevealAsync(request.Assets).ConfigureAwait(true);

    // One Cooked Inspector per workspace; it shows its asset links in this workspace's Content Browser.
    private void RegisterCookedInspector(IContainer childContainer)
    {
        childContainer.Register<ContentBrowserAssetReveal>(Reuse.Singleton);
        childContainer.RegisterMapping<IAssetReveal, ContentBrowserAssetReveal>();
        childContainer.RegisterDelegate(
            resolver => new CookedInspectionViewModel(
                resolver.Resolve<IContentPipelineService>(),
                resolver.Resolve<Oxygen.Editor.Assets.IAssetFeed>(),
                resolver.Resolve<Oxygen.Editor.Projects.IProjectContextService>(),
                uri => resolver.Resolve<IAssetReveal>().RevealAsync([uri])),
            Reuse.Singleton);
        childContainer.Register<CookedInspectionView>(Reuse.Transient);
        childContainer.Register<CookedInspections>(Reuse.Singleton);
        childContainer.RegisterMapping<ICookedInspections, CookedInspections>();
        _ = childContainer.Resolve<CookedInspectionViewModel>();
        this.assetReveal = childContainer.Resolve<IAssetReveal>();
        this.cookedInspections = childContainer.Resolve<ICookedInspections>();
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
