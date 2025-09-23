// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.ProjectExplorer;
using Oxygen.Editor.WorldEditor.PropertiesEditor;
using Oxygen.Editor.WorldEditor.Routing;
using Oxygen.Editor.WorldEditor.ViewModels;
using Oxygen.Editor.WorldEditor.Views;

namespace Oxygen.Editor.WorldEditor.Workspace;

/// <summary>
///     The ViewModel of the world editor docking workspace.
/// </summary>
/// <param name="container">The IoC container that should be used for resolving view models, views, and service.</param>
/// <param name="router">The router using to navigate within the docking workspace.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the
///     recognition process. If <see langword="null" />, logging is disabled.
/// </param>
/// <remarks>
///     The world editor uses a child IoC container and a child router. This gurantees that routes and
///     resolutions are isolated from the rest of the application, and that the workspace can be easily
///     replaced or extended by other modules. In the other hand, this also requires that resolutions
///     inside the workspace must always use the child container, and that navigations, even the absolute
///     ones, will always be relative to the workspace.
/// </remarks>
public partial class WorkspaceViewModel(IContainer container, IRouter router, ILoggerFactory? loggerFactory = null)
    : DockingWorkspaceViewModel(container, router, loggerFactory)
{
    /// <inheritdoc />
    protected override object ThisViewModel => this;

    /// <inheritdoc />
    protected override IRoutes RoutesConfig { get; } = new Routes(
    [
        new Route
        {
            Path = string.Empty,
            Children = new Routes(
            [
                new Route { Outlet = "renderer", Path = "dx", ViewModelType = typeof(RendererViewModel) },
                new Route
                {
                    Outlet = "pe",
                    Path = "pe",
                    MatchMethod = PathMatch.Full,
                    ViewModelType = typeof(SceneExplorerViewModel),
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
                    ViewModelType = typeof(SceneNodeEditorViewModel),
                },
                new Route
                {
                    Outlet = "log", Path = "log", MatchMethod = PathMatch.Full, ViewModelType = typeof(LogsViewModel),
                },
            ]),
        },
    ]);

    /// <inheritdoc />
    protected override string CenterOutletName => "renderer";

    /// <inheritdoc />
    protected override void OnSetupChildContainer(IContainer childContainer)
    {
        childContainer.Register<IMessenger, StrongReferenceMessenger>(Reuse.Singleton);

        childContainer.Register<RendererViewModel>(Reuse.Transient);
        childContainer.Register<RendererView>(Reuse.Singleton);
        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        childContainer.Register<LogsViewModel>(Reuse.Singleton);
        childContainer.Register<LogsView>(Reuse.Singleton);

        childContainer.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        childContainer.Register<SceneNodeEditorView>(Reuse.Transient);
        childContainer.Register<TransformViewModel>(Reuse.Transient);
        childContainer.Register<TransformView>(Reuse.Transient);
    }

    /// <inheritdoc />
    protected override Task OnInitialNavigationAsync(ILocalRouterContext context)
        => context.LocalRouter.NavigateAsync(
            "/(renderer:dx//pe:pe;right;w=300//props:props;bottom=pe//cb:cb;bottom;h=300//log:log;with=cb)");
}
