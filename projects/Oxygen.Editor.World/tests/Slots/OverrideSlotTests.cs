// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Tests.Slots;

/// <summary>
/// Verifies override slot registration, dynamic factory instantiation.
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

    private sealed record UnregisteredSlotData : OverrideSlotData;
}
