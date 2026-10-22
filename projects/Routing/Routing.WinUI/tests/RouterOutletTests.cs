// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Routing.WinUI.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("RouterOutlet")]
[TestCategory("UITest")]
public partial class RouterOutletTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task InitializesWithNullViewModel_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet();

        // Act
        await LoadTestContentAsync(outlet).ConfigureAwait(true);

        // Assert
        _ = outlet.ViewModel.Should().BeNull();
    });

    [TestMethod]
    public Task UsesDefaultVmToViewConverter_Async() => EnqueueAsync(
    async () =>
    {
        var resources = Application.Current.Resources;
        var hadOriginal = resources.TryGetValue("VmToViewConverter", out var original);
        var converter = new TestVmToViewConverter();
        try
        {
            resources["VmToViewConverter"] = converter;
            var outlet = new RouterOutlet() { VmToViewConverter = null };
            await LoadTestContentAsync(outlet).ConfigureAwait(true);
            _ = outlet.VmToViewConverter.Should().BeSameAs(converter);
        }
        finally
        {
            if (hadOriginal)
            {
                resources["VmToViewConverter"] = original;
            }
            else
            {
                _ = resources.Remove("VmToViewConverter");
            }
        }
    });

    [TestMethod]
    public Task VisualState_Inactive_WhenViewModelIsNull_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet() { ViewModel = new TestViewModel() };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);
        var vsm = InstallCustomVisualStateManager(outlet);

        // Act
        outlet.ViewModel = null;

        // Assert
        _ = outlet.OutletContent.Should().BeNull();
        _ = vsm.GetCurrentStates(outlet).Should().Contain("Inactive");
    });

    [TestMethod]
    public Task VisualState_Normal_WhenViewModelIsSet_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet() { VmToViewConverter = new TestVmToViewConverter() };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);
        var vsm = InstallCustomVisualStateManager(outlet);

        // Act
        outlet.ViewModel = new TestViewModel();

        // Assert
        _ = outlet.OutletContent.Should().NotBeNull();
        _ = vsm.GetCurrentStates(outlet).Should().Contain(RouterOutlet.NormalVisualState);
    });

    [TestMethod]
    public Task HasOutletContentAsDescendant_InNormalState_Async() => EnqueueAsync(
        async () =>
        {
            // Setup
            const string label = "Test Label";
            var outlet = new RouterOutlet() { ViewModel = new TestViewModel { Label = label }, VmToViewConverter = new TestVmToViewConverter() };

            // Act
            await LoadTestContentAsync(outlet).ConfigureAwait(true);

            // Assert
            var contentPresenter = outlet.FindDescendant<ContentPresenter>(e => string.Equals(e.Name, RouterOutlet.ContentPresenter, StringComparison.Ordinal));
            _ = contentPresenter.Should().NotBeNull();
            _ = contentPresenter!.Content.Should().Be(outlet.OutletContent);
        });

    [TestMethod]
    public Task VisualState_Error_WhenContentIsNull_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet() { VmToViewConverter = new TestVmToViewConverter() };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);
        var vsm = InstallCustomVisualStateManager(outlet);

        // Act
        outlet.ViewModel = new TestViewModel { Label = TestVmToViewConverter.InvalidViewModelLabel, };

        // Assert
        _ = outlet.OutletContent.Should().BeNull();
        _ = vsm.GetCurrentStates(outlet).Should().Contain(RouterOutlet.ErrorVisualState);
    });

    [TestMethod]
    public Task UpdatesContent_WhenViewModelChanges_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet() { VmToViewConverter = new TestVmToViewConverter() };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);
        _ = outlet.OutletContent.Should().BeNull();

        // Act
        const string label = "Test Label";
        outlet.ViewModel = new TestViewModel { Label = label, };

        // Assert
        _ = outlet.OutletContent.Should().BeOfType<TextBlock>();
        _ = ((TextBlock)outlet.OutletContent!).Text.Should().Be(label);
    });

    [TestMethod]
    public Task UpdatesContent_WhenVmToViewConverterChanges_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        const string label = "Test Label";
        var outlet = new RouterOutlet() { ViewModel = new TestViewModel { Label = label } };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);
        _ = outlet.OutletContent.Should().BeNull();

        // Act
        outlet.VmToViewConverter = new TestVmToViewConverter();

        // Assert
        _ = outlet.OutletContent.Should().BeOfType<TextBlock>();
        _ = ((TextBlock)outlet.OutletContent!).Text.Should().Be(label);
    });

    [TestMethod]
    public Task ConvertsTheViewModelOnce_WhenLoaded_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var converter = new TestVmToViewConverter();
        var outlet = new RouterOutlet() { ViewModel = new TestViewModel { Label = "Test Label" }, VmToViewConverter = converter };

        // Act
        await LoadTestContentAsync(outlet).ConfigureAwait(true);

        // Assert
        _ = converter.Conversions.Should().Be(1);
    });

    [TestMethod]
    public Task KeepsTheView_WhenVmToViewConverterChanges_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet() { ViewModel = new TestViewModel { Label = "Test Label" }, VmToViewConverter = new TestVmToViewConverter() };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);
        var shown = outlet.OutletContent;
        var other = new TestVmToViewConverter();

        // Act
        outlet.VmToViewConverter = other;

        // Assert
        _ = outlet.OutletContent.Should().BeSameAs(shown);
        _ = other.Conversions.Should().Be(0);
    });

    [TestMethod]
    public Task ReleasesTheView_WhenViewModelChanges_Async() => EnqueueAsync(
    async () =>
    {
        // Arrange
        var locator = new ResolvedViews();
        var first = new TestViewModel { Label = "First" };
        var outlet = new RouterOutlet() { ViewModel = first, VmToViewConverter = new ViewModelToView(locator) };
        await LoadTestContentAsync(outlet).ConfigureAwait(true);

        // Act
        var second = new TestViewModel { Label = "Second" };
        outlet.ViewModel = second;

        // Assert
        _ = locator.Views.Select(view => view.ViewModel).Should().Equal(null, second);
        _ = outlet.OutletContent.Should().BeSameAs(locator.Views[1]);
    });

    private static TestVisualStateManager InstallCustomVisualStateManager(RouterOutlet control)
    {
        var vsm = new TestVisualStateManager();
        var vsmTarget = control.FindDescendant<Grid>(
            e => string.Equals(e.Name, "PartRootGrid", StringComparison.Ordinal));
        _ = vsmTarget.Should().NotBeNull();
        VisualStateManager.SetCustomVisualStateManager(vsmTarget, vsm);
        return vsm;
    }

    private sealed partial class TestVmToViewConverter : IValueConverter
    {
        public const string InvalidViewModelLabel = "__INVALID__";

        public int Conversions { get; private set; }

        public object? Convert(object value, Type targetType, object parameter, string language)
        {
            this.Conversions++;
            return value is TestViewModel testViewModel
                ? testViewModel.Label.Equals(InvalidViewModelLabel, StringComparison.OrdinalIgnoreCase)
                    ? null
                    : new TextBlock { Text = testViewModel.Label }
                : (object?)null;
        }

        public object ConvertBack(object value, Type targetType, object parameter, string language) => throw new InvalidOperationException();
    }

    private sealed class TestViewModel
    {
        public string Label { get; init; } = string.Empty;
    }

    /// <summary>Resolves a new view for every request and keeps them in creation order.</summary>
    private sealed class ResolvedViews : IViewLocator
    {
        public List<TestView> Views { get; } = [];

        public object? ResolveView(object viewModel)
        {
            var view = new TestView();
            this.Views.Add(view);
            return view;
        }

        public IViewFor<T>? ResolveView<T>()
            where T : class
            => throw new InvalidOperationException();
    }

    private sealed partial class TestView : UserControl, IViewFor
    {
        public object? ViewModel { get; set; }
    }
}
