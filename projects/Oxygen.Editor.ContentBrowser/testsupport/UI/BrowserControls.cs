// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Controls;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserQuerySupport;

namespace Oxygen.Editor.ContentBrowser.TestSupport;

internal sealed class BrowserControls : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Creates the assets pane over a layout whose view the pane shows.</summary>
    /// <param name="provider">The asset feed.</param>
    /// <param name="projects">The active project.</param>
    /// <param name="state">The browser state.</param>
    /// <param name="layout">The layout view model.</param>
    /// <param name="layoutView">The view the layout resolves to.</param>
    /// <param name="messenger">The messenger the pane sends requests on.</param>
    /// <param name="pipeline">The content pipeline; a mock when null.</param>
    /// <returns>The pane, which the caller disposes.</returns>
    internal static AssetsViewModel CreateBrowserModel(IAssetFeed provider, IProjectContextService projects, ContentBrowserState state, object layout, object layoutView, IMessenger messenger, IContentPipelineService? pipeline = null)
    {
        var locator = new Mock<IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(layout)).Returns(layoutView);
        return new AssetsViewModel(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
            new ViewModelToView(locator.Object),
            state,
            projects,
            pipeline ?? Mock.Of<IContentPipelineService>(),
            provider,
            Mock.Of<IOperationResultPublisher>(),
            Mock.Of<IStatusReducer>(),
            Mock.Of<DroidNet.Storage.IStorageProvider>(),
            messenger,
            Mock.Of<IDialogService>(),
            Mock.Of<IWindowManagerService>(),
            Mock.Of<IAssetShell>());
    }

    internal static void AssertBrowserRowState(ListViewBase selector, AssetBrowserRow row, Control container, object? focus, double offset, AssetsLayoutViewModel model, AssetEntry expected)
    {
        _ = selector.SelectedItem.Should().BeSameAs(row);
        _ = selector.ContainerFromItem(row).Should().BeSameAs(container);
        _ = FocusManager.GetFocusedElement(selector.XamlRoot).Should().BeSameAs(focus);
        _ = selector.FindDescendant<ScrollViewer>()!.VerticalOffset.Should().BeApproximately(offset, 1);
        _ = model.SelectedAsset.Should().BeSameAs(expected);
        var status = container.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Queued", StringComparison.Ordinal));
        _ = status.Should().NotBeNull();
        _ = ToolTipService.GetToolTip(status!).Should().Be(expected.PrimaryBadgeTooltip);
        var position = status!.TransformToVisual(selector).TransformPoint(new(0, 0));
        _ = position.Y.Should().BeInRange(0, selector.ActualHeight - status.ActualHeight + 1);
    }

    internal static Grid CreateQueryTestRoot(AssetQueryView queryView, UserControl assetsView, bool light)
    {
        var root = new Grid
        {
            Width = 620,
            Height = 360,
            RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark,
            Background = new SolidColorBrush(light ? Microsoft.UI.Colors.WhiteSmoke : Microsoft.UI.Colors.Black),
        };
        root.RowDefinitions.Add(new() { Height = GridLength.Auto });
        root.RowDefinitions.Add(new() { Height = new(1, GridUnitType.Star) });
        root.Children.Add(queryView);
        Grid.SetRow(assetsView, 1);
        root.Children.Add(assetsView);
        return root;
    }

    internal static async Task SelectBrowserFiltersAsync(AssetQueryView view, params string[] labels)
    {
        var filter = (ToolBarButton)view.FindName("AssetFilterButton");
        var flyout = (Flyout)filter.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(filter);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            foreach (var label in labels)
            {
                SetFilterChoice((FrameworkElement)flyout.Content, label);
            }
        }
        finally
        {
            flyout.Hide();
        }
    }
}
