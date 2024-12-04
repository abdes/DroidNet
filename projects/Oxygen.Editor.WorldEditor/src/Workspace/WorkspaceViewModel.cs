// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.ProjectExplorer;
using Oxygen.Editor.WorldEditor.ViewModels;
using Oxygen.Editor.WorldEditor.Views;

namespace Oxygen.Editor.WorldEditor.Workspace;

public partial class WorkspaceViewModel(IContainer container, IRouter router, ILoggerFactory? loggerFactory = null)
    : DockingWorkspaceViewModel(container, router, loggerFactory)
{
    /// <inheritdoc/>
    public override object ThisViewModel => this;

    /// <inheritdoc/>
    public override IRoutes RoutesConfig { get; } = new Routes(
        [
        new Route
        {
            Path = string.Empty,
            Children = new Routes(
            [
                new Route()
                {
                    Outlet = "renderer",
                    Path = "dx",
                    ViewModelType = typeof(RendererViewModel),
                },
                new Route
                {
                    Outlet = "pe",
                    Path = "pe",
                    MatchMethod = PathMatch.Full,
                    ViewModelType = typeof(ProjectExplorerViewModel),
                },
                new Route
                {
                    Outlet = "cb",
                    Path = "cb",
                    MatchMethod = PathMatch.Full,
                    ViewModelType = typeof(ContentBrowserViewModel),
                },
                new Route
                {
                    Outlet = "props",
                    Path = "props",
                    MatchMethod = PathMatch.Full,
                    ViewModelType = typeof(SceneDetailsViewModel),
                },
                new Route
                {
                    Outlet = "log",
                    Path = "log",
                    MatchMethod = PathMatch.Full,
                    ViewModelType = typeof(LogsViewModel),
                },
            ]),
        },
    ]);

    /// <inheritdoc/>
    protected override string CenterOutletName => "renderer";

    /// <inheritdoc/>
    protected override void OnSetupChildContainer(IContainer childContainer)
    {
        childContainer.Register<RendererViewModel>(Reuse.Transient);
        childContainer.Register<RendererView>(Reuse.Singleton);
        childContainer.Register<ProjectExplorerViewModel>(Reuse.Transient);
        childContainer.Register<ProjectExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        childContainer.Register<SceneDetailsViewModel>(Reuse.Transient);
        childContainer.Register<SceneDetailsView>(Reuse.Transient);
        childContainer.Register<LogsViewModel>(Reuse.Singleton);
        childContainer.Register<LogsView>(Reuse.Singleton);
    }

    /// <inheritdoc/>
    protected override Task OnInitialNavigationAsync(LocalRouterContext context)
        => context.LocalRouter.NavigateAsync("/(renderer:dx//pe:pe;right;w=300//props:props;bottom=pe//cb:cb;bottom;h=300//log:log;with=cb)");
}
