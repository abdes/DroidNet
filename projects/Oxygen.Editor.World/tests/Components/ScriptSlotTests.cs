// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="ScriptSlot"/> override modifications and value-sensitive equality.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ScriptSlot))]
public sealed class ScriptSlotTests
{
    private static readonly Uri TestScript = new("asset:///Content/Scripts/Test.oscript");

    [TestMethod]
    public void WithOverride_AddsAndReplacesOverrideValues()
    {
        var slot = new ScriptSlot { Script = TestScript };

        var updated = slot.WithOverride("speed", ScriptValue.FromFloat(10f));
        _ = updated.Overrides.Should().ContainKey("speed");
        _ = updated.Overrides["speed"].FloatValue.Should().Be(10f);

        var replaced = updated.WithOverride("speed", ScriptValue.FromFloat(20f));
        _ = replaced.Overrides["speed"].FloatValue.Should().Be(20f);
    }

    [TestMethod]
    public void WithoutOverrides_RemovesSpecifiedOverrideKeys()
    {
        var slot = new ScriptSlot { Script = TestScript }
            .WithOverride("speed", ScriptValue.FromFloat(10f))
            .WithOverride("tag", ScriptValue.FromString("player"));

        var stripped = slot.WithoutOverrides(["speed"]);

        _ = stripped.Overrides.Should().NotContainKey("speed");
        _ = stripped.Overrides.Should().ContainKey("tag");
    }

    [TestMethod]
    public void Equals_ComparesOverrideValues()
    {
        var id = Guid.NewGuid();
        var left = new ScriptSlot { Id = id, Script = TestScript }
            .WithOverride("speed", ScriptValue.FromFloat(5f));
        var rightSame = new ScriptSlot { Id = id, Script = TestScript }
            .WithOverride("speed", ScriptValue.FromFloat(5f));
        var rightDifferent = new ScriptSlot { Id = id, Script = TestScript }
            .WithOverride("speed", ScriptValue.FromFloat(10f));

        _ = left.Should().Be(rightSame);
        _ = left.Should().NotBe(rightDifferent);
    }
}
