// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Docking;
using DryIoc;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Reveals explicitly requested source and dependency identities from an inspection document.</summary>
public partial class WorkspaceViewModel
{
    /// <summary>The dock id of the Cooked Inspector panel.</summary>
    internal const string InspectorDockId = "inspect";

    private Oxygen.Editor.World.Inspection.CookedInspectionViewModel? cookedInspector;

    /// <inheritdoc />
    public async Task<bool> InspectAsync(CookRunSnapshot run)
    {
        if (this.projectContextService.ActiveProject is not { } project || project.ProjectId != run.ProjectId
            || project.ProjectRoot != run.ProjectRoot || this.messenger is null)
        {
            return false;
        }

        var scope = run.Request.TargetKind == CookTargetKind.Project ? null : run.Request.ScopeUri;
        var request = this.messenger.Send(new OpenCookedInspectionRequestMessage(project, scope, validate: false)
        {
            Asset = run.Request.TargetKind is CookTargetKind.Asset or CookTargetKind.CurrentScene ? scope : null,
            DisplayName = run.DisplayName,
        });
        return request.HasReceivedResponse && await request.Response.ConfigureAwait(true);
    }

    /// <inheritdoc />
    public async Task<bool> ShowImportedAssetsAsync(CookRunSnapshot run)
    {
        if (this.projectContextService.ActiveProject is not { } project || project.ProjectId != run.ProjectId
            || project.ProjectRoot != run.ProjectRoot
            || run.ImportedOutputs.IsEmpty || Dockable.FromId("cb") is not { ViewModel: ContentBrowserViewModel browser } dockable
            || !await browser.ShowAssetsAsync(run.ImportedOutputs).ConfigureAwait(true))
        {
            return false;
        }

        this.RevealTool(dockable);
        return true;
    }

    private async Task<bool> ShowInspectionAssetAsync(ShowAssetRequestMessage request)
    {
        if (this.projectContextService.ActiveProject is not { } project
            || request.Project.ProjectId != project.ProjectId
            || request.Project.ProjectRoot != project.ProjectRoot
            || Dockable.FromId("cb") is not { ViewModel: ContentBrowserViewModel browser } dockable
            || !await (request.Assets is [var single]
                ? browser.ShowAssetAsync(single)
                : browser.ShowAssetsAsync(request.Assets)).ConfigureAwait(true))
        {
            return false;
        }

        this.RevealTool(dockable);
        return true;
    }

    // One Cooked Inspector per workspace; it shows its asset links in this workspace's Content Browser.
    private void RegisterCookedInspector(IContainer childContainer)
    {
        childContainer.RegisterDelegate(
            resolver => new Oxygen.Editor.World.Inspection.CookedInspectionViewModel(
                resolver.Resolve<IContentPipelineService>(),
                resolver.Resolve<Oxygen.Editor.Assets.IAssetFeed>(),
                resolver.Resolve<Oxygen.Editor.Projects.IProjectContextService>(),
                async uri =>
                {
                    if (this.projectContextService.ActiveProject is not { } project || this.messenger is null)
                    {
                        return false;
                    }

                    var request = this.messenger.Send(new ShowAssetRequestMessage(project, uri));
                    return request.HasReceivedResponse && await request.Response.ConfigureAwait(true);
                }),
            Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.World.Inspection.CookedInspectionView>(Reuse.Transient);
        this.cookedInspector = childContainer.Resolve<Oxygen.Editor.World.Inspection.CookedInspectionViewModel>();
    }

    // An inspection request sets the panel's scope and shows the panel; a request for the shown scope inspects it again.
    private Task<bool> OpenInspectionAsync(OpenCookedInspectionRequestMessage request)
    {
        if (this.cookedInspector is not { } inspector || !ReferenceEquals(request.Project, this.projectContextService.ActiveProject))
        {
            return Task.FromResult(false);
        }

        _ = inspector.InspectAsync(new Oxygen.Editor.World.Inspection.CookedInspectionScope(request.Project, request.Scope, request.Validate)
        {
            Asset = request.Asset,
            CookedSource = request.CookedSource,
        });
        if (Dockable.FromId(InspectorDockId) is { } dockable)
        {
            this.RevealTool(dockable);
        }

        return Task.FromResult(true);
    }
}
