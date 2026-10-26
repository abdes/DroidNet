// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// The tree's pointer-press selection, driven through the press hooks with explicit modifiers: a left press selects and
/// focuses, a press on a row of a multiple selection is deferred until release so the whole selection can be dragged,
/// and a row that is not a <see cref="TreeItemAdapter"/> follows the same rules.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreePointerTests : VisualUserInterfaceTests, IDisposable
{
    private TestableDynamicTree? tree;
    private TestViewModel? viewModel;
    private GenericItem? first;
    private GenericItem? second;
    private TestItemAdapter? adapter;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task Press_WithTheLeftButton_SelectsAndFocusesTheRowFromThePointer_Async() => EnqueueAsync(() =>
    {
        var handled = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);

        _ = handled.Should().BeTrue();
        _ = this.adapter!.IsSelected.Should().BeTrue();
        _ = this.viewModel!.FocusedItem.Should().NotBeNull();
        _ = this.viewModel.FocusedItem!.Item.Should().BeSameAs(this.adapter);
        _ = this.viewModel.FocusedItem.Origin.Should().Be(RequestOrigin.PointerInput);
    });

    [TestMethod]
    public Task Press_WithAnotherButton_IsNotHandledAndSelectsNothing_Async() => EnqueueAsync(() =>
    {
        var handled = this.tree!.InvokeTreeItemPointerPressed(this.adapter!, leftButtonPressed: false);

        _ = handled.Should().BeFalse();
        _ = this.viewModel!.SelectedItemsCount.Should().Be(0);
    });

    [TestMethod]
    public Task Press_OnARowOfAMultipleSelection_DefersTheSelectionChange_Async() => EnqueueAsync(() =>
    {
        _ = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);
        _ = this.tree.InvokeTreeItemPointerPressed(this.first!, isControlDown: true);
        _ = this.viewModel!.SelectedItemsCount.Should().Be(2);

        var handled = this.tree.InvokeTreeItemPointerPressed(this.adapter!);

        _ = handled.Should().BeTrue();
        _ = this.viewModel.SelectedItemsCount.Should().Be(2, "a drag of the whole selection may start from this press");
        _ = this.first!.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task Press_OnAnUnselectedRow_CollapsesAMultipleSelectionToIt_Async() => EnqueueAsync(() =>
    {
        _ = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);
        _ = this.tree.InvokeTreeItemPointerPressed(this.first!, isControlDown: true);

        _ = this.tree.InvokeTreeItemPointerPressed(this.second!);

        _ = this.viewModel!.SelectedItemsCount.Should().Be(1);
        _ = this.second!.IsSelected.Should().BeTrue();
        _ = this.adapter!.IsSelected.Should().BeFalse();
        _ = this.first!.IsSelected.Should().BeFalse();
    });

    [TestMethod]
    public Task Press_OnAGenericRow_SelectsIt_Async() => EnqueueAsync(() =>
    {
        _ = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);

        var handled = this.tree.InvokeTreeItemPointerPressed(this.first!);

        _ = handled.Should().BeTrue();
        _ = this.first!.IsSelected.Should().BeTrue();
        _ = this.adapter!.IsSelected.Should().BeFalse();
        _ = this.viewModel!.FocusedItem!.Item.Should().BeSameAs(this.first);
    });

    [TestMethod]
    public Task Press_OnAGenericRowWithControl_KeepsTheMultipleSelection_Async() => EnqueueAsync(() =>
    {
        _ = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);

        var handled = this.tree.InvokeTreeItemPointerPressed(this.first!, isControlDown: true);

        _ = handled.Should().BeTrue();
        _ = this.viewModel!.SelectedItemsCount.Should().Be(2);
        _ = this.adapter!.IsSelected.Should().BeTrue();
        _ = this.first!.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task Press_OnASelectedGenericRowWithControl_TogglesItOff_Async() => EnqueueAsync(() =>
    {
        _ = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);
        _ = this.tree.InvokeTreeItemPointerPressed(this.first!, isControlDown: true);

        _ = this.tree.InvokeTreeItemPointerPressed(this.first!, isControlDown: true);

        _ = this.first!.IsSelected.Should().BeFalse();
        _ = this.adapter!.IsSelected.Should().BeTrue();
        _ = this.viewModel!.SelectedItemsCount.Should().Be(1);
    });

    [TestMethod]
    public Task Press_OnAGenericRowWithShift_ExtendsTheSelectionOverTheRange_Async() => EnqueueAsync(() =>
    {
        _ = this.tree!.InvokeTreeItemPointerPressed(this.adapter!);

        _ = this.tree.InvokeTreeItemPointerPressed(this.second!, isShiftDown: true);

        _ = this.viewModel!.SelectedItemsCount.Should().Be(3);
        _ = this.first!.IsSelected.Should().BeTrue("the row between the anchor and the target is part of the range");
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.adapter = new TestItemAdapter(new TestTreeItem { Label = "Adapter" });
                this.first = new GenericItem("First");
                this.second = new GenericItem("Second");
                var root = new GenericItem("Root", this.adapter, this.first, this.second);

                this.viewModel = new TestViewModel(this.LoggerFactory) { SelectionMode = SelectionMode.Multiple };
                await this.viewModel.InitializeRootAsync(root, skipRoot: true).ConfigureAwait(true);
                this.tree = new TestableDynamicTree { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    /// <summary>A flat row that is deliberately not a <see cref="TreeItemAdapter"/>.</summary>
    private sealed class GenericItem : ITreeItem
    {
        private readonly ReadOnlyObservableCollection<ITreeItem> children;

        public GenericItem(string label, params ITreeItem[] children)
        {
            this.Label = label;
            this.children = new ReadOnlyObservableCollection<ITreeItem>(new ObservableCollection<ITreeItem>(children));
        }

        public event EventHandler<NotifyCollectionChangedEventArgs>? ChildrenCollectionChanged
        {
            add { }
            remove { }
        }

        public string Label { get; set; }

        public bool IsSelected { get; set; }

        public bool IsLocked { get; set; }

        public bool IsExpanded { get; set; }

        public bool IsCut { get; set; }

        public bool IsRoot { get; init; }

        public ITreeItem? Parent => null;

        public int Depth => 0;

        public Task<ReadOnlyObservableCollection<ITreeItem>> Children => Task.FromResult(this.children);

        public int ChildrenCount => this.children.Count;

        public bool CanAcceptChildren => false;

        public bool ValidateItemName(string name) => !string.IsNullOrWhiteSpace(name);

        public Task AddChildAsync(ITreeItem child) => throw new NotSupportedException();

        public Task InsertChildAsync(int index, ITreeItem child) => throw new NotSupportedException();

        public Task<int> RemoveChildAsync(ITreeItem child) => throw new NotSupportedException();

        public bool Equals(ITreeItem? other) => ReferenceEquals(this, other);

        public override bool Equals(object? obj) => obj is ITreeItem other && this.Equals(other);

        public override int GetHashCode() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);
    }
}
