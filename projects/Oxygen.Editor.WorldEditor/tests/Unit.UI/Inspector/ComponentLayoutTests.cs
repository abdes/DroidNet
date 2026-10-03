// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using Windows.Graphics.Imaging;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.SceneTestData;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class ComponentLayoutTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

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
        using var fixture = new SceneAuthoringFixture();
        MakeGeometryOnly(fixture);
        fixture.Node.Name = "A scene node with a longer display name";
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        if (multiple)
        {
            var second = new SceneNode(fixture.Scene)
            {
                Name = "Second"
            };
            _ = second.AddComponent(CreateInspectorGeometry());
            fixture.Scene.RootNodes.Add(second);
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        }

        var view = new SceneNodeEditorView
        {
            ViewModel = model,
            Width = width,
            Height = height,
            RequestedTheme = theme
        };
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
        var headerRows = ((StackPanel)header.Child).Children.OfType<FrameworkElement>().ToArray();
        var expectedHeaderHeight = headerRows.Sum(row => row.ActualHeight)
            + (((StackPanel)header.Child).Spacing * (headerRows.Length - 1))
            + header.Padding.Top + header.Padding.Bottom;
        _ = header.ActualHeight.Should().BeApproximately(expectedHeaderHeight, 1, "the title, selection and component rows must not reserve unused dock height");
        _ = components.ExtentHeight.Should().BeApproximately(64, 1);
        _ = properties.ActualHeight.Should().BePositive("even a short dock must retain a scrollable property viewport");
        AssertAllToggle(view, multiple);
        await AssertPropertyEditorRowsAsync(view, properties, this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertScaledComponentSelectionAsync(view, multiple).ConfigureAwait(true);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await ScaledXamlHost.ScrollToEndAsync(components, this.TestContext.CancellationToken).ConfigureAwait(true);
        await ScaledXamlHost.ScrollToEndAsync(properties, this.TestContext.CancellationToken).ConfigureAwait(true);
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
        using var fixture = new SceneAuthoringFixture();
        _ = fixture.Node.AddComponent(CreateInspectorGeometry());
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        if (mixedTypes)
        {
            var second = new SceneNode(fixture.Scene)
            {
                Name = "Second"
            };
            _ = second.AddComponent(CreateInspectorGeometry());
            _ = second.AddComponent(new OrthographicCamera { Name = "Camera" });
            _ = second.AddComponent(new PointLightComponent { Name = "Light" });
            var third = new SceneNode(fixture.Scene)
            {
                Name = "Third"
            };
            _ = third.AddComponent(CreateInspectorGeometry());
            _ = third.AddComponent(new PerspectiveCamera { Name = "Camera" });
            _ = third.AddComponent(new SpotLightComponent { Name = "Light" });
            fixture.Scene.RootNodes.Add(second);
            fixture.Scene.RootNodes.Add(third);
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second, third]));
        }

        var view = new SceneNodeEditorView
        {
            ViewModel = model,
            Width = 360,
            Height = 500
        };
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
        var all = multiple ? (ToolBarToggleButton)view.FindName("MultiAllComponentsButton") : (ToolBarToggleButton)((SceneNodeDetailsView)view.FindName("NodeDetails")).FindName("AllComponentsButton");
        Toggle(all);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = view.ViewModel!.PropertyEditors.Should().Equal(editors);
        _ = view.ViewModel!.IsAllComponentsSelected.Should().BeTrue();
    }

    private static void AssertAllToggle(SceneNodeEditorView view, bool multiple)
    {
        var all = multiple ? (ToolBarToggleButton)view.FindName("MultiAllComponentsButton") : (ToolBarToggleButton)((SceneNodeDetailsView)view.FindName("NodeDetails")).FindName("AllComponentsButton");
        var toolbar = all.FindAscendant<ToolBar>()!;
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        _ = all.ActualWidth.Should().BePositive();
        _ = all.IsTabStop.Should().BeTrue();
        _ = all.ToolBarLabelPosition.Should().Be(ToolBarLabelPosition.Collapsed);
        _ = ToolTipService.GetToolTip(all).Should().Be("Show all component properties");
    }

    private static async Task AssertPropertyEditorRowsAsync(SceneNodeEditorView view, ScrollViewer properties, CancellationToken cancellationToken)
    {
        var number = await FindInspectorControlAsync(properties, () => view.FindDescendant<VectorBox>()?.FindDescendant<NumberBox>(), "Position", cancellationToken).ConfigureAwait(true);
        var card = number.FindAscendant<Oxygen.Editor.Controls.PropertyCard>()!;
        var label = card.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PropertyName", StringComparison.Ordinal))!;
        var vector = card.FindDescendant<VectorBox>()!;
        var labelPoint = label.TransformToVisual(card).TransformPoint(default);
        var editorPoint = vector.TransformToVisual(card).TransformPoint(default);
        if (card.ActualLayout == Oxygen.Editor.Controls.PropertyLayout.Stacked)
        {
            _ = editorPoint.Y.Should().BeGreaterThanOrEqualTo(labelPoint.Y + label.ActualHeight);
        }
        else
        {
            var rowWidth = card.ActualWidth - card.Padding.Left - card.Padding.Right;
            _ = editorPoint.X.Should().BeApproximately(card.Padding.Left + ((rowWidth - 12) * 0.4) + 12, 1);
        }

        _ = ToolTipService.GetToolTip(label).Should().Be(card.PropertyName);
    }
}
