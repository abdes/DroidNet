// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using Moq;
using Moq.Protected;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Unit test cases for the <see cref="SelectionModel{T}" /> class.
/// </summary>
[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory($"{nameof(DynamicTree)} / {nameof(TreeItemAdapter)}")]
public class TreeItemAdapterTests
{
    [TestMethod]
    public void RootItem_IsAlwaysLocked()
    {
        var sutMock = new Mock<TreeItemAdapter>(true, false) { CallBase = true };
        var sut = sutMock.Object;

        _ = sut.IsRoot.Should().BeTrue();

        _ = sut.IsLocked.Should().BeTrue();

        sut.IsLocked = true;
        _ = sut.IsLocked.Should().BeTrue();

        sut.IsLocked = false;
        _ = sut.IsLocked.Should().BeTrue();
    }

    [TestMethod]
    public void NonRootItem_StartsUnlocked()
    {
        var sutMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        _ = sut.IsLocked.Should().BeFalse();
    }

    [TestMethod]
    public void NonRootItem_CanBeLockedAndUnlocked()
    {
        var sutMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        _ = sut.IsLocked.Should().BeFalse();
        sut.IsLocked = true;
        _ = sut.IsLocked.Should().BeTrue();
        sut.IsLocked = false;
        _ = sut.IsLocked.Should().BeFalse();
    }

    [TestMethod]
    public void RootItem_WhenHidden_HasMinusOneDepth()
    {
        var sutMock = new Mock<TreeItemAdapter>(true, true) { CallBase = true };
        var sut = sutMock.Object;
        _ = sut.IsRoot.Should().BeTrue();

        _ = sut.Depth.Should().Be(-1);
    }

    [TestMethod]
    public void RootItem_WhenNotHidden_HasZeroDepth()
    {
        var sutMock = new Mock<TreeItemAdapter>(true, false) { CallBase = true };
        var sut = sutMock.Object;
        _ = sut.IsRoot.Should().BeTrue();

        _ = sut.Depth.Should().Be(0);
    }

    [TestMethod]
    public async Task Children_IsLazilyInitialized_OnlyOneTime()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        _ = sutMock.Setup<Task>(a => a.LoadChildrenPublic())
            .Returns(
                async () =>
                {
                    sut.AddChildInternalPublic(child);
                    await Task.CompletedTask.ConfigureAwait(false);
                });

        var children = await sut.Children.ConfigureAwait(false);

        _ = children.Count.Should().Be(1);
        _ = children.Should().Contain(child);

        // Access the Children collection again - No initialization should happen
        _ = await sut.Children.ConfigureAwait(false);

        sutMock.Protected().Verify<Task>("LoadChildren", Times.Once());
    }

    [TestMethod]
    public async Task ChildrenCount_WhenChildrenLoaded_ShouldNotCallGetChildrenCount()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        _ = sutMock.Setup<Task>(a => a.LoadChildrenPublic())
            .Returns(
                async () =>
                {
                    sut.AddChildInternalPublic(child);
                    await Task.CompletedTask.ConfigureAwait(false);
                });

        _ = await sut.Children.ConfigureAwait(false);

        // After initialization, GetChildrenCount should not be called anymore
        _ = sut.ChildrenCount.Should().Be(1);
        sutMock.Protected().Verify("DoGetChildrenCount", Times.Never());
    }

    [TestMethod]
    public void ChildrenCount_WhenChildrenNotLoaded_ShouldCallGetChildrenCount()
    {
        var sutMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        const int childrenCount = 1;
        _ = sutMock.Protected().Setup<int>("DoGetChildrenCount").Returns(childrenCount);

        _ = sut.ChildrenCount.Should().Be(childrenCount);
        sutMock.Protected().Verify<int>("DoGetChildrenCount", Times.Once());
    }

    [TestMethod]
    public async Task AddChild_WhenChildHasParent_Throws()
    {
        var parentMock = new Mock<TreeItemAdapterStub>(true, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var child = childMock.Object;
        await parentMock.Object.AddChildAsync(child).ConfigureAwait(false);
        _ = child.Parent.Should().NotBeNull();

        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        var act = async () => await sut.AddChildAsync(child).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task AddChild_WhenChildIsRoot_Throws()
    {
        var childMock = new Mock<TreeItemAdapter>(true, false) { CallBase = true };
        var child = childMock.Object;
        _ = child.IsRoot.Should().BeTrue();

        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        var act = async () => await sut.AddChildAsync(child).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task AddChild_WhenChildIsSelf_Throws()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        var act = async () => await sut.AddChildAsync(sut).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task AddChild_AddsChildAndUpdatesItsProperties()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var child1Mock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var child2Mock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child1 = child1Mock.Object;
        var child2 = child2Mock.Object;

        await sut.AddChildAsync(child1).ConfigureAwait(false);
        await sut.AddChildAsync(child2).ConfigureAwait(false);

        _ = sut.ChildrenCount.Should().Be(2);
        var children = await sut.Children.ConfigureAwait(false);
        _ = children.Count.Should().Be(2);
        _ = children.Should().ContainInConsecutiveOrder([child1, child2]);

        _ = child1.Depth.Should().Be(1);
        _ = child1.Parent.Should().Be(sut);

        _ = child2.Depth.Should().Be(1);
        _ = child2.Parent.Should().Be(sut);
    }

    [TestMethod]
    public async Task InsertChild_WhenChildHasParent_Throws()
    {
        var parentMock = new Mock<TreeItemAdapterStub>(true, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var child = childMock.Object;
        await parentMock.Object.AddChildAsync(child).ConfigureAwait(false);
        _ = child.Parent.Should().NotBeNull();

        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        var act = async () => await sut.InsertChildAsync(0, child).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task InsertChild_WhenChildIsRoot_Throws()
    {
        var childMock = new Mock<TreeItemAdapter>(true, false) { CallBase = true };
        var child = childMock.Object;
        _ = child.IsRoot.Should().BeTrue();

        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        var act = async () => await sut.InsertChildAsync(0, child).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task InsertChild_WhenChildIsSelf_Throws()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var sut = sutMock.Object;

        var act = async () => await sut.InsertChildAsync(0, sut).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task InsertChild_InsertsChildAtCorrectIndexAndUpdatesItsProperties()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var child1Mock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var child2Mock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child1 = child1Mock.Object;
        var child2 = child2Mock.Object;

        await sut.InsertChildAsync(0, child1).ConfigureAwait(false);
        await sut.InsertChildAsync(0, child2).ConfigureAwait(false);

        _ = sut.ChildrenCount.Should().Be(2);
        var children = await sut.Children.ConfigureAwait(false);
        _ = children.Count.Should().Be(2);
        _ = children.Should().ContainInConsecutiveOrder([child2, child1]);

        _ = child1.Depth.Should().Be(1);
        _ = child1.Parent.Should().Be(sut);

        _ = child2.Depth.Should().Be(1);
        _ = child2.Parent.Should().Be(sut);
    }

    [TestMethod]
    public async Task InsertChild_WhenIndexOutOfRange_Throws()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        var act = async () => await sut.InsertChildAsync(1, child).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<ArgumentOutOfRangeException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RemoveChild_WhenItemIsNotChild_ReturnsNegativeOne()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        var result = await sut.RemoveChildAsync(child).ConfigureAwait(false);

        _ = result.Should().Be(-1);
    }

    [TestMethod]
    public async Task RemoveChild_WhenItemIsChild_ShouldRemoveChild()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var child1Mock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var child2Mock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child1 = child1Mock.Object;
        var child2 = child2Mock.Object;
        await sut.AddChildAsync(child1).ConfigureAwait(false);
        await sut.AddChildAsync(child2).ConfigureAwait(false);

        var result = await sut.RemoveChildAsync(child2).ConfigureAwait(false);
        _ = result.Should().Be(1);

        result = await sut.RemoveChildAsync(child1).ConfigureAwait(false);
        _ = result.Should().Be(0);
    }

    [TestMethod]
    public async Task RemoveChild_ShouldResetChildParentAndDepth()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;
        await sut.AddChildAsync(child).ConfigureAwait(false);

        _ = await sut.RemoveChildAsync(child).ConfigureAwait(false);

        _ = child.Depth.Should().Be(int.MinValue);
        _ = child.Parent.Should().BeNull();
    }

    /*
     * ChildrenCollectionChanged event
     */

    [TestMethod]
    public async Task LoadChildren_ShouldFireEvent_Reset()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        _ = sutMock.Setup<Task>(a => a.LoadChildrenPublic())
            .Returns(
                async () =>
                {
                    sut.AddChildInternalPublic(child);
                    await Task.CompletedTask.ConfigureAwait(false);
                });

        using var monitoredSubject = sut.Monitor();

        _ = await sut.Children.ConfigureAwait(false);

        _ = monitoredSubject
            .Should()
            .Raise("ChildrenCollectionChanged")
            .WithSender(sut)
            .WithArgs<NotifyCollectionChangedEventArgs>(args => args.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public async Task AddChild_ShouldFireEvent_Add()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        using var monitoredSubject = sut.Monitor();

        await sut.AddChildAsync(child).ConfigureAwait(false);

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
    public async Task InsertChild_ShouldFireEvent_Add()
    {
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;

        using var monitoredSubject = sut.Monitor();

        await sut.InsertChildAsync(0, child).ConfigureAwait(false);

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
        var sutMock = new Mock<TreeItemAdapterStub>(false, false) { CallBase = true };
        var childMock = new Mock<TreeItemAdapter>(false, false) { CallBase = true };
        var sut = sutMock.Object;
        var child = childMock.Object;
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

    internal abstract class TreeItemAdapterStub(bool isRoot = false, bool isHidden = false)
        : TreeItemAdapter(isRoot, isHidden)
    {
        /// <inheritdoc/>
        public override required string Label { get; set; }

        public abstract Task LoadChildrenPublic();

        public void AddChildInternalPublic(TreeItemAdapter child) => this.AddChildInternal(child);

        /// <inheritdoc/>
        protected override Task LoadChildren() => this.LoadChildrenPublic();
    }
}
