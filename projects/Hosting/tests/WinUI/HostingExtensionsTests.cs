// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.WinUI;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Uint tests for the <see cref="HostingExtensions" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class HostingExtensionsTests
{
    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension prefers to use the
    /// context in the builder's Properties when available.
    /// </summary>
    [TestMethod]
    [TestCategory("Context")]
    public void WhenProvidedWithContextItUsesIt()
    {
        var builder = Host.CreateDefaultBuilder();

        // Setup and provision the hosting context for the User Interface
        // service.
        builder.Properties.Add(
            HostingExtensions.HostingContextKey,
            new HostingContext() { IsLifetimeLinked = false });

        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        _ = host.Should().NotBeNull();

        var context = host.Services.GetRequiredService<HostingContext>();
        _ = context.IsLifetimeLinked.Should().BeFalse();
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension creates and uses a
    /// default hosting context when not provided with one.
    /// </summary>
    [TestMethod]
    [TestCategory("Context")]
    public void WhenNotProvidedWithContextItUsesDefault()
    {
        var builder = Host.CreateDefaultBuilder();
        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        _ = host.Should().NotBeNull();

        var context = host.Services.GetRequiredService<HostingContext>();
        _ = context.IsLifetimeLinked.Should().BeTrue("the default is true");
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension registers an instance
    /// of <see cref="UserInterfaceThread" /> in the Dependency Injector
    /// service provider.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void RegistersUserInterfaceThread()
    {
        var builder = Host.CreateDefaultBuilder();
        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        _ = host.Should().NotBeNull();

        var act = () => _ = host.Services.GetRequiredService<IUserInterfaceThread>();

        _ = act.Should().NotThrow();
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension registers an instance
    /// of <see cref="UserInterfaceHostedService" /> as a IHostedService in the
    /// Dependency Injector service provider.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void RegistersUserInterfaceHostedService()
    {
        var builder = Host.CreateDefaultBuilder();
        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        _ = host.Should().NotBeNull();

        var uiService = host.Services.GetServices<IHostedService>()
            .First(service => service is UserInterfaceHostedService);

        _ = uiService.Should().NotBeNull();
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension registers an instance
    /// of the application class in the Dependency Injector service provider,
    /// which can be found either using the specific type or the base type <see cref="Application" />.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void RegistersApplication()
    {
        var builder = Host.CreateDefaultBuilder().ConfigureWinUI<MyApp>();

        _ = builder.ConfigureServices((_, services) => services.Should().Contain(s => s.ServiceType == typeof(MyApp)));
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension can also work with an
    /// application type that is exactly <see cref="Application" />.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void RegistersApplicationWithBaseType()
    {
        var builder = Host.CreateDefaultBuilder().ConfigureWinUI<MyApp>();

        _ = builder.ConfigureServices(
            (_, services) => services.Should().Contain(s => s.ServiceType == typeof(Application)));
    }
}

internal sealed class MyApp : Application;
