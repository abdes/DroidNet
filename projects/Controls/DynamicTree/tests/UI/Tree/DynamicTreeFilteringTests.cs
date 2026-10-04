// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

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
    public Task FilterBar_VisibilityRequiresBothFlags_AndCollapsedRowConsumesNoSpace_Async(bool enabled, bool visible) => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            control.IsFilterBarEnabled = enabled;
            control.IsFilterBarVisible = visible;
            control.UpdateLayout();

            var bar = control.FindDescendant<Grid>(e => string.Equals(e.Name, "PartFilterBar", StringComparison.Ordinal))!;
            var rootGrid = control.FindDescendant<Grid>(e => string.Equals(e.Name, DynamicTree.RootGridPart, StringComparison.Ordinal))!;
            _ = bar.Visibility.Should().Be(enabled && visible ? Visibility.Visible : Visibility.Collapsed);
            if (!enabled || !visible)
            {
                _ = rootGrid.RowDefinitions[0].ActualHeight.Should().Be(0);
            }
            else
            {
                _ = rootGrid.RowDefinitions[0].ActualHeight.Should().BePositive();
            }

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

            var input = control.FindDescendant<TextBox>(e => string.Equals(e.Name, "PartFilterTextBox", StringComparison.Ordinal))!;
            var icon = control.FindDescendant<FontIcon>(e => string.Equals(e.Name, "PartFilterSearchIcon", StringComparison.Ordinal))!;
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
    public Task FilterBar_NativeClearButtonClearsText_AndRestoresSearchIcon_Async() => EnqueueAsync(
        async () =>
        {
            var control = this.tree!;
            control.IsFilterBarEnabled = true;
            control.FilterText = "My filter";
            var input = control.FindDescendant<TextBox>(e => string.Equals(e.Name, "PartFilterTextBox", StringComparison.Ordinal))!;
            _ = input.Focus(FocusState.Programmatic).Should().BeTrue();
            control.UpdateLayout();
            var clearButton = input.FindDescendant<Button>(e => string.Equals(e.Name, "DeleteButton", StringComparison.Ordinal));
            _ = clearButton.Should().NotBeNull();
            _ = clearButton!.Visibility.Should().Be(Visibility.Visible);
            ((IInvokeProvider)new ButtonAutomationPeer(clearButton).GetPattern(PatternInterface.Invoke)).Invoke();
            await Task.Yield();

            _ = input.Text.Should().BeEmpty();
            _ = control.FilterText.Should().BeEmpty();
            var icon = control.FindDescendant<FontIcon>(e => string.Equals(e.Name, "PartFilterSearchIcon", StringComparison.Ordinal))!;
            _ = icon.Visibility.Should().Be(Visibility.Visible);
        });

    [TestMethod]
    [DataRow(true, 28d, 24d)]
    [DataRow(false, 40d, 32d)]
    public Task FilterBar_UsesNativeDensityStylesAndMatchesAccessoryHeight_Async(bool compact, double rowHeight, double barHeight) => EnqueueAsync(
        () =>
        {
            var control = this.tree!;
            control.IsFilterBarEnabled = true;
            control.ItemRowHeight = rowHeight;
            var styles = new ResourceDictionary
            {
                Source = new Uri(compact
                    ? "ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/CompactFilterBarStyles.xaml"
                    : "ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/FilterBarStyles.xaml"),
            };
            var selectedStyles = styles;
            if (compact)
            {
                control.Resources.MergedDictionaries.Add(selectedStyles);
            }

            control.FilterBarInputStyle = (Style)selectedStyles["FilterBarInputStyle"];
            var accessory = new Button
            {
                Style = (Style)selectedStyles["FilterBarButtonStyle"],
                Content = new FontIcon { Glyph = "\uE71C", FontSize = 14 },
            };
            control.FilterBarAccessoryContent = accessory;
            control.UpdateLayout();

            var input = control.FindDescendant<TextBox>(e => string.Equals(e.Name, "PartFilterTextBox", StringComparison.Ordinal))!;
            var icon = control.FindDescendant<FontIcon>(e => string.Equals(e.Name, "PartFilterSearchIcon", StringComparison.Ordinal))!;
            _ = input.MinHeight.Should().Be(barHeight);
            _ = input.ActualHeight.Should().Be(barHeight);
            _ = accessory.ActualHeight.Should().BeApproximately(input.ActualHeight, 0.1);
            _ = accessory.ActualWidth.Should().BeApproximately(barHeight, 0.1);
            _ = accessory.TransformToVisual(control).TransformPoint(default).Y.Should()
                .BeApproximately(input.TransformToVisual(control).TransformPoint(default).Y, 0.1);
            _ = input.FontSize.Should().Be(14);
            _ = icon.FontSize.Should().Be(14);
            if (compact)
            {
                var nativePadding = (Thickness)selectedStyles.MergedDictionaries[0]["TextControlThemePadding"];
                _ = input.Padding.Top.Should().Be(nativePadding.Top);
                _ = input.Padding.Bottom.Should().Be(nativePadding.Bottom);
            }

            // Let the native text input measure larger text rather than clipping it to a fixed density height.
            input.FontSize = 36;
            control.UpdateLayout();
            _ = input.ActualHeight.Should().BeGreaterThan(barHeight);
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
            var input = control.FindDescendant<TextBox>(e => string.Equals(e.Name, "PartFilterTextBox", StringComparison.Ordinal))!;
            _ = input.Text.Should().Be("R");
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

            var bar = control.FindDescendant<Grid>(e => string.Equals(e.Name, "PartFilterBar", StringComparison.Ordinal))!;
            _ = bar.FindDescendant<Button>(e => ReferenceEquals(e, accessory)).Should().BeSameAs(accessory);
            return Task.CompletedTask;
        });

    [TestMethod]
    public Task FilterBar_FocusingInputDoesNotRedirectFocusToTreeItems_Async() => EnqueueAsync(
        async () =>
        {
            var control = this.tree!;
            var vm = this.viewModel!;
            vm.SelectAllCommand.Execute(parameter: null);
            control.IsFilterBarEnabled = true;
            var input = control.FindDescendant<TextBox>(e => string.Equals(e.Name, "PartFilterTextBox", StringComparison.Ordinal))!;
            _ = input.Focus(FocusState.Programmatic).Should().BeTrue();
            await Task.Yield();

            _ = FocusManager.GetFocusedElement(control.XamlRoot).Should().BeSameAs(input);
            _ = vm.SelectedItemsCount.Should().Be(vm.ShownItemsCount);
        });

    [TestMethod]
    public Task FilteringDisabled_RendersUnfilteredShownItems_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (ITreeItem)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Expand the first two children so grandchildren become part of the shown-items list.
            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = false;

            var repeater = this.tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
            _ = repeater.Should().NotBeNull();

            _ = repeater!.ItemsSourceView.Count.Should().Be(vm.ShownItemsCount);
        });

    [TestMethod]
    public Task FilteringEnabled_RendersMatchesPlusAncestors_InPreOrder_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (ITreeItem)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = true;

            var repeater = this.tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
            _ = repeater.Should().NotBeNull();

            var items = repeater!.ItemsSourceView;
            _ = items.Count.Should().Be(5);

            _ = ((ITreeItem)items.GetAt(0)).Label.Should().Be("R");
            _ = ((ITreeItem)items.GetAt(1)).Label.Should().Be("R-C1");
            _ = ((ITreeItem)items.GetAt(2)).Label.Should().Be("R-C1-GC1");
            _ = ((ITreeItem)items.GetAt(3)).Label.Should().Be("R-C2");
            _ = ((ITreeItem)items.GetAt(4)).Label.Should().Be("R-C2-GC1");

            // Filtering must not mutate operation semantics.
            _ = vm.ShownItemsCount.Should().Be(7);
        });

    [TestMethod]
    public Task SelectionCommands_ActOnAllUnfilteredShownItems_EvenWhenFilteringEnabled_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (ITreeItem)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);

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
}
