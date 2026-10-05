// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls.Demo.Tree;
using DroidNet.Controls.Demo.Tree.Model;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

public sealed partial class DynamicTreeBasicTests
{
    [TestMethod]
    public Task TrailingTemplate_TakesPrecedenceAndUpdatesRecycledRows_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var item = this.viewModel!.ShownItems.First();
        var direct = CreateTemplate("<TextBlock Text='{Binding Label}' />");
        var fallback = CreateTemplate("<TextBlock Text='Fallback' />");
        control.TrailingContentTemplate = direct;
        control.TrailingContentTemplateSelector = new TestTrailingContentSelector(fallback);
        control.TrailingContentWidth = 96;
        control.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var row = control.FindDescendant<TreeItemControl>(value => ReferenceEquals(value.ItemAdapter, item))!;
        _ = row.TrailingContentTemplate.Should().BeSameAs(direct);
        _ = row.TrailingContentWidth.Should().Be(96);

        var replacement = new EntityAdapter(new Entity("Replacement"));
        row.ItemAdapter = replacement;
        control.UpdateLayout();
        _ = row.TrailingContent.Should().BeSameAs(replacement);
        _ = row.TrailingContentTemplate.Should().BeSameAs(direct);
        var presenter = row.FindDescendant<ContentPresenter>(value => string.Equals(value.Name, TreeItemControl.TrailingContentPresenterPart, StringComparison.Ordinal))!;
        _ = presenter.FindDescendant<TextBlock>()!.Text.Should().Be("Replacement");
        row.ItemAdapter = null;
        _ = row.TrailingContent.Should().BeNull();
        _ = row.TrailingContentTemplate.Should().BeNull();
        row.ItemAdapter = item;

        control.TrailingContentTemplate = null;
        _ = row.TrailingContentTemplate.Should().BeSameAs(fallback);
        control.TrailingContentTemplateSelector = null;
        control.UpdateLayout();
        _ = row.TrailingContentTemplate.Should().BeNull();
        _ = row.TrailingContentWidth.Should().Be(0);
        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    [DataRow(0d, 0d)]
    [DataRow(96d, 96d)]
    public Task TrailingSelector_EmptyRowReservesOnlyExplicitWidth_Async(double width, double expected) => EnqueueAsync(() =>
    {
        var control = this.tree!;
        control.TrailingContentTemplateSelector = new EmptyTrailingSelector();
        control.TrailingContentWidth = width;
        control.UpdateLayout();
        var row = control.FindDescendant<TreeItemControl>()!;
        var root = row.FindDescendant<Grid>(value => string.Equals(value.Name, TreeItemControl.RootGridPart, StringComparison.Ordinal))!;
        _ = root.ColumnDefinitions[1].ActualWidth.Should().Be(expected);
        return Task.CompletedTask;
    });

    [TestMethod]
    [DataRow(32d)]
    [DataRow(40d)]
    public Task DemoColumns_HoverCommandsAndStatusStayAlignedAcrossDepths_Async(double height) => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var parent = (await root.Children.ConfigureAwait(true))[0];
        await vm.ExpandItemAsync(parent).ConfigureAwait(true);
        var grandchild = (await parent.Children.ConfigureAwait(true))[0];

        // Keep real shown identities while supplying the demo's row content for two different depths.
        var first = new EntityAdapter(new Entity("Loaded"));
        var second = new EntityAdapter(new Entity("Unloaded") { IsLoaded = false });
        control.ItemRowHeight = height;
        control.TrailingContentWidth = 120;
        control.TrailingContentTemplate = (DataTemplate)XamlReader.Load(
            "<DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:demo='using:DroidNet.Controls.Demo.Tree'><demo:EntityRowActions /></DataTemplate>");
        control.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        double[]? centers = null;
        foreach (var (item, adapter) in new[] { (Item: parent, Adapter: first), (Item: grandchild, Adapter: second) })
        {
            var row = control.FindDescendant<TreeItemControl>(value => ReferenceEquals(value.ItemAdapter, item))!;
            var actions = row.FindDescendant<EntityRowActions>()!;
            actions.DataContext = adapter;
            control.UpdateLayout();
            var lockButton = actions.FindDescendant<Button>(value => string.Equals(value.Name, "LockButton", StringComparison.Ordinal))!;
            var visibilityButton = actions.FindDescendant<Button>(value => string.Equals(value.Name, "VisibilityButton", StringComparison.Ordinal))!;
            var loaded = actions.FindDescendant<FontIcon>(value => string.Equals(value.Name, "LoadedIcon", StringComparison.Ordinal))!;
            _ = lockButton.Visibility.Should().Be(Visibility.Collapsed);
            _ = visibilityButton.Visibility.Should().Be(Visibility.Collapsed);
            _ = loaded.Glyph.Should().Be(adapter.LoadedGlyph);
            var originalWidth = actions.ActualWidth;
            actions.SetRowHovered(hovered: true);
            control.UpdateLayout();
            centers = AssertActionAlignment(control, row, [lockButton, visibilityButton, loaded], centers);

            AssertDemoActionCommands(lockButton, visibilityButton, adapter);
            _ = item.IsSelected.Should().BeFalse();
            actions.SetRowHovered(hovered: false);
            control.UpdateLayout();
            _ = actions.ActualWidth.Should().BeApproximately(originalWidth, 0.5);
            _ = lockButton.Visibility.Should().Be(Visibility.Collapsed);
            _ = loaded.Glyph.Should().Be(adapter.LoadedGlyph);
            actions.DataContext = new EntityAdapter(new Entity("Recycled"));
            _ = lockButton.Visibility.Should().Be(Visibility.Collapsed);
            _ = loaded.Glyph.Should().Be("\uE8A5");
        }
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task TrailingContent_IsolatesButtonsButNotPlainStatusContent_Async(bool buttonIsTabStop) => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        control.TrailingContentTemplate = CreateTemplate(
            "<StackPanel Orientation='Horizontal'><TextBlock Text='Status' /><Button Content='Action' /></StackPanel>");
        control.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var row = control.FindDescendant<TreeItemControl>()!;
        var presenter = row.FindDescendant<ContentPresenter>(value => string.Equals(value.Name, TreeItemControl.TrailingContentPresenterPart, StringComparison.Ordinal))!;
        var status = presenter.FindDescendant<TextBlock>()!;
        var button = presenter.FindDescendant<Button>()!;
        button.IsTabStop = buttonIsTabStop;
        _ = row.IsInteractiveContentElement(status).Should().BeFalse();
        _ = row.IsInteractiveContentElement(button).Should().BeTrue();
        _ = row.IsInteractiveContentElement(row).Should().BeFalse();
    });

    private static void AssertDemoActionCommands(Button lockButton, Button visibilityButton, EntityAdapter adapter)
    {
        var lockProvider = (IInvokeProvider)new ButtonAutomationPeer(lockButton).GetPattern(PatternInterface.Invoke);
        lockProvider.Invoke();
        _ = adapter.IsLocked.Should().BeTrue();
        _ = AutomationProperties.GetName(lockButton).Should().Be("Unlock entity");
        lockProvider.Invoke();
        _ = adapter.IsLocked.Should().BeFalse();
        ((IInvokeProvider)new ButtonAutomationPeer(visibilityButton).GetPattern(PatternInterface.Invoke)).Invoke();
        _ = adapter.IsVisible.Should().BeFalse();
        _ = AutomationProperties.GetName(visibilityButton).Should().Be("Show entity");
    }

    private static double[] AssertActionAlignment(
        FrameworkElement control,
        TreeItemControl row,
        IReadOnlyList<FrameworkElement> elements,
        double[]? previousCenters)
    {
        var centers = elements.Select(value => value.TransformToVisual(control).TransformPoint(default).X + (value.ActualWidth / 2)).ToArray();
        if (previousCenters is not null)
        {
            for (var index = 0; index < centers.Length; index++)
            {
                _ = centers[index].Should().BeApproximately(previousCenters[index], 0.5);
            }
        }

        foreach (var element in elements)
        {
            var position = element.TransformToVisual(row).TransformPoint(default);
            _ = (position.Y + (element.ActualHeight / 2)).Should().BeApproximately(row.ActualHeight / 2, 0.5);
        }

        return centers;
    }

    private sealed partial class EmptyTrailingSelector : DataTemplateSelector
    {
        protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container) => null;
    }
}
