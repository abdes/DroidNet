// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserQuerySupport;

namespace Oxygen.Editor.ContentBrowser.TestSupport;

internal sealed class BrowserControls : DroidNet.Tests.VisualUserInterfaceTests
{
    internal static void AssertBrowserRowState(ListViewBase selector, AssetBrowserRow row, Control container, object? focus, double offset, AssetsLayoutViewModel model, ContentBrowserAssetItem expected)
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

    internal static AssetsViewModel CreateBuiltinBrowserModel(IContentBrowserAssetProvider provider, ProjectContextService projects, ContentBrowserState state, AssetsLayoutViewModel layout, FrameworkElement layoutView, IMessenger? messenger = null, IContentPipelineService? pipeline = null)
    {
        var locator = new Mock<DroidNet.Mvvm.IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(layout)).Returns(layoutView);
        return new AssetsViewModel(Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.ICookRunService>(), new DroidNet.Mvvm.Converters.ViewModelToView(locator.Object), state, projects, Mock.Of<IProjectManagerService>(), Mock.Of<IAuthoringTargetResolver>(), pipeline ?? Mock.Of<IContentPipelineService>(), provider, Mock.Of<IOperationResultPublisher>(), Mock.Of<IStatusReducer>(), Mock.Of<DroidNet.Storage.IStorageProvider>(), messenger ?? new StrongReferenceMessenger(), Mock.Of<DroidNet.Aura.Dialogs.IDialogService>(), Mock.Of<DroidNet.Aura.Windowing.IWindowManagerService>());
    }
}
