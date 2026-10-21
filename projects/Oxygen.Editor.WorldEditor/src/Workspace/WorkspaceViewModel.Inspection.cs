// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Routes the Content Browser's requests to inspect cooked output, and registers the ports it implements.</summary>
public partial class WorkspaceViewModel
{
    private ICookedInspections? cookedInspections;

    // The window's Content Browser shows the assets that parts ask to reveal, and creates the scripts they ask for.
    private void RegisterContentBrowserPorts(IContainer childContainer)
    {
        childContainer.Register<ContentBrowserAssetReveal>(Reuse.Singleton);
        childContainer.RegisterMapping<IAssetReveal, ContentBrowserAssetReveal>();
        childContainer.Register<ContentBrowserScriptCreation>(Reuse.Singleton);
        childContainer.RegisterMapping<IScriptCreation, ContentBrowserScriptCreation>();
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
