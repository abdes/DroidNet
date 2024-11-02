// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo;

using System.Globalization;
using DroidNet.Controls.Demo.DemoBrowser;
using DroidNet.Controls.Demo.DynamicTree;
using DroidNet.Controls.OutputLog;
using DroidNet.Controls.OutputLog.Theming;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Serilog;
using Serilog.Events;
using Serilog.Extensions.Logging;
using Serilog.Templates;

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
        // Set up the view model to view converters. We're using the standard
        // converter, and a custom one with fall back if the view cannot be
        // located.
        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        container.Register<DemoBrowserView>(Reuse.Singleton);
        container.Register<DemoBrowserViewModel>(Reuse.Singleton);
        container.Register<ProjectLayoutView>(Reuse.Singleton);
        container.Register<ProjectLayoutViewModel>(Reuse.Singleton);
    }

    private static void CreateLogger(IContainer container) =>

        // https://nblumhardt.com/2021/06/customize-serilog-text-output/
        Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .MinimumLevel.Override("Microsoft", LogEventLevel.Information)
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /* .WriteTo.Seq("http://localhost:5341/") */
            .WriteTo.OutputLogView<RichTextBlockSink>(container, theme: Themes.Literate)
            .CreateLogger();
}
