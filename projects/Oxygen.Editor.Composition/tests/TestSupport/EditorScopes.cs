// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Composition;
using DroidNet.Config;
using DroidNet.Controls.OutputConsole.Model;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DroidNet.TestHelpers;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ContentExplorer;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Layout;
using Oxygen.Editor.Shell.Scopes;
using Oxygen.Editor.Workspace;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Composition.UI.Tests.TestSupport;

/// <summary>
///     The editor's scopes as the application builds them: the editor scope over a host-equivalent container, a workspace
///     window's scope and the Content Explorer's scope under it.
/// </summary>
/// <remarks>
///     <para>
///     The editor scope is the host container with the bootstrapper's services (early services, MVVM, the application
///     router, WinUI hosting) and <see cref="EditorModule" />'s registrations, with its settings initialized, as
///     <c>Program</c> composes them.
///     </para>
///     <para>
///     The window and panel scopes are created through the editor's <see cref="ScopeFactory" />, with the modules their
///     view models use and in the same order, so the factory's undeclared-shadow check runs on them. Their activation is
///     left out: it starts the preview, the engine and the window's document area, which need a running window. The
///     topology checks that every activated service is provided by its scope.
///     </para>
/// </remarks>
internal sealed class EditorScopes : IAsyncDisposable
{
    private EditorScopes(IContainer host, IReadOnlySet<int> hostFactories)
    {
        this.Host = host;
        this.HostFactories = hostFactories;
        this.Root = host.Resolve<ScopeHandle>();
        this.Factory = host.Resolve<ScopeFactory>();
        this.Features = [.. host.Resolve<IEnumerable<IFeatureModule>>()];
    }

    /// <summary>Gets the host container, which holds the editor scope.</summary>
    public IContainer Host { get; }

    /// <summary>Gets the factories the host registers before the editor's: the bootstrapper's.</summary>
    public IReadOnlySet<int> HostFactories { get; }

    /// <summary>Gets the handle of the editor scope.</summary>
    public ScopeHandle Root { get; }

    /// <summary>Gets the editor's scope factory.</summary>
    public ScopeFactory Factory { get; }

    /// <summary>Gets the features of the workspace windows.</summary>
    public IReadOnlyList<IFeatureModule> Features { get; }

    /// <summary>Creates the editor scope.</summary>
    /// <returns>The editor's scopes.</returns>
    public static async Task<EditorScopes> CreateAsync()
    {
        var host = HostContainer.Create(services => services.ConfigureWinUI<App>(isLifetimeLinked: false));

        // The bootstrapper's early services (with the output console's log buffer), MVVM and the application's router.
        var fileSystem = new MockFileSystem();
        host.RegisterInstance(new OutputLogBuffer());
        host.RegisterInstance<IFileSystem>(fileSystem);
        host.RegisterInstance<IPathFinder>(new PathFinder(fileSystem, new PathFinderConfig(PathFinder.DevelopmentMode, "DroidNet", "Oxygen Editor Composition Tests")));
        host.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        host.Register<ViewModelToView>(Reuse.Singleton);
        host.RegisterDelegate((Func<IResolverContext, IValueConverter>)(c => c.Resolve<ViewModelToView>()), serviceKey: "VmToView");
        host.ConfigureRouter(EditorModule.CreateRoutes());
        var hostFactories = host.GetServiceRegistrations().Select(static registration => registration.Factory.FactoryID).ToHashSet();

        EditorModule.Register(host);

        // The host initializes the settings before anything resolves them; here without the user's settings files.
        await host.Resolve<SettingsManager>().InitializeAsync().ConfigureAwait(false);
        return new EditorScopes(host, hostFactories);
    }

    /// <summary>Gets the registrations of the editor scope that the editor makes, without the bootstrapper's.</summary>
    /// <returns>The editor's registrations in the editor scope.</returns>
    public IReadOnlyList<ServiceRegistrationInfo> EditorRegistrations()
        => [.. ScopeTopology.Owned(this.Root).Where(registration => !this.HostFactories.Contains(registration.Factory.FactoryID))];

    /// <summary>Gets the modules of a workspace window's scope, as the workspace view model creates them.</summary>
    /// <returns>The window's modules, in registration and activation order.</returns>
    public IReadOnlyList<IScopeModule<IWindowLayer>> WindowModules()
        => [new RouterScopeModule(new Routes([]), new LocalRouterContext(new object())), new DockingWindowModule(), new WorkspaceWindowModule(this.Features), .. this.Features];

    /// <summary>Gets the modules of the Content Explorer's scope, as the explorer creates them.</summary>
    /// <returns>The explorer's modules, in registration and activation order.</returns>
    public static IReadOnlyList<IScopeModule<IPanelLayer>> ExplorerModules()
        => [new RouterScopeModule(new Routes([]), new LocalRouterContext(new object())), new ContentExplorerPanelModule()];

    /// <summary>Creates a workspace window's scope under the editor scope.</summary>
    /// <returns>The window's scope.</returns>
    public ScopeHandle CreateWindow()
        => this.Factory.Create<IWindowLayer>(
            this.Root,
            this.WindowModules().Select(static module => new RegistrationsOf<IWindowLayer>(module)),
            window => window.RegisterInstance(new WindowId(1)));

    /// <summary>Creates the Content Explorer's scope under a window's scope.</summary>
    /// <param name="window">The window's scope.</param>
    /// <returns>The explorer's scope.</returns>
    public ScopeHandle CreateExplorer(ScopeHandle window)
        => this.Factory.Create<IPanelLayer>(
            window,
            ExplorerModules().Select(static module => new RegistrationsOf<IPanelLayer>(module)),
            panel => panel.RegisterInstance(new ContentExplorerState()));

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        await this.Root.DisposeAsync().ConfigureAwait(false);
        this.Host.Dispose();
    }

    /// <summary>A module's registrations and shadows, without its activation.</summary>
    /// <typeparam name="TLayer">The module's layer.</typeparam>
    /// <param name="module">The module.</param>
    private sealed class RegistrationsOf<TLayer>(IScopeModule<TLayer> module) : IScopeModule<TLayer>
        where TLayer : IScopeLayer
    {
        public IReadOnlyCollection<Type> Shadows => module.Shadows;

        public IReadOnlyList<Type> Activation => [];

        public void Register(IRegistrator scope) => module.Register(scope);
    }
}
