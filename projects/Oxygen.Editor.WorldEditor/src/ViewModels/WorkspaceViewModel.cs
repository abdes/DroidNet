// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ViewModels;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Docking.Workspace;
using DroidNet.Hosting.Generators;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// The view model for the World Editor main view.
/// </summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class WorkspaceViewModel : ObservableObject
{
    [ObservableProperty]
    private UIElement? workspaceContent;

    public WorkspaceViewModel(IResolver resolver)
    {
        var dockViewFactory = new DockViewFactory(resolver);
        var docker = new Docker();
        RestoreDefaultWorkspace(resolver, docker);

        var layout = new GridFlowLayout(dockViewFactory);

        this.UpdateContent(docker, layout);

        docker.LayoutChanged += (_, args) =>
        {
            if (args.Reason is LayoutChangeReason.Docking)
            {
                this.UpdateContent(docker, layout);
            }
        };
    }

    private static void RestoreDefaultWorkspace(IResolver resolver, Docker docker)
    {
        docker.Dock(CenterDock.New(), new Anchor(AnchorPosition.Center));

        var projectExplorer = ToolDock.New();
        var dockable = Dockable.New("pe");
        dockable.ViewModel = resolver.Resolve<Func<IDockable, ProjectExplorerViewModel>>()(dockable);
        dockable.PreferredWidth = new Width(300);
        projectExplorer.AdoptDockable(dockable);
        docker.Dock(projectExplorer, new AnchorRight());

        var details = ToolDock.New();
        dockable = Dockable.New("details");
        dockable.ViewModel = resolver.Resolve<Func<IDockable, SceneDetailsViewModel>>()(dockable);
        dockable.PreferredWidth = new Width(300);
        details.AdoptDockable(dockable);
        docker.Dock(details, new AnchorBottom(projectExplorer.Dockables[0]));

        var contentBrowser = ToolDock.New();
        dockable = Dockable.New("ce");
        dockable.ViewModel = resolver.Resolve<Func<IDockable, ContentBrowserViewModel>>()(dockable);
        dockable.PreferredHeight = new Height(200);
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
