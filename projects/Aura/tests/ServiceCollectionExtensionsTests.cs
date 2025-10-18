// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.WindowManagement;
using DroidNet.Config;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Tests;

/// <summary>
/// Integration tests for <see cref="ServiceCollectionExtensions"/> validating
/// the unified <see cref="ServiceCollectionExtensions.WithAura"/> registration method.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class ServiceCollectionExtensionsTests
{
    /// <summary>
    /// Validates that <see cref="ServiceCollectionExtensions.WithAura"/> with no configuration
    /// registers only mandatory services.
    /// </summary>
    [TestMethod]
    public void WithAura_MinimalSetup_RegistersOnlyMandatoryServices()
    {
        // Arrange
        var services = new ServiceCollection();

        // Act
        _ = services.WithAura();
        var provider = services.BuildServiceProvider();

        // Assert - Mandatory services should be registered
        _ = provider.GetService<IWindowFactory>().Should().NotBeNull("IWindowFactory is mandatory");
        _ = provider.GetService<IWindowContextFactory>().Should().NotBeNull("IWindowContextFactory is mandatory");
        _ = provider.GetService<IWindowManagerService>().Should().NotBeNull("IWindowManagerService is mandatory");

        // Assert - Optional services should NOT be registered
        _ = provider.GetService<ISettingsService<WindowDecorationSettings>>().Should().BeNull("decoration settings not requested");
        _ = provider.GetService<ISettingsService<IAppearanceSettings>>().Should().BeNull("appearance settings not requested");
        _ = provider.GetService<WindowBackdropService>().Should().BeNull("backdrop service not requested");
        _ = provider.GetService<IAppThemeModeService>().Should().BeNull("theme mode service not requested");
    }

    /// <summary>
    /// Validates that <see cref="ServiceCollectionExtensions.WithAura"/> with full configuration
    /// registers all optional services.
    /// </summary>
    [TestMethod]
    public void WithAura_FullSetup_RegistersAllOptionalServices()
    {
        // Arrange
        var services = new ServiceCollection();

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
        _ = provider.GetService<ISettingsService<WindowDecorationSettings>>().Should().NotBeNull();
        _ = provider.GetService<ISettingsService<IAppearanceSettings>>().Should().NotBeNull();
        _ = provider.GetService<WindowBackdropService>().Should().NotBeNull();
        _ = provider.GetService<IAppThemeModeService>().Should().NotBeNull();
    }

    /// <summary>
    /// Validates that settings services are registered as interface only (no dual registration).
    /// </summary>
    /// <remarks>
    /// This test ensures compliance with the Config module pattern: settings services must be
    /// registered ONLY as <c>ISettingsService&lt;T&gt;</c> interface to prevent multiple instances.
    /// </remarks>
    [TestMethod]
    public void WithAura_SettingsServices_RegisteredAsInterfaceOnly()
    {
        // Arrange
        var services = new ServiceCollection();

        // Act
        _ = services.WithAura(options => options
            .WithDecorationSettings()
            .WithAppearanceSettings());

        // Assert - Check registration descriptors
        var decorationSettingsDescriptor = services.FirstOrDefault(d =>
            d.ServiceType == typeof(ISettingsService<WindowDecorationSettings>));

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
    }

    /// <summary>
    /// Validates that menu providers registered separately are resolvable from enumerable.
    /// </summary>
    [TestMethod]
    public void WithAura_MenuProviders_RegisteredSeparately_AreResolvable()
    {
        // Arrange
        var services = new ServiceCollection();

        // Act - Register Aura services
        _ = services.WithAura();

        // Register menu providers separately (as documented pattern)
        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.MainMenu", () => throw new NotImplementedException("Test provider")));
        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.ContextMenu", () => throw new NotImplementedException("Test provider")));

        var provider = services.BuildServiceProvider();

        // Assert - All menu providers should be resolvable
        var menuProviders = provider.GetServices<IMenuProvider>().ToList();
        _ = menuProviders.Should().HaveCount(2);
        _ = menuProviders.Should().Contain(p => p.ProviderId == "App.MainMenu");
        _ = menuProviders.Should().Contain(p => p.ProviderId == "App.ContextMenu");
    }

    /// <summary>
    /// Validates that custom window factory registration works correctly.
    /// </summary>
    [TestMethod]
    public void WithAura_CustomWindowFactory_IsRegistered()
    {
        // Arrange
        var services = new ServiceCollection();

        // Act
        _ = services.WithAura(options => options
            .WithCustomWindowFactory<TestWindowFactory>());

        var provider = services.BuildServiceProvider();

        // Assert
        var factory = provider.GetService<IWindowFactory>();
        _ = factory.Should().NotBeNull();
        _ = factory.Should().BeOfType<TestWindowFactory>();
    }

    /// <summary>
    /// Validates that <see cref="ServiceCollectionExtensions.AddWindow{TWindow}"/>
    /// still works with the new <see cref="ServiceCollectionExtensions.WithAura"/> method.
    /// </summary>
    [TestMethod]
    public void AddWindow_WorksWithWithAura()
    {
        // Arrange
        var services = new ServiceCollection();

        // Act
        _ = services.WithAura();
        _ = services.AddWindow<Window>();

        var provider = services.BuildServiceProvider();

        // Assert
        var window = provider.GetService<Window>();
        _ = window.Should().NotBeNull();
    }

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
    public void WithAura_PartialConfiguration_RegistersOnlyRequestedServices()
    {
        // Arrange
        var services = new ServiceCollection();

        // Act - Only enable decoration settings and backdrop service
        _ = services.WithAura(options => options
            .WithDecorationSettings()
            .WithBackdropService());

        var provider = services.BuildServiceProvider();

        // Assert - Requested services should be registered
        _ = provider.GetService<ISettingsService<WindowDecorationSettings>>().Should().NotBeNull();
        _ = provider.GetService<WindowBackdropService>().Should().NotBeNull();

        // Assert - Non-requested services should NOT be registered
        _ = provider.GetService<ISettingsService<IAppearanceSettings>>().Should().BeNull();
        _ = provider.GetService<IAppThemeModeService>().Should().BeNull();
    }

    /// <summary>
    /// Test window factory for validation purposes.
    /// </summary>
    private sealed class TestWindowFactory : IWindowFactory
    {
        public TWindow CreateWindow<TWindow>()
            where TWindow : Window
            => throw new NotImplementedException("Test factory");

        public Window CreateWindow(string windowTypeName)
            => throw new NotImplementedException("Test factory");

        public bool TryCreateWindow<TWindow>(out TWindow? window)
            where TWindow : Window
        {
            window = null;
            return false;
        }
    }
}
