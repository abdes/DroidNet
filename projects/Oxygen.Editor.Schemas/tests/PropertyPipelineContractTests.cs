// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Schemas.Bindings;

namespace Oxygen.Editor.Schemas.Tests;

/// <summary>
/// Tests the generic property edit, binding and apply contracts.
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
        var edit = PropertyEdit.SingleEdit(X, 2.0f);

        var act = () => PropertyApply.ApplyToTarget(
            target,
            edit,
            new Dictionary<PropertyId, PropertyDescriptor>());

        _ = act.Should().Throw<KeyNotFoundException>()
            .WithMessage("*test#/x*");
        _ = target.X.Should().Be(1.0f);
    }

    /// <summary>
    /// Verifies an edit is immutable: adding a value makes a new edit and leaves the original unchanged.
    /// </summary>
    [TestMethod]
    public void WithReturnsANewEditAndLeavesTheOriginalUnchanged()
    {
        var original = PropertyEdit.SingleEdit(X, 1.0f);

        var changed = original.With(X, 2.0f);

        _ = original.GetTyped(X, out var originalValue).Should().BeTrue();
        _ = originalValue.Should().Be(1.0f);
        _ = changed.GetTyped(X, out var changedValue).Should().BeTrue();
        _ = changedValue.Should().Be(2.0f);
    }

    private sealed class Target
    {
        public float X { get; set; }
    }
}
