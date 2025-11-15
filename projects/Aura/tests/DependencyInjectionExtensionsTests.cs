// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Drag;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Theming;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DryIoc;

namespace DroidNet.Aura.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class DependencyInjectionExtensionsTests
{
    [TestMethod]
    public void WithAura_WithDrag_Registers_DragServices()
    {
        // Arrange
        using var container = new Container();

        // Act
        _ = container.WithAura(options => options.WithDrag());

        // Assert
        _ = container.IsRegistered<IDragVisualService>().Should().BeTrue();
        _ = container.IsRegistered<ITabDragCoordinator>().Should().BeTrue();
    }

    [TestMethod]
    public void WithAura_WithoutDrag_DoesNotRegister_DragServices()
    {
        // Arrange
        using var container = new Container();

        // Act
        _ = container.WithAura(options => options
            .WithAppearanceSettings()
            .WithBackdropService());

        // Assert
        _ = container.IsRegistered<IDragVisualService>().Should().BeFalse();
        _ = container.IsRegistered<ITabDragCoordinator>().Should().BeFalse();
    }

    [TestMethod]
    public void WithAura_WithDecorationSettings_Registers_DecorationSettingsService()
    {
        // Arrange
        using var container = new Container();

        // Act
        _ = container.WithAura(options => options.WithDecorationSettings());

        // Assert
        _ = container.IsRegistered<ISettingsService<IWindowDecorationSettings>>().Should().BeTrue();
    }

    [TestMethod]
    public void WithAura_WithoutDecorationSettings_DoesNotRegister_DecorationSettingsService()
    {
        using var container = new Container();
        _ = container.WithAura();
        _ = container.IsRegistered<ISettingsService<IWindowDecorationSettings>>().Should().BeFalse();
    }

    [TestMethod]
    public void WithAura_WithAppearanceSettings_Registers_AppearanceSettingsService()
    {
        using var container = new Container();
        _ = container.WithAura(options => options.WithAppearanceSettings());
        _ = container.IsRegistered<ISettingsService<IAppearanceSettings>>().Should().BeTrue();
    }

    [TestMethod]
    public void WithAura_WithBackdropService_Registers_WindowBackdropService()
    {
        using var container = new Container();
        _ = container.WithAura(options => options.WithBackdropService());
        _ = container.IsRegistered<WindowBackdropService>().Should().BeTrue();
    }

    [TestMethod]
    public void WithAura_WithChromeService_Registers_WindowChromeService()
    {
        using var container = new Container();
        _ = container.WithAura(options => options.WithChromeService());
        _ = container.IsRegistered<WindowChromeService>().Should().BeTrue();
    }

    [TestMethod]
    public void WithAura_WithThemeModeService_Registers_ThemeModeService()
    {
        using var container = new Container();
        _ = container.WithAura(options => options.WithThemeModeService());
        _ = container.IsRegistered<IAppThemeModeService>().Should().BeTrue();
    }

    [TestMethod]
    public void WithAura_WithCustomWindowFactory_Registers_CustomFactory()
    {
        using var container = new Container();

        // Act
        _ = container.WithAura(options => options.WithCustomWindowFactory<TestWindowFactory>());

        // Assert
        _ = container.IsRegistered<IWindowFactory>().Should().BeTrue();
        var resolved = container.Resolve<IWindowFactory>();
        _ = resolved.Should().BeOfType<TestWindowFactory>();
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "Used as a type argument for DI registration tests; not directly instantiated.")]
    private sealed class TestWindowFactory : IWindowFactory
    {
        public Task<TWindow> CreateWindow<TWindow>(IReadOnlyDictionary<string, object>? metadata = null)
            where TWindow : Microsoft.UI.Xaml.Window => Task.FromResult<TWindow>(null!);

        public Task<Microsoft.UI.Xaml.Window> CreateWindow(string key, IReadOnlyDictionary<string, object>? metadata = null)
            => Task.FromResult<Microsoft.UI.Xaml.Window>(null!);

        public Task<TWindow> CreateDecoratedWindow<TWindow>(WindowCategory category, IReadOnlyDictionary<string, object>? metadata = null)
            where TWindow : Microsoft.UI.Xaml.Window => Task.FromResult<TWindow>(null!);
    }
}
