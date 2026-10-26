// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
[TestCategory("Filtering")]
public sealed partial class DynamicTreeFilteringTests : VisualUserInterfaceTests, IDisposable
{
    private DynamicTree? tree;
    private TestViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task FilterBar_DefaultsToOptOut_Async() => EnqueueAsync(
        () =>
        {
            _ = this.tree!.IsFilterBarEnabled.Should().BeFalse();
            _ = this.tree.IsFilterBarVisible.Should().BeTrue();
            _ = this.tree.FilterText.Should().BeEmpty();
            _ = this.tree.FilterBarPlaceholderText.Should().Be("Filter");
            return Task.CompletedTask;
        });

    [TestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task FilterBar_IsVisibleOnlyWhenEnabledAndVisible_Async(bool enabled, bool visible) => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            control.IsFilterBarEnabled = enabled;
            control.IsFilterBarVisible = visible;
            control.UpdateLayout();

            var bar = control.FindDescendant<Grid>(e => string.Equals(e.Name, DynamicTree.FilterBarPart, StringComparison.Ordinal))!;
            _ = bar.Visibility.Should().Be(enabled && visible ? Visibility.Visible : Visibility.Collapsed);
            return Task.CompletedTask;
        });

    [TestMethod]
    public Task FilterBar_TextSynchronizesBothWays_AndSearchIconOnlyAppearsWhenEmpty_Async() => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            control.IsFilterBarEnabled = true;
            control.FilterBarPlaceholderText = "Find an item";
            control.FilterText = "Initial";
            _ = control.IsFilteringEnabled.Should().BeFalse();
            _ = this.viewModel!.FilterPredicate.Should().BeNull();

            var input = FindFilterInput(control);
            var icon = FindFilterIcon(control);
            _ = input.PlaceholderText.Should().Be("Find an item");
            _ = input.Text.Should().Be("Initial");
            _ = icon.Visibility.Should().Be(Visibility.Collapsed);

            input.Text = "Edited";
            _ = control.FilterText.Should().Be("Edited");
            input.Text = string.Empty;
            _ = control.FilterText.Should().BeEmpty();
            _ = icon.Visibility.Should().Be(Visibility.Visible);
            return Task.CompletedTask;
        });

    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task FilterBar_AppliesInputStyle_AndAccessoryMatchesInputHeight_Async(bool compact) => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            control.IsFilterBarEnabled = true;
            var styles = new ResourceDictionary
            {
                Source = new Uri(compact
                    ? "ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/CompactFilterBarStyles.xaml"
                    : "ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/FilterBarStyles.xaml"),
            };
            if (compact)
            {
                control.Resources.MergedDictionaries.Add(styles);
            }

            var inputStyle = (Style)styles["FilterBarInputStyle"];
            control.FilterBarInputStyle = inputStyle;
            var accessory = new Button
            {
                Style = (Style)styles["FilterBarButtonStyle"],
                Content = new FontIcon { Glyph = "", FontSize = 14 },
            };
            control.FilterBarAccessoryContent = accessory;
            control.UpdateLayout();

            var input = FindFilterInput(control);
            _ = input.Style.Should().BeSameAs(inputStyle);
            _ = accessory.ActualHeight.Should().BeApproximately(input.ActualHeight, 0.1);
            return Task.CompletedTask;
        });

    [TestMethod]
    public Task FilterBar_HidingOrDisablingPreservesTextAndFiltering_Async() => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            var vm = this.viewModel!;
            control.IsFilterBarEnabled = true;
            control.IsFilteringEnabled = true;
            control.FilterText = "R";
            vm.FilterPredicate = item => string.Equals(item.Label, "R", StringComparison.Ordinal);
            var predicate = vm.FilterPredicate;

            control.IsFilterBarVisible = false;
            control.IsFilterBarEnabled = false;
            _ = control.FilterText.Should().Be("R");
            _ = control.IsFilteringEnabled.Should().BeTrue();
            _ = vm.FilterPredicate.Should().BeSameAs(predicate);
            _ = control.DisplayedItems.Should().BeSameAs(vm.FilteredItems);

            control.IsFilterBarEnabled = true;
            control.IsFilterBarVisible = true;
            _ = FindFilterInput(control).Text.Should().Be("R");
            return Task.CompletedTask;
        });

    [TestMethod]
    public Task FilterBar_AccessoryContentIsDisplayed_Async() => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            var accessory = new Button { Content = "Types" };
            control.FilterBarAccessoryContent = accessory;
            control.IsFilterBarEnabled = true;
            control.UpdateLayout();

            var bar = control.FindDescendant<Grid>(e => string.Equals(e.Name, DynamicTree.FilterBarPart, StringComparison.Ordinal))!;
            _ = bar.FindDescendant<Button>(e => ReferenceEquals(e, accessory)).Should().BeSameAs(accessory);
            return Task.CompletedTask;
        });

    [TestMethod]
    public Task FilteringEnabled_RepeaterSourceSwitchesToTheFilteredItems_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            await this.ExpandTwoBranchesAsync().ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = true;

            var repeater = this.tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal))!;
            _ = this.tree.DisplayedItems.Should().BeSameAs(vm.FilteredItems);
            _ = repeater.ItemsSourceView.Count.Should().Be(vm.FilteredItems.Count());
            _ = repeater.ItemsSourceView.Count.Should().BeLessThan(vm.ShownItemsCount);
        });

    [TestMethod]
    public Task SelectionCommands_ActOnAllUnfilteredShownItems_EvenWhenFilteringEnabled_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            await this.ExpandTwoBranchesAsync().ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = true;

            // Select all should always operate on the unfiltered shown-items list.
            vm.SelectAllCommand.Execute(parameter: null);

            _ = vm.SelectedItemsCount.Should().Be(vm.ShownItemsCount);

            var hiddenGrandChild = vm.ShownItems.First(i => string.Equals(i.Label, "R-C1-GC2", StringComparison.Ordinal));
            _ = hiddenGrandChild.IsSelected.Should().BeTrue();
        });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.viewModel = new TestViewModel();
                await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
                this.viewModel.SelectionMode = SelectionMode.Multiple;

                this.tree = new DynamicTree { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });

        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    private static TextBox FindFilterInput(DynamicTree control)
        => control.FindDescendant<TextBox>(e => string.Equals(e.Name, DynamicTree.FilterTextBoxPart, StringComparison.Ordinal))!;

    private static FontIcon FindFilterIcon(DynamicTree control)
        => control.FindDescendant<FontIcon>(e => string.Equals(e.Name, DynamicTree.FilterSearchIconPart, StringComparison.Ordinal))!;

    /// <summary>Expands the root and its first two children, so grandchildren join the shown items.</summary>
    /// <returns>A task that completes when the branches are expanded.</returns>
    private async Task ExpandTwoBranchesAsync()
    {
        var vm = this.viewModel!;
        var root = (ITreeItem)vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);

        var children = await root.Children.ConfigureAwait(true);
        await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
        await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);
    }
}
