// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media.Imaging;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Windows.Graphics.Imaging;

namespace Oxygen.Editor.World.Tests;

/// <summary>Measures and captures compact single- and multi-node inspectors.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>The header fits its component rows; extra dock height goes to property editors.</summary>
    /// <param name="width">The dock width in logical pixels.</param>
    /// <param name="height">The dock height in logical pixels.</param>
    /// <param name="multiple">Whether to select two nodes.</param>
    /// <param name="theme">The theme to render.</param>
    /// <param name="rasterizationScale">The effective XAML rasterization scale.</param>
    /// <returns>The asynchronous layout regression.</returns>
    [TestMethod]
    [DataRow(360d, 620d, false, ElementTheme.Dark, 1d)]
    [DataRow(360d, 620d, false, ElementTheme.Dark, 1.5d)]
    [DataRow(360d, 620d, false, ElementTheme.Dark, 2d)]
    [DataRow(280d, 320d, false, ElementTheme.Light, 1d)]
    [DataRow(280d, 320d, false, ElementTheme.Light, 1.5d)]
    [DataRow(280d, 320d, false, ElementTheme.Light, 2d)]
    [DataRow(360d, 160d, true, ElementTheme.Dark, 1d)]
    [DataRow(360d, 160d, true, ElementTheme.Dark, 1.5d)]
    [DataRow(360d, 160d, true, ElementTheme.Dark, 2d)]
    [DataRow(540d, 480d, true, ElementTheme.Light, 1d)]
    [DataRow(540d, 480d, true, ElementTheme.Light, 1.5d)]
    [DataRow(540d, 480d, true, ElementTheme.Light, 2d)]
    public Task ComponentHeaderStaysCompactAndAllIconRemainsVisible(double width, double height, bool multiple, ElementTheme theme, double rasterizationScale) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        MakeGeometryOnly(fixture);
        fixture.Node.Name = "A scene node with a longer display name";
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        if (multiple)
        {
            var second = new SceneNode(fixture.Scene) { Name = "Second" };
            _ = second.AddComponent(CreateInspectorGeometry());
            fixture.Scene.RootNodes.Add(second);
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        }

        var view = new SceneNodeEditorView { ViewModel = model, Width = width, Height = height, RequestedTheme = theme };
        var captureHost = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}' />");
        captureHost.RequestedTheme = theme;
        captureHost.Width = width;
        captureHost.Height = height;
        captureHost.Child = view;
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(captureHost, rasterizationScale, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        view.UpdateLayout();
        var header = (Border)view.FindName("CompactNodeHeader");
        var properties = (ScrollViewer)view.FindName("PropertyScroll");
        var components = (ScrollViewer)view.FindName("ComponentScroll");
        _ = header.ActualHeight.Should().BeLessThanOrEqualTo(116);
        _ = components.ExtentHeight.Should().BeApproximately(64, 1);
        _ = properties.ActualHeight.Should().BeGreaterThan(40);
        AssertAllToggle(view, multiple);
        AssertPropertyEditorRows(view);
        await this.CaptureComponentLayoutAsync(captureHost, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"inspector-{width}-{height}-{multiple}-{theme}-{rasterizationScale}.png")).ConfigureAwait(true);
        await AssertScaledComponentSelectionAsync(view, multiple).ConfigureAwait(true);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await ScaledXamlHost.ScrollToEndAsync(components).ConfigureAwait(true);
        await ScaledXamlHost.ScrollToEndAsync(properties).ConfigureAwait(true);
        await this.CaptureComponentLayoutAsync(captureHost, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"inspector-bottom-{width}-{height}-{multiple}-{theme}-{rasterizationScale}.png")).ConfigureAwait(true);
        await AssertExtraHeightGoesToPropertiesAsync(view, captureHost, header, properties).ConfigureAwait(true);
    });

    /// <summary>Four valid components fit directly; a longer multi-node type list is bounded.</summary>
    /// <param name="mixedTypes">Whether the selection contains different camera and light types.</param>
    /// <returns>The asynchronous component-list sizing regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ComponentSelectorBoundsLongListsWithoutTakingPropertySpace(bool mixedTypes) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        _ = fixture.Node.AddComponent(CreateInspectorGeometry());
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        if (mixedTypes)
        {
            var second = new SceneNode(fixture.Scene) { Name = "Second" };
            _ = second.AddComponent(CreateInspectorGeometry());
            _ = second.AddComponent(new OrthographicCamera { Name = "Camera" });
            _ = second.AddComponent(new PointLightComponent { Name = "Light" });
            var third = new SceneNode(fixture.Scene) { Name = "Third" };
            _ = third.AddComponent(CreateInspectorGeometry());
            _ = third.AddComponent(new PerspectiveCamera { Name = "Camera" });
            _ = third.AddComponent(new SpotLightComponent { Name = "Light" });
            fixture.Scene.RootNodes.Add(second);
            fixture.Scene.RootNodes.Add(third);
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second, third]));
        }

        var view = new SceneNodeEditorView { ViewModel = model, Width = 360, Height = 500 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        _ = model.ComponentFilters.Should().HaveCount(mixedTypes ? 7 : 4);
        var components = (ScrollViewer)view.FindName("ComponentScroll");
        _ = components.ActualHeight.Should().BeLessThanOrEqualTo(128);
        _ = components.ExtentHeight.Should().BeApproximately((mixedTypes ? 7 : 4) * 32, 1);
        _ = ((ScrollViewer)view.FindName("PropertyScroll")).ActualHeight.Should().BeGreaterThan(250);
        _ = model.ComponentFilters.Single(option => option.ComponentType == typeof(GeometryComponent)).IsAvailable.Should().BeTrue();
    });

    private static async Task AssertExtraHeightGoesToPropertiesAsync(SceneNodeEditorView view, Border captureHost, Border header, ScrollViewer properties)
    {
        if (view.Height > 300)
        {
            var previousHeader = header.ActualHeight;
            var previousProperties = properties.ActualHeight;
            view.Height += 100;
            captureHost.Height = view.Height;
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = header.ActualHeight.Should().BeApproximately(previousHeader, 1);
            _ = properties.ActualHeight.Should().BeApproximately(previousProperties + 100, 1);
        }
    }

    private static async Task AssertScaledComponentSelectionAsync(SceneNodeEditorView view, bool multiple)
    {
        var editors = view.ViewModel!.PropertyEditors.ToArray();
        Toggle(ComponentButton(view, typeof(GeometryComponent)));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = view.ViewModel!.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<Oxygen.Editor.World.Inspector.Geometry.GeometryViewModel>();
        var all = multiple ? (ToolBarToggleButton)view.FindName("MultiAllComponentsButton")
            : (ToolBarToggleButton)((SceneNodeDetailsView)view.FindName("NodeDetails")).FindName("AllComponentsButton");
        Toggle(all);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = view.ViewModel!.PropertyEditors.Should().Equal(editors);
        _ = view.ViewModel!.IsAllComponentsSelected.Should().BeTrue();
    }

    private static void AssertAllToggle(SceneNodeEditorView view, bool multiple)
    {
        var all = multiple ? (ToolBarToggleButton)view.FindName("MultiAllComponentsButton")
            : (ToolBarToggleButton)((SceneNodeDetailsView)view.FindName("NodeDetails")).FindName("AllComponentsButton");
        var toolbar = all.FindAscendant<ToolBar>()!;
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        _ = all.ActualWidth.Should().BePositive();
        _ = all.IsTabStop.Should().BeTrue();
        _ = all.ToolBarLabelPosition.Should().Be(ToolBarLabelPosition.Collapsed);
        _ = ToolTipService.GetToolTip(all).Should().Be("Show all component properties");
    }

    private static void AssertPropertyEditorRows(SceneNodeEditorView view)
    {
        var card = view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>()!;
        var label = card.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PropertyName", StringComparison.Ordinal))!;
        var grid = (Grid)label.Parent;
        _ = grid.RowDefinitions.Should().BeEmpty("property names must remain beside their values at every width");
        _ = grid.ColumnDefinitions.Should().HaveCount(3);
        _ = ToolTipService.GetToolTip(label).Should().Be(card.PropertyName);
    }

    private async Task CaptureComponentLayoutAsync(FrameworkElement view, string name)
    {
        await Task.Delay(350, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(view);
        var pixels = (await bitmap.GetPixelsAsync()).ToArray();
        var directory = Directory.CreateTempSubdirectory("OxygenComponentLayout-");
        var path = Path.Combine(directory.FullName, name);
        var file = File.Create(path);
        await using var lifetime = file.ConfigureAwait(false);
        using var stream = file.AsRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, pixels);
        await encoder.FlushAsync();
        this.TestContext.AddResultFile(path);
    }
}
