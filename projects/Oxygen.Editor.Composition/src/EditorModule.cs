// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura;
using DroidNet.Composition;
using DroidNet.Config;
using DroidNet.Coordinates;
using DroidNet.Routing;
using DryIoc;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Diagnostics;
using Oxygen.Editor.DocumentWindows;
using Oxygen.Editor.LivePreview;
using Oxygen.Editor.Materials;
using Oxygen.Editor.ProjectExplorer;
using Oxygen.Editor.ProjectExplorer.ViewModels;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Textures;
using Oxygen.Editor.Workspace;

namespace Oxygen.Editor;

/// <summary>
///     The editor scope, the root of every scope of the editor: the host's settings and windowing, and the modules of
///     the subsystems whose services every project and window shares.
/// </summary>
/// <remarks>
///     The application host registers them into its container once it is built, before it initializes the settings and
///     runs; the topology tests compose the same registrations over a host-equivalent container. The editor scope's
///     handle is registered after its modules, which then create their activation lists.
/// </remarks>
public static class EditorModule
{
    // The modules of the editor scope, in registration and activation order.
    private static readonly IScopeModule<IApplicationLayer>[] Modules =
    [
        new ShellModule(),
        new ProjectsModule(),
        new ProjectExplorerModule(),
        new ContentPipelineModule(),
        new LivePreviewModule(),
        new MaterialsModule(),
        new TexturesModule(),
        new SceneEditorSettingsModule(),
        new ApplicationModule(),
        new EditorDataModule(),
        new DiagnosticsModule(),
        new EngineModule(),
        new WorkspaceModule(),
        new DocumentWindowsModule(),
    ];

    /// <summary>
    ///     Creates the routes of the application's root router: the Project Explorer, the workspace window and the
    ///     document windows.
    /// </summary>
    /// <returns>The routes.</returns>
    public static Routes CreateRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            ViewModelType = typeof(MainShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    Path = "we",
                    MatchMethod = PathMatch.Prefix,
                    ViewModelType = typeof(WorkspaceViewModel),
                },
                new Route
                {
                    Path = "doc",
                    MatchMethod = PathMatch.Prefix,
                    ViewModelType = typeof(DocumentWindowViewModel),
                },
                new Route
                {
                    Path = "pe",
                    MatchMethod = PathMatch.Prefix,
                    ViewModelType = typeof(MainViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = "home",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(HomeViewModel),
                        },
                        new Route
                        {
                            Path = "new",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(NewProjectViewModel),
                        },
                        new Route
                        {
                            Path = "open",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(OpenProjectViewModel),
                        },
                        /* TODO: uncomment after settings page is refactored
                        new Route()
                        {
                            Path = "settings",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(SettingsViewModel),
                        },
                        */
                    ]),
                },
            ]),
        },
    ]);

    /// <summary>
    ///     Registers the editor scope's services into the host's container.
    /// </summary>
    /// <param name="container">The container the host built.</param>
    public static void Register(IContainer container)
    {
        ArgumentNullException.ThrowIfNull(container);

        // The host's settings and windowing extend its container directly.
        _ = container.WithConfig();
        _ = container
            .WithAura(options => options
                .WithAppearanceSettings()
                .WithDecorationSettings()
                .WithBackdropService()
                .WithChromeService()
                .WithThemeModeService()
                .WithDrag())
            .WithSpatialMapping();
        _ = container.WithSettings<IEngineStartupSettings, EngineStartupSettingsService>();

        // Every scope of the editor is created under the root's handle.
        _ = ScopeFactory.CreateRoot(container, Modules);
    }
}
