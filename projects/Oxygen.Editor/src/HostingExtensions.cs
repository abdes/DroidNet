// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System.Reflection;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Extensions.Logging;

public static class HostingExtensions
{
    public static IHostBuilder ConfigureLogging(this IHostBuilder builder) =>
        builder.ConfigureContainer<DryIocServiceProvider>(
            (_, serviceProvider) =>
            {
                var container = serviceProvider.Container;
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
            });

    public static IHostBuilder ConfigureAutoInjected(this IHostBuilder builder) => builder.ConfigureServices(
        (context, services) =>
        {
            _ = context; // unused

            // Get all loaded assemblies
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                // Look for the AutoInjectExtensions class
                var autoInjectType = assembly.GetTypes()
                    .FirstOrDefault(
                        t => t.IsClass && t.IsSealed && t.IsAbstract && string.Equals(
                            t.Name,
                            "AutoInjectExtensions",
                            StringComparison.Ordinal));

                if (autoInjectType != null)
                {
                    // Look for the UseAutoInject method
                    var useAutoInjectMethod = autoInjectType.GetMethod(
                        "UseAutoInject",
                        BindingFlags.Public | BindingFlags.Static,
                        binder: null,
                        [typeof(IServiceCollection)],
                        modifiers: null);

                    // Invoke the UseAutoInject method on the given services
                    _ = useAutoInjectMethod?.Invoke(null, [services]);
                }
            }
        });
}
