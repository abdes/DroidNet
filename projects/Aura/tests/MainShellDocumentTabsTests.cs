// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Resources;
using DroidNet.Tests;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>A window's shell shows the document tabs of the source the application binds to the window.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class MainShellDocumentTabsTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task TheShellFollowsTheTabSourceBoundToItsWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var managed = new Mock<IManagedWindow>();
        _ = managed.SetupProperty(context => context.DocumentTabSource);
        using var viewModel = this.CreateViewModel();
        viewModel.Context = managed.Object;
        var changes = new List<string?>();
        viewModel.PropertyChanged += (_, args) => changes.Add(args.PropertyName);
        var tabs = Mock.Of<IDocumentTabSource>();

        // Act: the application binds the window's tabs once it knows what the window shows.
        managed.Object.DocumentTabSource = tabs;
        managed.Raise(context => context.PropertyChanged += null, new PropertyChangedEventArgs(nameof(IManagedWindow.DocumentTabSource)));

        // Assert
        _ = viewModel.DocumentTabSource.Should().BeSameAs(tabs);
        _ = changes.Should().Contain(nameof(MainShellViewModel.DocumentTabSource));

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task AWindowWithoutATabSourceShowsNoTabs_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = this.CreateViewModel();

        viewModel.Context = Mock.Of<IManagedWindow>();

        _ = viewModel.DocumentTabSource.Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    private MainShellViewModel CreateViewModel()
    {
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };
        var settings = new Mock<ISettingsService<IAppearanceSettings>>();
        _ = settings.SetupGet(service => service.Settings).Returns(Mock.Of<IAppearanceSettings>());
        return new MainShellViewModel(hosting, new AssetResolverService(this.LoggerFactory), settings.Object, Mock.Of<IWindowManagerService>());
    }
}
