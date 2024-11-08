// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System;
using System.Globalization;
using System.IO.Abstractions;
using DroidNet.Controls.OutputLog;
using DroidNet.Controls.OutputLog.Theming;
using DroidNet.Docking.Controls;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.ProjectBrowser.Views;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Storage;
using Oxygen.Editor.Services;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.ProjectExplorer;
using Oxygen.Editor.WorldEditor.ViewModels;
using Oxygen.Editor.WorldEditor.Views;
using Serilog;
using Serilog.Events;
using Serilog.Extensions.Logging;
using Serilog.Templates;
using Testably.Abstractions;
using IContainer = DryIoc.IContainer;

public static class HostingExtensions
{
    public static void ConfigureLogging(this IContainer container)
    {
        CreateLogger(container);

        container.RegisterInstance<ILoggerFactory>(new SerilogLoggerFactory(Log.Logger));

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(null!)),
            setup: Setup.With(condition: r => r.Parent.ImplementationType == null));

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(Arg.Index<Type>(0)),
                r => r.Parent.ImplementationType),
            setup: Setup.With(condition: r => r.Parent.ImplementationType != null));
    }

    public static void ConfigureApplicationServices(this IContainer container)
    {
        /*
         * Register core services.
         */

        container.Register<IFileSystem, RealFileSystem>(Reuse.Singleton);
        container.Register<NativeStorageProvider>(Reuse.Singleton);
        container.Register<IPathFinder, DevelopmentPathFinder>(Reuse.Singleton); // TODO: release version
        container.Register<IActivationService, ActivationService>(Reuse.Singleton);

        /*
         * Register domain specific services.
         */

        // Register the universal template source with NO key, so it gets selected when injected an instance of ITemplateSource.
        // Register specific template source implementations KEYED. They are injected only as a collection of implementation
        // instances, only by the universal source.
        container.Register<ITemplatesSource, UniversalTemplatesSource>(reuse: Reuse.Singleton);
        container.Register<ITemplatesSource, LocalTemplatesSource>(
            reuse: Reuse.Singleton,
            serviceKey: Uri.UriSchemeFile);
        container.Register<ITemplatesService, TemplatesService>(Reuse.Singleton);

        // TODO: use keyed registration and parameter name to key mappings
        // https://github.com/dadhi/DryIoc/blob/master/docs/DryIoc.Docs/SpecifyDependencyAndPrimitiveValues.md#complete-example-of-matching-the-parameter-name-to-the-service-key
        container.Register<IStorageProvider, NativeStorageProvider>(Reuse.Singleton);
        container.Register<LocalProjectsSource>(Reuse.Singleton);
        container.Register<IProjectSource, UniversalProjectSource>(Reuse.Singleton);
        container.Register<IProjectBrowserService, ProjectBrowserService>(Reuse.Singleton);
        container.Register<IProjectManagerService, ProjectManagerService>(Reuse.Singleton);

        // Register the project instance using a delegate that will request the currently open project from the project
        // browser service.
        container.RegisterDelegate(resolverContext => resolverContext.Resolve<IProjectManagerService>().CurrentProject);

        /*
         * Set up the view model to view converters. We're using the standard converter, and a custom one with fall back
         * if the view cannot be located.
         */

        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the requested url. The
         * target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        // Views and ViewModels
        RegisterViewsAndViewModels(container);
    }

    private static void CreateLogger(IContainer container)
        => Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .MinimumLevel.Override("Microsoft", LogEventLevel.Error)
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /* .WriteTo.Seq("http://localhost:5341/") */
            .WriteTo.OutputLogView<RichTextBlockSink>(container, theme: Themes.Literate)
            .CreateLogger();

    private static void RegisterViewsAndViewModels(IContainer container)
    {
        container.Register<ShellViewModel>(Reuse.Singleton);
        container.Register<ShellView>(Reuse.Singleton);

        container.Register<MainViewModel>(Reuse.Transient);
        container.Register<MainView>(Reuse.Transient);
        container.Register<HomeViewModel>(Reuse.Transient);
        container.Register<HomeView>(Reuse.Transient);
        container.Register<NewProjectViewModel>(Reuse.Transient);
        container.Register<NewProjectView>(Reuse.Transient);
        container.Register<OpenProjectViewModel>(Reuse.Transient);
        container.Register<OpenProjectView>(Reuse.Transient);

        container.Register<WorkspaceViewModel>(Reuse.Transient);
        container.Register<WorkspaceView>(Reuse.Transient);
        container.Register<SceneDetailsView>(Reuse.Transient);
        container.Register<SceneDetailsViewModel>(Reuse.Transient);
        container.Register<RendererView>(Reuse.Transient);
        container.Register<RendererViewModel>(Reuse.Transient);
        container.Register<LogsView>(Reuse.Transient);
        container.Register<LogsViewModel>(Reuse.Transient);
        container.Register<ProjectExplorerView>(Reuse.Transient);
        container.Register<ProjectExplorerViewModel>(Reuse.Transient);
        container.Register<ContentBrowserView>(Reuse.Transient);
        container.Register<ContentBrowserViewModel>(Reuse.Transient);
    }
}
