// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Theming;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
/// Unit tests for the <see cref="AppThemeModeService"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
[TestCategory("AppThemeModeServiceTests")]
public partial class AppThemeModeServiceTests : VisualUserInterfaceTests, IDisposable
{
    private Mock<ISettingsService<IAppearanceSettings>> mockAppearanceSettings = null!;
    private Mock<IWindowManagerService> mockWindowManager = null!;
    private HostingContext hostingContext = null!;
    private AppThemeModeService service = null!;
    private Window? testWindow;
    private bool isDisposed;

    public required TestContext TestContext { get; set; }

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(() =>
    {
        this.mockAppearanceSettings = new Mock<ISettingsService<IAppearanceSettings>>();
        this.mockWindowManager = new Mock<IWindowManagerService>();
        _ = this.mockWindowManager.Setup(m => m.OpenWindows).Returns([]);
        _ = this.mockWindowManager.Setup(m => m.WindowEvents).Returns(System.Reactive.Linq.Observable.Empty<WindowLifecycleEvent>());

        var dispatcher = DispatcherQueue.GetForCurrentThread();
        this.hostingContext = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };

        this.service = new AppThemeModeService(
            this.hostingContext,
            this.mockAppearanceSettings.Object,
            this.mockWindowManager.Object,
            NullLoggerFactory.Instance);
    });

    [TestCleanup]
    public Task CleanupAsync() => EnqueueAsync(() =>
    {
        this.testWindow?.Close();
        this.testWindow = null;
    });

    [TestMethod]
    public Task ApplyThemeToWindow_SetsLightTheme() => EnqueueAsync(() =>
    {
        // Arrange
        this.testWindow = new Window { Content = new Grid() };

        // Act
        this.service.ApplyThemeToWindow(this.testWindow, ElementTheme.Light);

        // Assert
        var content = this.testWindow.Content as FrameworkElement;
        _ = content.Should().NotBeNull();
        _ = content!.RequestedTheme.Should().Be(ElementTheme.Light);
    });

    [TestMethod]
    public Task ApplyThemeToWindow_SetsDarkTheme() => EnqueueAsync(() =>
    {
        // Arrange
        this.testWindow = new Window { Content = new Grid() };

        // Act
        this.service.ApplyThemeToWindow(this.testWindow, ElementTheme.Dark);

        // Assert
        var content = this.testWindow.Content as FrameworkElement;
        _ = content.Should().NotBeNull();
        _ = content!.RequestedTheme.Should().Be(ElementTheme.Dark);
    });

    [TestMethod]
    public Task ApplyThemeToWindow_DefaultTheme_ResolvesToSystemTheme() => EnqueueAsync(() =>
    {
        // Arrange
        this.testWindow = new Window { Content = new Grid() };

        // Act
        this.service.ApplyThemeToWindow(this.testWindow, ElementTheme.Default);

        // Assert
        var content = this.testWindow.Content as FrameworkElement;
        _ = content.Should().NotBeNull();

        var expectedTheme = Application.Current.RequestedTheme == ApplicationTheme.Light
            ? ElementTheme.Light
            : ElementTheme.Dark;
        _ = content!.RequestedTheme.Should().Be(expectedTheme);
    });

    [TestMethod]
    public Task ApplyThemeToWindow_ThrowsException_WhenContentIsNull() => EnqueueAsync(() =>
    {
        // Arrange
        this.testWindow = new Window { Content = null };

        // Act
        var act = () => this.service.ApplyThemeToWindow(this.testWindow, ElementTheme.Light);

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("window");
    });

    [TestMethod]
    public Task ApplyThemeToWindow_UpdatesApplicationResources() => EnqueueAsync(() =>
    {
        // Arrange
        this.testWindow = new Window { Content = new Grid() };

        // Act
        this.service.ApplyThemeToWindow(this.testWindow, ElementTheme.Light);

        // Assert
        _ = Application.Current.Resources.Should().ContainKey("AppTheme");
        _ = Application.Current.Resources["AppTheme"].Should().Be(ElementTheme.Light);
    });

    [TestMethod]
    public Task Dispose_CanBeCalledMultipleTimes() => EnqueueAsync(() =>
    {
        // Act
        this.service.Dispose();
        var act = () => this.service.Dispose();

        // Assert
        _ = act.Should().NotThrow();
    });

    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!this.isDisposed)
        {
            if (disposing)
            {
            }

            this.service?.Dispose();
            this.isDisposed = true;
        }
    }
}
