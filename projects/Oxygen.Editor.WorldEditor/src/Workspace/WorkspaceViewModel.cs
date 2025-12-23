// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Routing;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Output;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.World.Workspace;

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
                new Route { Outlet = "renderer", Path = "dx", ViewModelType = typeof(DocumentHostViewModel) },
                new Route
                {
                    Outlet = "se",
                    Path = "se",
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
                    Outlet = "log", Path = "log", MatchMethod = PathMatch.Full, ViewModelType = typeof(OutputViewModel),
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

        // DocumentHostViewModel must be registered and resolved first to ensure it subscribes to
        // IDocumentService events before DocumentManager starts handling open requests.
        childContainer.Register<DocumentHostViewModel>(Reuse.Singleton);
        childContainer.Register<DocumentHostView>(Reuse.Singleton);

        // Resolve DocumentHostViewModel immediately to ensure it subscribes to document events
        _ = childContainer.Resolve<DocumentHostViewModel>();

        // DocumentManager must be a singleton and resolved immediately to ensure it is always listening for messages
        // even if the router hasn't navigated to any editor yet.
        childContainer.Register<DocumentManager>(Reuse.Singleton);
        _ = childContainer.Resolve<DocumentManager>();

        // Register shared services at workspace level
        childContainer.Register<IAssetIndexingService, ContentBrowser.AssetsIndexingService>(Reuse.Singleton);

        // Register scene-engine synchronization service
        childContainer.Register<ISceneEngineSync, SceneEngineSync>(Reuse.Singleton);
        childContainer.Register<ISceneMutator, SceneMutator>(Reuse.Singleton);
        childContainer.Register<ISceneOrganizer, SceneOrganizer>(Reuse.Singleton);
        childContainer.Register<ISceneExplorerService, SceneExplorerService>(Reuse.Singleton);

        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        childContainer.Register<OutputViewModel>(Reuse.Singleton);
        childContainer.Register<OutputView>(Reuse.Transient);

        childContainer.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        childContainer.Register<SceneNodeEditorView>(Reuse.Transient);
        childContainer.Register<SceneEditorView>(Reuse.Transient);
        childContainer.Register<TransformViewModel>(Reuse.Transient);
        childContainer.Register<TransformView>(Reuse.Transient);

        childContainer.Register<GeometryViewModel>(Reuse.Transient);
        childContainer.Register<GeometryView>(Reuse.Transient);
    }

    /// <inheritdoc />
    protected override Task OnInitialNavigationAsync(ILocalRouterContext context)
        => context.LocalRouter.NavigateAsync(
            "/(renderer:dx//se:se;right;w=300//props:props;bottom=se//cb:cb;bottom;h=300//log:log;with=cb)");
}
