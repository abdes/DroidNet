// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Schemas.Bindings;

namespace Oxygen.Editor.Schemas.Tests;

[TestClass]
public sealed class PropertyPipelineContractTests
{
    private static readonly PropertyId<float> X = new("test", "/x");
    private static readonly PropertyDescriptor<float> XDescriptor = new(
        X,
        static target => ((Target)target).X,
        static (target, value) => ((Target)target).X = value,
        static value => float.IsFinite(value) ? ValidationResult.Ok : ValidationResult.Fail("bad", "bad"),
        new EditorAnnotation { Label = "X", Renderer = "numberbox" },
        "test.x");

    [TestMethod]
    public async Task ApplyAsync_WhenUndoRedoSnapshotsAreSwapped_ShouldPreserveStructuralIdentity()
    {
        var nodeId = Guid.NewGuid();
        var target = new Target { X = 1.0f };
        var descriptors = new Dictionary<PropertyId, PropertyDescriptor> { [X.Id] = XDescriptor };
        var before = new PropertySnapshot(new Dictionary<Guid, PropertyEdit> { [nodeId] = PropertyEdit.Single(X, 1.0f) });
        var after = new PropertySnapshot(new Dictionary<Guid, PropertyEdit> { [nodeId] = PropertyEdit.Single(X, 5.0f) });
        var op = new PropertyOp([nodeId], before, after, "Set X");
        var resolver = new TargetResolver(nodeId, target);

        await PropertyApply.ApplyAsync(op, ApplySide.After, resolver, descriptors).ConfigureAwait(false);
        var stateAfterOriginalApply = target.X;

        await PropertyApply.ApplyAsync(op, ApplySide.Before, resolver, descriptors).ConfigureAwait(false);
        await PropertyApply.ApplyAsync(op, ApplySide.After, resolver, descriptors).ConfigureAwait(false);

        _ = target.X.Should().Be(stateAfterOriginalApply);
        _ = resolver.Pushed.Should().HaveCount(3);
        _ = resolver.Pushed.Should().OnlyContain(edit => edit.Contains(X.Id));
    }

    [TestMethod]
    public void PropertyBinding_UpdateFromModel_ShouldRepresentMixedValuesAsBindingState()
    {
        var first = Guid.NewGuid();
        var second = Guid.NewGuid();
        var targets = new Dictionary<Guid, Target>
        {
            [first] = new() { X = 1.0f },
            [second] = new() { X = 2.0f },
        };
        var binding = new PropertyBinding<float>(XDescriptor);

        binding.UpdateFromModel([first, second], id => targets[id]);

        _ = binding.HasValue.Should().BeTrue();
        _ = binding.IsMixed.Should().BeTrue();
        _ = binding.Value.Should().Be(1.0f);
        _ = binding.Nodes.Should().Equal(first, second);
    }

    [TestMethod]
    public void ApplyToTarget_WhenDescriptorIsMissing_ShouldFailWithPropertyId()
    {
        var target = new Target { X = 1.0f };
        var edit = PropertyEdit.Single(X, 2.0f);

        var act = () => PropertyApply.ApplyToTarget(
            target,
            edit,
            new Dictionary<PropertyId, PropertyDescriptor>());

        _ = act.Should().Throw<KeyNotFoundException>()
            .WithMessage("*test#/x*");
        _ = target.X.Should().Be(1.0f);
    }

    [TestMethod]
    public async Task CommitGroupController_WhenSessionClosesBeforeIdleDelay_ShouldNotFireStaleCommit()
    {
        var controller = new CommitGroupController();
        var nodeId = Guid.NewGuid();
        var before = new PropertySnapshot(new Dictionary<Guid, PropertyEdit>
        {
            [nodeId] = PropertyEdit.Single(X, 1.0f),
        });
        var after = new PropertySnapshot(new Dictionary<Guid, PropertyEdit>
        {
            [nodeId] = PropertyEdit.Single(X, 2.0f),
        });
        _ = controller.Begin("test#/x", [nodeId], before, "Set X");
        var fired = false;

        var idle = controller.ScheduleIdleCommitAsync(
            "test#/x",
            TimeSpan.FromMilliseconds(25),
            () =>
            {
                fired = true;
                return Task.CompletedTask;
            });
        _ = controller.Close("test#/x", after);

        await idle.ConfigureAwait(false);

        _ = fired.Should().BeFalse();
    }

    private sealed class Target
    {
        public float X { get; set; }
    }

    private sealed class TargetResolver(Guid nodeId, Target target) : IPropertyTarget
    {
        public List<PropertyEdit> Pushed { get; } = [];

        public bool TryGetTarget(Guid id, out object? resolved)
        {
            resolved = id == nodeId ? target : null;
            return resolved is not null;
        }

        public Task PushToEngineAsync(Guid id, PropertyEdit edit)
        {
            _ = id;
            this.Pushed.Add(edit.Clone());
            return Task.CompletedTask;
        }
    }
}
