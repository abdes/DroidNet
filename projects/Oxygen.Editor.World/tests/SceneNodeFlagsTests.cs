// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Tests.TestSupport;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies node flag composition, flag property notifications, bitmask conversions, and mutation helpers.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneNode))]
public sealed class SceneNodeFlagsTests
{
    [TestMethod]
    public void Flags_SetWritesEveryIndividualFlagProperty_AndGetComposesThem()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };

        const SceneNodeFlags allFlags = SceneNodeFlags.Visible
            | SceneNodeFlags.CastsShadows
            | SceneNodeFlags.ReceivesShadows
            | SceneNodeFlags.RayCastingSelectable
            | SceneNodeFlags.IgnoreParentTransform
            | SceneNodeFlags.Static;

        node.Flags = allFlags;

        _ = node.IsVisible.Should().BeTrue();
        _ = node.CastsShadows.Should().BeTrue();
        _ = node.ReceivesShadows.Should().BeTrue();
        _ = node.IsRayCastingSelectable.Should().BeTrue();
        _ = node.IgnoreParentTransform.Should().BeTrue();
        _ = node.IsStatic.Should().BeTrue();
        _ = node.Flags.Should().Be(allFlags);

        node.Flags = SceneNodeFlags.None;

        _ = node.IsVisible.Should().BeFalse();
        _ = node.CastsShadows.Should().BeFalse();
        _ = node.ReceivesShadows.Should().BeFalse();
        _ = node.IsRayCastingSelectable.Should().BeFalse();
        _ = node.IgnoreParentTransform.Should().BeFalse();
        _ = node.IsStatic.Should().BeFalse();
        _ = node.Flags.Should().Be(SceneNodeFlags.None);
    }

    [TestMethod]
    public void Flags_EachPropertyRaisesPropertyChanged()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };
        var recorder = new PropertyChangeRecorder(node);

        node.IsVisible = false;
        node.CastsShadows = true;
        node.ReceivesShadows = true;
        node.IsRayCastingSelectable = false;
        node.IgnoreParentTransform = true;
        node.IsStatic = true;

        _ = recorder.ChangedCount(nameof(SceneNode.IsVisible)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(SceneNode.CastsShadows)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(SceneNode.ReceivesShadows)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(SceneNode.IsRayCastingSelectable)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(SceneNode.IgnoreParentTransform)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(SceneNode.IsStatic)).Should().Be(1);
    }

    [TestMethod]
    public void ToggleFlagAndHasFlag_UpdateAndReportFlagState()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene)
        {
            Name = "NodeValue",
            Flags = SceneNodeFlags.None,
        };

        _ = node.HasFlag(SceneNodeFlags.CastsShadows).Should().BeFalse();

        node.ToggleFlag(SceneNodeFlags.CastsShadows);
        _ = node.HasFlag(SceneNodeFlags.CastsShadows).Should().BeTrue();
        _ = node.CastsShadows.Should().BeTrue();

        node.ToggleFlag(SceneNodeFlags.CastsShadows);
        _ = node.HasFlag(SceneNodeFlags.CastsShadows).Should().BeFalse();
        _ = node.CastsShadows.Should().BeFalse();
    }

    [TestMethod]
    public void SceneNodeFlagsExtensions_ToBitmaskAndFromBitmask_ProduceExactEngineBitValues()
    {
        _ = SceneNodeFlags.Visible.ToBitmask().Should().Be(1u);
        _ = SceneNodeFlags.CastsShadows.ToBitmask().Should().Be(2u);
        _ = SceneNodeFlags.ReceivesShadows.ToBitmask().Should().Be(4u);
        _ = SceneNodeFlags.RayCastingSelectable.ToBitmask().Should().Be(8u);
        _ = SceneNodeFlags.IgnoreParentTransform.ToBitmask().Should().Be(16u);
        _ = SceneNodeFlags.Static.ToBitmask().Should().Be(32u);

        const uint combinedMask = 1u | 2u | 4u | 8u | 16u | 32u;
        var decoded = SceneNodeFlagsExtensions.FromBitmask(combinedMask);

        _ = decoded.Should().Be(
            SceneNodeFlags.Visible
            | SceneNodeFlags.CastsShadows
            | SceneNodeFlags.ReceivesShadows
            | SceneNodeFlags.RayCastingSelectable
            | SceneNodeFlags.IgnoreParentTransform
            | SceneNodeFlags.Static);
    }
}
