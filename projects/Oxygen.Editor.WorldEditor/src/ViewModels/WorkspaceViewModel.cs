// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ViewModels;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Docking.Workspace;
using DryIoc;
using Microsoft.UI.Xaml;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// The view model for the World Editor main view.
/// </summary>
public sealed partial class WorkspaceViewModel : ObservableObject, IDisposable
{
    private readonly Docker docker;

    [ObservableProperty]
    private UIElement? workspaceContent;

    // TODO Move the DockViewFactory in the World Editor to the Oxygen.Editor project
    /* container.Register<DockViewFactory>(Reuse.Singleton); */

    public WorkspaceViewModel(IResolver resolver)
    {
        var dockViewFactory = new DockViewFactory(resolver);
        this.docker = new Docker();
        RestoreDefaultWorkspace(resolver, this.docker);

        var layout = new GridFlowLayout(dockViewFactory);

        this.UpdateContent(this.docker, layout);

        this.docker.LayoutChanged += (_, args) =>
        {
            if (args.Reason is LayoutChangeReason.Docking)
            {
                this.UpdateContent(this.docker, layout);
            }
        };
    }

    public void Dispose() => this.docker.Dispose();

    private static void RestoreDefaultWorkspace(IResolver resolver, Docker docker)
    {
        docker.Dock(CenterDock.New(), new Anchor(AnchorPosition.Center));

        var projectExplorer = ToolDock.New();
        var dockable = Dockable.New("pe");
        dockable.TabbedTitle = "Project Explorer";
        dockable.ViewModel = resolver.Resolve<Func<IDockable, ProjectExplorerViewModel>>()(dockable);
        dockable.PreferredWidth = new Width(300);
        projectExplorer.AdoptDockable(dockable);
        docker.Dock(projectExplorer, new AnchorRight());

        var details = ToolDock.New();
        dockable = Dockable.New("details");
        dockable.TabbedTitle = "Properties";
        dockable.ViewModel = resolver.Resolve<Func<IDockable, SceneDetailsViewModel>>()(dockable);
        dockable.PreferredWidth = new Width(300);
        details.AdoptDockable(dockable);
        docker.Dock(details, new AnchorBottom(projectExplorer.Dockables[0]));

        var contentBrowser = ToolDock.New();
        dockable = Dockable.New("ce");
        dockable.TabbedTitle = " Content Browser";
        dockable.ViewModel = resolver.Resolve<Func<IDockable, ContentBrowserViewModel>>()(dockable);
        dockable.PreferredHeight = new Height(400);
        contentBrowser.AdoptDockable(dockable);
        dockable = Dockable.New("output");
        dockable.TabbedTitle = "Output";
        dockable.ViewModel = resolver.Resolve<Func<IDockable, LogsViewModel>>()(dockable);
        contentBrowser.AdoptDockable(dockable);
        docker.Dock(contentBrowser, new AnchorBottom());

        docker.DumpWorkspace();
    }

    private void UpdateContent(Docker docker, GridFlowLayout layout)
    {
        docker.Layout(layout);
        var content = layout.CurrentGrid;
        this.WorkspaceContent = content as UIElement ??
                                throw new InvalidOperationException(
                                    "the provided layout engine does not produce a UIElement");
    }
}
