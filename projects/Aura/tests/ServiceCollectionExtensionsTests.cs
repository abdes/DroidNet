// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Settings;
using DroidNet.Aura.WindowManagement;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
/// Integration tests for <see cref="DependencyInjectionExtensions"/> validating
/// the unified <see cref="DependencyInjectionExtensions.WithAura"/> registration method.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class ServiceCollectionExtensionsTests : VisualUserInterfaceTests
{
    /// <summary>
    /// Registers common test dependencies required by Aura services.
    /// </summary>
    /// <param name="services">The service collection to configure.</param>
    private static void RegisterTestDependencies(IServiceCollection services)
    {
        // Register logging as application concern
        services.AddLogging();

        // Register HostingContext required by WindowManagerService
        var dispatcherQueue = DispatcherQueue.GetForCurrentThread();
        var hostingContext = new HostingContext
        {
            Dispatcher = dispatcherQueue,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcherQueue),
        };
        services.AddSingleton(hostingContext);

        // Register IFileSystem required by settings services (using mock)
        var mockFileSystem = new Mock<IFileSystem>();
        services.AddSingleton(mockFileSystem.Object);

        // Register IPathFinder required by settings services (using mock)
        var mockPathFinder = new Mock<IPathFinder>();
        mockPathFinder.Setup(pf => pf.GetConfigFilePath(It.IsAny<string>()))
            .Returns<string>(fileName => Path.Combine(Path.GetTempPath(), fileName));
        services.AddSingleton(mockPathFinder.Object);
    }

    /// <summary>
    /// Validates that <see cref="DependencyInjectionExtensions.WithAura"/> with no configuration
    /// registers only mandatory services.
    /// </summary>
    [TestMethod]
    public Task WithAura_MinimalSetup_RegistersOnlyMandatoryServices() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);

        // Act
        _ = services.WithAura();
        var provider = services.BuildServiceProvider();

        // Assert - Mandatory services should be registered
        _ = provider.GetService<IWindowFactory>().Should().NotBeNull("IWindowFactory is mandatory");
        _ = provider.GetService<IWindowContextFactory>().Should().NotBeNull("IWindowContextFactory is mandatory");
        _ = provider.GetService<IWindowManagerService>().Should().NotBeNull("IWindowManagerService is mandatory");

        // Assert - Optional services should NOT be registered
        _ = provider.GetService<ISettingsService<IWindowDecorationSettings>>().Should().BeNull("decoration settings not requested");
        _ = provider.GetService<ISettingsService<IAppearanceSettings>>().Should().BeNull("appearance settings not requested");
        _ = provider.GetService<WindowBackdropService>().Should().BeNull("backdrop service not requested");
        _ = provider.GetService<IAppThemeModeService>().Should().BeNull("theme mode service not requested");

        await Task.CompletedTask;
    });

    /// <summary>
    /// Validates that <see cref="DependencyInjectionExtensions.WithAura"/> with full configuration
    /// registers all optional services.
    /// </summary>
    [TestMethod]
    public Task WithAura_FullSetup_RegistersAllOptionalServices() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);

        // Act
        _ = services.WithAura(options => options
            .WithDecorationSettings()
            .WithAppearanceSettings()
            .WithBackdropService()
            .WithThemeModeService());

        var provider = services.BuildServiceProvider();

        // Assert - All services should be registered
        _ = provider.GetService<IWindowFactory>().Should().NotBeNull();
        _ = provider.GetService<IWindowContextFactory>().Should().NotBeNull();
        _ = provider.GetService<IWindowManagerService>().Should().NotBeNull();
        _ = provider.GetService<ISettingsService<IWindowDecorationSettings>>().Should().NotBeNull();
        _ = provider.GetService<ISettingsService<IAppearanceSettings>>().Should().NotBeNull();
        _ = provider.GetService<WindowBackdropService>().Should().NotBeNull();
        _ = provider.GetService<IAppThemeModeService>().Should().NotBeNull();

        await Task.CompletedTask;
    });

    /// <summary>
    /// Validates that settings services are registered as interface only (no dual registration).
    /// </summary>
    /// <remarks>
    /// This test ensures compliance with the Config module pattern: settings services must be
    /// registered ONLY as <c>ISettingsService&lt;T&gt;</c> interface to prevent multiple instances.
    /// </remarks>
    [TestMethod]
    public Task WithAura_SettingsServices_RegisteredAsInterfaceOnly() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);

        // Act
        _ = services.WithAura(options => options
            .WithDecorationSettings()
            .WithAppearanceSettings());

        // Assert - Check registration descriptors
        var decorationSettingsDescriptor = services.FirstOrDefault(d =>
            d.ServiceType == typeof(ISettingsService<IWindowDecorationSettings>));

        var appearanceSettingsDescriptor = services.FirstOrDefault(d =>
            d.ServiceType == typeof(ISettingsService<IAppearanceSettings>));

        _ = decorationSettingsDescriptor.Should().NotBeNull("decoration settings should be registered");
        _ = decorationSettingsDescriptor!.Lifetime.Should().Be(ServiceLifetime.Singleton);

        _ = appearanceSettingsDescriptor.Should().NotBeNull("appearance settings should be registered");
        _ = appearanceSettingsDescriptor!.Lifetime.Should().Be(ServiceLifetime.Singleton);

        // Ensure concrete types are NOT registered separately
        var concreteDecorationDescriptor = services.FirstOrDefault(d =>
            d.ServiceType == typeof(WindowDecorationSettingsService));
        var concreteAppearanceDescriptor = services.FirstOrDefault(d =>
            d.ServiceType == typeof(AppearanceSettingsService));

        _ = concreteDecorationDescriptor.Should().BeNull("concrete decoration settings type should not be registered separately");
        _ = concreteAppearanceDescriptor.Should().BeNull("concrete appearance settings type should not be registered separately");

        await Task.CompletedTask;
    });

    /// <summary>
    /// Validates that menu providers registered separately are resolvable from enumerable.
    /// </summary>
    [TestMethod]
    public Task WithAura_MenuProviders_RegisteredSeparately_AreResolvable() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);

        // Act - Register Aura services
        _ = services.WithAura();

        // Register menu providers separately (as documented pattern)
#pragma warning disable MA0025 // Implement the functionality - These are test stubs
        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.MainMenu", () => throw new NotImplementedException("Test provider")));
        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.ContextMenu", () => throw new NotImplementedException("Test provider")));
#pragma warning restore MA0025

        var provider = services.BuildServiceProvider();

        // Assert - All menu providers should be resolvable
        var menuProviders = provider.GetServices<IMenuProvider>().ToList();
        _ = menuProviders.Should().HaveCount(2);
        _ = menuProviders.Should().Contain(p => p.ProviderId == "App.MainMenu");
        _ = menuProviders.Should().Contain(p => p.ProviderId == "App.ContextMenu");

        await Task.CompletedTask;
    });

    /// <summary>
    /// Validates that custom window factory registration works correctly.
    /// </summary>
    [TestMethod]
    public Task WithAura_CustomWindowFactory_IsRegistered() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);
        services.AddLogging(); // Register logging as application concern

        // Act
        _ = services.WithAura(options => options
            .WithCustomWindowFactory<TestWindowFactory>());

        var provider = services.BuildServiceProvider();

        // Assert
        var factory = provider.GetService<IWindowFactory>();
        _ = factory.Should().NotBeNull();
        _ = factory.Should().BeOfType<TestWindowFactory>();

        await Task.CompletedTask;
    });

    /// <summary>
    /// Validates that <see cref="DependencyInjectionExtensions.AddWindow{TWindow}"/>
    /// still works with the new <see cref="DependencyInjectionExtensions.WithAura"/> method.
    /// </summary>
    [TestMethod]
    public Task AddWindow_WorksWithWithAura() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);

        // Act
        _ = services.WithAura();
        _ = services.AddWindow<Window>();

        var provider = services.BuildServiceProvider();

        // Assert - Create window instance on UI thread
        var window = provider.GetService<Window>();
        _ = window.Should().NotBeNull();

        await Task.CompletedTask;
    });

    /// <summary>
    /// Validates that fluent configuration methods return the same instance for chaining.
    /// </summary>
    [TestMethod]
    public void AuraOptions_FluentMethods_ReturnSameInstance()
    {
        // Arrange
        var options = new AuraOptions();

        // Act & Assert - Each method should return the same instance
        var result1 = options.WithDecorationSettings();
        _ = result1.Should().BeSameAs(options);

        var result2 = options.WithAppearanceSettings();
        _ = result2.Should().BeSameAs(options);

        var result3 = options.WithBackdropService();
        _ = result3.Should().BeSameAs(options);

        var result4 = options.WithThemeModeService();
        _ = result4.Should().BeSameAs(options);

        var result5 = options.WithCustomWindowFactory<TestWindowFactory>();
        _ = result5.Should().BeSameAs(options);
    }

    /// <summary>
    /// Validates that partial optional service configuration works correctly.
    /// </summary>
    [TestMethod]
    public Task WithAura_PartialConfiguration_RegistersOnlyRequestedServices() => EnqueueAsync(async () =>
    {
        // Arrange
        var services = new ServiceCollection();
        RegisterTestDependencies(services);

        // Act - Only enable decoration settings and backdrop service
        _ = services.WithAura(options => options
            .WithDecorationSettings()
            .WithBackdropService());

        var provider = services.BuildServiceProvider();

        // Assert - Requested services should be registered
        _ = provider.GetService<ISettingsService<IWindowDecorationSettings>>().Should().NotBeNull();
        _ = provider.GetService<WindowBackdropService>().Should().NotBeNull();

        // Assert - Non-requested services should NOT be registered
        _ = provider.GetService<ISettingsService<IAppearanceSettings>>().Should().BeNull();
        _ = provider.GetService<IAppThemeModeService>().Should().BeNull();

        await Task.CompletedTask;
    });

    /// <summary>
    /// Test window factory for validation purposes.
    /// </summary>
    private sealed class TestWindowFactory : IWindowFactory
    {
#pragma warning disable MA0025 // Implement the functionality - This is a test stub
        public TWindow CreateWindow<TWindow>()
            where TWindow : Window
            => throw new NotImplementedException("Test factory");

        public Window CreateWindow(string windowTypeName)
            => throw new NotImplementedException("Test factory");
#pragma warning restore MA0025

        public bool TryCreateWindow<TWindow>(out TWindow? window)
            where TWindow : Window
        {
            window = null;
            return false;
        }
    }
}
