// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DroidNet.Routing;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using DryIoc;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ProjectExplorer.Activation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Services;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Services;

namespace Oxygen.Editor;

/// <summary>
///     The application's own services in the editor scope: its paths and storage, its activation, the activation of
///     projects and their sessions, the placement of its windows, the windows themselves, what closing them returns to
///     or ends, and the scope factory every scope is created by.
/// </summary>
internal sealed class ApplicationModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        scope.Register<IOxygenPathFinder, OxygenPathFinder>(Reuse.Singleton);
        scope.Register<NativeStorageProvider>(Reuse.Singleton);
        scope.Register<IActivationService, ActivationService>(Reuse.Singleton);

        // Window placement service, which saves a window's placement when it closes.
        scope.Register<WindowPlacementService, WindowPlacementService>(Reuse.Singleton);
        scope.RegisterMapping<IWindowCloseParticipant, WindowPlacementService>();

        RegisterStorage(scope);
        scope.Register<IProjectActivationCoordinator, ProjectActivationCoordinator>(Reuse.Singleton);

        // The open project's session, under which its windows open. Closing its last window returns to the Start
        // window; closing the editor's last window otherwise exits it.
        scope.Register<ProjectSessionHost>(Reuse.Singleton);
        scope.RegisterMapping<IProjectSessionHost, ProjectSessionHost>();
        scope.RegisterMapping<ISessionWindows, ProjectSessionHost>();
        scope.Register<IWindowCloseParticipant, SessionWindowClose>(Reuse.Singleton);
        scope.Register<IWindowCloseParticipant, EditorExit>(Reuse.Singleton);

        RegisterWindows(scope);
        scope.Register<MainShellViewModel>(Reuse.Transient);
        scope.Register<MainShellView>(Reuse.Transient);

        // Every scope of the editor is created under the root's handle.
        scope.Register<ScopeFactory>(Reuse.Singleton);
    }

    private static void RegisterStorage(IRegistrator scope)
    {
        scope.RegisterDelegate<IMemoryCache>(_ => new MemoryCache(new MemoryCacheOptions()), Reuse.Singleton, setup: Setup.With(preventDisposal: true));

        // TODO: use keyed registration and parameter name to key mappings
        // https://github.com/dadhi/DryIoc/blob/master/docs/DryIoc.Docs/SpecifyDependencyAndPrimitiveValues.md#complete-example-of-matching-the-parameter-name-to-the-service-key
        scope.Register<IStorageProvider, NativeStorageProvider>(Reuse.Singleton);
        scope.RegisterDelegate(context => context.Resolve<IStorageProvider>().AtomicFiles, Reuse.Singleton);
    }

    private static void RegisterWindows(IRegistrator scope)
    {
        /*
         * Configure the Application's Windows. Each window represents a target in which to open the
         * requested url. Now we create the instances via the registered IWindowFactory and annotate
         * them with the window category that matches the target Name. This enables the Aura window
         * manager to apply category-specific decoration.
         */

        // In Oxygen Editor, we don't really have a "Main" window - all windows are created equally.
        scope.Register<RoutedWindow>(Reuse.Transient);

        // Workspace window
        scope.RegisterDelegate<Window>(
            resolverContext =>
            {
                var windowFactory = resolverContext.Resolve<IWindowFactory>();
                return windowFactory.CreateDecoratedWindow<RoutedWindow>(EditorWindows.WorkspaceCategory).GetAwaiter().GetResult();
            },
            Reuse.Transient,
            serviceKey: new Target { Name = EditorWindows.WorkspaceTarget });

        // Document window, one for each tab torn out of the session's windows
        scope.RegisterDelegate<Window>(
            resolverContext =>
            {
                var windowFactory = resolverContext.Resolve<IWindowFactory>();
                return windowFactory.CreateDecoratedWindow<RoutedWindow>(EditorWindows.DocumentCategory).GetAwaiter().GetResult();
            },
            Reuse.Transient,
            serviceKey: new Target { Name = EditorWindows.DocumentTarget });

        // Project Explorer window
        scope.RegisterDelegate<Window>(
            resolverContext =>
            {
                var windowFactory = resolverContext.Resolve<IWindowFactory>();
                return windowFactory.CreateDecoratedWindow<RoutedWindow>(EditorWindows.StartCategory).GetAwaiter().GetResult();
            },
            Reuse.Transient,
            serviceKey: new Target { Name = EditorWindows.StartTarget });
    }
}
