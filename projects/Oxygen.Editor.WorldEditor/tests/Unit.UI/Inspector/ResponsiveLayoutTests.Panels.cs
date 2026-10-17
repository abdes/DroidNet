// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Oxygen.Editor.Assets;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using static Oxygen.Editor.WorldEditor.TestSupport.SceneTestData;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>The panels around the property rows: the node editor dock and the material document header.</summary>
internal sealed partial class ResponsiveLayoutTests
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
                Name = "Second",
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
            RequestedTheme = theme,
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
        await AssertScaledComponentSelectionAsync(view, multiple).ConfigureAwait(true);
        _ = fixture.Document.IsDirty.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
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
                Name = "Second",
            };
            _ = second.AddComponent(CreateInspectorGeometry());
            _ = second.AddComponent(new OrthographicCamera { Name = "Camera" });
            _ = second.AddComponent(new PointLightComponent { Name = "Light" });
            var third = new SceneNode(fixture.Scene)
            {
                Name = "Third",
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
            Height = 500,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        _ = model.ComponentFilters.Should().HaveCount(mixedTypes ? 7 : 4);
        var components = (ScrollViewer)view.FindName("ComponentScroll");
        _ = components.ActualHeight.Should().BeLessThanOrEqualTo(128);
        _ = components.ExtentHeight.Should().BeApproximately((mixedTypes ? 7 : 4) * 32, 1);
        _ = ((ScrollViewer)view.FindName("PropertyScroll")).ActualHeight.Should().BeGreaterThan(250);
        _ = model.ComponentFilters.Single(option => option.ComponentType == typeof(GeometryComponent)).IsAvailable.Should().BeTrue();
    });

    /// <summary>The material document keeps its cook status beside the title and every header action inside the document.</summary>
    /// <param name="width">The document width in logical pixels.</param>
    /// <param name="rasterizationScale">The effective XAML scale.</param>
    /// <returns>The asynchronous header layout regression.</returns>
    [TestMethod]
    [DataRow(920d, 1d)]
    [DataRow(920d, 2d)]
    [DataRow(560d, 1.5d)]
    [DataRow(360d, 1d)]
    [DataRow(360d, 2d)]
    public Task MaterialHeaderKeepsStatusBesideTheTitleAndActionsInside(double width, double rasterizationScale) => EnqueueAsync(async () =>
    {
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([CreateStatusAsset(0) with { IdentityUri = new("asset:///Content/Materials/UI.omat.json") }]);
        using var model = CreateMaterialEditor(MaterialStatusTests.StatusProvider(updates));
        var view = new MaterialEditorView { ViewModel = model, Width = width, Height = 600 };
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(view, rasterizationScale, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var chip = (Border)view.FindName("CookStatusChip");
        _ = chip.ActualHeight.Should().BeLessThanOrEqualTo(26, "routine status uses one compact line");
        var chipPoint = chip.TransformToVisual(view).TransformPoint(default);
        _ = chipPoint.Y.Should().BeLessThan(40, "status belongs beside the title");
        var buttons = view.FindDescendants().OfType<Button>().Where(button => button.Content is "Undo" or "Redo" or "Save" or "Cook").ToArray();
        _ = buttons.Should().HaveCount(4);
        foreach (var button in buttons)
        {
            var point = button.TransformToVisual(view).TransformPoint(default);
            _ = point.X.Should().BeGreaterThanOrEqualTo(0);
            _ = (point.X + button.ActualWidth).Should().BeLessThanOrEqualTo(view.ActualWidth);
            if (Math.Abs(point.Y - chipPoint.Y) < 26)
            {
                _ = (chipPoint.X + chip.ActualWidth).Should().BeLessThanOrEqualTo(point.X);
            }
        }
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
}
