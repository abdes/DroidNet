// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Demo;

using DroidNet.Hosting.Demo.Services;
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

    public static IHostBuilder ConfigureDemoServices(this IHostBuilder builder) =>
        builder.ConfigureServices(
            (_, services) =>
                services.AddScoped<ITestInterface, TestService>());
}
