// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Tests.WinUI;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Hosting.WinUI;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Uint tests for the <see cref="WinUiHostingExtensions" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class WinUiHostingExtensionsTests
{
    private readonly IHost defaultHost;

    public WinUiHostingExtensionsTests()
        => this.defaultHost = Host.CreateDefaultBuilder()
            .ConfigureServices(sc => sc.ConfigureWinUI<MyApp>(isLifetimeLinked: true))
            .Build();

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension prefers to use the
    /// context in the builder's Properties when available.
    /// </summary>
    [TestMethod]
    [TestCategory("Context")]
    public void ConfigureWinUI_UsesProvidedIsLifetimeLinkedValue()
    {
        var builder = Host.CreateDefaultBuilder();

        var host = builder.ConfigureServices(sc => sc.ConfigureWinUI<MyApp>(isLifetimeLinked: false))
            .Build();
        _ = host.Should().NotBeNull();

        var context = host.Services.GetRequiredService<HostingContext>();
        _ = context.IsLifetimeLinked.Should().BeFalse();
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension registers an instance
    /// of <see cref="UserInterfaceThread" /> in the Dependency Injector
    /// service provider.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void ConfigureWinUI_RegistersUserInterfaceThread()
    {
        var act = () => _ = this.defaultHost.Services.GetRequiredService<IUserInterfaceThread>();

        _ = act.Should().NotThrow();
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension registers an instance
    /// of <see cref="UserInterfaceHostedService" /> as a IHostedService in the
    /// Dependency Injector service provider.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void ConfigureWinUI_RegistersUserInterfaceHostedService()
    {
        var uiService = this.defaultHost.Services.GetServices<IHostedService>()
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
    public void ConfigureWinUI_RegistersApplication()
    {
        var builder = Host.CreateDefaultBuilder()
            .ConfigureServices(sc => sc.ConfigureWinUI<MyApp>(isLifetimeLinked: true));

        _ = builder.ConfigureServices((_, services) => services.Should().Contain(s => s.ServiceType == typeof(MyApp)));
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension can also work with an
    /// application type that is exactly <see cref="Application" />.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void ConfigureWinUI_RegistersApplicationWithBaseType()
    {
        var builder = Host.CreateDefaultBuilder()
            .ConfigureServices(sc => sc.ConfigureWinUI<MyApp>(isLifetimeLinked: true));

        _ = builder.ConfigureServices(
            (_, services) => services.Should().Contain(s => s.ServiceType == typeof(Application)));
    }
}

/// <summary>
/// A dummy <see cref="Application" /> for testing.
/// </summary>
[ExcludeFromCodeCoverage]
public sealed partial class MyApp : Application;
