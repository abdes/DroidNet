// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
/// Unit tests for the <see cref="AppThemeModeService"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class AppThemeModeServiceTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task ApplyThemeMode_SetsLightTheme_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appearanceSettings = new Mock<ISettingsService<AppearanceSettings>>();
        var service = new AppThemeModeService(appearanceSettings.Object);
        var window = new Window
        {
            Content = new Grid(),
        };

        try
        {
            // Act
            service.ApplyThemeMode(window, ElementTheme.Light);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            var content = window.Content as FrameworkElement;
            _ = content.Should().NotBeNull();
            _ = content!.RequestedTheme.Should().Be(ElementTheme.Light);
        }
        finally
        {
            window.Close();
            service.Dispose();
        }
    });

    [TestMethod]
    public Task ApplyThemeMode_SetsDarkTheme_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appearanceSettings = new Mock<ISettingsService<AppearanceSettings>>();
        var service = new AppThemeModeService(appearanceSettings.Object);
        var window = new Window
        {
            Content = new Grid(),
        };

        try
        {
            // Act
            service.ApplyThemeMode(window, ElementTheme.Dark);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            var content = window.Content as FrameworkElement;
            _ = content.Should().NotBeNull();
            _ = content!.RequestedTheme.Should().Be(ElementTheme.Dark);
        }
        finally
        {
            window.Close();
            service.Dispose();
        }
    });

    [TestMethod]
    public Task ApplyThemeMode_DefaultTheme_ResolvesToSystemTheme_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appearanceSettings = new Mock<ISettingsService<AppearanceSettings>>();
        var service = new AppThemeModeService(appearanceSettings.Object);
        var window = new Window
        {
            Content = new Grid(),
        };

        try
        {
            // Act
            service.ApplyThemeMode(window, ElementTheme.Default);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            var content = window.Content as FrameworkElement;
            _ = content.Should().NotBeNull();

            var expectedTheme = Application.Current.RequestedTheme == ApplicationTheme.Light
                ? ElementTheme.Light
                : ElementTheme.Dark;
            _ = content!.RequestedTheme.Should().Be(expectedTheme);
        }
        finally
        {
            window.Close();
            service.Dispose();
        }
    });

    [TestMethod]
    public Task ApplyThemeMode_ThrowsException_WhenContentIsNotFrameworkElement_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appearanceSettings = new Mock<ISettingsService<AppearanceSettings>>();
        var service = new AppThemeModeService(appearanceSettings.Object);
        var window = new Window
        {
            Content = null,
        };

        try
        {
            // Act
            var act = () => service.ApplyThemeMode(window, ElementTheme.Light);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = act.Should().Throw<ArgumentException>()
                .WithParameterName("window");
        }
        finally
        {
            window.Close();
            service.Dispose();
        }
    });

    [TestMethod]
    public Task ApplyThemeMode_UpdatesApplicationResources_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appearanceSettings = new Mock<ISettingsService<AppearanceSettings>>();
        var service = new AppThemeModeService(appearanceSettings.Object);
        var window = new Window
        {
            Content = new Grid(),
        };

        try
        {
            // Act
            service.ApplyThemeMode(window, ElementTheme.Light);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = Application.Current.Resources.Should().ContainKey("AppTheme");
            _ = Application.Current.Resources["AppTheme"].Should().Be(ElementTheme.Light);
        }
        finally
        {
            window.Close();
            service.Dispose();
        }
    });

    [TestMethod]
    public Task Dispose_UnsubscribesFromPropertyChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appearanceSettings = new Mock<ISettingsService<AppearanceSettings>>();
        var service = new AppThemeModeService(appearanceSettings.Object);

        // Act
        service.Dispose();
        await WaitForRenderAsync().ConfigureAwait(true);

        // Assert - Multiple disposes should not throw
        var act = () => service.Dispose();
        _ = act.Should().NotThrow();
    });

    private static async Task WaitForRenderAsync() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);
}
