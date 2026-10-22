// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="NodeFlagsComponent"/> defaults, change notification, and persistence.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(NodeFlagsComponent))]
public sealed class NodeFlagsComponentTests
{
    [TestMethod]
    public void NewComponent_HasTheDocumentedDefaults()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };

        _ = flags.IsVisible.Should().BeTrue();
        _ = flags.CastsShadows.Should().BeFalse();
        _ = flags.ReceivesShadows.Should().BeFalse();
        _ = flags.IsStatic.Should().BeFalse();
        _ = flags.IsRayCastingSelectable.Should().BeTrue();
        _ = flags.IgnoreParentTransform.Should().BeFalse();
    }

    [TestMethod]
    public void IsLocked_IsTrue()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };

        _ = flags.IsLocked.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(nameof(NodeFlagsComponent.IsVisible))]
    [DataRow(nameof(NodeFlagsComponent.CastsShadows))]
    [DataRow(nameof(NodeFlagsComponent.ReceivesShadows))]
    [DataRow(nameof(NodeFlagsComponent.IsStatic))]
    [DataRow(nameof(NodeFlagsComponent.IsRayCastingSelectable))]
    [DataRow(nameof(NodeFlagsComponent.IgnoreParentTransform))]
    public void Setter_RaisesPropertyChangedOnceWithThePropertyName(string propertyName)
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };
        var recorder = new PropertyChangeRecorder(flags);
        var property = typeof(NodeFlagsComponent).GetProperty(propertyName)!;
        var current = (bool)property.GetValue(flags)!;

        property.SetValue(flags, !current);

        _ = recorder.Changed.Should().Equal(propertyName);
        _ = ((bool)property.GetValue(flags)!).Should().Be(!current);
    }

    [TestMethod]
    public void DehydrateThenHydrate_RoundTripsAllSixFlags()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };
        flags.IsVisible = false;
        flags.CastsShadows = true;
        flags.ReceivesShadows = true;
        flags.IsStatic = true;
        flags.IsRayCastingSelectable = false;
        flags.IgnoreParentTransform = true;

        var data = (NodeFlagsData)flags.Dehydrate();
        var restored = new NodeFlagsComponent { Name = "Other" };
        restored.Hydrate(data);

        _ = restored.IsVisible.Should().BeFalse();
        _ = restored.CastsShadows.Should().BeTrue();
        _ = restored.ReceivesShadows.Should().BeTrue();
        _ = restored.IsStatic.Should().BeTrue();
        _ = restored.IsRayCastingSelectable.Should().BeFalse();
        _ = restored.IgnoreParentTransform.Should().BeTrue();
        _ = restored.Id.Should().Be(flags.Id);
        _ = restored.Name.Should().Be(NodeFlagsComponent.DefaultName);
    }
}
