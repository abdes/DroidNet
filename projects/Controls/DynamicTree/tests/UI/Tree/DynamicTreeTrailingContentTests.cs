// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

public sealed partial class DynamicTreeBasicTests
{
    [TestMethod]
    public Task TrailingTemplate_TakesPrecedenceOverTheSelector_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var item = this.viewModel!.ShownItems.First();
        var direct = CreateTemplate("<TextBlock Text='{Binding Label}' />");
        var fallback = CreateTemplate("<TextBlock Text='Fallback' />");
        control.TrailingContentTemplate = direct;
        control.TrailingContentTemplateSelector = new TestTrailingContentSelector(fallback);
        control.TrailingContentWidth = 96;
        control.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        var row = this.FindRow(item);

        _ = row.TrailingContentTemplate.Should().BeSameAs(direct);
        _ = row.TrailingContentWidth.Should().Be(96);

        control.TrailingContentTemplate = null;
        _ = row.TrailingContentTemplate.Should().BeSameAs(fallback, "the selector applies once the direct template is gone");
    });

    [TestMethod]
    public Task TrailingTemplate_WhenRowIsRecycledToAnotherItem_RebindsItsContent_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var item = this.viewModel!.ShownItems.First();
        var direct = CreateTemplate("<TextBlock Text='{Binding Label}' />");
        control.TrailingContentTemplate = direct;
        control.TrailingContentWidth = 96;
        control.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        var row = this.FindRow(item);
        var replacement = new TestItemAdapter(new TestTreeItem { Label = "Replacement" });

        row.ItemAdapter = replacement;
        control.UpdateLayout();

        _ = row.TrailingContent.Should().BeSameAs(replacement);
        _ = row.TrailingContentTemplate.Should().BeSameAs(direct);
        _ = this.FindTrailingPresenter(replacement).FindDescendant<TextBlock>()!.Text.Should().Be("Replacement");
    });

    [TestMethod]
    public Task TrailingTemplate_WhenRowIsRecycledWithoutItem_ClearsItsContent_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var item = this.viewModel!.ShownItems.First();
        control.TrailingContentTemplate = CreateTemplate("<TextBlock Text='{Binding Label}' />");
        control.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        var row = this.FindRow(item);

        row.ItemAdapter = null;

        _ = row.TrailingContent.Should().BeNull();
        _ = row.TrailingContentTemplate.Should().BeNull();
    });

    [TestMethod]
    public Task TrailingTemplate_WhenSelectorIsRemoved_ClearsTemplateAndWidth_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var item = this.viewModel!.ShownItems.First();
        control.TrailingContentTemplateSelector = new TestTrailingContentSelector(CreateTemplate("<TextBlock Text='Fallback' />"));
        control.TrailingContentWidth = 96;
        control.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        var row = this.FindRow(item);

        control.TrailingContentTemplateSelector = null;
        control.UpdateLayout();

        _ = row.TrailingContentTemplate.Should().BeNull();
        _ = row.TrailingContentWidth.Should().Be(0);
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
        var column = root.ColumnDefinitions[1].Width;

        _ = this.FindTrailingPresenter(row.ItemAdapter!).Visibility.Should().Be(Visibility.Collapsed, "the selector gave the row no content");
        _ = column.IsAbsolute.Should().BeTrue("an empty row never sizes the column to content");
        _ = column.Value.Should().Be(expected);
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task TrailingContent_IsolatesButtonsButNotPlainStatusContent_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        control.TrailingContentTemplate = CreateTemplate(
            "<StackPanel Orientation='Horizontal'><TextBlock Text='Status' /><Button Content='Action' /></StackPanel>");
        control.UpdateLayout();
        await RenderAsync().ConfigureAwait(true);
        var row = control.FindDescendant<TreeItemControl>()!;
        var presenter = this.FindTrailingPresenter(row.ItemAdapter!);
        var status = presenter.FindDescendant<TextBlock>()!;
        var button = presenter.FindDescendant<Button>()!;
        button.IsTabStop = false;

        _ = row.IsInteractiveContentElement(status).Should().BeFalse();
        _ = row.IsInteractiveContentElement(button).Should().BeTrue("a button is interactive even when it is not a tab stop");
        _ = row.IsInteractiveContentElement(row).Should().BeFalse();
    });

    private sealed partial class EmptyTrailingSelector : DataTemplateSelector
    {
        protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container) => null;
    }
}
