// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.SceneExplorer;

/// <summary>Verifies the Scene Explorer header's search and row-filter affordances.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based test names follow the repository MSTest convention.")]
public sealed class SceneExplorerHeaderTests : VisualUserInterfaceTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Verifies compact category layout, composed filtering, and preserved scene selection.</summary>
    /// <returns>The asynchronous UI regression.</returns>
    [TestMethod]
    public Task SearchAndCategoryFilters_UseAvailableWidthAndPreserveSelection() => EnqueueAsync(async () =>
    {
        using var host = await SceneRowActionsTests.CreateRowHostAsync().ConfigureAwait(true);

        var search = host.View.FindName("SearchBox").Should().BeOfType<AutoSuggestBox>().Subject;
        _ = search.QueryIcon.Should().NotBeNull();
        _ = host.View.FindName("ClearSearchButton").Should().BeNull();

        var meshToggle = (ToggleButton)host.View.FindName("MeshCategoryButton")!;
        var lightToggle = (ToggleButton)host.View.FindName("LightCategoryButton")!;
        var cameraToggle = (ToggleButton)host.View.FindName("CameraCategoryButton")!;
        var categoryPanel = (Grid)host.View.FindName("CategoryFilterPanel")!;
        _ = meshToggle.Visibility.Should().Be(Visibility.Collapsed);
        _ = lightToggle.Visibility.Should().Be(Visibility.Visible);
        _ = cameraToggle.Visibility.Should().Be(Visibility.Visible);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = lightToggle.ActualWidth.Should().BeApproximately(cameraToggle.ActualWidth, 1);
        _ = (lightToggle.ActualWidth + cameraToggle.ActualWidth).Should()
            .BeApproximately(categoryPanel.ActualWidth - (2 * categoryPanel.ColumnSpacing), 2);

        lightToggle.ApplyTemplate();
        var categoryBackground = lightToggle.FindDescendant<Border>(static element => string.Equals(element.Name, "CategoryBackground", StringComparison.Ordinal))!;
        lightToggle.IsChecked = false;
        await WaitForRenderAsync().ConfigureAwait(true);
        var uncheckedBackground = categoryBackground.Background;
        lightToggle.IsChecked = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = categoryBackground.Background.Should().NotBeSameAs(uncheckedBackground, "a checked Explorer category filter must have a visible selected state");

        _ = (await host.ViewModel.SearchAsync("Secondary").ConfigureAwait(true)).Should().Be(1);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = ((TextBlock)host.View.FindName("SearchResultCountText")!).Text.Should().Be("1 match");

        _ = await host.ViewModel.SetSelectedNodes([host.Node.Id]).ConfigureAwait(true);
        lightToggle.IsChecked = false;
        cameraToggle.IsChecked = false;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.ViewModel.FilteredItems.Should().Contain(host.SecondaryAdapter).And.NotContain(host.Adapter);
        _ = host.Adapter.IsSelected.Should().BeTrue("Explorer row filters must not change scene selection");

        _ = (await host.ViewModel.SearchAsync("Camera").ConfigureAwait(true)).Should().Be(0);
        cameraToggle.IsChecked = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.ViewModel.SearchResultCount.Should().Be(1, "active search results should be recomputed when a category is enabled");
        _ = host.ViewModel.FilteredItems.Should().Contain(host.Adapter);
        _ = host.Adapter.IsSelected.Should().BeTrue();

        _ = (await host.ViewModel.SearchAsync("No object has this name").ConfigureAwait(true)).Should().Be(0);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = ((TextBlock)host.View.FindName("SearchResultCountText")!).Text.Should().Be("No matches");
    });
}
