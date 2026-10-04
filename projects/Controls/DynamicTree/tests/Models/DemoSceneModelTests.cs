// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls.Demo.Tree.Model;
using DroidNet.Controls.Demo.Tree.Services;

namespace DroidNet.Controls.Tests;

[TestClass]
public sealed class DemoSceneModelTests
{
    [TestMethod]
    public async Task ChildrenCountNotifiesAfterInsertionAndRemoval()
    {
        var parent = new EntityAdapter(new Entity("Parent"));
        var child = new EntityAdapter(new Entity("Child"));
        var counts = new List<int>();
        parent.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(parent.ChildrenCount), StringComparison.Ordinal))
            {
                counts.Add(parent.ChildrenCount);
            }
        };
        await parent.AddChildAsync(child).ConfigureAwait(false);
        _ = await parent.RemoveChildAsync(child).ConfigureAwait(false);
        _ = counts.Should().Equal(1, 0);
    }

    [TestMethod]
    public async Task SceneRootLoadsOneHierarchyAndCannotBeInsertedOrRemoved()
    {
        var scene = new SceneAdapter(new Scene("Scene 1"));
        var children = await scene.Children.ConfigureAwait(false);
        _ = scene.IsRoot.Should().BeTrue();
        _ = scene.IsLocked.Should().BeTrue();
        _ = scene.Parent.Should().BeNull();
        _ = children.Should().OnlyContain(item => item is EntityAdapter);
        var service = new DomainModelService();
        _ = service.TryInsert(scene, children[0], 0, out _).Should().BeFalse();
        _ = service.TryRemove(scene, children[0], out _).Should().BeFalse();
    }

    [TestMethod]
    public async Task InsertClonedSubtreeSynchronizesNestedModelAndSupportsDescendantRemoval()
    {
        var original = new EntityAdapter(new Entity("Environment")
        {
            Entities = [new Entity("Props") { Entities = [new Entity("Crate")] }],
        });
        var props = (EntityAdapter)(await original.Children.ConfigureAwait(false))[0];
        var crate = (EntityAdapter)(await props.Children.ConfigureAwait(false))[0];
        var clone = (EntityAdapter)original.CloneSelf();
        var propsClone = (EntityAdapter)props.CloneSelf();
        var crateClone = (EntityAdapter)crate.CloneSelf();
        await propsClone.AddChildAsync(crateClone).ConfigureAwait(false);
        await clone.AddChildAsync(propsClone).ConfigureAwait(false);
        var scene = new SceneAdapter(new Scene("Destination"));
        var service = new DomainModelService();
        _ = service.TryInsert(clone, scene, 0, out _).Should().BeTrue();
        _ = scene.AttachedObject.Entities[0].Should().BeSameAs(clone.AttachedObject);
        _ = clone.AttachedObject.Entities[0].Should().BeSameAs(propsClone.AttachedObject);
        _ = propsClone.AttachedObject.Entities[0].Should().BeSameAs(crateClone.AttachedObject);
        _ = service.TryRemove(crateClone, propsClone, out _).Should().BeTrue();
        _ = propsClone.AttachedObject.Entities.Should().BeEmpty();
        _ = props.AttachedObject.Entities.Should().ContainSingle();
    }
}
