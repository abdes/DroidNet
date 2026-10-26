// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Unit test cases for the <see cref="TreeItemAdapter" /> class.
/// </summary>
[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory($"{nameof(DynamicTree)} / {nameof(TreeItemAdapter)}")]
public class TreeItemAdapterTests
{
    [TestMethod]
    public void RootItem_IsAlwaysLocked()
    {
        var sut = new TestTreeItemAdapter(isRoot: true) { Label = "Root" };

        _ = sut.IsRoot.Should().BeTrue();
        _ = sut.IsLocked.Should().BeTrue();

        sut.IsLocked = true;
        _ = sut.IsLocked.Should().BeTrue();

        sut.IsLocked = false;
        _ = sut.IsLocked.Should().BeTrue();
    }

    [TestMethod]
    public void NonRootItem_CanBeLockedAndUnlocked()
    {
        var sut = new TestTreeItemAdapter { Label = "Item" };

        _ = sut.IsLocked.Should().BeFalse();
        sut.IsLocked = true;
        _ = sut.IsLocked.Should().BeTrue();
        sut.IsLocked = false;
        _ = sut.IsLocked.Should().BeFalse();
    }

    [TestMethod]
    [DataRow(true, -1)]
    [DataRow(false, 0)]
    public void RootItem_Depth_DependsOnWhetherItIsHidden(bool isHidden, int expectedDepth)
    {
        var sut = new RootAdapter(isHidden) { Label = "Root" };

        _ = sut.IsRoot.Should().BeTrue();
        _ = sut.Depth.Should().Be(expectedDepth);
    }

    [TestMethod]
    public async Task Children_AreLoadedOnlyOnce()
    {
        var sut = new LazyTreeItem("Parent", childCount: 2);

        var first = await sut.Children.ConfigureAwait(false);
        var second = await sut.Children.ConfigureAwait(false);

        _ = first.Should().HaveCount(2);
        _ = second.Should().BeSameAs(first);
        _ = sut.LoadCount.Should().Be(1);
    }

    [TestMethod]
    public void ChildrenCount_BeforeLoading_IsAvailableWithoutLoadingChildren()
    {
        var sut = new LazyTreeItem("Parent", childCount: 3);

        _ = sut.ChildrenCount.Should().Be(3);

        _ = sut.LoadCount.Should().Be(0);
        _ = sut.AreChildrenLoaded.Should().BeFalse();
    }

    [TestMethod]
    public async Task ChildrenCount_AfterLoading_ReflectsTheLoadedCollection()
    {
        var sut = new LazyTreeItem("Parent", childCount: 3);
        _ = await sut.Children.ConfigureAwait(false);

        await sut.AddChildAsync(new TestTreeItemAdapter { Label = "Extra" }).ConfigureAwait(false);

        _ = sut.ChildrenCount.Should().Be(4);
        _ = sut.LoadCount.Should().Be(1);
    }

    [TestMethod]
    [DataRow(false, InvalidChild.HasParent)]
    [DataRow(false, InvalidChild.IsRoot)]
    [DataRow(false, InvalidChild.IsSelf)]
    [DataRow(true, InvalidChild.HasParent)]
    [DataRow(true, InvalidChild.IsRoot)]
    [DataRow(true, InvalidChild.IsSelf)]
    public async Task AddOrInsertChild_WhenChildIsNotAddable_Throws(bool useInsert, InvalidChild kind)
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child = await CreateInvalidChildAsync(sut, kind).ConfigureAwait(false);

        var act = async () =>
        {
            if (useInsert)
            {
                await sut.InsertChildAsync(0, child).ConfigureAwait(false);
            }
            else
            {
                await sut.AddChildAsync(child).ConfigureAwait(false);
            }
        };

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task AddChild_AddsChildrenInOrderAndUpdatesTheirProperties()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child1 = new TestTreeItemAdapter { Label = "Child1" };
        var child2 = new TestTreeItemAdapter { Label = "Child2" };

        await sut.AddChildAsync(child1).ConfigureAwait(false);
        await sut.AddChildAsync(child2).ConfigureAwait(false);

        _ = sut.ChildrenCount.Should().Be(2);
        var children = await sut.Children.ConfigureAwait(false);
        _ = children.Should().Equal(child1, child2);
        _ = child1.Depth.Should().Be(1);
        _ = child1.Parent.Should().BeSameAs(sut);
        _ = child2.Depth.Should().Be(1);
        _ = child2.Parent.Should().BeSameAs(sut);
    }

    [TestMethod]
    public async Task InsertChild_InsertsChildAtIndexAndUpdatesItsProperties()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child1 = new TestTreeItemAdapter { Label = "Child1" };
        var child2 = new TestTreeItemAdapter { Label = "Child2" };

        await sut.InsertChildAsync(0, child1).ConfigureAwait(false);
        await sut.InsertChildAsync(0, child2).ConfigureAwait(false);

        var children = await sut.Children.ConfigureAwait(false);
        _ = children.Should().Equal(child2, child1);
        _ = child1.Depth.Should().Be(1);
        _ = child1.Parent.Should().BeSameAs(sut);
        _ = child2.Depth.Should().Be(1);
        _ = child2.Parent.Should().BeSameAs(sut);
    }

    [TestMethod]
    public async Task InsertChild_WhenIndexOutOfRange_Throws()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child = new TestTreeItemAdapter { Label = "Child" };

        var act = async () => await sut.InsertChildAsync(1, child).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<ArgumentOutOfRangeException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RemoveChild_WhenItemIsNotChild_ReturnsNegativeOne()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child = new TestTreeItemAdapter { Label = "Child" };

        var result = await sut.RemoveChildAsync(child).ConfigureAwait(false);

        _ = result.Should().Be(-1);
    }

    [TestMethod]
    public async Task RemoveChild_WhenItemIsChild_ReturnsItsIndexAndRemovesIt()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child1 = new TestTreeItemAdapter { Label = "Child1" };
        var child2 = new TestTreeItemAdapter { Label = "Child2" };
        await sut.AddChildAsync(child1).ConfigureAwait(false);
        await sut.AddChildAsync(child2).ConfigureAwait(false);

        var result = await sut.RemoveChildAsync(child2).ConfigureAwait(false);
        _ = result.Should().Be(1);

        result = await sut.RemoveChildAsync(child1).ConfigureAwait(false);
        _ = result.Should().Be(0);
        _ = (await sut.Children.ConfigureAwait(false)).Should().BeEmpty();
    }

    [TestMethod]
    public async Task RemoveChild_ShouldResetChildParent()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child = new TestTreeItemAdapter { Label = "Child" };
        await sut.AddChildAsync(child).ConfigureAwait(false);

        _ = await sut.RemoveChildAsync(child).ConfigureAwait(false);

        _ = child.Parent.Should().BeNull();
    }

    [TestMethod]
    public async Task LoadChildren_ShouldFireEvent_Reset()
    {
        var sut = new LazyTreeItem("Parent", childCount: 1);

        using var monitoredSubject = sut.Monitor();

        _ = await sut.Children.ConfigureAwait(false);

        _ = monitoredSubject
            .Should()
            .Raise("ChildrenCollectionChanged")
            .WithSender(sut)
            .WithArgs<NotifyCollectionChangedEventArgs>(args => args.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task AddOrInsertChild_ShouldFireEvent_Add(bool useInsert)
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child = new TestTreeItemAdapter { Label = "Child" };

        using var monitoredSubject = sut.Monitor();

        if (useInsert)
        {
            await sut.InsertChildAsync(0, child).ConfigureAwait(false);
        }
        else
        {
            await sut.AddChildAsync(child).ConfigureAwait(false);
        }

        _ = monitoredSubject
            .Should()
            .Raise("ChildrenCollectionChanged")
            .WithSender(sut)
            .WithArgs<NotifyCollectionChangedEventArgs>(args => args.Action == NotifyCollectionChangedAction.Add)
            .WithArgs<NotifyCollectionChangedEventArgs>(args => args.NewStartingIndex == 0)
            .WithArgs<NotifyCollectionChangedEventArgs>(
                args => args.NewItems != null && args.NewItems.Count == 1 && args.NewItems.Contains(child));
    }

    [TestMethod]
    public async Task RemoveChild_ShouldFireEvent_Remove()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        var child = new TestTreeItemAdapter { Label = "Child" };
        await sut.AddChildAsync(child).ConfigureAwait(false);

        using var monitoredSubject = sut.Monitor();

        var index = await sut.RemoveChildAsync(child).ConfigureAwait(false);
        _ = index.Should().Be(0);

        _ = monitoredSubject
            .Should()
            .Raise("ChildrenCollectionChanged")
            .WithSender(sut)
            .WithArgs<NotifyCollectionChangedEventArgs>(args => args.Action == NotifyCollectionChangedAction.Remove)
            .WithArgs<NotifyCollectionChangedEventArgs>(args => args.OldStartingIndex == index)
            .WithArgs<NotifyCollectionChangedEventArgs>(
                args => args.OldItems != null && args.OldItems.Count == 1 && args.OldItems.Contains(child));
    }

    [TestMethod]
    public async Task AddChild_RaisesTheEventOnceAfterTheChildParentIsSet()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        _ = await sut.Children.ConfigureAwait(false);
        var child = new TestTreeItemAdapter { Label = "Child" };
        var raised = new List<(NotifyCollectionChangedAction Action, int Index, ITreeItem? ParentAtRaise)>();
        sut.ChildrenCollectionChanged += (_, args) => raised.Add((args.Action, args.NewStartingIndex, child.Parent));

        await sut.AddChildAsync(child).ConfigureAwait(false);

        _ = raised.Should().Equal((NotifyCollectionChangedAction.Add, 0, (ITreeItem?)sut));
    }

    [TestMethod]
    public async Task InsertChild_RaisesTheEventOnceWithTheInsertIndex()
    {
        var sut = new TestTreeItemAdapter { Label = "Sut" };
        await sut.AddChildAsync(new TestTreeItemAdapter { Label = "First" }).ConfigureAwait(false);
        await sut.AddChildAsync(new TestTreeItemAdapter { Label = "Second" }).ConfigureAwait(false);
        var raised = new List<NotifyCollectionChangedEventArgs>();
        sut.ChildrenCollectionChanged += (_, args) => raised.Add(args);

        await sut.InsertChildAsync(1, new TestTreeItemAdapter { Label = "Inserted" }).ConfigureAwait(false);

        _ = raised.Should().ContainSingle().Which.NewStartingIndex.Should().Be(1);
    }

    [TestMethod]
    public async Task Children_WhenLoadingFailed_ReleasesThePartialResultAndLoadsAgainOnTheNextAccess()
    {
        var sut = new ControlledLoadItem("Item", childCount: 2) { FailFirstLoad = true };
        var firstAttempt = async () => await sut.Children.ConfigureAwait(false);
        _ = await firstAttempt.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = sut.AreChildrenLoaded.Should().BeFalse();

        var children = await sut.Children.ConfigureAwait(false);

        _ = children.Should().HaveCount(2);
        _ = sut.AreChildrenLoaded.Should().BeTrue();
        _ = sut.ChildrenCount.Should().Be(2);
        _ = sut.LoadCount.Should().Be(2);
    }

    [TestMethod]
    public async Task Depth_UpdatesRecursively_WhenParentChanges()
    {
        var root = new TestTreeItemAdapter(isRoot: true) { Label = "Root" };
        var folder = new TestTreeItemAdapter { Label = "Folder" };
        var parent = new TestTreeItemAdapter { Label = "Parent" };
        var child = new TestTreeItemAdapter { Label = "Child" };

        // Build the initial tree: Root -> Parent -> Child, and Root -> Folder.
        await root.AddChildAsync(parent).ConfigureAwait(false);
        await parent.AddChildAsync(child).ConfigureAwait(false);
        await root.AddChildAsync(folder).ConfigureAwait(false);
        _ = new[] { root.Depth, parent.Depth, child.Depth, folder.Depth }.Should().Equal(0, 1, 2, 1);

        // Act: move Parent under Folder.
        _ = await root.RemoveChildAsync(parent).ConfigureAwait(false);
        await folder.AddChildAsync(parent).ConfigureAwait(false);

        // Assert
        _ = parent.Depth.Should().Be(2);
        _ = child.Depth.Should().Be(3);
    }

    private static async Task<TestTreeItemAdapter> CreateInvalidChildAsync(TestTreeItemAdapter sut, InvalidChild kind)
    {
        switch (kind)
        {
            case InvalidChild.IsSelf:
                return sut;

            case InvalidChild.IsRoot:
                return new TestTreeItemAdapter(isRoot: true) { Label = "Root" };

            default:
                var child = new TestTreeItemAdapter { Label = "Child" };
                var otherParent = new TestTreeItemAdapter { Label = "Other" };
                await otherParent.AddChildAsync(child).ConfigureAwait(false);
                return child;
        }
    }

    public enum InvalidChild
    {
        HasParent,
        IsRoot,
        IsSelf,
    }

    /// <summary>
    /// A root item that can be hidden, which <see cref="TestTreeItemAdapter"/> does not expose.
    /// </summary>
    private sealed class RootAdapter(bool isHidden) : TreeItemAdapter(isRoot: true, isHidden)
    {
        public override required string Label { get; set; }

        public override bool ValidateItemName(string name) => true;

        protected override int DoGetChildrenCount() => 0;

        protected override Task LoadChildren() => Task.CompletedTask;
    }
}
