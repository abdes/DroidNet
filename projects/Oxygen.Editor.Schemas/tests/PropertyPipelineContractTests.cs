// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Schemas.Bindings;

namespace Oxygen.Editor.Schemas.Tests;

/// <summary>
/// Tests the generic property edit, binding, apply, and commit-group contracts.
/// </summary>
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

    /// <summary>
    /// Verifies undo and redo are the same property apply operation over opposite snapshots.
    /// </summary>
    /// <returns>A task that completes when the apply sequence has been verified.</returns>
    [TestMethod]
    public async Task ApplyAsyncPreservesStructuralIdentityWhenSnapshotsAreSwapped()
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

    /// <summary>
    /// Verifies multi-selection mixed values are represented by the binding state.
    /// </summary>
    [TestMethod]
    public void PropertyBindingRepresentsMixedValuesAsBindingState()
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

    /// <summary>
    /// Verifies missing descriptor entries fail with the unresolved property id.
    /// </summary>
    [TestMethod]
    public void ApplyToTargetFailsWithPropertyIdWhenDescriptorIsMissing()
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

    /// <summary>
    /// Verifies closing a commit group cancels any stale scheduled idle commit.
    /// </summary>
    /// <returns>A task that completes when the idle commit delay has elapsed.</returns>
    [TestMethod]
    public async Task CommitGroupControllerDoesNotFireStaleCommitAfterSessionCloses()
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
