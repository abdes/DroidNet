// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Tests.Slots;

/// <summary>
/// Verifies override slot registration, dynamic factory instantiation, and slot reuse.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(OverrideSlot))]
public sealed class OverrideSlotTests
{
    [TestMethod]
    public void CreateAndHydrate_WithUnregisteredRecordType_ThrowsInvalidOperationException()
    {
        var unregistered = new UnregisteredSlotData();

        Action act = () => OverrideSlot.CreateAndHydrate(unregistered);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void GetOrCreateSlot_ReturnsSameSlotOnSecondCallAndAddsOnlyOne()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };

        var first = node.GetOrCreateSlot<RenderingSlot>();
        var second = node.GetOrCreateSlot<RenderingSlot>();

        _ = second.Should().BeSameAs(first);
        _ = node.OverrideSlots.Count(s => s is RenderingSlot).Should().Be(1);
    }

    private sealed record UnregisteredSlotData : OverrideSlotData;
}
