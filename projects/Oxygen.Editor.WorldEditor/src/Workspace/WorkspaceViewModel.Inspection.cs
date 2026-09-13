// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Docking;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Reveals explicitly requested source and dependency identities from an inspection document.</summary>
public partial class WorkspaceViewModel
{
    /// <inheritdoc />
    public async Task<bool> InspectAsync(CookRunSnapshot run)
    {
        if (this.projectContextService.ActiveProject is not { } project || project.ProjectId != run.ProjectId || this.messenger is null)
        {
            return false;
        }

        var scope = run.Request.TargetKind == CookTargetKind.Project ? null : run.Request.ScopeUri;
        var request = this.messenger.Send(new OpenCookedInspectionRequestMessage(project, scope, validate: false));
        return request.HasReceivedResponse && await request.Response.ConfigureAwait(true);
    }

    private async Task<bool> ShowInspectionAssetAsync(ShowAssetRequestMessage request)
    {
        if (this.projectContextService.ActiveProject is not { } project
            || request.Project.ProjectId != project.ProjectId
            || !string.Equals(request.Project.ProjectRoot, project.ProjectRoot, StringComparison.OrdinalIgnoreCase)
            || Dockable.FromId("cb") is not { ViewModel: ContentBrowserViewModel browser } dockable
            || !await browser.ShowAssetAsync(request.AssetUri).ConfigureAwait(true))
        {
            return false;
        }

        this.RevealTool(dockable);
        return true;
    }
}
