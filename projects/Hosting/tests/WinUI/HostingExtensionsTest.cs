// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.WinUI;

using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Uint tests for the <see cref="HostingExtensions" /> class.
/// </summary>
[TestClass]
public class HostingExtensionsTest
{
    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension prefers to use the
    /// context in the builder's Properties when available.
    /// </summary>
    [TestMethod]
    [TestCategory("Context")]
    public void WhenProvidedWithContextItUsesIt()
    {
        var builder = new HostApplicationBuilder();

        // Setup and provision the hosting context for the User Interface
        // service.
        ((IHostApplicationBuilder)builder).Properties.Add(
            HostingExtensions.HostingContextKey,
            new HostingContext() { IsLifetimeLinked = false });

        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        host.Should().NotBeNull();

        var context = host.Services.GetRequiredService<HostingContext>();
        context.IsLifetimeLinked.Should().BeFalse();
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension creates and uses a
    /// default hosting context when not provided with one.
    /// </summary>
    [TestMethod]
    [TestCategory("Context")]
    public void WhenNotProvidedWithContextItUsesDefault()
    {
        var builder = new HostApplicationBuilder();
        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        host.Should().NotBeNull();

        var context = host.Services.GetRequiredService<HostingContext>();
        context.IsLifetimeLinked.Should().BeTrue("the default is true");
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
        var builder = new HostApplicationBuilder();
        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        host.Should().NotBeNull();

        var act = () => _ = host.Services.GetRequiredService<IUserInterfaceThread>();

        act.Should().NotThrow();
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
        var builder = new HostApplicationBuilder();
        var host = builder.ConfigureWinUI<MyApp>()
            .Build();
        host.Should().NotBeNull();

        var uiService = host.Services.GetServices<IHostedService>()
            .First(service => service is UserInterfaceHostedService);

        uiService.Should().NotBeNull();
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
        var builder = new HostApplicationBuilder().ConfigureWinUI<MyApp>();

        builder.Services.Count(desc => desc.ServiceType.IsAssignableFrom(typeof(MyApp))).Should().Be(2);
    }

    /// <summary>
    /// Tests that the <c>ConfigureWinUI()</c> extension can also work with an
    /// application type that is exactly <see cref="Application" />.
    /// </summary>
    [TestMethod]
    [TestCategory("Dependency Injector")]
    public void RegistersApplicationWithBaseType()
    {
        var builder = new HostApplicationBuilder().ConfigureWinUI<MyApp>();

        builder.Services.Single(desc => desc.ServiceType.IsAssignableFrom(typeof(Application))).Should().NotBeNull();
    }
}

internal sealed class MyApp : Application;
